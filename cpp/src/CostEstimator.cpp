#include "CostEstimator.h"
#include <cmath>
#include <mutex>

CostEstimator::CostEstimator(double alpha)
    : alpha_(alpha) {}

void CostEstimator::update(int complexity, double actual_ms) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = estimates_.find(complexity);
    if (it == estimates_.end()) {
        estimates_[complexity] = actual_ms;
        sample_counts_[complexity] = 1;
    } else {
        it->second = alpha_ * actual_ms + (1.0 - alpha_) * it->second;
        sample_counts_[complexity]++;
    }
}

double CostEstimator::estimate(int complexity) const {
    std::lock_guard<std::mutex> lock(mtx_);

    // Exact match
    auto it = estimates_.find(complexity);
    if (it != estimates_.end()) {
        return it->second;
    }

    // Nearby bucket within ±10%
    double low = complexity * 0.9;
    double high = complexity * 1.1;
    double weighted_sum = 0.0;
    double weight_total = 0.0;
    for (const auto& kv : estimates_) {
        if (kv.first >= low && kv.first <= high) {
            int cnt = sample_counts_.count(kv.first) ? sample_counts_.at(kv.first) : 1;
            weighted_sum += kv.second * cnt;
            weight_total += cnt;
        }
    }
    if (weight_total > 0.0) {
        return weighted_sum / weight_total;
    }

    // Fallback: rough estimate
    return complexity * 0.001;
}

void CostEstimator::reset() {
    std::lock_guard<std::mutex> lock(mtx_);
    estimates_.clear();
    sample_counts_.clear();
}
