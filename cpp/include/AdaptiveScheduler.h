#pragma once
#include "Scheduler.h"
#include "CostEstimator.h"
#include "StealingPolicy.h"
#include <atomic>

class AdaptiveScheduler : public Scheduler {
public:
    using Scheduler::Scheduler;
    ExperimentMetrics run(std::vector<Task> tasks) override;
private:
    CostEstimator estimator_;
    std::atomic<int> total_steal_attempts_{0};
    std::atomic<int> total_steal_successes_{0};
    std::atomic<int> total_tasks_stolen_{0};
    std::atomic<double> total_sched_overhead_ms_{0.0};
};
