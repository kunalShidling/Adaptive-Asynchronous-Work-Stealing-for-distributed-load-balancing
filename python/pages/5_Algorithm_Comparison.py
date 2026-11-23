"""Page 5 – Algorithm Comparison."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from result_manager import load_comparison, list_experiments, load_summary
from charts import algorithm_comparison_bar

st.set_page_config(page_title="Algorithm Comparison", page_icon="📈", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("📈 Algorithm Comparison")

exp_ids = list_experiments()

# Collect experiments that have a comparison.json
comparison_exps = []
for eid in sorted(exp_ids, reverse=True):
    comp = load_comparison(eid)
    if comp and isinstance(comp, list) and len(comp) > 1:
        comparison_exps.append((eid, comp))

if not comparison_exps:
    st.info(
        "No comparison experiments found.\n\n"
        "Run an experiment with **Algorithm = 'All Algorithms (Comparison)'** "
        "to generate side-by-side results for all 4 schedulers."
    )
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Configure a Comparison Experiment")
    st.stop()

# ── Experiment selector ───────────────────────────────────────────────────────
exp_labels = {f"Experiment #{eid}": comp for eid, comp in comparison_exps}
selected_label = st.selectbox("Select Comparison Experiment", list(exp_labels.keys()))
comparison: list = exp_labels[selected_label]

# ── Metric charts ─────────────────────────────────────────────────────────────
st.markdown("---")
st.subheader("Performance Comparison")

m1, m2 = st.columns(2)
with m1:
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "wall_ms", "Total Execution Time", "Wall Time (ms)"),
        use_container_width=True,
    )
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "avg_utilization", "Avg Worker Utilization", "Utilization"),
        use_container_width=True,
    )
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "load_imbalance", "Load Imbalance", "Imbalance (lower = better)"),
        use_container_width=True,
    )
with m2:
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "speedup", "Speedup", "Speedup (×)"),
        use_container_width=True,
    )
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "efficiency", "Parallel Efficiency", "Efficiency"),
        use_container_width=True,
    )
    st.plotly_chart(
        algorithm_comparison_bar(comparison, "steal_success_rate", "Steal Success Rate", "Rate"),
        use_container_width=True,
    )

# ── Comparison table ──────────────────────────────────────────────────────────
st.markdown("---")
st.subheader("Comparison Table")

rows = []
for entry in comparison:
    if not isinstance(entry, dict):
        continue
    row = {
        "Algorithm": entry.get("algorithm", "?").replace("_", " ").title(),
        "Wall Time (ms)": round(float(entry.get("wall_ms", 0)), 2),
        "Seq. Time (ms)": round(float(entry.get("sequential_ms", 0)), 2),
        "Speedup (×)": round(float(entry.get("speedup", 0)), 3),
        "Efficiency": round(float(entry.get("efficiency", 0)), 3),
        "Avg Utilization": round(float(entry.get("avg_utilization", 0)), 3),
        "Load Imbalance": round(float(entry.get("load_imbalance", 0)), 4),
        "Steal Attempts": entry.get("total_steals_attempted", 0),
        "Steal Successes": entry.get("total_steals_succeeded", 0),
        "Steal Rate": round(float(entry.get("steal_success_rate", 0)) * 100, 1),
        "Sched. Overhead (ms)": round(float(entry.get("scheduling_overhead_ms", 0)), 3),
    }
    rows.append(row)

if rows:
    df = pd.DataFrame(rows)
    st.dataframe(df, use_container_width=True)

    # ── Interpretation ──────────────────────────────────────────────────────
    st.markdown("---")
    st.subheader("Interpretation Guide")
    st.markdown("""
| Metric | Better when… | Notes |
|--------|-------------|-------|
| Wall Time | Lower | Total real-world elapsed time |
| Speedup | Higher | Sequential ÷ Parallel time |
| Parallel Efficiency | Higher (≤ 1.0) | Speedup ÷ Workers; 1.0 = perfect |
| Avg Utilization | Higher | Fraction of time workers are busy |
| Load Imbalance | Lower | Normalised std dev of worker busy times |
| Steal Rate | Higher | Fraction of steal attempts that transfer work |
| Sched. Overhead | Lower | Time spent in scheduling decisions |

> **Note:** The adaptive algorithm does not guarantee the best performance in every scenario.
> Its advantage depends on workload heterogeneity, task cost variance, and the number of workers.
> Run experiments with **heterogeneous** and **imbalanced** workloads to observe the most benefit.
    """)
