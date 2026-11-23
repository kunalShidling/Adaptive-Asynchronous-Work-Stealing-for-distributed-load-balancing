"""
Main Streamlit entry point.
Run with: streamlit run python/app.py
"""
import streamlit as st
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

st.set_page_config(
    page_title="Adaptive Scheduler Research Dashboard",
    page_icon="⚡",
    layout="wide",
    initial_sidebar_state="expanded",
)

# ── Global dark-theme CSS ────────────────────────────────────────────────────
st.markdown("""
<style>
    .stApp { background-color: #0e1117; }
    section[data-testid="stSidebar"] { background-color: #161b2e; }
    .metric-card {
        background: linear-gradient(135deg, #1e2130, #252a3a);
        border: 1px solid #3d4455;
        border-radius: 10px;
        padding: 18px 20px;
        margin: 6px 0;
    }
    .activity-feed {
        font-family: 'Courier New', monospace;
        font-size: 11px;
        max-height: 280px;
        overflow-y: auto;
        background: #1a1d2e;
        padding: 10px 14px;
        border-radius: 6px;
        border: 1px solid #3d4455;
        color: #c9d1d9;
    }
    h1, h2, h3 { color: #e6edf3; }
    .stDataFrame { border-radius: 8px; overflow: hidden; }
</style>
""", unsafe_allow_html=True)

# ── Sidebar ──────────────────────────────────────────────────────────────────
with st.sidebar:
    st.markdown("## ⚡ Adaptive Scheduler")
    st.markdown("**Research Dashboard**")
    st.markdown("---")
    st.markdown("### Pages")
    st.page_link("pages/1_Overview.py",               label="📊 Overview")
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️  Configure Experiment")
    st.page_link("pages/3_Live_Monitor.py",           label="🔴 Live Monitor")
    st.page_link("pages/4_Adaptive_Analysis.py",      label="🧠 Adaptive Analysis")
    st.page_link("pages/5_Algorithm_Comparison.py",   label="📈 Algorithm Comparison")
    st.page_link("pages/6_Performance_Analytics.py",  label="🔬 Performance Analytics")
    st.page_link("pages/7_Experiment_History.py",     label="📋 Experiment History")
    st.markdown("---")
    st.caption("Cost-Aware Adaptive Work-Stealing\nfor Multicore Systems — A2WS Research")

# ── Home page ────────────────────────────────────────────────────────────────
st.title("⚡ Cost-Aware Adaptive Work-Stealing Scheduler")
st.markdown("### Interactive Research Dashboard for Multicore Scheduling Analysis")

col1, col2, col3 = st.columns(3)
with col1:
    st.info("👈 **Sidebar navigation** to switch pages")
with col2:
    st.info("⚙️ **Configure Experiment** to start")
with col3:
    st.info("📊 **Live Monitor** for real-time events")

st.markdown("---")

st.markdown("""
## About This Project

This dashboard implements and benchmarks the **Cost-Aware Adaptive Work-Stealing** scheduler
for shared-memory multicore systems, inspired by the A2WS research on distributed load balancing.

### Four Scheduling Algorithms

| Algorithm | Description | Key Feature |
|-----------|-------------|-------------|
| 🔵 **Static** | Pre-assigned, fixed distribution | Baseline — no runtime adaptation |
| 🟡 **Centralized Dynamic** | Workers compete on a shared queue | Synchronization overhead |
| 🟢 **Random Work-Stealing** | Idle workers steal from random victims | Low overhead, uniform stealing |
| 🔴 **Adaptive Work-Stealing** | Cost-aware victim selection, adaptive batch sizing | **Research contribution** |

### Cost-Aware Adaptive Algorithm

The proposed algorithm estimates task execution costs using **Exponentially Weighted Moving Average (EWMA)**:

$$\\hat{c}_t = \\alpha \\cdot c_t + (1-\\alpha) \\cdot \\hat{c}_{t-1}$$

It selects stealing victims based on **estimated remaining workload** and only steals when the
expected benefit exceeds the estimated overhead — avoiding unnecessary steal operations.

---
*Navigate to **Configure Experiment** to begin your first experiment.*
""")
