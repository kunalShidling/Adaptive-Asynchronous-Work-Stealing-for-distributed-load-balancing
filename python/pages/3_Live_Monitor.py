"""Page 3 – Live Execution Monitor."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
import pandas as pd
from charts import (
    queue_length_bar,
    task_completion_timeline,
    steal_timeline,
    workload_before_redistribution_bar,
    workload_after_redistribution_bar,
    redistribution_comparison_bar,
    node_throughput_bar,
)

st.set_page_config(page_title="Live Monitor", page_icon="🔴", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
.node-card {
    background: linear-gradient(135deg, #1c2033, #252b45);
    border: 1px solid #3d4455;
    border-radius: 8px;
    padding: 14px 16px;
    margin-bottom: 12px;
}
.cat-badge-very-fast {
    background-color: #9B51E0; color: white; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;
}
.cat-badge-fast {
    background-color: #27AE60; color: white; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;
}
.cat-badge-medium {
    background-color: #2F80ED; color: white; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;
}
.cat-badge-slow {
    background-color: #EB5757; color: white; padding: 2px 8px; border-radius: 4px; font-weight: bold; font-size: 11px;
}
.activity-feed {
    font-family: 'Courier New', monospace; font-size: 11px;
    max-height: 260px; overflow-y: auto;
    background: #1a1d2e; padding: 10px 14px;
    border-radius: 6px; border: 1px solid #3d4455; color: #c9d1d9;
}
</style>
""", unsafe_allow_html=True)

st.title("🔴 Live Heterogeneous Execution Monitor")

runner = st.session_state.get("runner", None)
if runner is None:
    st.info("🔎 No experiment is currently loaded or running. Configure and start one first.")
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Configure Experiment")
    st.stop()

snapshot = runner.processor.get_snapshot()
exp_running = not runner.is_done()
config = st.session_state.get("last_config", None)

# ── Status banner ─────────────────────────────────────────────────────────────
if exp_running:
    st.success("⏳ Execution in progress — dynamically balancing heterogeneous workloads…")
elif snapshot.get("experiment_completed"):
    st.success(f"✅ Experiment completed in **{snapshot['wall_ms']:.1f} ms**")
elif snapshot.get("error"):
    st.error(f"❌ Error: {snapshot['error']}")
else:
    st.warning("Experiment status unknown")

# ── Top-level Cluster KPIs ───────────────────────────────────────────────────
num_tasks = config.num_tasks if config else 0
completed = snapshot.get("tasks_completed_total", 0)
pending_total = snapshot.get("pending_total", 0)
cluster_tp = snapshot.get("cluster_throughput", 0.0)
redist_count = snapshot.get("redistribution_count", 0)

# Estimate completion time
if cluster_tp > 0 and pending_total > 0:
    est_completion_sec = pending_total / cluster_tp
    est_comp_str = f"{est_completion_sec * 1000:.1f} ms"
else:
    est_comp_str = "0.0 ms" if not exp_running else "Calculating…"

kpi1, kpi2, kpi3, kpi4, kpi5 = st.columns(5)
kpi1.metric("Total Completed Tasks", f"{completed} / {num_tasks}")
kpi2.metric("Total Pending Tasks", f"{pending_total}")
kpi3.metric("Cluster Throughput", f"{cluster_tp:.1f} tasks/s")
kpi4.metric("Redistribution Operations", f"{redist_count}")
kpi5.metric("Est. Remaining Time", est_comp_str)

# Progress bar
if num_tasks > 0:
    st.progress(min(1.0, completed / num_tasks), text=f"Overall Progress: {completed} / {num_tasks} tasks ({completed/num_tasks*100:.1f}%)")

auto_refresh = st.checkbox("🔄 Auto-refresh live data (1s)", value=True)

# ── Node Performance & Status Cards ──────────────────────────────────────────
st.subheader("🖥️ Computing Node Status & Capabilities")
workers_dict = snapshot.get("workers", {})

