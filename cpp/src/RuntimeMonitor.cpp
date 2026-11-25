#include "RuntimeMonitor.h"
#include <chrono>
#include <string>

RuntimeMonitor::RuntimeMonitor(std::ostream& out)
    : out_(out), enabled_(true) {}

double RuntimeMonitor::now_ms() const {
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

static std::string safe_str(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == '"') result += '\'';
        else result += c;
    }
    return result;
}

void RuntimeMonitor::emit_raw(const std::string& json_str) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!enabled_) return;
    out_ << json_str << "\n";
    out_.flush();
}

void RuntimeMonitor::emit_experiment_started(int num_workers, int num_tasks,
                                              const std::string& algorithm,
                                              uint64_t exp_id) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"experiment_started\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"exp_id\":" + std::to_string(exp_id) +
        ",\"num_workers\":" + std::to_string(num_workers) +
        ",\"num_tasks\":" + std::to_string(num_tasks) +
        ",\"algorithm\":\"" + safe_str(algorithm) + "\"}";
    emit_raw(json);
}

void RuntimeMonitor::emit_experiment_completed(uint64_t exp_id,
                                                double wall_ms,
                                                int tasks_completed) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"experiment_completed\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"exp_id\":" + std::to_string(exp_id) +
        ",\"wall_ms\":" + std::to_string(wall_ms) +
        ",\"tasks_completed\":" + std::to_string(tasks_completed) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_worker_started(int worker_id) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"worker_started\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_worker_idle(int worker_id, int queue_len) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"worker_idle\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"queue_len\":" + std::to_string(queue_len) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_task_started(int worker_id, uint64_t task_id,
                                        int complexity, double est_cost,
                                        int queue_len) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"task_started\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"task_id\":" + std::to_string(task_id) +
        ",\"complexity\":" + std::to_string(complexity) +
        ",\"est_cost\":" + std::to_string(est_cost) +
        ",\"queue_len\":" + std::to_string(queue_len) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_task_completed(int worker_id, uint64_t task_id,
                                          double actual_ms, double est_cost,
                                          int queue_len) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"task_completed\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"task_id\":" + std::to_string(task_id) +
        ",\"actual_ms\":" + std::to_string(actual_ms) +
        ",\"est_cost\":" + std::to_string(est_cost) +
        ",\"queue_len\":" + std::to_string(queue_len) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_steal_attempted(int thief_id, int victim_id) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"steal_attempted\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"thief_id\":" + std::to_string(thief_id) +
        ",\"victim_id\":" + std::to_string(victim_id) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_steal_succeeded(int thief_id, int victim_id,
                                           int tasks_stolen,
                                           double victim_remaining) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"steal_succeeded\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"thief_id\":" + std::to_string(thief_id) +
        ",\"victim_id\":" + std::to_string(victim_id) +
        ",\"tasks_stolen\":" + std::to_string(tasks_stolen) +
        ",\"victim_remaining\":" + std::to_string(victim_remaining) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_steal_failed(int thief_id, int victim_id,
                                        const std::string& reason) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"steal_failed\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"thief_id\":" + std::to_string(thief_id) +
        ",\"victim_id\":" + std::to_string(victim_id) +
        ",\"reason\":\"" + safe_str(reason) + "\"}";
    emit_raw(json);
}

void RuntimeMonitor::emit_victim_selected(int thief_id, int victim_id,
                                           double victim_score,
                                           int thief_queue_len,
                                           int victim_queue_len) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"victim_selected\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"thief_id\":" + std::to_string(thief_id) +
        ",\"victim_id\":" + std::to_string(victim_id) +
        ",\"victim_score\":" + std::to_string(victim_score) +
        ",\"thief_queue_len\":" + std::to_string(thief_queue_len) +
        ",\"victim_queue_len\":" + std::to_string(victim_queue_len) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_cost_estimated(int worker_id, uint64_t task_id,
                                          int complexity, double estimated_ms) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"cost_estimated\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"task_id\":" + std::to_string(task_id) +
        ",\"complexity\":" + std::to_string(complexity) +
        ",\"estimated_ms\":" + std::to_string(estimated_ms) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_batch_stolen(int thief_id, int victim_id,
                                        int batch_size, double overhead_est) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"batch_stolen\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"thief_id\":" + std::to_string(thief_id) +
        ",\"victim_id\":" + std::to_string(victim_id) +
        ",\"batch_size\":" + std::to_string(batch_size) +
        ",\"overhead_est\":" + std::to_string(overhead_est) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_node_classified(int worker_id, const std::string& category,
                                          double speed_factor, double throughput) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"node_classified\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"category\":\"" + safe_str(category) + "\""
        ",\"speed_factor\":" + std::to_string(speed_factor) +
        ",\"throughput\":" + std::to_string(throughput) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_initial_allocation(int worker_id, int count, const std::string& category) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"initial_allocation\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"count\":" + std::to_string(count) +
        ",\"category\":\"" + safe_str(category) + "\"}";
    emit_raw(json);
}

void RuntimeMonitor::emit_redistribution_performed(int donor_id, int recipient_id,
                                                   int count, double donor_rem,
                                                   double recipient_rem) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"redistribution_performed\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"donor_id\":" + std::to_string(donor_id) +
        ",\"recipient_id\":" + std::to_string(recipient_id) +
        ",\"tasks_transferred\":" + std::to_string(count) +
        ",\"donor_rem\":" + std::to_string(donor_rem) +
        ",\"recipient_rem\":" + std::to_string(recipient_rem) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_node_slowdown(int worker_id, double new_speed_factor) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"node_slowdown\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"new_speed_factor\":" + std::to_string(new_speed_factor) + "}";
    emit_raw(json);
}

void RuntimeMonitor::emit_node_failed(int worker_id, int recovered_tasks) {
    double ts = now_ms();
    std::string json =
        "{\"type\":\"node_failed\""
        ",\"ts\":" + std::to_string(ts) +
        ",\"worker_id\":" + std::to_string(worker_id) +
        ",\"recovered_tasks\":" + std::to_string(recovered_tasks) + "}";
    emit_raw(json);
}

// set_enabled and is_enabled are inline in RuntimeMonitor.h
