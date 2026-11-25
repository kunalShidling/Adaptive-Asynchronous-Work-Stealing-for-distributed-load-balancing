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

    algo_keys = list(ALGORITHMS.keys())
    default_algo_idx = algo_keys.index("adaptive") if "adaptive" in algo_keys else 0
    algorithm = st.selectbox(
        "Scheduling Algorithm",
        options=algo_keys,
        index=default_algo_idx,
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

    # ── Heterogeneous Nodes & Cluster Capabilities ───────────────────────────
    st.markdown("---")
    st.subheader("🌐 Heterogeneous Node Classification & Speeds")
    cluster_profile = st.selectbox(
        "Node Capability Profile",
        [
            "Heterogeneous Cluster (VERY_FAST [8x], FAST [4x], MEDIUM [2x], SLOW [1x])",
            "8-Node Heterogeneous (2 VERY_FAST, 2 FAST, 2 MEDIUM, 2 SLOW)",
            "Uniform Cluster (All nodes equal 1.0x)",
            "Custom Speeds per Worker",
        ],
        index=0,
    )

    worker_speeds = []
    heterogeneous_nodes = True
    if "8-Node" in cluster_profile:
        worker_speeds = [8.0, 8.0, 4.0, 4.0, 2.0, 2.0, 1.0, 1.0]
        num_workers = 8
    elif "Uniform" in cluster_profile:
        heterogeneous_nodes = False
        worker_speeds = [1.0] * int(num_workers)
    elif "Custom" in cluster_profile:
        speeds_str = st.text_input("Worker Speeds (comma-separated)", "8.0, 4.0, 2.0, 1.0")
        try:
            worker_speeds = [float(x.strip()) for x in speeds_str.split(",") if x.strip()]
        except Exception:
            worker_speeds = [8.0, 4.0, 2.0, 1.0]
    else:
        # 4-node 8:4:2:1 default
        worker_speeds = [8.0, 4.0, 2.0, 1.0]

    dynamic_redist = st.checkbox("Enable Dynamic Redistribution (A2WS Runtime Balancing)", value=True)

    with st.expander("🧪 Fault Injection & Node Slowdown Simulation", expanded=False):
        fcol1, fcol2 = st.columns(2)
        with fcol1:
            sim_slowdown_worker = st.number_input("Slowdown Worker ID (-1 = disabled)", -1, 15, -1)
            sim_slowdown_factor = st.number_input("New Speed Factor", 0.05, 1.0, 0.2, 0.05)
            sim_slowdown_after = st.number_input("Trigger Slowdown After Tasks", 1, 1000, 15)
        with fcol2:
            sim_fail_worker = st.number_input("Fail Worker ID (-1 = disabled)", -1, 15, -1)
            sim_fail_after = st.number_input("Trigger Failure After Tasks", 1, 1000, 15)

    # ── Adaptive-only parameters ─────────────────────────────────────────────
    if algorithm in ("adaptive", "all"):
        st.markdown("---")
        st.subheader("🧠 Adaptive Algorithm Parameters")
        p1, p2 = st.columns(2)
        with p1:
            ewma_alpha     = st.slider("EWMA Alpha (learning rate)", 0.05, 1.0, 0.3, 0.05,
                                       help="α in: est = α·actual + (1-α)·prev_est")
            min_benefit_ms = float(st.number_input("Min Steal Benefit (ms)", 0.0, 10.0, 0.001, 0.01,
                                                    help="Stealing is skipped if benefit < threshold"))
            overhead_ms    = float(st.number_input("Estimated Steal Overhead (ms)", 0.0, 5.0, 0.001, 0.01))
        with p2:
            max_batch_size    = int(st.number_input("Max Batch Size", 1, 32, 16))
            min_victim_queue  = int(st.number_input("Min Victim Queue Size", 1, 20, 1))
            victim_policy     = st.selectbox(
                "Victim Selection Policy",
                ["max_remaining_cost", "max_queue", "random"],
                help="max_remaining_cost: pick victim with most remaining estimated work",
            )
    else:
        ewma_alpha, min_benefit_ms, overhead_ms = 0.3, 0.001, 0.001
        max_batch_size, min_victim_queue, victim_policy = 16, 1, "max_remaining_cost"

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
        heterogeneous_nodes=heterogeneous_nodes,
        worker_speeds=worker_speeds,
        dynamic_redistribution=dynamic_redist,
        simulate_slowdown_worker=int(sim_slowdown_worker),
        simulate_slowdown_factor=float(sim_slowdown_factor),
        simulate_slowdown_after_tasks=int(sim_slowdown_after),
        simulate_fail_worker=int(sim_fail_worker),
        simulate_fail_after_tasks=int(sim_fail_after),
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
