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

    // Heterogeneous node configuration
    bool heterogeneous_nodes = true;
    std::vector<double> worker_speeds; // Custom speeds per worker (or empty for default profile)
    bool dynamic_redistribution = true;
    double redistribution_interval_ms = 5.0;
    double imbalance_threshold = 0.15; // 15% imbalance triggers redistribution

    // Fault injection & slowdown simulation
    int simulate_slowdown_worker = -1;
    double simulate_slowdown_factor = 0.5;
    int simulate_slowdown_after_tasks = 10;
    int simulate_fail_worker = -1;
    int simulate_fail_after_tasks = 10;
};

// Returns configured or default speed factors for all workers
inline std::vector<double> get_cluster_speeds(const SchedulerConfig& cfg) {
    std::vector<double> speeds;
    speeds.reserve(cfg.num_workers);
    if (!cfg.worker_speeds.empty()) {
        for (int i = 0; i < cfg.num_workers; ++i) {
            speeds.push_back(cfg.worker_speeds[i % cfg.worker_speeds.size()]);
        }
    } else if (cfg.heterogeneous_nodes) {
        // Default 4-tier heterogeneous pattern: VERY_FAST (8.0), FAST (4.0), MEDIUM (2.0), SLOW (1.0)
        const double default_profile[] = {8.0, 4.0, 2.0, 1.0};
        for (int i = 0; i < cfg.num_workers; ++i) {
            speeds.push_back(default_profile[i % 4]);
        }
    } else {
        for (int i = 0; i < cfg.num_workers; ++i) {
            speeds.push_back(1.0);
        }
    }
    return speeds;
}

// Capacity-aware weighted task allocation (Largest Remainder Method / Hamilton-Hare)
// Guarantees integer-safe allocation: sum(allocation) == num_tasks exactly
inline std::vector<int> calculate_capacity_allocation(int num_tasks, const std::vector<double>& capacities) {
    int n = static_cast<int>(capacities.size());
    std::vector<int> allocation(n, 0);
    if (n == 0 || num_tasks <= 0) return allocation;

    double total_capacity = 0.0;
    for (double c : capacities) total_capacity += std::max(0.001, c);

    int allocated_sum = 0;
    struct RemainderItem {
        int index;
        double remainder;
        double capacity;
    };
    std::vector<RemainderItem> remainders(n);

    for (int i = 0; i < n; ++i) {
        double share = (std::max(0.001, capacities[i]) / total_capacity) * static_cast<double>(num_tasks);
        allocation[i] = static_cast<int>(std::floor(share));
        allocated_sum += allocation[i];
        remainders[i] = {i, share - static_cast<double>(allocation[i]), capacities[i]};
    }

    int leftover = num_tasks - allocated_sum;
    std::sort(remainders.begin(), remainders.end(), [](const RemainderItem& a, const RemainderItem& b) {
        if (std::abs(a.remainder - b.remainder) > 1e-9) {
            return a.remainder > b.remainder;
        }
        return a.capacity > b.capacity;
    });

    for (int i = 0; i < leftover; ++i) {
        allocation[remainders[i % n].index]++;
    }

    return allocation;
}

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
