#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct WorkerMetrics {
    int worker_id;
    std::string node_category;
    double speed_factor;
    int initial_assigned;
    int tasks_completed;
    int tasks_failed;
    int tasks_redistributed_in;
    int tasks_redistributed_out;
    int tasks_stolen_from;
    int tasks_stolen_by;
    int steal_attempts;
    int steal_successes;
    double busy_ms;
    double idle_ms;
    double utilization; // busy_ms / wall_ms
    double avg_task_duration_ms;
    double throughput_tasks_per_sec;
};

struct ExperimentMetrics {
    uint64_t experiment_id;
    std::string algorithm;
    int num_workers;
    int num_tasks;
    std::string workload_type;
    int random_seed;

    // Timing
    double wall_ms;         // total experiment duration
    double sequential_ms;   // sequential baseline (1 worker)

    // Computed metrics
    double speedup;          // sequential_ms / wall_ms
    double efficiency;       // speedup / num_workers
    double avg_utilization;  // mean utilization across workers
    double load_imbalance;   // (max_busy - min_busy) / avg_busy or normalized std dev
    double cluster_throughput; // total tasks / (wall_ms / 1000)

    // Stealing & Redistribution
    int total_steals_attempted;
    int total_steals_succeeded;
    int total_tasks_stolen;
    int total_redistributions;
    double steal_success_rate; // succeeded / attempted (or 0 if no attempts)

    // Scheduling overhead (measured separately)
    double scheduling_overhead_ms;

    // Cost estimation error (mean absolute percentage error)
    double cost_estimation_mape;

    // Per-worker breakdown
    std::vector<WorkerMetrics> workers;

    // Utility
    static ExperimentMetrics compute(
        uint64_t exp_id,
        const std::string& algorithm,
        int num_workers,
        int num_tasks,
        const std::string& workload_type,
        int seed,
        double wall_ms,
        double sequential_ms,
        const std::vector<WorkerMetrics>& worker_data,
        int steals_attempted,
        int steals_succeeded,
        int tasks_stolen,
        double sched_overhead_ms,
        double cost_mape,
        int total_redistributions = 0
    );

    std::string to_json() const;
};
