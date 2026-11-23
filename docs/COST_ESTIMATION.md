# Cost Estimation & Mathematical Modeling

This document describes the mathematical formulation, convergence properties, and runtime behavior of the Exponentially Weighted Moving Average (EWMA) cost estimator used in the Cost-Aware Adaptive Scheduler.

---

## 1. Mathematical Formulation

### 1.1 Exponentially Weighted Moving Average (EWMA)
For a task $t$ belonging to complexity bucket $k \in \mathbb{N}$, upon observing its actual execution duration $c_t$, the estimate is updated recursively:

$$\hat{c}_t(k) = \alpha \cdot c_t + (1 - \alpha) \cdot \hat{c}_{t-1}(k)$$

where:
- $\hat{c}_t(k)$ is the updated cost estimate for complexity $k$ at time step $t$.
- $c_t$ is the actual measured duration of task $t$ (in milliseconds).
- $\hat{c}_{t-1}(k)$ is the prior estimate for complexity $k$.
- $\alpha \in (0, 1]$ is the weight / learning rate parameter (`ewma_alpha`).

### 1.2 Unfolding the Recurrence
Expanding the recursion reveals that recent measurements receive exponentially higher weights:

$$\hat{c}_t(k) = \alpha \sum_{j=0}^{m-1} (1 - \alpha)^j c_{t-j} + (1 - \alpha)^m \hat{c}_0(k)$$

where $m$ is the number of samples collected for bucket $k$. As $m \to \infty$, the influence of the initial fallback prior $\hat{c}_0(k)$ decays to zero.

### 1.3 Effective Window Size
The effective number of past samples considered by the EWMA filter is approximately:

$$N_{\text{eff}} \approx \frac{2 - \alpha}{\alpha}$$

- For $\alpha = 0.3$: $N_{\text{eff}} \approx 5.67$ samples. Fast adaptation to transient thermal throttling or OS context switching.
- For $\alpha = 0.1$: $N_{\text{eff}} \approx 19$ samples. Smooth estimate, resilient against momentary scheduling noise.

---

## 2. Cold-Start and Nearest-Neighbor Fallback

When a task of complexity $k$ has not yet been observed in the sample history:
1. **Nearby Bucket Interpolation**: The estimator scans for existing buckets within a $\pm 10\%$ neighborhood ($k \in [0.9k, 1.1k]$). If matches are found, it computes a sample-count-weighted average:
   $$\hat{c}(k) = \frac{\sum_{i} n_i \cdot \hat{c}(k_i)}{\sum_{i} n_i}$$
2. **Linear Prior Fallback**: If no nearby buckets exist, a baseline linear model is applied:
   $$\hat{c}_0(k) = k \cdot \beta$$
   where $\beta \approx 0.001$ ms per complexity unit.

---

## 3. Evaluation Metric: Mean Absolute Percentage Error (MAPE)

The accuracy of the cost estimator is evaluated across all completed tasks $M$:

$$\text{MAPE} = \frac{1}{|M|} \sum_{i \in M} \left| \frac{\hat{c}_i - c_i}{c_i} \right| \times 100\%$$

### Safe Division & Zero Handling
To prevent division-by-zero or distortion from microsecond-level noise, tasks with actual duration $c_i < 0.001$ ms are excluded from the percentage error computation.

---

## 4. Workload Imbalance Scoring & Batch Sizing

### 4.1 Workload Imbalance Calculation
The estimated total remaining execution time in worker $w$'s queue is:
$$W(w) = \sum_{t \in Q_w} \hat{c}(t.\text{complexity})$$

When worker $i$ becomes idle ($W(i) \approx 0$), it calculates the imbalance score for each potential victim $v \neq i$:
$$\text{score}(v) = W(v) - W(i) = W(v)$$

### 4.2 Minimum Benefit Gating
Stealing incurs an overhead $c_{\text{overhead}}$ (cache line transfers, synchronization latency). The theoretical time saved by transferring half the excess workload is $\frac{W(v) - W(i)}{2}$. Stealing is only initiated if:
$$\frac{W(v) - W(i)}{2} - c_{\text{overhead}} > \theta_{\min}$$

### 4.3 Dynamic Batch Sizing
To equalize workloads in a single operation rather than repeated single-task steals, the number of tasks transferred is computed as:
$$\text{batch\_size} = \min\left(B_{\max}, \max\left(1, \left\lfloor \frac{W(v) - W(i)}{2 \cdot \bar{c}_v} \right\rfloor\right)\right)$$
where $\bar{c}_v = \frac{W(v)}{|Q_v|}$ is the average estimated cost per task currently residing in $Q_v$.
