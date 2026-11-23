# Experimentation & Reproducibility Guide

This guide describes how to generate synthetic workloads, configure benchmarks, reproduce experimental results, and interpret findings.

---

## 1. Synthetic Workload Types

All comparison runs generate a **single reproducible task set** using a fixed random seed (`std::mt19937`). All four algorithms receive deep copies of the exact same task instances.

### 1. Uniform Workload (`uniform`)
- **Characteristics**: Task complexities vary minimally around the midpoint:
  $$\text{complexity} \in \left[\text{mid} - \Delta, \text{mid} + \Delta\right], \quad \Delta = \frac{\text{max} - \text{min}}{10}$$
- **Hypothesis**: Static and dynamic schedulers perform adequately because natural load variance is low. Work-stealing schedulers should have few steal attempts.

### 2. Heterogeneous Workload (`heterogeneous`)
- **Characteristics**: Task complexities are uniformly distributed across the entire spectrum:
  $$\text{complexity} \sim \mathcal{U}(\text{min\_complexity}, \text{max\_complexity})$$
- **Hypothesis**: Static scheduling suffers from load imbalance due to uneven task distribution. Work-stealing improves balance; adaptive stealing reduces probe overhead.

### 3. Initially Imbalanced Workload (`imbalanced`)
- **Characteristics**: Front-loaded with heavy tasks (20% of tasks have 90% of max complexity) assigned to initial workers, while other workers receive lightweight tasks (10% of min complexity).
- **Hypothesis**: Emphasizes the need for adaptive work-stealing. Random stealing takes multiple 1-task steals to relieve overloaded workers, whereas adaptive stealing transfers large batches directly from the overloaded workers.

---

## 2. Automated Benchmark Execution

To execute the automated benchmark suite comparing all 4 algorithms across multiple configurations:

```cmd
python scripts/benchmark.py
```

This runs 9 benchmark scenarios:
- Workloads: `uniform`, `heterogeneous`, `imbalanced`
- Worker Threads: `2`, `4`, `8`
- Total Tasks: `300` per scenario
- Total runs: 36 scheduler executions

Output is saved to:
`results/reports/benchmark_summary.csv`

---

## 3. Command Line Interface

The C++ engine executable (`build/scheduler.exe`) accepts configuration via standard input (stdin) or through the `--config` flag:

```cmd
# Running with config file
d:\MAP_PROJECT\build\scheduler.exe --config configs/test_config.json

# Running with stdin pipe
type configs\test_config.json | d:\MAP_PROJECT\build\scheduler.exe
```
