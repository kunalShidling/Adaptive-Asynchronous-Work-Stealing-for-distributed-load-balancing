"""Page 1 – Overview Dashboard."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from result_manager import load_history, load_summary, list_experiments
from charts import worker_utilization_bar, tasks_per_worker_pie
from metrics import format_ms, safe_div

st.set_page_config(page_title="Overview", page_icon="📊", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("📊 Overview Dashboard")

history = load_history()
exp_ids = list_experiments()

if history.empty:
    st.info("👋 **Welcome!** No experiments have been run yet.")
    st.markdown("""
    ### Getting Started

    1. **Build the C++ engine** — run `scripts\\build.bat` from the project root  
    2. **Configure an experiment** — go to ⚙️ Configure Experiment  
    3. **Start the experiment** — click ▶️ Start Experiment  
    4. **Watch results** — open 🔴 Live Monitor for real-time progress  
    5. **Analyse** — use 🧠 Adaptive Analysis and 📈 Algorithm Comparison  
    """)
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Go to Configure Experiment")
    st.stop()

# ── KPI row ──────────────────────────────────────────────────────────────────
latest = history.iloc[-1]
col1, col2, col3, col4 = st.columns(4)
col1.metric("Total Experiments", len(history))
col2.metric("Latest Wall Time", format_ms(float(latest.get("wall_ms", 0) or 0)))
speedup = float(latest.get("speedup", 0) or 0)
col3.metric("Latest Speedup", f"{speedup:.2f}×")
steal_rate = float(latest.get("steal_success_rate", 0) or 0)
col4.metric("Latest Steal Success Rate", f"{steal_rate * 100:.1f}%")

st.markdown("---")

# ── Recent experiments table ─────────────────────────────────────────────────
st.subheader("Recent Experiments")
display_cols = [
    c for c in [
        "experiment_id", "timestamp", "algorithm", "workload_type",
        "num_workers", "num_tasks", "speedup", "avg_utilization",
        "steal_success_rate",
    ]
    if c in history.columns
]
st.dataframe(
    history[display_cols].sort_values("experiment_id", ascending=False).head(10),
    use_container_width=True,
)

# ── Latest experiment detail ─────────────────────────────────────────────────
if exp_ids:
    latest_id = max(exp_ids)
    summary = load_summary(latest_id)
    if summary:
        st.markdown(f"---\n### Latest Experiment (#{latest_id}) Detail")
        workers = summary.get("workers", [])
        wall_ms = float(summary.get("wall_ms", 1) or 1)
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Algorithm", summary.get("algorithm", "?"))
        col2.metric("Workers", summary.get("num_workers", "?"))
        col3.metric("Tasks", summary.get("num_tasks", "?"))
        col4.metric("Load Imbalance", f"{float(summary.get('load_imbalance', 0)):.3f}")

        if workers:
            c1, c2 = st.columns(2)
            with c1:
                st.plotly_chart(worker_utilization_bar(workers, wall_ms),
                                use_container_width=True)
            with c2:
                st.plotly_chart(tasks_per_worker_pie(workers),
                                use_container_width=True)
