#include "Metrics.h"
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <string>

ExperimentMetrics ExperimentMetrics::compute(
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
    int total_redistributions)
{
    ExperimentMetrics m;
    m.experiment_id           = exp_id;
    m.algorithm               = algorithm;
    m.num_workers             = num_workers;
    m.num_tasks               = num_tasks;
    m.workload_type           = workload_type;
    m.random_seed             = seed;
    m.wall_ms                 = wall_ms;
    m.sequential_ms           = sequential_ms;
    m.total_steals_attempted  = steals_attempted;
    m.total_steals_succeeded  = steals_succeeded;
    m.total_tasks_stolen      = tasks_stolen;
    m.total_redistributions   = total_redistributions;
    m.scheduling_overhead_ms  = sched_overhead_ms;
    m.cost_estimation_mape    = cost_mape;
    m.workers                 = worker_data;

    // Cluster throughput (tasks / sec)
    m.cluster_throughput = (wall_ms > 0.0) ? (static_cast<double>(num_tasks) / (wall_ms / 1000.0)) : 0.0;

    // Speedup and efficiency
    m.speedup    = (wall_ms > 0.0) ? (sequential_ms / wall_ms) : 0.0;
    m.efficiency = (num_workers > 0) ? (m.speedup / num_workers) : 0.0;

    // Average utilization
    if (!worker_data.empty()) {
        double sum_util = 0.0;
        for (const auto& wd : worker_data) sum_util += wd.utilization;
        m.avg_utilization = sum_util / worker_data.size();
    } else {
        m.avg_utilization = 0.0;
    }

    // Load imbalance: normalized std dev of busy_ms
    if (!worker_data.empty()) {
        double sum_busy = 0.0;
        for (const auto& wd : worker_data) sum_busy += wd.busy_ms;
        double mean_busy = sum_busy / worker_data.size();
        if (mean_busy > 0.0) {
            double sq_sum = 0.0;
            for (const auto& wd : worker_data) {
                double diff = wd.busy_ms - mean_busy;
                sq_sum += diff * diff;
            }
            double std_dev = std::sqrt(sq_sum / worker_data.size());
            m.load_imbalance = std_dev / mean_busy;
        } else {
            m.load_imbalance = 0.0;
        }
    } else {
        m.load_imbalance = 0.0;
    }

    // Steal success rate
    m.steal_success_rate = (steals_attempted > 0)
        ? (static_cast<double>(steals_succeeded) / steals_attempted)
        : 0.0;

    return m;
}

std::string ExperimentMetrics::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "{"
        << "\"experiment_id\":" << experiment_id
        << ",\"algorithm\":\"" << algorithm << "\""
        << ",\"num_workers\":" << num_workers
        << ",\"num_tasks\":" << num_tasks
        << ",\"workload_type\":\"" << workload_type << "\""
        << ",\"random_seed\":" << random_seed
        << ",\"wall_ms\":" << wall_ms
        << ",\"sequential_ms\":" << sequential_ms
        << ",\"speedup\":" << speedup
        << ",\"efficiency\":" << efficiency
        << ",\"avg_utilization\":" << avg_utilization
        << ",\"load_imbalance\":" << load_imbalance
        << ",\"total_steals_attempted\":" << total_steals_attempted
        << ",\"total_steals_succeeded\":" << total_steals_succeeded
        << ",\"total_tasks_stolen\":" << total_tasks_stolen
        << ",\"total_redistributions\":" << total_redistributions
        << ",\"cluster_throughput\":" << cluster_throughput
        << ",\"steal_success_rate\":" << steal_success_rate
        << ",\"scheduling_overhead_ms\":" << scheduling_overhead_ms
        << ",\"cost_estimation_mape\":" << cost_estimation_mape
        << ",\"workers\":[";

    for (size_t i = 0; i < workers.size(); ++i) {
        const auto& wd = workers[i];
        if (i > 0) oss << ",";
        oss << "{"
            << "\"worker_id\":" << wd.worker_id
            << ",\"node_category\":\"" << wd.node_category << "\""
            << ",\"speed_factor\":" << wd.speed_factor
            << ",\"initial_assigned\":" << wd.initial_assigned
            << ",\"tasks_completed\":" << wd.tasks_completed
            << ",\"tasks_failed\":" << wd.tasks_failed
            << ",\"tasks_redistributed_in\":" << wd.tasks_redistributed_in
            << ",\"tasks_redistributed_out\":" << wd.tasks_redistributed_out
            << ",\"tasks_stolen_by\":" << wd.tasks_stolen_by
            << ",\"tasks_stolen_from\":" << wd.tasks_stolen_from
            << ",\"steal_attempts\":" << wd.steal_attempts
            << ",\"steal_successes\":" << wd.steal_successes
            << ",\"busy_ms\":" << wd.busy_ms
            << ",\"idle_ms\":" << wd.idle_ms
            << ",\"utilization\":" << wd.utilization
            << ",\"avg_task_duration_ms\":" << wd.avg_task_duration_ms
            << ",\"throughput_tasks_per_sec\":" << wd.throughput_tasks_per_sec
            << "}";
    }
    oss << "]}";
    return oss.str();
}
