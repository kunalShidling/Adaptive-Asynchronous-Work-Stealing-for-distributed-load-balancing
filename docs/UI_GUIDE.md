# Streamlit Dashboard User Interface Guide

This document describes the layout, navigation, visual elements, and interactive controls of the Streamlit web dashboard.

---

## 1. Launching the Dashboard

From the project root:
```cmd
# Using the launch batch script
scripts\run.bat

# Or directly with Python
python -m streamlit run python/app.py
```
Open a web browser at: `http://localhost:8501`

---

## 2. Page Navigation & Capabilities

### Page 1: Overview Dashboard (`pages/1_Overview.py`)
- **Top KPI Metrics**: Total experiments executed, latest wall time, speedup factor, and steal success rate.
- **Empty State**: Welcoming instructions and step-by-step onboarding when no experiments have yet run.
- **Recent Experiments**: Interactive sortable table displaying recent experiment IDs, algorithms, task counts, and efficiency.
- **Latest Experiment Spotlight**: Real-time worker utilization bar chart and donut chart of task distribution.

### Page 2: Workload Configuration (`pages/2_Workload_Configuration.py`)
- **Algorithm Selector**: Choose between Static, Centralized Dynamic, Random Work-Stealing, Adaptive Work-Stealing, or All (Side-by-Side Comparison).
- **Core Parameters**:
  - Worker threads slider: 1 to 16
  - Number of tasks input: 1 to 50,000
  - Workload type: `Uniform`, `Heterogeneous`, `Initially Imbalanced`
  - Random seed input for reproducibility
  - Min and Max task complexity sliders
- **Adaptive Parameter Tuning** (conditionally visible for adaptive/all modes):
  - EWMA Alpha slider (learning rate $\alpha \in [0.05, 1.0]$)
  - Minimum Steal Benefit threshold $\theta_{\min}$ (ms)
  - Estimated Steal Overhead (ms)
  - Maximum Batch Size cap ($B_{\max}$)
  - Minimum Victim Queue Length threshold
  - Victim Selection Policy (`max_remaining_cost`, `max_queue`, `random`)
- **Live Validation**: Displays instant validation alerts for invalid parameter combinations.
- **Action Buttons**: ▶️ Start Experiment, 🔄 Reset Config, ⏹ Stop Running Experiment.

### Page 3: Live Execution Monitor (`pages/3_Live_Monitor.py`)
- **Live State Streaming**: Connects directly to the background C++ subprocess event queue.
- **Progress Bar**: Real-time task completion tracking ($X / N$ tasks completed).
- **Worker Cards**: Live state indicators (🟢 Running, ⚪ Idle, 🟡 Stealing, 🔴 Stopped), currently executed task ID, and local queue length.
- **Interactive Visualizations**:
  - Current Queue Lengths per worker
  - Cumulative Task Completions over time
  - Stealing Events timeline (successful vs failed attempts)
- **Activity Feed**: Monospace streaming log of actual runtime events (e.g., `Worker 2 completed Task 45 (1.2ms)`, `Worker 1 stole 3 tasks from Worker 3`).

### Page 4: Adaptive Scheduler Analysis (`pages/4_Adaptive_Analysis.py`)
- **Mathematical Formulations**: LaTeX-rendered equations for EWMA cost estimation, victim scoring, benefit thresholding, and batch sizing.
- **Cost Estimation Accuracy**: Scatter plot comparing estimated cost vs. actual measured duration against a 1:1 perfect prediction line.
- **MAPE Metric**: Mean Absolute Percentage Error calculation with accuracy classification badges.
- **Stealing Timeline & Decision Log**: Tabular breakdown of every evaluated steal point, including thief ID, victim ID, expected benefit, batch size, and approval/rejection reason.

### Page 5: Algorithm Comparison (`pages/5_Algorithm_Comparison.py`)
- **Side-by-Side Visuals**:
  - Wall-clock execution time
  - Speedup relative to sequential baseline
  - Parallel efficiency
  - Average worker utilization
  - Load imbalance coefficient
  - Steal success rate
- **Comprehensive Comparison Table**: Full metric matrix for the selected comparison experiment.
- **Interpretation Guide**: Research guidelines on analyzing trade-offs across different workload categories.

### Page 6: Performance Analytics (`pages/6_Performance_Analytics.py`)
- **Executive Metric Cards**: Wall time, Sequential time, Speedup, Efficiency, Load Imbalance, Average Utilization, Steal Rate, and Scheduling Overhead.
- **Worker Utilization Breakdown**: Per-worker busy/idle times and percentage utilization bar chart.
- **Task Distribution**: Donut chart showing task completion share per worker thread.
- **Export & Inspect**: Full JSON viewer of raw metric payloads.

### Page 7: Experiment History & Reports (`pages/7_Experiment_History.py`)
- **Historical Table**: Comprehensive registry of all past experiments.
- **Filtering**: Multi-select filters by algorithm, workload type, and worker count.
- **Exports**:
  - Download entire history as CSV
  - Download single-experiment summary JSON
  - Download per-worker breakdown CSV
