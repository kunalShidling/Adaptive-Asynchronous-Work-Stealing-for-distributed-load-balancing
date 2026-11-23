# Scheduling Algorithms Specification

This document details the operational principles, data structures, and mathematical formulations of the four scheduling algorithms implemented in the research engine.

---

## Algorithm 1: Static Scheduling (`StaticScheduler.cpp`)

### Concept
Static block or round-robin distribution pre-assigns tasks to worker threads before execution commences. Once assigned, task queues are fixed; no task migration or stealing occurs at runtime.

### Execution Flow
1. Measure sequential execution time on a baseline task set.
2. Distribute $N$ tasks to $W$ workers using round-robin mapping: worker $i$ receives task $j$ if $j \pmod W = i$.
3. Launch $W$ threads, each draining its local `TaskQueue`.
4. When a worker's queue becomes empty, it enters the `Stopped` state immediately.
5. All threads join; calculate wall time and variance in worker busy times.

### Characteristics & Trade-offs
- **Overhead**: Virtually zero runtime scheduling overhead.
- **Vulnerability**: Susceptible to severe load imbalance under heterogeneous or front-loaded workloads. The execution time is bounded by the slowest or most heavily loaded worker:
  $$T_{\text{static}} = \max_{w \in W} \sum_{t \in \text{queue}(w)} c(t)$$

---

## Algorithm 2: Centralized Dynamic Scheduling (`DynamicScheduler.cpp`)

### Concept
All submitted tasks are maintained in a single globally shared task queue. Worker threads compete to acquire tasks on demand as they complete previous work.

### Execution Flow
1. All $N$ tasks are placed in `std::deque<Task> global_queue_`.
2. $W$ worker threads loop continuously:
   - Acquire `std::unique_lock<std::mutex>`.
   - Pop one task from `global_queue_`.
   - Release the lock.
   - Execute the task.
   - Signal other workers via `std::condition_variable` when work completes or queue is empty.
3. Termination is signaled when `tasks_remaining == 0`.

### Characteristics & Trade-offs
- **Load Balancing**: Automatically balances load; faster workers process more tasks.
- **Bottleneck**: Centralized lock contention scales poorly with thread count ($W \ge 8$). Scheduling overhead increases linearly with the frequency of task acquisitions.

---

## Algorithm 3: Traditional Random Work-Stealing (`WorkStealingScheduler.cpp`)

### Concept
Each worker maintains a private, double-ended queue (`TaskQueue`). Tasks are initially distributed round-robin. Workers execute tasks locally; when a worker becomes idle, it randomly chooses a victim worker and attempts to steal a task.

### Execution Flow
1. Owner worker pushes and pops from the **front** of its deque (LIFO order).
2. When the local queue is empty, the worker becomes a **thief**:
   - Randomly pick victim $v \in \{0, \dots, W-1\} \setminus \{\text{self}\}$.
   - Attempt to steal one task from the **back** of $v$'s deque (FIFO order).
   - If successful, push stolen task to own queue and resume execution.
   - If empty, yield execution briefly and retry.
3. Termination is detected when global `tasks_remaining == 0`.

### Characteristics & Trade-offs
- **Decentralized**: Avoids a centralized bottleneck.
- **Shortcomings**:
  - Steal attempts are uniform and blind to victim queue lengths.
  - Idle workers frequently query victims that are also idle, generating failed steal overhead.
  - Steals a fixed single task regardless of the magnitude of workload imbalance.

---

## Algorithm 4: Cost-Aware Adaptive Work-Stealing (`AdaptiveScheduler.cpp`)

### Primary Research Contribution
Adapts distributed load-balancing principles (A2WS) to shared-memory multicore architectures using **runtime cost estimation** and **adaptive multi-task transfers**.

### 1. Runtime Cost Learning
Tasks are grouped by complexity bucket. The scheduler updates an Exponentially Weighted Moving Average (EWMA) model after each completed task:
$$\hat{c}_t(k) = \alpha \cdot c_{\text{actual}} + (1 - \alpha) \cdot \hat{c}_{\text{prev}}(k)$$
where $\alpha \in (0, 1]$ is the user-configured learning rate.

### 2. Remaining Workload Estimation
For each worker queue $w$, the remaining execution workload is estimated as:
$$W(w) = \sum_{t \in \text{queue}(w)} \hat{c}(t.\text{complexity})$$

### 3. Workload-Directed Victim Selection
Instead of random probing, the thief queries all worker queues and scores potential victims by excess workload:
$$\text{score}(v) = W(v) - W(\text{thief})$$
The victim with the highest positive score is chosen:
$$v^* = \arg\max_{v \neq \text{thief}} \text{score}(v)$$

### 4. Cost-Benefit Gating
Stealing incurs synchronization and memory bus invalidation overhead $c_{\text{overhead}}$. Stealing is approved only if:
$$\text{Expected Benefit} = \frac{\text{score}(v^*)}{2} - c_{\text{overhead}} > \theta_{\min}$$
If the expected benefit falls below the threshold $\theta_{\min}$, the steal attempt is **aborted**, preventing fruitless cache contention.

### 5. Adaptive Batch Size Sizing
Rather than stealing an arbitrary single task, the batch size adapts dynamically to equalize workloads:
$$\text{batch\_size} = \left\lfloor \frac{\text{score}(v^*)}{2 \cdot \bar{c}_{v^*}} \right\rfloor$$
clamped to $[1, \text{max\_batch\_size}]$, where $\bar{c}_{v^*}$ is the average estimated cost per task in the victim's queue.

---

## Summary Comparison

| Dimension | Static | Centralized Dynamic | Random Work-Stealing | Adaptive Work-Stealing |
|-----------|--------|---------------------|----------------------|------------------------|
| Queue Architecture | Distributed (isolated) | Single shared queue | Distributed (stealing) | Distributed (cost-aware stealing) |
| Runtime Synchronization | None | High (single lock) | Low (per-queue lock) | Low (per-queue lock + heuristic gating) |
| Steal Victim Selection | N/A | N/A | Uniform Random | Max Estimated Remaining Workload |
| Transfer Granularity | None | 1 task | 1 task | Dynamic batch $[1, B_{\max}]$ |
| Overhead Under Balance | Zero | High | Moderate (blind probes) | Minimized (aborts unprofitable steals) |
