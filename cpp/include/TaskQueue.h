#pragma once
#include "Task.h"
#include <deque>
#include <mutex>
#include <optional>
#include <vector>
#include <cstddef>

// Thread-safe double-ended queue for work-stealing.
// Owner pushes/pops from the FRONT (LIFO for cache locality).
// Thieves steal from the BACK.
class TaskQueue {
public:
    TaskQueue() = default;
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    // Owner operations (called by the queue's owning worker)
    void push_front(Task t);
    void push_back(Task t);
    void push_batch_front(std::vector<Task> tasks);
    std::optional<Task> pop_front();

    // Thief operations (called by other workers stealing work)
    std::optional<Task> steal_back();
    std::vector<Task> steal_batch(int count);

    // Node failure & recovery operations
    std::vector<Task> drain_all();

    // Inspection
    size_t size() const;
    bool empty() const;
    double estimated_remaining_cost() const; // sum of estimated_cost of queued tasks

private:
    mutable std::mutex mtx_;
    std::deque<Task> deque_;
};
