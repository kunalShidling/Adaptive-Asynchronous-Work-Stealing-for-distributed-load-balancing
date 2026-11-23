#pragma once
#include "Task.h"
#include "Worker.h"
#include "RuntimeMonitor.h"
#include "Metrics.h"
#include "CostEstimator.h"
#include "StealingPolicy.h"
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>

struct SchedulerConfig {
    int num_workers = 4;
    int num_tasks = 100;
    std::string workload_type = "uniform";
    int min_complexity = 500;
    int max_complexity = 5000;
    int random_seed = 42;
    std::string algorithm = "adaptive";
    StealingConfig stealing;
    uint64_t experiment_id = 0;
};

// Abstract base for all schedulers
class Scheduler {
public:
    explicit Scheduler(const SchedulerConfig& cfg, RuntimeMonitor& monitor);
    virtual ~Scheduler() = default;

    // Run the full experiment and return metrics.
    // tasks: pre-generated task list (same for all algorithms in comparison)
    virtual ExperimentMetrics run(std::vector<Task> tasks) = 0;

    const SchedulerConfig& config() const { return cfg_; }

protected:
    SchedulerConfig cfg_;
    RuntimeMonitor& monitor_;

    // Helper: generate tasks if not provided externally
    std::vector<Task> generate_tasks() const;
};
