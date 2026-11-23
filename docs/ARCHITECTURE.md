# System Architecture & Technical Design

## Cost-Aware Adaptive Work-Stealing Scheduler with Runtime Feedback for Multicore Systems

---

## 1. High-Level Architecture Overview

The system is designed with a strict decoupling between:
1. **The C++17 Core Engine**: High-performance multi-threaded parallel task executor, real-time lock-free event streaming, and adaptive scheduling heuristics.
2. **The Inter-Process Communication Layer**: Asynchronous streaming through standard input/output with structured JSONL messages and final summary JSON files.
3. **The Python Integration & Dashboard Layer**: Subprocess orchestration, non-blocking state reconstruction, and reactive Streamlit web dashboard.

```
+-------------------------------------------------------------------------+
|                       Streamlit Web Dashboard                            |
|  (Overview, Config, Live Monitor, Adaptive Analysis, Comparison, etc.)  |
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

## 2. Core C++ Components

### 2.1 Task Abstraction (`Task.h`, `Task.cpp`)
- **`Task` Structure**: Contains `uint64_t id`, `TaskType type`, `int complexity`, `double estimated_cost`, `double actual_duration`, `TaskStatus status`, `assigned_worker`, and `executed_by_worker`.
- **Synthetic Workload**: CPU-bound floating-point arithmetic designed to emulate computational kernel operations. Execution time scales linearly with `complexity`.

### 2.2 Thread-Safe Work-Stealing Deque (`TaskQueue.h`, `TaskQueue.cpp`)
- Implements a dual-ended queue where the worker thread pushes and pops from the **FRONT** (LIFO order, maximizing CPU cache locality for private work), while external thief threads steal from the **BACK** (FIFO order, stealing older, larger tasks).
- Mutual exclusion via `std::mutex` and `std::lock_guard`.
- Methods: `push_front`, `pop_front`, `steal_back`, `steal_batch(count)`, `size()`, `empty()`, and `estimated_remaining_cost()`.

### 2.3 Runtime Cost Estimator (`CostEstimator.h`, `CostEstimator.cpp`)
- Maintains Exponentially Weighted Moving Average (EWMA) estimates of task execution durations categorized by complexity.
- Formula:
  $$\hat{c}_t = \alpha \cdot c_t + (1 - \alpha) \cdot \hat{c}_{t-1}$$
- Provides fast $O(1)$ lookups with nearest-neighbor bucket smoothing for unseen complexities and fallback heuristics.

### 2.4 Adaptive Stealing Policy (`StealingPolicy.h`, `StealingPolicy.cpp`)
- Evaluates queue states across all workers.
- Selects victims with highest estimated remaining workload $W(v)$.
- Calculates expected steal benefit:
  $$\text{Benefit} = \frac{W(v) - W(\text{thief})}{2} - \text{Overhead}$$
- Adapts batch transfer size:
  $$\text{Batch} = \max\left(1, \min\left(\text{max\_batch}, \left\lfloor \frac{W(v) - W(\text{thief})}{2 \cdot \bar{c}} \right\rfloor\right)\right)$$

### 2.5 Runtime Event Monitor (`RuntimeMonitor.h`, `RuntimeMonitor.cpp`)
- Emits structured JSONL events to stdout with microsecond timestamps:
  - `experiment_started`, `worker_started`, `task_started`, `task_completed`
  - `victim_selected`, `steal_attempted`, `steal_succeeded`, `steal_failed`
  - `batch_stolen`, `cost_estimated`, `worker_idle`, `experiment_completed`
- Thread-safe serialization with negligible instrumentation overhead.

---

## 3. Communication Protocol

### Input Format (stdin or `--config <file>`)
```json
{
  "experiment_id": 101,
  "algorithm": "adaptive",
  "num_workers": 4,
  "num_tasks": 500,
  "workload_type": "heterogeneous",
  "min_complexity": 500,
  "max_complexity": 5000,
  "random_seed": 42,
  "results_dir": "results/experiments",
  "stealing": {
    "ewma_alpha": 0.3,
    "min_benefit_ms": 0.5,
    "overhead_ms": 0.1,
    "max_batch_size": 8,
    "min_victim_queue": 2,
    "victim_policy": "max_remaining_cost"
  }
}
```

### Event Streaming Format (stdout, JSONL)
```json
{"type":"task_completed","ts":1795336635602.445,"worker_id":0,"task_id":17,"actual_ms":0.0129,"est_cost":1.402,"queue_len":4}
{"type":"victim_selected","ts":1795336635603.085,"thief_id":1,"victim_id":3,"victim_score":1.810,"thief_queue_len":0,"victim_queue_len":4}
{"type":"steal_succeeded","ts":1795336635603.480,"thief_id":1,"victim_id":3,"tasks_stolen":1,"victim_remaining":3.820}
```

### Output Summary Format (`results/experiments/<id>/summary.json`)
Contains complete experiment metadata, total wall duration, sequential baseline duration, speedup, parallel efficiency, average worker utilization, load imbalance coefficient, total steal attempts, successes, stolen task count, scheduling overhead, MAPE, and per-worker breakdown.
