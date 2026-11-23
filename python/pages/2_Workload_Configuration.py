"""Page 2 – Workload Configuration & Experiment Launcher."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import streamlit as st
from config import (
    ExperimentConfig, StealingConfig,
    ALGORITHMS, WORKLOAD_TYPES, EXECUTABLE,
    next_experiment_id,
)
from engine_runner import EngineRunner
from result_manager import save_experiment

st.set_page_config(page_title="Configure Experiment", page_icon="⚙️", layout="wide")

st.markdown("""
<style>
.stApp { background-color: #0e1117; }
section[data-testid="stSidebar"] { background-color: #161b2e; }
</style>
""", unsafe_allow_html=True)

st.title("⚙️ Workload Configuration")

# ── Session state initialisation ─────────────────────────────────────────────
for key, default in [("runner", None), ("exp_running", False), ("last_config", None)]:
    if key not in st.session_state:
        st.session_state[key] = default

# ── Engine build check ───────────────────────────────────────────────────────
engine_built = os.path.exists(EXECUTABLE)
if not engine_built:
    st.warning(
        f"⚠️ C++ engine not found at:\n```\n{EXECUTABLE}\n```\n"
        "Build it first by running:\n```\nscripts\\build.bat\n```\n"
        "or from the project root:\n"
        "```\ncmake -S . -B build -G \"MinGW Makefiles\"\ncmake --build build\n```"
    )

# ── Auto-complete a finished run ─────────────────────────────────────────────
if st.session_state.exp_running and st.session_state.runner:
    runner: EngineRunner = st.session_state.runner
    if runner.is_done():
        st.session_state.exp_running = False
        summary = runner.get_summary()
        if summary:
            save_experiment(summary)
            st.success("✅ Experiment completed! View results in Performance Analytics.")
        elif runner.processor.error:
            st.error(f"❌ Engine error:\n{runner.processor.error}")

# ── Layout ───────────────────────────────────────────────────────────────────
col_left, col_right = st.columns([3, 2])

with col_left:
    st.subheader("Experiment Settings")

    algorithm = st.selectbox(
        "Scheduling Algorithm",
        options=list(ALGORITHMS.keys()),
        format_func=lambda k: ALGORITHMS[k],
    )

    r1c1, r1c2 = st.columns(2)
    with r1c1:
        num_workers = st.slider("Worker Threads", 1, 16, 4)
        workload_type = st.selectbox(
            "Workload Type",
            list(WORKLOAD_TYPES.keys()),
            format_func=lambda k: WORKLOAD_TYPES[k],
        )
    with r1c2:
        num_tasks = st.number_input("Number of Tasks", 1, 50000, 200, step=50)
        random_seed = st.number_input("Random Seed", 0, 99999, 42)

    r2c1, r2c2 = st.columns(2)
    with r2c1:
        min_complexity = int(st.number_input("Min Task Complexity", 100, 500000, 500, step=100))
    with r2c2:
        max_complexity = int(st.number_input("Max Task Complexity", 100, 1000000, 10000, step=500))

    # ── Adaptive-only parameters ─────────────────────────────────────────────
    if algorithm in ("adaptive", "all"):
        st.markdown("---")
        st.subheader("🧠 Adaptive Algorithm Parameters")
        p1, p2 = st.columns(2)
        with p1:
            ewma_alpha     = st.slider("EWMA Alpha (learning rate)", 0.05, 1.0, 0.3, 0.05,
                                       help="α in: est = α·actual + (1-α)·prev_est")
            min_benefit_ms = float(st.number_input("Min Steal Benefit (ms)", 0.0, 10.0, 0.5, 0.1,
                                                    help="Stealing is skipped if benefit < threshold"))
            overhead_ms    = float(st.number_input("Estimated Steal Overhead (ms)", 0.0, 5.0, 0.1, 0.05))
        with p2:
            max_batch_size    = int(st.number_input("Max Batch Size", 1, 32, 8))
            min_victim_queue  = int(st.number_input("Min Victim Queue Size", 1, 20, 2))
            victim_policy     = st.selectbox(
                "Victim Selection Policy",
                ["max_remaining_cost", "max_queue", "random"],
                help="max_remaining_cost: pick victim with most remaining estimated work",
            )
    else:
        ewma_alpha, min_benefit_ms, overhead_ms = 0.3, 0.5, 0.1
        max_batch_size, min_victim_queue, victim_policy = 8, 2, "max_remaining_cost"

with col_right:
    st.subheader("Configuration Summary")
    config = ExperimentConfig(
        algorithm=algorithm,
        num_workers=int(num_workers),
        num_tasks=int(num_tasks),
        workload_type=workload_type,
        min_complexity=min_complexity,
        max_complexity=max_complexity,
        random_seed=int(random_seed),
        stealing=StealingConfig(
            ewma_alpha=ewma_alpha,
            min_benefit_ms=min_benefit_ms,
            overhead_ms=overhead_ms,
            max_batch_size=max_batch_size,
            min_victim_queue=min_victim_queue,
            victim_policy=victim_policy,
        ),
    )
    errors = config.validate()

    if errors:
        for e in errors:
            st.error(e)
    else:
        st.success("✅ Configuration valid")

    with st.expander("View JSON Config", expanded=False):
        st.code(config.to_json(), language="json")

    # Status of the running experiment
    if st.session_state.exp_running:
        runner = st.session_state.runner
        if runner and not runner.is_done():
            snap = runner.processor.get_snapshot()
            done = snap.get("tasks_completed_total", 0)
            total = config.num_tasks
            st.progress(min(1.0, done / max(total, 1)),
                        text=f"⏳ {done}/{total} tasks completed")

# ── Action buttons ────────────────────────────────────────────────────────────
st.markdown("---")
b1, b2, b3 = st.columns([3, 1, 1])

with b1:
    start_disabled = bool(errors) or not engine_built or st.session_state.exp_running
    if st.button("▶️ Start Experiment", disabled=start_disabled,
                 type="primary", use_container_width=True):
        config.experiment_id = next_experiment_id()
        runner = EngineRunner(config)
        runner.start()
        st.session_state.runner = runner
        st.session_state.exp_running = True
        st.session_state.last_config = config
        st.success(
            f"✅ Experiment **#{config.experiment_id}** started! "
            "Open 🔴 Live Monitor to watch progress."
        )
        st.page_link("pages/3_Live_Monitor.py", label="🔴 Open Live Monitor →")

with b2:
    if st.button("🔄 Reset", use_container_width=True):
        st.rerun()

with b3:
    if st.session_state.exp_running:
        if st.button("⏹ Stop", type="secondary", use_container_width=True):
            if st.session_state.runner:
                st.session_state.runner.stop()
            st.session_state.exp_running = False
            st.warning("Experiment stopped by user.")
            st.rerun()
