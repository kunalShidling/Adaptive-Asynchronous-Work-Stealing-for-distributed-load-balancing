"""Page 6 – Performance Analytics."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from result_manager import load_summary, list_experiments
from charts import worker_utilization_bar, tasks_per_worker_pie, worker_activity_heatmap
from metrics import format_ms, safe_div, summarize_workers

st.set_page_config(page_title="Performance Analytics", page_icon="🔬", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("🔬 Performance Analytics")

exp_ids = list_experiments()
if not exp_ids:
    st.info("No experiments found. Run one first from Configure Experiment.")
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Configure Experiment")
    st.stop()

selected_id = st.selectbox("Select Experiment", sorted(exp_ids, reverse=True))
summary = load_summary(selected_id)

if not summary:
    st.error(f"Could not load summary for experiment #{selected_id}")
    st.stop()

# ── Extract fields ────────────────────────────────────────────────────────────
wall_ms        = float(summary.get("wall_ms", 0) or 0)
seq_ms         = float(summary.get("sequential_ms", 0) or 0)
speedup        = float(summary.get("speedup", 0) or 0)
efficiency     = float(summary.get("efficiency", 0) or 0)
avg_util       = float(summary.get("avg_utilization", 0) or 0)
load_imbalance = float(summary.get("load_imbalance", 0) or 0)
workers        = summary.get("workers", [])
num_workers    = int(summary.get("num_workers", 1) or 1)
algorithm      = summary.get("algorithm", "?")

# ── KPI cards ─────────────────────────────────────────────────────────────────
st.subheader("📊 Summary Metrics")
c1, c2, c3, c4, c5 = st.columns(5)
c1.metric("Algorithm", algorithm.replace("_", " ").title())
c2.metric("Wall Time", format_ms(wall_ms))
c3.metric("Speedup", f"{speedup:.2f}×" if speedup > 0 else "N/A")
c4.metric("Parallel Efficiency", f"{efficiency * 100:.1f}%" if efficiency > 0 else "N/A")
c5.metric("Load Imbalance", f"{load_imbalance:.4f}")

c6, c7, c8, c9, c10 = st.columns(5)
c6.metric("Sequential Baseline", format_ms(seq_ms) if seq_ms > 0 else "N/A")
c7.metric("Avg Utilization", f"{avg_util * 100:.1f}%")
c8.metric("Steal Attempts", summary.get("total_steals_attempted", 0))
c9.metric("Successful Steals", summary.get("total_steals_succeeded", 0))
steal_rate = float(summary.get("steal_success_rate", 0) or 0)
c10.metric("Steal Rate", f"{steal_rate * 100:.1f}%")

# ── Metric definitions ────────────────────────────────────────────────────────
with st.expander("📐 Metric Definitions & Formulas"):
    st.markdown("""
| Metric | Formula | Source |
|--------|---------|--------|
| **Wall Time** | Clock time from experiment start to end | Measured with `high_resolution_clock` |
| **Sequential Baseline** | Single-thread execution of same tasks | Measured before parallel phase |
| **Speedup** | Sequential Time / Wall Time | Calculated |
| **Parallel Efficiency** | Speedup / Num Workers | Calculated |
| **Avg Utilization** | Mean(Worker Busy Time / Wall Time) | Per-worker, then averaged |
| **Load Imbalance** | Std Dev(Busy Times) / Mean(Busy Times) | Normalised; 0 = perfect balance |
| **Steal Rate** | Successful Steals / Total Attempts | Counted atomically in each scheduler |
| **MAPE** | Mean \|Est − Actual\| / Actual × 100 | Only for adaptive scheduler |
| **Sched. Overhead** | Time in mutex wait / stealing decisions | Measured separately from task execution |
    """)

st.markdown("---")

# ── Worker charts ─────────────────────────────────────────────────────────────
if workers:
    st.subheader("Worker Performance")
    ch1, ch2 = st.columns(2)
    with ch1:
        st.plotly_chart(worker_utilization_bar(workers, wall_ms), use_container_width=True)
    with ch2:
        st.plotly_chart(tasks_per_worker_pie(workers), use_container_width=True)

    # Worker table
    st.subheader("Per-Worker Metrics")
    df = summarize_workers(workers, wall_ms)
    st.dataframe(df, use_container_width=True)

# ── Stealing metrics ──────────────────────────────────────────────────────────
st.markdown("---")
st.subheader("Stealing Metrics")
s1, s2, s3, s4, s5 = st.columns(5)
s1.metric("Total Attempts", summary.get("total_steals_attempted", 0))
s2.metric("Successful", summary.get("total_steals_succeeded", 0))
s3.metric("Tasks Stolen", summary.get("total_tasks_stolen", 0))
s4.metric("Steal Rate", f"{steal_rate * 100:.1f}%")
s5.metric("Sched. Overhead", format_ms(float(summary.get("scheduling_overhead_ms", 0) or 0)))

# Cost estimation (adaptive only)
mape = summary.get("cost_estimation_mape", None)
if mape is not None and float(mape or 0) > 0:
    st.markdown("---")
    st.subheader("Cost Estimation Quality")
    st.metric("MAPE", f"{float(mape):.2f}%",
              help="Mean Absolute Percentage Error between EWMA estimate and actual task duration")
    if float(mape) < 15:
        st.success("✅ Good cost estimation accuracy")
    elif float(mape) < 40:
        st.info("ℹ️ Moderate accuracy — improves as more tasks are observed")
    else:
        st.warning("⚠️ High error — common at the start when estimates are uninitialized")

# ── Full JSON ─────────────────────────────────────────────────────────────────
with st.expander("View full summary JSON"):
    st.json({k: v for k, v in summary.items() if k != "workers"})
