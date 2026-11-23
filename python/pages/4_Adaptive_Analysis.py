"""Page 4 – Adaptive Scheduler Analysis."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from result_manager import load_summary, list_experiments
from charts import cost_estimation_scatter, steal_timeline
from metrics import compute_mape, safe_div

st.set_page_config(page_title="Adaptive Analysis", page_icon="🧠", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("🧠 Adaptive Scheduler Analysis")

# ── Algorithm explanation ─────────────────────────────────────────────────────
with st.expander("📐 How the Adaptive Algorithm Works", expanded=True):
    st.markdown(r"""
### Cost Estimation — Exponentially Weighted Moving Average (EWMA)

$$\hat{c}_t = \alpha \cdot c_t + (1-\alpha) \cdot \hat{c}_{t-1}$$

| Symbol | Meaning |
|--------|---------|
| $\hat{c}_t$ | Updated cost estimate after observing task $t$ |
| $c_t$ | Actual measured execution duration of task $t$ |
| $\alpha$ | Learning rate (0 < α ≤ 1); higher → faster adaptation |

---

### Remaining Workload Estimation

$$W(w) = \sum_{i \in \text{queue}(w)} \hat{c}_i$$

Summing estimated costs of all tasks in a worker's queue gives an estimate of how much
work remains for that worker.

---

### Victim Selection Score

$$\text{score}(v) = W(v) - W(\text{thief})$$

The worker with the **highest positive score** (most excess work over the thief) is chosen.

---

### Steal Decision Condition

$$\text{steal if:} \quad \frac{\text{score}(v)}{2} - c_{\text{overhead}} > \theta_{\min}$$

Stealing is only triggered when the expected benefit exceeds both the overhead and a
configurable minimum threshold $\theta_{\min}$.

---

### Adaptive Batch Size

$$\text{batch} = \left\lfloor \frac{\text{score}(v)}{2 \cdot \bar{c}} \right\rfloor, \quad \text{clamped to } [1,\, \text{batch}_{\max}]$$

where $\bar{c}$ is the average estimated task cost in the victim's queue.
The batch size scales with the **observed imbalance** — larger imbalance → more tasks transferred.
    """)

st.markdown("---")

# ── Data source ───────────────────────────────────────────────────────────────
exp_ids = list_experiments()
runner  = st.session_state.get("runner", None)

task_records   = []
steal_events   = []
exp_summary    = {}

# Prefer live data from an active run
if runner:
    snapshot     = runner.processor.get_snapshot()
    task_records = snapshot.get("task_records", [])
    steal_events = snapshot.get("steal_events", [])

# Fall back to a completed experiment
if not task_records and exp_ids:
    sel_id = st.selectbox("Select completed experiment for analysis",
                          sorted(exp_ids, reverse=True))
    summary = load_summary(sel_id)
    if summary:
        exp_summary = summary
        # task_records not persisted in summary yet; show what we have
        st.caption("Note: live task records not available for completed runs — cost chart reflects what was captured.")

st.subheader("Cost Estimation Accuracy")
col1, col2 = st.columns(2)
with col1:
    st.plotly_chart(cost_estimation_scatter(task_records), use_container_width=True)
    if task_records:
        estimated = [t.get("est_cost", 0) for t in task_records]
        actual    = [t.get("actual_ms", 0) for t in task_records]
        mape = compute_mape(estimated, actual)
        st.metric(
            "MAPE (Cost Estimation Error)",
            f"{mape:.2f}%" if mape is not None else "N/A",
            help="Mean Absolute Percentage Error — lower is better",
        )
        if mape is not None:
            if mape < 10:
                st.success(f"✅ MAPE {mape:.1f}% — good cost estimation accuracy")
            elif mape < 30:
                st.info(f"ℹ️ MAPE {mape:.1f}% — moderate accuracy; more tasks improve estimates")
            else:
                st.warning(f"⚠️ MAPE {mape:.1f}% — estimates diverge from actuals (expected early in run)")
    else:
        st.caption("No task records available yet.")

with col2:
    st.subheader("Stealing Events")
    st.plotly_chart(steal_timeline(steal_events), use_container_width=True)
    if steal_events:
        successful = [e for e in steal_events if e.get("success")]
        failed     = [e for e in steal_events if not e.get("success")]
        m1, m2, m3 = st.columns(3)
        m1.metric("Total Events", len(steal_events))
        m2.metric("Successful", len(successful))
        m3.metric("Failed", len(failed))

# ── Stealing decision log ─────────────────────────────────────────────────────
if steal_events:
    st.markdown("---")
    st.subheader("Stealing Decision Log")
    df = pd.DataFrame(steal_events)
    if not df.empty:
        display = [c for c in
                   ["ts", "thief_id", "victim_id", "success", "tasks_stolen",
                    "batch_size", "expected_benefit", "victim_remaining", "reason"]
                   if c in df.columns]
        st.dataframe(df[display].tail(100), use_container_width=True)

        # Explanation of adaptive decisions
        st.markdown("#### Interpreting the Decisions")
        total_att  = len(steal_events)
        total_succ = len(successful)
        rate = safe_div(total_succ, total_att) * 100
        st.markdown(f"""
- **{total_att}** steal decision points evaluated  
- **{total_succ}** approved steals ({rate:.1f}% approval rate)  
- **{total_att - total_succ}** steals blocked by cost-benefit check  

The adaptive algorithm blocks steals when the expected workload benefit ÷ 2 minus the
steal overhead falls below the configured minimum threshold.  
This avoids wasted steal attempts when queues are already nearly balanced.
        """)

# ── Summary from completed experiment ────────────────────────────────────────
if exp_summary:
    st.markdown("---")
    st.subheader("Experiment Metrics (from summary)")
    c1, c2, c3, c4 = st.columns(4)
    c1.metric("MAPE", f"{float(exp_summary.get('cost_estimation_mape', 0)):.2f}%")
    c2.metric("Steal Rate",
              f"{float(exp_summary.get('steal_success_rate', 0)) * 100:.1f}%")
    c3.metric("Steal Attempts",
              exp_summary.get("total_steals_attempted", 0))
    c4.metric("Sched. Overhead",
              f"{float(exp_summary.get('scheduling_overhead_ms', 0)):.2f}ms")
