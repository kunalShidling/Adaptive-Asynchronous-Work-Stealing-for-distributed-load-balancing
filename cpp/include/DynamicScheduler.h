#pragma once
#include "Scheduler.h"
#include <deque>
#include <mutex>
#include <condition_variable>

class DynamicScheduler : public Scheduler {
public:
    using Scheduler::Scheduler;
    ExperimentMetrics run(std::vector<Task> tasks) override;
private:
    std::deque<Task> global_queue_;
    std::mutex queue_mtx_;
    std::condition_variable queue_cv_;
    std::atomic<int> active_workers_{0};
    std::atomic<int> tasks_remaining_{0};
};
