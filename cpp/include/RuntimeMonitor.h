#pragma once
#include <string>
#include <mutex>
#include <ostream>
#include <chrono>
#include <atomic>
#include <cstdint>

// Lightweight structured event emitter.
// Emits one JSON object per line (JSONL) to the provided output stream.
// Output is buffered per-event and uses a mutex to prevent interleaving.
class RuntimeMonitor {
public:
    explicit RuntimeMonitor(std::ostream& out);

    // Experiment lifecycle
    void emit_experiment_started(int num_workers, int num_tasks, const std::string& algorithm, uint64_t exp_id);
    void emit_experiment_completed(uint64_t exp_id, double wall_ms, int tasks_completed);

    // Worker lifecycle
    void emit_worker_started(int worker_id);
    void emit_worker_idle(int worker_id, int queue_len);

    // Task lifecycle
    void emit_task_started(int worker_id, uint64_t task_id, int complexity, double est_cost, int queue_len);
    void emit_task_completed(int worker_id, uint64_t task_id, double actual_ms, double est_cost, int queue_len);

    // Stealing events
    void emit_steal_attempted(int thief_id, int victim_id);
    void emit_steal_succeeded(int thief_id, int victim_id, int tasks_stolen, double victim_remaining);
    void emit_steal_failed(int thief_id, int victim_id, const std::string& reason);
    void emit_victim_selected(int thief_id, int victim_id, double victim_score, int thief_queue_len, int victim_queue_len);
    void emit_cost_estimated(int worker_id, uint64_t task_id, int complexity, double estimated_ms);
    void emit_batch_stolen(int thief_id, int victim_id, int batch_size, double overhead_est);

    // Enable/disable (for performance-sensitive runs)
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool is_enabled() const { return enabled_; }

private:
    std::ostream& out_;
    std::mutex mtx_;
    std::atomic<bool> enabled_{true};

    double now_ms() const; // milliseconds since epoch
    void emit_raw(const std::string& json);
};
