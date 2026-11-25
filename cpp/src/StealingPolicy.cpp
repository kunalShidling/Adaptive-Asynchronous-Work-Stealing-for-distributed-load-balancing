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
    int thief_id,
    const std::vector<double>& speeds) const
{
    int n = static_cast<int>(queues.size());
    double thief_speed = (thief_id >= 0 && thief_id < static_cast<int>(speeds.size()))
                         ? speeds[thief_id] : 1.0;

    if (cfg_.policy == VictimSelectionPolicy::Random) {
        std::vector<int> candidates;
        for (int i = 0; i < n; ++i) {
            if (i == thief_id) continue;
            if (queues[i]->empty()) continue;
            // Strict descending capability hierarchy: Slower nodes must NEVER steal from faster nodes!
            if (!speeds.empty()) {
                double victim_speed = (i < static_cast<int>(speeds.size())) ? speeds[i] : 1.0;
                if (thief_speed < victim_speed) continue;
            }
            candidates.push_back(i);
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
            // Strict hierarchy: Slower nodes must NEVER steal from faster nodes!
            if (!speeds.empty()) {
                double victim_speed = (i < static_cast<int>(speeds.size())) ? speeds[i] : 1.0;
                if (thief_speed < victim_speed) continue;
            }
            size_t s = queues[i]->size();
            if (s > best_size) {
                best_size = s;
                best = i;
            }
        }
        return (best_size >= static_cast<size_t>(cfg_.min_victim_queue)) ? best : -1;

    } else { // MaxRemainingCost (default A2WS with capability hierarchy)
        int best = -1;
        double best_cost = -1.0;
        for (int i = 0; i < n; ++i) {
            if (i == thief_id) continue;
            if (queues[i]->empty()) continue;

            // Strict descending capability hierarchy:
            // Slower nodes must NEVER steal from faster nodes!
            // VERY_FAST (8x) steals from FAST, MEDIUM, SLOW.
            // FAST (4x) steals from MEDIUM, SLOW.
            // MEDIUM (2x) steals from SLOW.
            // SLOW (1x) cannot steal from any faster node.
            double victim_speed = 1.0;
            if (!speeds.empty()) {
                victim_speed = (i < static_cast<int>(speeds.size())) ? speeds[i] : 1.0;
                if (thief_speed < victim_speed) {
                    continue; // Disallow stealing from faster nodes
                }
            }

            double cost = queue_remaining_cost(*queues[i]);
            double raw_score = (cost > 0.0) ? cost : static_cast<double>(queues[i]->size());

            // Prioritize slower victims that have backlogged queues:
            // Relieving the slowest node first yields the highest system throughput improvement!
            double speed_mult = (!speeds.empty() && victim_speed > 0.01) ? (thief_speed / victim_speed) : 1.0;
            double score = raw_score * speed_mult;

            if (score > best_cost) {
                best_cost = score;
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
    double thief_queue_remaining,
    const std::vector<double>& speeds) const
{
    int victim_id = select_victim(queues, thief_id, speeds);
    if (victim_id == -1) {
        return {false, -1, 0, 0.0, 0.0, 0.0, thief_queue_remaining, "no suitable slower/equal victim"};
    }

    TaskQueue* victim_q = queues[victim_id];
    size_t vsize = victim_q->size();

    if (static_cast<int>(vsize) < cfg_.min_victim_queue) {
        return {false, victim_id, 0, 0.0, 0.0, 0.0, thief_queue_remaining, "victim queue too small"};
    }

    double victim_remaining = queue_remaining_cost(*victim_q);
    if (victim_remaining <= 0.0) {
        victim_remaining = static_cast<double>(vsize) * 0.5;
    }
    double workload_diff = victim_remaining - thief_queue_remaining;

    // If thief has completely empty queue and victim has tasks, always steal
    if (workload_diff <= 0.0 && thief_queue_remaining <= 0.0001 && vsize > 0) {
        workload_diff = victim_remaining;
    }

    if (workload_diff <= 0.0) {
        return {false, victim_id, 0, 0.0, cfg_.overhead_ms,
                victim_remaining, thief_queue_remaining, "no imbalance"};
    }

    // Avg cost per task in victim queue
    double avg_cost = (vsize > 0) ? (victim_remaining / static_cast<double>(vsize)) : 0.5;

    int batch = compute_batch_size(workload_diff, avg_cost);

    // If thief is significantly faster than victim (e.g. VERY_FAST vs SLOW),
    // scale batch size so the high-throughput node aggressively drains work from the slow bottleneck
    double thief_speed = (thief_id >= 0 && thief_id < static_cast<int>(speeds.size())) ? speeds[thief_id] : 1.0;
    double victim_speed = (victim_id >= 0 && victim_id < static_cast<int>(speeds.size())) ? speeds[victim_id] : 1.0;
    if (thief_speed > victim_speed && victim_speed > 0.01) {
        double ratio = thief_speed / victim_speed;
        batch = static_cast<int>(std::ceil(batch * std::min(4.0, ratio)));
    }

    if (batch < 1) batch = 1;
    batch = std::min(batch, static_cast<int>(std::max(size_t(1), vsize / 2)));
    batch = std::min(batch, cfg_.max_batch_size);

    double expected_benefit = workload_diff / 2.0 - cfg_.overhead_ms;
    if (expected_benefit <= 0.0) {
        expected_benefit = 0.01; // minimal positive benefit for emptying queue
    }

    if (expected_benefit < cfg_.min_benefit_ms) {
        return {false, victim_id, batch, expected_benefit, cfg_.overhead_ms,
                victim_remaining, thief_queue_remaining, "benefit below threshold"};
    }

    return {true, victim_id, batch, expected_benefit, cfg_.overhead_ms,
            victim_remaining, thief_queue_remaining, "steal approved"};
}
