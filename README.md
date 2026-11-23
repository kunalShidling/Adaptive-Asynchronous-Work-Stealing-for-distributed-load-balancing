# Cost-Aware Adaptive Work-Stealing Scheduler with Runtime Feedback for Multicore Systems

A research-grade shared-memory multicore scheduling framework and interactive analytics dashboard inspired by the **Adaptive Asynchronous Work-Stealing (A2WS)** paradigm.

---

## 1. Project Overview & Research Motivation

In modern shared-memory multicore architectures, efficient load balancing across heterogeneous and dynamically varying workloads remains a critical challenge. Traditional work-stealing schedulers (such as the classical Cilk model) rely on uniform random victim selection and single-task migration. While provably optimal for unweighted, fully strict fork-join task graphs under idealized assumptions, random work-stealing encounters significant performance degradation under:

1. **Heterogeneous Task Complexities**: Randomly choosing a victim with a single tiny task yields negligible benefit while paying the cache-invalidation penalty of stealing.
2. **Initially Imbalanced Queues**: When workloads are front-loaded or clustered on specific cores, random single-task steals require excessive inter-core communication rounds to balance the system.
3. **Cache & Synchronization Overhead**: Fruitless steal attempts to near-empty or balanced queues generate unnecessary memory bus contention and lock acquisition overhead.

### The Research Question
Can runtime task-cost estimation combined with cost-aware adaptive work-stealing reduce scheduling overhead, minimize worker idle times, and improve parallel speedup on multicore systems?

---

## 2. Research Problem & Identified Gap

The research paper **"Adaptive Asynchronous Work-Stealing for Distributed Load-Balancing in Heterogeneous Systems (A2WS)"** established that distributed systems benefit substantially when stealing decisions account for node capacity and queue weights. However, adapting these concepts to shared-memory multicore platforms presents unique architectural differences:

- **Ultra-low latency constraints**: Multicore stealing decisions occur in microseconds, requiring lightweight $O(1)$ heuristics rather than heavy distributed message exchanges.
- **Cache hierarchy awareness**: Transferring tasks between core-private caches (L1/L2) must be justified by the computational weight of the migrated tasks.
- **Dynamic cost feedback**: Shared-memory systems frequently exhibit variable task runtimes due to memory bandwidth saturation, hardware prefetching, and CPU frequency scaling.

---

## 3. Proposed Solution

Our engine implements **Cost-Aware Adaptive Work-Stealing (CAWS)**:

1. **Exponentially Weighted Moving Average (EWMA) Cost Estimator**: Learns task duration distributions per complexity bucket at runtime without offline profiling.
2. **Remaining Workload Estimation**: Continuously computes the aggregate computational weight in each worker's local queue.
3. **Excess-Workload Victim Selection**: Targets the worker with the highest surplus workload relative to the idle thief.
4. **Cost-Benefit Gating**: Aborts stealing attempts if the expected load equalization benefit does not exceed the estimated steal overhead.
5. **Adaptive Multi-Task Batch Sizing**: Dynamically computes the optimal batch size to equalize load in a single atomic transaction.

---

## 4. System Architecture

```
+-------------------------------------------------------------------------+
|                       Streamlit Web Dashboard                            |
|    (Overview, Workload Config, Live Monitor, Analysis, Comparisons)     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                         Python Integration Layer                        |
|  EngineRunner (Subprocess) <---> EventProcessor (Thread-Safe Event Bus) |
+-------------------------------------------------------------------------+
        |  stdin (JSON Config)             ^ stdout (JSONL Real-Time Events)
        v                                  |
+-------------------------------------------------------------------------+
|                         C++17 Scheduling Engine                         |
|                                                                         |
|  +--------------------+     +-------------------+     +---------------+ |
|  | WorkloadGenerator  | --> |     Scheduler     | --> | RuntimeMonitor| |
|  +--------------------+     +-------------------+     +---------------+ |
|                               /       |       \                         |
|                              v        v        v                        |
|                     Static   Dynamic   Random   Adaptive                |
|                                        Stealing  Stealing               |
|                                                     |                   |
|                                        +------------+------------+      |
|                                        |                         |      |
|                                        v                         v      |
|                                  CostEstimator            StealingPolicy|
|                                  (EWMA Filter)             (Decision &  |
|                                                           Batch Sizing) |
+-------------------------------------------------------------------------+
                                    |
                                    v
                         results/experiments/<id>/
                         ├── summary.json
                         └── comparison.json
```

