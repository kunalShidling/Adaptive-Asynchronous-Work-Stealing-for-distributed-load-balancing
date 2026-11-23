"""Page 3 – Live Execution Monitor."""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
from charts import (
    queue_length_bar,
    task_completion_timeline,
    steal_timeline,
    worker_activity_heatmap,
)

st.set_page_config(page_title="Live Monitor", page_icon="🔴", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
.activity-feed {
    font-family: 'Courier New', monospace; font-size: 11px;
    max-height: 260px; overflow-y: auto;
    background: #1a1d2e; padding: 10px 14px;
    border-radius: 6px; border: 1px solid #3d4455; color: #c9d1d9;
}
</style>
""", unsafe_allow_html=True)

st.title("🔴 Live Execution Monitor")

runner = st.session_state.get("runner", None)
if runner is None:
    st.info("🔎 No experiment is running. Configure and start one first.")
    st.page_link("pages/2_Workload_Configuration.py", label="⚙️ Configure Experiment")
    st.stop()

snapshot = runner.processor.get_snapshot()
exp_running = not runner.is_done()
config = st.session_state.get("last_config", None)

# ── Status banner ─────────────────────────────────────────────────────────────
if exp_running:
    st.success("⏳ Experiment in progress…")
elif snapshot.get("experiment_completed"):
    st.success(f"✅ Experiment completed in **{snapshot['wall_ms']:.1f} ms**")
elif snapshot.get("error"):
    st.error(f"❌ Error: {snapshot['error']}")
else:
    st.warning("Experiment status unknown")

# ── Auto-refresh toggle ───────────────────────────────────────────────────────
auto_refresh = st.checkbox("🔄 Auto-refresh (every second)", value=True)

# ── Progress bar ─────────────────────────────────────────────────────────────
num_tasks = config.num_tasks if config else 0
completed = snapshot.get("tasks_completed_total", 0)
if num_tasks > 0:
    st.progress(
        min(1.0, completed / num_tasks),
        text=f"Tasks completed: {completed} / {num_tasks}",
    )

# ── Worker status grid ────────────────────────────────────────────────────────
st.subheader("Worker Status")
workers_dict = snapshot.get("workers", {})
if workers_dict:
    sorted_ids = sorted(workers_dict.keys())
    max_cols = min(len(sorted_ids), 4)
    cols = st.columns(max_cols)
    state_emoji = {"running": "🟢", "idle": "⚪", "stealing": "🟡", "stopped": "🔴"}
    for idx, wid in enumerate(sorted_ids):
        w = workers_dict[wid]
        col = cols[idx % max_cols]
        state = w.get("state", "idle")
        emoji = state_emoji.get(state, "⚪")
        with col:
            st.metric(
                f"{emoji} Worker {wid}",
                f"{w.get('tasks_completed', 0)} tasks",
                f"Queue: {w.get('queue_len', 0)}",
            )
            cur = w.get("current_task_id")
            if cur:
                st.caption(f"Running Task #{cur}")
            steal_info = f"Stolen: {w.get('tasks_stolen_by', 0)} | Attempts: {w.get('steal_attempts', 0)}"
            st.caption(steal_info)
else:
    st.info("Waiting for workers to initialise…")

# ── Charts row ────────────────────────────────────────────────────────────────
c1, c2 = st.columns(2)
with c1:
    st.plotly_chart(queue_length_bar(snapshot), use_container_width=True)
with c2:
    st.plotly_chart(
        task_completion_timeline(snapshot.get("task_records", [])),
        use_container_width=True,
    )

# ── Stealing timeline ─────────────────────────────────────────────────────────
steal_events = snapshot.get("steal_events", [])
if steal_events:
    successes = sum(1 for e in steal_events if e.get("success"))
    failures  = sum(1 for e in steal_events if not e.get("success"))
    sc1, sc2, sc3 = st.columns(3)
    sc1.metric("Total Steal Events", len(steal_events))
    sc2.metric("Successful Steals", successes)
    sc3.metric("Failed Steals", failures)
    st.plotly_chart(steal_timeline(steal_events), use_container_width=True)

# ── Activity feed ─────────────────────────────────────────────────────────────
st.subheader("📋 Activity Feed")
feed = snapshot.get("activity_feed", [])
if feed:
    feed_html = "\n".join(reversed(feed[-25:]))
    st.markdown(
        f'<div class="activity-feed"><pre>{feed_html}</pre></div>',
        unsafe_allow_html=True,
    )
else:
    st.caption("No events yet…")

# ── Auto-refresh ──────────────────────────────────────────────────────────────
if auto_refresh and exp_running:
    time.sleep(1)
    st.rerun()
