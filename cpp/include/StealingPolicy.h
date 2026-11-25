#pragma once
#include "Task.h"
#include "CostEstimator.h"
#include "TaskQueue.h"
#include <vector>
#include <string>

enum class VictimSelectionPolicy {
    Random,           // pick any non-empty queue randomly
    MaxQueue,         // pick worker with most tasks
    MaxRemainingCost  // pick worker with highest estimated remaining cost
};

struct StealingConfig {
    double min_benefit_ms = 0.001;     // minimum benefit to justify a steal (calibrated for fast synthetic tasks)
    double overhead_ms = 0.001;        // estimated cost of a steal operation
    int max_batch_size = 16;           // cap on tasks stolen at once
    int min_victim_queue = 1;          // minimum victim queue length to attempt steal
    VictimSelectionPolicy policy = VictimSelectionPolicy::MaxRemainingCost;
    double ewma_alpha = 0.3;           // passed to CostEstimator
};

struct StealDecision {
    bool should_steal = false;
    int victim_id = -1;
    int batch_size = 0;
    double expected_benefit = 0.0;
    double estimated_overhead = 0.0;
    double victim_remaining_cost = 0.0;
    double thief_remaining_cost = 0.0;
    std::string reason; // explanation for logging
};

class StealingPolicy {
public:
    explicit StealingPolicy(const StealingConfig& cfg, const CostEstimator& estimator);

    // Decide whether to steal, from whom, and how many tasks.
    // queues: pointer to each worker's queue (indexed by worker_id)
    // thief_id: the worker who wants to steal
    // thief_queue_remaining: estimated remaining cost of thief's current queue
    // speeds: speed factor of each worker (enforcing descending capability stealing)
    StealDecision decide(const std::vector<TaskQueue*>& queues,
                         int thief_id,
                         double thief_queue_remaining,
                         const std::vector<double>& speeds = {}) const;

    const StealingConfig& config() const { return cfg_; }

private:
    StealingConfig cfg_;
    const CostEstimator& estimator_;

    // Returns estimated remaining cost for a queue
    double queue_remaining_cost(const TaskQueue& q) const;

    // Select victim id, or -1 if no suitable victim (enforces faster-steals-from-slower hierarchy)
    int select_victim(const std::vector<TaskQueue*>& queues,
                      int thief_id,
                      const std::vector<double>& speeds = {}) const;

    // Calculate batch size given workload imbalance and avg cost
    int compute_batch_size(double workload_diff, double avg_cost) const;
};
