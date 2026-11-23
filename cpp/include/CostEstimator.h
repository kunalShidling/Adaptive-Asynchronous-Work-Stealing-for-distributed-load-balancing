#pragma once
#include <unordered_map>
#include <mutex>
#include <cstdint>

// Exponentially Weighted Moving Average cost estimator.
// est[t] = alpha * actual[t] + (1 - alpha) * est[t-1]
// Maintains per-task-type estimates keyed by complexity bucket.
class CostEstimator {
public:
    explicit CostEstimator(double alpha = 0.3);

    // Update with a measured actual duration (ms) for a task of given complexity
    void update(int complexity, double actual_ms);

    // Get current estimate for a complexity level (ms)
    double estimate(int complexity) const;

    // Reset all estimates
    void reset();

    double get_alpha() const { return alpha_; }
    void set_alpha(double a) { alpha_ = a; }

private:
    double alpha_;
    mutable std::mutex mtx_;
    std::unordered_map<int, double> estimates_; // complexity -> estimated ms
    std::unordered_map<int, int> sample_counts_; // complexity -> sample count
};
