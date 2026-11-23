#pragma once
#include "Task.h"
#include "TaskQueue.h"
#include "RuntimeMonitor.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

enum class WorkerState { Idle, Running, Stealing, Stopped };

class Worker {
public:
    int id;
    TaskQueue queue;

    // Statistics (atomic for safe concurrent reads from monitor)
    std::atomic<int> tasks_completed{0};
    std::atomic<int> tasks_stolen_from{0}; // tasks stolen away from this worker
    std::atomic<int> tasks_stolen_by{0};   // tasks this worker stole from others
    std::atomic<int> steal_attempts{0};
    std::atomic<int> steal_successes{0};
    std::atomic<double> total_busy_ms{0.0};
    std::atomic<double> total_idle_ms{0.0};
    std::atomic<WorkerState> state{WorkerState::Idle};

    // Current task (for monitoring)
    std::atomic<uint64_t> current_task_id{0};

    explicit Worker(int id_) : id(id_) {}
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    double utilization(double wall_ms) const {
        if (wall_ms <= 0.0) return 0.0;
        return total_busy_ms.load() / wall_ms;
    }
};
