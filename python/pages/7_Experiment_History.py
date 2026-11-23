"""Page 7 – Experiment History & Reports."""
import os, sys, json
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from result_manager import (
    load_history, load_summary, export_worker_csv,
    list_experiments, HISTORY_FILE,
)

st.set_page_config(page_title="Experiment History", page_icon="📋", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("📋 Experiment History & Reports")

history = load_history()
exp_ids = list_experiments()

if history.empty:
    st.info("No experiments yet. Configure and run one first.")
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Configure Experiment")
    st.stop()

# ── Filters ───────────────────────────────────────────────────────────────────
st.subheader("History")
fc1, fc2, fc3 = st.columns(3)
with fc1:
    algo_opts = history["algorithm"].dropna().unique().tolist() if "algorithm" in history.columns else []
    algo_filter = st.multiselect("Filter by Algorithm", algo_opts)
with fc2:
    wl_opts = history["workload_type"].dropna().unique().tolist() if "workload_type" in history.columns else []
    wl_filter = st.multiselect("Filter by Workload", wl_opts)
with fc3:
    nw_opts = sorted(history["num_workers"].dropna().unique().tolist()) if "num_workers" in history.columns else []
    workers_filter = st.multiselect("Filter by Workers", nw_opts)

filtered = history.copy()
if algo_filter:
    filtered = filtered[filtered["algorithm"].isin(algo_filter)]
if wl_filter:
    filtered = filtered[filtered["workload_type"].isin(wl_filter)]
if workers_filter:
    filtered = filtered[filtered["num_workers"].isin(workers_filter)]

sort_col = "experiment_id" if "experiment_id" in filtered.columns else filtered.columns[0]
st.dataframe(
    filtered.sort_values(sort_col, ascending=False),
    use_container_width=True,
)

# ── Export history CSV ────────────────────────────────────────────────────────
st.subheader("Export")
e1, e2 = st.columns(2)
with e1:
    if st.button("📥 Prepare History CSV"):
        csv_bytes = filtered.to_csv(index=False).encode("utf-8")
        st.download_button(
            "⬇️ Download experiment_history.csv",
            csv_bytes,
            "experiment_history.csv",
            "text/csv",
        )
with e2:
    st.caption(f"History file: `{HISTORY_FILE}`")

# ── Individual experiment detail ──────────────────────────────────────────────
st.markdown("---")
st.subheader("Experiment Detail")

if exp_ids:
    sel_id = st.selectbox("Select Experiment", sorted(exp_ids, reverse=True))
    summary = load_summary(sel_id)

    if summary:
        d1, d2 = st.columns(2)
        with d1:
            st.markdown("**Experiment Summary**")
            meta = {k: v for k, v in summary.items() if k != "workers"}
            st.json(meta)
        with d2:
            workers = summary.get("workers", [])
            if workers:
                st.markdown("**Per-Worker Metrics**")
                st.dataframe(pd.DataFrame(workers), use_container_width=True)

        # Download buttons
        dl1, dl2 = st.columns(2)
        with dl1:
            json_str = json.dumps(summary, indent=2)
            st.download_button(
                f"📥 Download Summary JSON (#{sel_id})",
                json_str.encode("utf-8"),
                f"exp_{sel_id}_summary.json",
                "application/json",
            )
        with dl2:
            csv_path = export_worker_csv(sel_id)
            if csv_path and os.path.exists(csv_path):
                with open(csv_path, encoding="utf-8") as f:
                    csv_data = f.read()
                st.download_button(
                    f"📥 Download Worker CSV (#{sel_id})",
                    csv_data.encode("utf-8"),
                    f"exp_{sel_id}_workers.csv",
                    "text/csv",
                )
    else:
        st.warning(f"No summary file found for experiment #{sel_id}")
