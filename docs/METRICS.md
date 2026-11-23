# Performance Metrics Specification

This document details the definition, mathematical formula, source of instrumentation, and interpretation for every performance metric calculated and displayed in the research dashboard.

---

## 1. Primary Metrics Table

| Metric Name | Mathematical Formula | Measurement Source | Units | Better When |
|-------------|----------------------|--------------------|-------|-------------|
| **Wall Clock Time** | $T_{\text{wall}} = t_{\text{end}} - t_{\text{start}}$ | `std::chrono::high_resolution_clock` | ms | Lower |
| **Sequential Baseline Time** | $T_{\text{seq}} = \sum_{i=1}^N c_i$ | Single-thread run on identical tasks | ms | Reference |
| **Speedup** | $S = \frac{T_{\text{seq}}}{T_{\text{wall}}}$ | Calculated | $\times$ ratio | Higher ($\le W$) |
| **Parallel Efficiency** | $E = \frac{S}{W} = \frac{T_{\text{seq}}}{W \cdot T_{\text{wall}}}$ | Calculated ($W = \text{worker count}$) | Fraction [0, 1] | Higher ($\le 1.0$) |
| **Worker Busy Time** | $B_w = \sum_{t \in \text{done}(w)} c_t$ | Per-worker atomic accumulator | ms | Balanced |
| **Worker Utilization** | $U_w = \frac{B_w}{T_{\text{wall}}}$ | Calculated per worker | % [0, 100] | Higher |
| **Average Utilization** | $\bar{U} = \frac{1}{W} \sum_{w=1}^W U_w$ | Mean across all workers | % | Higher |
| **Load Imbalance** | $\lambda = \frac{\sigma_{\text{busy}}}{\mu_{\text{busy}}}$ | Coefficient of Variation of $B_w$ | Normalized ratio | Lower ($\to 0$) |
| **Steal Attempts** | $A_{\text{steal}} = \sum_w A_w$ | Atomic counter across all workers | Count | Contextual |
| **Steal Successes** | $S_{\text{steal}} = \sum_w S_w$ | Atomic counter across all workers | Count | Contextual |
| **Steal Success Rate** | $R_{\text{steal}} = \frac{S_{\text{steal}}}{A_{\text{steal}}}$ | Calculated (0 if $A=0$) | % [0, 100] | Higher |
| **Tasks Stolen** | $K_{\text{stolen}}$ | Atomic counter of tasks transferred | Count | Contextual |
| **Scheduling Overhead** | $O_{\text{sched}} = \sum \Delta t_{\text{lock/heuristic}}$ | High-precision timer around lock/stealing | ms | Lower |
| **Cost Estimation MAPE** | $\text{MAPE} = \frac{1}{N} \sum \frac{\|\hat{c}_i - c_i\|}{c_i} \times 100$ | Measured duration vs prior estimate | % | Lower ($\to 0$) |

---

## 2. Metric Formulations & Nuances

### 2.1 Load Imbalance Formulation
Load imbalance is computed using the **Normalized Standard Deviation (Coefficient of Variation)** across worker busy times:

$$\mu_{\text{busy}} = \frac{1}{W} \sum_{w=1}^W B_w$$
$$\sigma_{\text{busy}} = \sqrt{\frac{1}{W} \sum_{w=1}^W (B_w - \mu_{\text{busy}})^2}$$
$$\lambda = \begin{cases} \frac{\sigma_{\text{busy}}}{\mu_{\text{busy}}} & \text{if } \mu_{\text{busy}} > 10^{-9} \\ 0.0 & \text{otherwise} \end{cases}$$

- $\lambda = 0$: Perfectly balanced workload across all threads.
- $\lambda > 0.3$: Noticeable imbalance; some threads spent significant time waiting or idle.

### 2.2 Scheduling Overhead Isolation
Scheduling overhead is explicitly measured separately from task execution time:
- In `DynamicScheduler`: Accumulated time spent waiting for and holding `global_queue_` mutex locks.
- In `AdaptiveScheduler`: Accumulated time spent querying worker queue lengths, computing victim scores, and evaluating cost-benefit thresholds.
- Task execution loops never include scheduling overhead in `task.actual_duration`.

### 2.3 Safe Division Handling
To guarantee stability in automated benchmarks:
- If $T_{\text{wall}} \le 0$, Speedup and Efficiency default to `0.0`.
- If $A_{\text{steal}} = 0$, Steal Success Rate defaults to `0.0`.
- If $c_i \le 0.001$ ms, MAPE computation ignores task $i$ to prevent division by floating-point zero.