if workers_dict:
    sorted_ids = sorted(workers_dict.keys())
    cols = st.columns(min(len(sorted_ids), 4))
    badge_classes = {
        "VERY_FAST": "cat-badge-very-fast",
        "FAST": "cat-badge-fast",
        "MEDIUM": "cat-badge-medium",
        "SLOW": "cat-badge-slow",
    }
    state_emoji = {"running": "🟢", "idle": "⚪", "stealing": "🟡", "stopped": "🔴", "failed": "🚨"}

    for idx, wid in enumerate(sorted_ids):
        w = workers_dict[wid]
        col = cols[idx % len(cols)]
        cat = w.get("category", "MEDIUM")
        badge_cls = badge_classes.get(cat, "cat-badge-medium")
        st_emoji = state_emoji.get(w.get("state", "idle"), "⚪")
        init_tasks = snapshot.get("initial_allocations", {}).get(wid, w.get("initial_assigned", 0))
        cur_tasks = snapshot.get("current_allocations", {}).get(wid, init_tasks)
        pct = (cur_tasks / num_tasks * 100) if num_tasks > 0 else 0.0

        with col:
            st.markdown(f"""
            <div class="node-card">
                <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">
                    <span style="font-size:16px; font-weight:bold;">{st_emoji} Node {wid}</span>
                    <span class="{badge_cls}">{cat}</span>
                </div>
                <div style="font-size:12px; color:#a0aec0; margin-bottom:6px;">
                    Speed Factor: <b>{w.get('speed_factor', 1.0):.1f}x</b> | Throughput: <b>{w.get('throughput_tasks_per_sec', 0.0):.1f} t/s</b>
                </div>
                <div style="font-size:12px; margin-bottom:4px;">
                    Assigned: <b>{init_tasks}</b> → Current: <b>{cur_tasks}</b> ({pct:.1f}%)
                </div>
                <div style="font-size:12px; margin-bottom:4px;">
                    Pending Queue: <b>{w.get('queue_len', 0)}</b> | Executing: <b>{'1' if w.get('current_task_id') else '0'}</b>
                </div>
                <div style="font-size:12px; margin-bottom:4px;">
                    Completed: <b>{w.get('tasks_completed', 0)}</b> | Failed: <b>{w.get('tasks_failed', 0)}</b>
                </div>
                <div style="font-size:11px; color:#38bdf8;">
                    📥 Stolen / Redistributed In: +{w.get('tasks_redistributed_in', 0)} | 📤 Donated / Out: -{w.get('tasks_redistributed_out', 0)}
                </div>
            </div>
            """, unsafe_allow_html=True)
else:
    st.info("Waiting for nodes to initialize…")

# ── Workload Visualizations: BEFORE vs AFTER REDISTRIBUTION ────────────────────
st.markdown("---")
st.subheader("⚖️ Workload Distribution Analysis: Before vs After Dynamic Redistribution")

v_col1, v_col2 = st.columns(2)
with v_col1:
    st.plotly_chart(workload_before_redistribution_bar(snapshot), use_container_width=True)
with v_col2:
    st.plotly_chart(workload_after_redistribution_bar(snapshot), use_container_width=True)

# Grouped comparison
st.plotly_chart(redistribution_comparison_bar(snapshot), use_container_width=True)

# ── Measured Processing Throughput & Queue Lengths ────────────────────────────
st.markdown("---")
tc1, tc2 = st.columns(2)
with tc1:
    st.plotly_chart(node_throughput_bar(snapshot), use_container_width=True)
with tc2:
    st.plotly_chart(queue_length_bar(snapshot), use_container_width=True)

# ── Task Timeline & Stealing Events ───────────────────────────────────────────
c1, c2 = st.columns(2)
with c1:
    st.plotly_chart(task_completion_timeline(snapshot.get("task_records", [])), use_container_width=True)
with c2:
    steal_events = snapshot.get("steal_events", [])
    st.plotly_chart(steal_timeline(steal_events), use_container_width=True)

# ── Activity Feed ─────────────────────────────────────────────────────────────
st.subheader("📋 Real-Time Activity Feed")
feed = snapshot.get("activity_feed", [])
if feed:
    feed_html = "\n".join(reversed(feed[-30:]))
    st.markdown(f'<div class="activity-feed"><pre>{feed_html}</pre></div>', unsafe_allow_html=True)
else:
    st.caption("No events yet…")

if auto_refresh and exp_running:
    time.sleep(1)
    st.rerun()
