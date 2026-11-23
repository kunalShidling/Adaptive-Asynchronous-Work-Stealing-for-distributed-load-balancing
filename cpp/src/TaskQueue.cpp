#include "TaskQueue.h"
#include <algorithm>

void TaskQueue::push_front(Task t) {
    std::lock_guard<std::mutex> lock(mtx_);
    deque_.push_front(std::move(t));
}

std::optional<Task> TaskQueue::pop_front() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (deque_.empty()) return std::nullopt;
    Task t = std::move(deque_.front());
    deque_.pop_front();
    return t;
}

std::optional<Task> TaskQueue::steal_back() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (deque_.size() < 1) return std::nullopt;
    Task t = std::move(deque_.back());
    deque_.pop_back();
    return t;
}

std::vector<Task> TaskQueue::steal_batch(int count) {
    std::lock_guard<std::mutex> lock(mtx_);
    int steal_count = std::min(count, static_cast<int>(deque_.size() / 2));
    if (steal_count < 1 && !deque_.empty()) steal_count = 1;
    std::vector<Task> stolen;
    stolen.reserve(steal_count);
    for (int i = 0; i < steal_count; ++i) {
        stolen.push_back(std::move(deque_.back()));
        deque_.pop_back();
    }
    return stolen;
}

size_t TaskQueue::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return deque_.size();
}

bool TaskQueue::empty() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return deque_.empty();
}

double TaskQueue::estimated_remaining_cost() const {
    std::lock_guard<std::mutex> lock(mtx_);
    double sum = 0.0;
    for (const auto& task : deque_) {
        sum += task.estimated_cost;
    }
    return sum;
}
