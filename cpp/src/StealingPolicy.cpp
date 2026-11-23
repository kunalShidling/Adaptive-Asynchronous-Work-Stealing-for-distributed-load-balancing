#include "StealingPolicy.h"
#include "TaskQueue.h"
#include <algorithm>
#include <random>
#include <cmath>

StealingPolicy::StealingPolicy(const StealingConfig& cfg, const CostEstimator& estimator)
    : cfg_(cfg), estimator_(estimator) {}

double StealingPolicy::queue_remaining_cost(const TaskQueue& q) const {
    return q.estimated_remaining_cost();
}

int StealingPolicy::select_victim(
    const std::vector<TaskQueue*>& queues,
    int thief_id) const
{
    int n = static_cast<int>(queues.size());

    if (cfg_.policy == VictimSelectionPolicy::Random) {
        std::vector<int> candidates;
        for (int i = 0; i < n; ++i) {
            if (i == thief_id) continue;
            if (!queues[i]->empty()) candidates.push_back(i);
        }
        if (candidates.empty()) return -1;
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> dist(0, static_cast<int>(candidates.size()) - 1);
        return candidates[dist(rng)];

    } else if (cfg_.policy == VictimSelectionPolicy::MaxQueue) {
        int best = -1;
        size_t best_size = 0;
        for (int i = 0; i < n; ++i) {
            if (i == thief_id) continue;
            size_t s = queues[i]->size();
            if (s > best_size) {
                best_size = s;
                best = i;
            }
        }
        return (best_size >= static_cast<size_t>(cfg_.min_victim_queue)) ? best : -1;

    } else { // MaxRemainingCost (default)
        int best = -1;
        double best_cost = -1.0;
        for (int i = 0; i < n; ++i) {
            if (i == thief_id) continue;
            if (queues[i]->empty()) continue;
            double cost = queue_remaining_cost(*queues[i]);
            if (cost > best_cost) {
                best_cost = cost;
                best = i;
            }
        }
        return best;
    }
}

int StealingPolicy::compute_batch_size(double workload_diff, double avg_cost) const {
    if (avg_cost <= 0.0) return 1;
    int batch = static_cast<int>(std::floor(workload_diff / (2.0 * avg_cost)));
    batch = std::max(1, batch);
    batch = std::min(batch, cfg_.max_batch_size);
    return batch;
}

StealDecision StealingPolicy::decide(
    const std::vector<TaskQueue*>& queues,
    int thief_id,
    double thief_queue_remaining) const
{
    int victim_id = select_victim(queues, thief_id);
    if (victim_id == -1) {
        return {false, -1, 0, 0.0, 0.0, 0.0, thief_queue_remaining, "no suitable victim"};
    }

    TaskQueue* victim_q = queues[victim_id];
    size_t vsize = victim_q->size();

    if (static_cast<int>(vsize) < cfg_.min_victim_queue) {
        return {false, victim_id, 0, 0.0, 0.0, 0.0, thief_queue_remaining, "victim queue too small"};
    }

    double victim_remaining = queue_remaining_cost(*victim_q);
    double workload_diff = victim_remaining - thief_queue_remaining;

    if (workload_diff <= 0.0) {
        return {false, victim_id, 0, 0.0, cfg_.overhead_ms,
                victim_remaining, thief_queue_remaining, "no imbalance"};
    }

    // Avg cost per task in victim queue
    double avg_cost = (vsize > 0) ? (victim_remaining / static_cast<double>(vsize)) : 1.0;

    int batch = compute_batch_size(workload_diff, avg_cost);
    double expected_benefit = workload_diff / 2.0 - cfg_.overhead_ms;

    if (expected_benefit < cfg_.min_benefit_ms) {
        return {false, victim_id, batch, expected_benefit, cfg_.overhead_ms,
                victim_remaining, thief_queue_remaining, "benefit below threshold"};
    }

    return {true, victim_id, batch, expected_benefit, cfg_.overhead_ms,
            victim_remaining, thief_queue_remaining, "steal approved"};
}