---

## 5. The Four Scheduling Algorithms

All algorithms execute on **identical task sets** generated from the same random seed for fair, reproducible comparisons:

1. **Static Scheduling (`StaticScheduler.cpp`)**: Pre-assigns tasks round-robin before execution. No migration or runtime communication occurs. Establishes the non-adaptive baseline.
2. **Centralized Dynamic Scheduling (`DynamicScheduler.cpp`)**: A single shared task queue protected by mutex synchronization. Workers pop tasks dynamically as they become free.
3. **Traditional Random Work-Stealing (`WorkStealingScheduler.cpp`)**: Workers maintain local double-ended queues (push/pop front). Idle workers randomly select victims and steal 1 task from the back.
4. **Proposed Cost-Aware Adaptive Work-Stealing (`AdaptiveScheduler.cpp`)**: Combines EWMA cost estimation, workload-directed victim scoring, overhead threshold gating, and dynamic batch sizing.

---

## 6. Technology Stack

- **C++ Scheduling Engine**: C++17 (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`, `std::chrono`, `std::deque`), compiled with GCC 13.2.0 (MinGW-w64).
- **Lightweight JSON**: `nlohmann/json` (single-header C++ library).
- **Python Integration Layer**: Python 3.12, `subprocess`, non-blocking multi-threading event queues.
- **Interactive Dashboard**: Streamlit 1.45.1, Plotly 6.1.1, Pandas 2.2.3, NumPy 1.26.4.

---

## 7. Installation & Setup

### Prerequisites
- Python 3.10+ (tested on Python 3.12)
- C++17 compatible compiler (MinGW-w64 GCC 13.2+ included in `d:\mingw64`)

### Setup Instructions
```cmd
# 1. Clone or navigate to the project directory
cd d:\MAP_PROJECT

# 2. Install Python dependencies
pip install streamlit plotly pandas numpy

# 3. Build the C++ Engine (static binary)
scripts\build.bat
```

---

## 8. Execution & Running the Dashboard

### Launching the Interactive Web UI
```cmd
# Launch via batch script
scripts\run.bat

# Or launch directly
python -m streamlit run python/app.py
```
Open your browser at `http://localhost:8501`.

### Running Automated Test Suites
```cmd
# Run C++ unit, concurrency, and algorithm tests
d:\MAP_PROJECT\build\test_correctness.exe

# Run Python integration and verification tests
python tests/python/test_integration.py
```

### Running Automated Benchmarks
```cmd
python scripts/benchmark.py
```
Benchmark reports are saved to `results/reports/benchmark_summary.csv`.

---

## 9. Dashboard Features & Page Guide

- **Page 1: Overview Dashboard**: High-level KPI summary cards, recent experiment history, and latest utilization charts.
- **Page 2: Workload Configuration**: Granular parameter sliders for workers (1-16), tasks (1-50,000), workload types (`uniform`, `heterogeneous`, `imbalanced`), random seeds, and adaptive tuning parameters.
- **Page 3: Live Execution Monitor**: Real-time streaming worker state cards (Running, Idle, Stealing, Stopped), queue length bars, cumulative progress lines, and monospace activity feed.
- **Page 4: Adaptive Scheduler Analysis**: LaTeX mathematical formulations, cost estimation scatter plots (Estimated vs Actual), MAPE error metrics, and detailed stealing decision logs.
- **Page 5: Algorithm Comparison**: Side-by-side bar charts for Wall Time, Speedup, Efficiency, Load Imbalance, and Steal Success Rate across all 4 algorithms.
- **Page 6: Performance Analytics**: Deep-dive performance analytics with per-worker busy time breakdowns and full JSON inspection.
- **Page 7: Experiment History & Reports**: Historical database with multi-select filtering, CSV download, and JSON exports.

---

## 10. Performance Metrics & Mathematical Formulations

- **Speedup**: $S = \frac{T_{\text{sequential}}}{T_{\text{wall}}}$
- **Parallel Efficiency**: $E = \frac{S}{W}$ where $W$ is worker count.
- **Load Imbalance**: Normalised coefficient of variation across worker busy times:
  $$\lambda = \frac{\sigma_{\text{busy}}}{\mu_{\text{busy}}}$$
- **Steal Success Rate**: $R_{\text{steal}} = \frac{S_{\text{steal}}}{A_{\text{steal}}}$
- **Mean Absolute Percentage Error (MAPE)**:
  $$\text{MAPE} = \frac{1}{N} \sum_{i=1}^N \left| \frac{\hat{c}_i - c_i}{c_i} \right| \times 100\%$$

---

## 11. Experimental Results & Findings

Empirical findings from the benchmark suite (`results/reports/benchmark_summary.csv`):

1. **Heterogeneous Workloads**: Cost-aware adaptive stealing achieved the highest speedup (up to **4.22x** on 4 workers) and the lowest load imbalance ($\lambda = 0.051$ vs $\lambda = 0.081$ for static).
2. **Imbalanced Workloads**: Dynamic multi-task batch sizing allowed adaptive stealing to rebalance overloaded workers in 1-2 transactions instead of 20-30 repetitive single-task steals.
3. **Overhead Minimization**: By gating unprofitable steals through the cost-benefit threshold, the adaptive scheduler avoided wasted cache line invalidations when queues were balanced.

---

## 12. Project Structure

```
cost-aware-adaptive-scheduler/
├── CMakeLists.txt
├── README.md
├── cpp/
│   ├── include/
│   │   ├── Task.h
│   │   ├── TaskQueue.h
│   │   ├── Worker.h
│   │   ├── Scheduler.h
│   │   ├── StaticScheduler.h
│   │   ├── DynamicScheduler.h
│   │   ├── WorkStealingScheduler.h
│   │   ├── AdaptiveScheduler.h
│   │   ├── CostEstimator.h
│   │   ├── StealingPolicy.h
│   │   ├── WorkloadGenerator.h
│   │   ├── RuntimeMonitor.h
│   │   └── Metrics.h
│   ├── src/
│   │   ├── Task.cpp
│   │   ├── TaskQueue.cpp
│   │   ├── StaticScheduler.cpp
│   │   ├── DynamicScheduler.cpp
│   │   ├── WorkStealingScheduler.cpp
│   │   ├── AdaptiveScheduler.cpp
│   │   ├── CostEstimator.cpp
│   │   ├── StealingPolicy.cpp
│   │   ├── WorkloadGenerator.cpp
│   │   ├── RuntimeMonitor.cpp
│   │   └── Metrics.cpp
│   ├── vendor/
│   │   └── nlohmann/json.hpp
│   └── main.cpp
├── python/
│   ├── app.py
│   ├── config.py
│   ├── engine_runner.py
│   ├── event_processor.py
│   ├── result_manager.py
│   ├── metrics.py
│   ├── charts.py
│   └── pages/
│       ├── 1_Overview.py
│       ├── 2_Workload_Configuration.py
│       ├── 3_Live_Monitor.py
│       ├── 4_Adaptive_Analysis.py
│       ├── 5_Algorithm_Comparison.py
│       ├── 6_Performance_Analytics.py
│       └── 7_Experiment_History.py
├── tests/
│   ├── cpp/
│   │   └── test_correctness.cpp
│   └── python/
│       └── test_integration.py
├── scripts/
│   ├── build.bat
│   ├── run.bat
│   └── benchmark.py
├── configs/
│   └── test_config.json
├── results/
│   ├── experiments/
│   └── reports/
│       └── benchmark_summary.csv
└── docs/
    ├── ARCHITECTURE.md
    ├── SCHEDULING_ALGORITHMS.md
    ├── COST_ESTIMATION.md
    ├── METRICS.md
    ├── EXPERIMENTS.md
    └── UI_GUIDE.md
```

---

## 13. Limitations & Future Extensions

- **NUMA Topologies**: Future work can extend the victim selection heuristic to account for Non-Uniform Memory Access (NUMA) distance matrices between CPU sockets.
- **Machine Learning Predictors**: A regression model (e.g., Random Forest or Gradient Boosting) could replace EWMA when task features include memory footprint and cache access counters.
- **Hardware Performance Counters**: Real-time integration with `perf` or Intel RAPL to incorporate CPU energy consumption directly into the stealing cost function.
