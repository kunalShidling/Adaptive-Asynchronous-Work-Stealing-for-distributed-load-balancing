#pragma once
#include "Task.h"
#include "TaskQueue.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>

enum class WorkerState { Idle, Running, Stealing, Stopped };

enum class NodeCategory {
    VERY_FAST,
    FAST,
    MEDIUM,
    SLOW
};

inline const char* to_string(NodeCategory cat) {
    switch (cat) {
        case NodeCategory::VERY_FAST: return "VERY_FAST";
        case NodeCategory::FAST:      return "FAST";
        case NodeCategory::MEDIUM:    return "MEDIUM";
        case NodeCategory::SLOW:      return "SLOW";
        default:                      return "UNKNOWN";
    }
}

inline NodeCategory category_from_string(const std::string& str) {
    if (str == "VERY_FAST") return NodeCategory::VERY_FAST;
    if (str == "FAST")      return NodeCategory::FAST;
    if (str == "MEDIUM")    return NodeCategory::MEDIUM;
    if (str == "SLOW")      return NodeCategory::SLOW;
    return NodeCategory::MEDIUM;
}

// Convert a speed weight into a category
inline NodeCategory category_from_speed(double speed) {
    if (speed >= 6.0) return NodeCategory::VERY_FAST;
    if (speed >= 3.0) return NodeCategory::FAST;
    if (speed >= 1.5) return NodeCategory::MEDIUM;
    return NodeCategory::SLOW;
}

class Worker {
public:
    int id;
    TaskQueue queue;

    // Node classification & capability
    std::atomic<double> speed_factor{1.0};
    std::atomic<NodeCategory> category{NodeCategory::MEDIUM};
    std::atomic<bool> is_alive{true};
    std::atomic<bool> is_slowed_down{false};

    // Workload tracking
    std::atomic<int> initial_assigned_tasks{0};
    std::atomic<int> tasks_completed{0};
    std::atomic<int> tasks_failed{0};
    std::atomic<int> tasks_redistributed_in{0};
    std::atomic<int> tasks_redistributed_out{0};
    std::atomic<int> tasks_stolen_from{0}; // tasks stolen away from this worker
    std::atomic<int> tasks_stolen_by{0};   // tasks this worker stole from others
    std::atomic<int> steal_attempts{0};
    std::atomic<int> steal_successes{0};
    std::atomic<double> total_busy_ms{0.0};
    std::atomic<double> total_idle_ms{0.0};
    std::atomic<WorkerState> state{WorkerState::Idle};

    // Performance measurements (EWMA filtered)
    std::atomic<double> avg_task_duration_ms{0.0};
    std::atomic<double> throughput_tasks_per_sec{0.0};

    // Current task (for monitoring)
    std::atomic<uint64_t> current_task_id{0};

    explicit Worker(int id_, double speed = 1.0)
        : id(id_), speed_factor(speed), category(category_from_speed(speed)) {}

    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    int total_assigned() const {
        return initial_assigned_tasks.load() + tasks_redistributed_in.load();
    }

    int pending_tasks() const {
        return static_cast<int>(queue.size());
    }

    int executing_tasks() const {
        return (current_task_id.load() != 0) ? 1 : 0;
    }

    double estimated_remaining_workload() const {
        return queue.estimated_remaining_cost();
    }

    double utilization(double wall_ms) const {
        if (wall_ms <= 0.0) return 0.0;
        return total_busy_ms.load() / wall_ms;
    }

    // Update performance metrics when a task is completed
    void record_task_completed(double actual_ms) {
        if (actual_ms <= 0.0) actual_ms = 0.001;

        // EWMA update of avg task duration (alpha = 0.25)
        double current_avg = avg_task_duration_ms.load();
        if (current_avg <= 0.0) {
            avg_task_duration_ms.store(actual_ms);
        } else {
            double new_avg = 0.25 * actual_ms + 0.75 * current_avg;
            avg_task_duration_ms.store(new_avg);
        }

        // Throughput in tasks per second based on rolling average
        double updated_avg = avg_task_duration_ms.load();
        if (updated_avg > 0.0) {
            throughput_tasks_per_sec.store(1000.0 / updated_avg);
        }
    }
};
