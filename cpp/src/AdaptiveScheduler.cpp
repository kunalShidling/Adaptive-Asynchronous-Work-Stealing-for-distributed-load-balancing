#include "AdaptiveScheduler.h"
#include "Worker.h"
#include "Metrics.h"
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cmath>
#include <algorithm>

ExperimentMetrics AdaptiveScheduler::run(std::vector<Task> tasks) {
    using clock = std::chrono::high_resolution_clock;
    using ms_d  = std::chrono::duration<double, std::milli>;

    int num_workers = cfg_.num_workers;
    int num_tasks   = static_cast<int>(tasks.size());

    // Reset per-run atomics
    total_steal_attempts_.store(0);
    total_steal_successes_.store(0);
    total_tasks_stolen_.store(0);
    total_redistributions_.store(0);
    total_sched_overhead_ms_.store(0.0);

    // Initialize cost estimator
    estimator_.set_alpha(cfg_.stealing.ewma_alpha);
    estimator_.reset();

    // Sequential baseline: run all tasks single-threaded with 1.0x speed
    std::vector<Task> seq_tasks = tasks;
    auto seq_start = clock::now();
    for (auto& t : seq_tasks) t.execute(1.0);
    auto seq_end = clock::now();
    double sequential_ms = ms_d(seq_end - seq_start).count();

    // Create StealingPolicy
    StealingPolicy stealing_policy(cfg_.stealing, estimator_);

    // Determine worker processing speeds
    std::vector<double> speeds = get_cluster_speeds(cfg_);

    // Create workers with configured capabilities
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers.emplace_back(std::make_unique<Worker>(i, speeds[i]));
    }

    // Build queue pointer list for the stealing policy
    std::vector<TaskQueue*> queue_ptrs;
    queue_ptrs.reserve(num_workers);
    for (auto& w : workers) queue_ptrs.push_back(&w->queue);

    // Capacity-aware weighted task allocation:
    // VERY_FAST nodes receive the largest share, FAST next, MEDIUM next, SLOW smallest share.
    // Integer-safe allocation ensures sum equals num_tasks exactly with zero loss/duplication.
    std::vector<int> initial_allocations = calculate_capacity_allocation(num_tasks, speeds);

    int task_idx = 0;
    for (int wid = 0; wid < num_workers; ++wid) {
        int count = initial_allocations[wid];
        workers[wid]->initial_assigned_tasks.store(count);
        for (int k = 0; k < count && task_idx < num_tasks; ++k, ++task_idx) {
            tasks[task_idx].assigned_worker = wid;
            tasks[task_idx].initial_assigned_worker = wid;
            tasks[task_idx].estimated_cost  = estimator_.estimate(tasks[task_idx].complexity);
            workers[wid]->queue.push_front(tasks[task_idx]);
        }
    }

    // Shared task counter
    std::atomic<int> tasks_remaining{num_tasks};
    std::atomic<bool> stop_coordinator{false};
    std::mutex coord_mtx;
    std::condition_variable coord_cv;

    // Keep all executed tasks for MAPE computation
    std::vector<std::vector<Task>> executed_per_worker(num_workers);

    monitor_.emit_experiment_started(num_workers, num_tasks, "adaptive", cfg_.experiment_id);
    for (int i = 0; i < num_workers; ++i) {
        monitor_.emit_node_classified(i, to_string(workers[i]->category.load()),
                                      workers[i]->speed_factor.load(), 0.0);
        monitor_.emit_initial_allocation(i, workers[i]->initial_assigned_tasks.load(),
                                         to_string(workers[i]->category.load()));
    }
    auto wall_start = clock::now();

    // ── Dedicated Dynamic Redistribution & Fault/Slowdown Coordinator ────────
    std::thread coordinator([&]() {
        int check_interval_ms = std::max(1, static_cast<int>(cfg_.redistribution_interval_ms));
        while (!stop_coordinator.load() && tasks_remaining.load() > 0) {
            {
                std::unique_lock<std::mutex> lock(coord_mtx);
                coord_cv.wait_for(lock, std::chrono::milliseconds(check_interval_ms), [&]() {
                    return stop_coordinator.load() || tasks_remaining.load() <= 0;
                });
            }
            if (tasks_remaining.load() <= 0 || stop_coordinator.load()) break;

            // 1. Check for simulated node slowdown
            if (cfg_.simulate_slowdown_worker >= 0 && cfg_.simulate_slowdown_worker < num_workers) {
                int s_wid = cfg_.simulate_slowdown_worker;
                Worker& sw = *workers[s_wid];
                if (!sw.is_slowed_down.load() && sw.tasks_completed.load() >= cfg_.simulate_slowdown_after_tasks) {
                    sw.is_slowed_down.store(true);
                    sw.speed_factor.store(cfg_.simulate_slowdown_factor);
                    NodeCategory new_cat = category_from_speed(cfg_.simulate_slowdown_factor);
                    sw.category.store(new_cat);
                    monitor_.emit_node_slowdown(s_wid, cfg_.simulate_slowdown_factor);
                    monitor_.emit_node_classified(s_wid, to_string(new_cat),
                                                  cfg_.simulate_slowdown_factor,
                                                  sw.throughput_tasks_per_sec.load());
                }
            }

            // 2. Check for simulated node failure
            if (cfg_.simulate_fail_worker >= 0 && cfg_.simulate_fail_worker < num_workers) {
                int f_wid = cfg_.simulate_fail_worker;
                Worker& fw = *workers[f_wid];
                if (fw.is_alive.load() && fw.tasks_completed.load() >= cfg_.simulate_fail_after_tasks) {
                    fw.is_alive.store(false);
                    fw.state.store(WorkerState::Stopped);
                    std::vector<Task> recovered = fw.queue.drain_all();
                    int rec_count = static_cast<int>(recovered.size());
                    fw.tasks_failed.fetch_add(rec_count);
                    monitor_.emit_node_failed(f_wid, rec_count);

                    // Reassign recovered tasks to remaining alive workers proportionally
                    std::vector<int> alive_ids;
                    std::vector<double> alive_caps;
                    for (int j = 0; j < num_workers; ++j) {
                        if (workers[j]->is_alive.load()) {
                            alive_ids.push_back(j);
                            alive_caps.push_back(workers[j]->speed_factor.load());
                        }
                    }
                    if (!alive_ids.empty() && !recovered.empty()) {
                        auto rec_alloc = calculate_capacity_allocation(rec_count, alive_caps);
                        int r_idx = 0;
                        for (size_t a = 0; a < alive_ids.size(); ++a) {
                            int c_count = rec_alloc[a];
                            int target_wid = alive_ids[a];
                            for (int c = 0; c < c_count && r_idx < rec_count; ++c, ++r_idx) {
                                recovered[r_idx].assigned_worker = target_wid;
                                recovered[r_idx].transfer_count++;
                                workers[target_wid]->queue.push_front(recovered[r_idx]);
                                workers[target_wid]->tasks_redistributed_in.fetch_add(1);
                            }
                        }
                        total_redistributions_.fetch_add(1);
                    }
                }
            }

                // 3. Proactive dynamic redistribution across active nodes
                // Strict descending capability hierarchy:
                // Slower nodes offload tasks to faster nodes!
                // VERY_FAST actively pulls tasks from FAST, MEDIUM, SLOW.
                // FAST actively pulls tasks from MEDIUM, SLOW.
                // MEDIUM actively pulls tasks from SLOW.
                // Tasks NEVER flow from faster nodes to slower nodes!
                if (cfg_.dynamic_redistribution) {
                    int donor = -1;
                    int recipient = -1;
                    double max_priority_score = -1.0;

                    for (int r = 0; r < num_workers; ++r) {
                        if (!workers[r]->is_alive.load()) continue;
                        double r_speed = std::max(0.1, workers[r]->speed_factor.load());
                        size_t r_qlen = workers[r]->queue.size();

                        for (int d = 0; d < num_workers; ++d) {
                            if (d == r || !workers[d]->is_alive.load()) continue;
                            double d_speed = std::max(0.1, workers[d]->speed_factor.load());
                            size_t d_qlen = workers[d]->queue.size();

                            if (d_qlen <= 1) continue;

                            // STRICT INVARIANT: Recipient MUST be faster than or equal to donor!
                            // Slower nodes are NEVER recipients from faster nodes!
                            if (r_speed < d_speed) continue;

                            // Calculate priority score:
                            // Heavily prioritize pulling backlogged tasks from slower bottleneck nodes into faster nodes
                            double speed_advantage = r_speed / d_speed; // e.g. 8.0/1.0 = 8.0 for VERY_FAST vs SLOW
                            double donor_backlog = static_cast<double>(d_qlen);
                            double recipient_readiness = 1.0 / (static_cast<double>(r_qlen) + 1.0);

                            double score = speed_advantage * donor_backlog * recipient_readiness;
                            if (score > max_priority_score) {
                                max_priority_score = score;
                                donor = d;
                                recipient = r;
                            }
                        }
                    }

                    if (donor >= 0 && recipient >= 0 && donor != recipient) {
                        size_t d_qlen = workers[donor]->queue.size();
                        if (d_qlen > 1) {
                            double r_speed = std::max(0.1, workers[recipient]->speed_factor.load());
                            double d_speed = std::max(0.1, workers[donor]->speed_factor.load());
                            double speed_ratio = r_speed / d_speed;

                            // Faster recipient takes larger batches from slower donor
                            int desired_batch = static_cast<int>(std::ceil(speed_ratio * 4.0));
                            int safe_max = static_cast<int>(d_qlen / 2);
                            if (safe_max < 1) safe_max = 1;
                            int batch_size = std::max(1, std::min(desired_batch, safe_max));
                            batch_size = std::min(batch_size, cfg_.stealing.max_batch_size);

                            std::vector<Task> moved = workers[donor]->queue.steal_batch(batch_size);
                            if (!moved.empty()) {
                                int moved_count = static_cast<int>(moved.size());
                                for (auto& t : moved) {
                                    t.assigned_worker = recipient;
                                    t.transfer_count++;
                                }
                                workers[recipient]->queue.push_batch_front(moved);
                                workers[donor]->tasks_redistributed_out.fetch_add(moved_count);
                                workers[recipient]->tasks_redistributed_in.fetch_add(moved_count);
                                workers[recipient]->tasks_stolen_by.fetch_add(moved_count);
                                workers[donor]->tasks_stolen_from.fetch_add(moved_count);
                                workers[recipient]->steal_attempts.fetch_add(1);
                                workers[recipient]->steal_successes.fetch_add(1);
                                total_steal_attempts_.fetch_add(1);
                                total_steal_successes_.fetch_add(1);
                                total_tasks_stolen_.fetch_add(moved_count);
                                total_redistributions_.fetch_add(1);

                                monitor_.emit_redistribution_performed(
                                    donor, recipient, moved_count,
                                    static_cast<double>(workers[donor]->queue.size()),
                                    static_cast<double>(workers[recipient]->queue.size())
                                );
                                monitor_.emit_steal_succeeded(
                                    recipient, donor, moved_count,
                                    static_cast<double>(workers[donor]->queue.size())
                                );
                            }
                        }
                    }
                }
        }
    });

    // ── Launch Worker Threads ────────────────────────────────────────────────
    std::vector<std::thread> threads;
    threads.reserve(num_workers);

    for (int wid = 0; wid < num_workers; ++wid) {
        threads.emplace_back([&, wid]() {
            Worker& w = *workers[wid];
            monitor_.emit_worker_started(wid);
            w.state.store(WorkerState::Running);

            auto& my_executed = executed_per_worker[wid];

            while (tasks_remaining.load() > 0 || !w.queue.empty()) {
                if (!w.is_alive.load()) {
                    w.state.store(WorkerState::Stopped);
                    break;
                }

                // Try own queue
                auto opt = w.queue.pop_front();
                if (opt) {
                    Task task = std::move(*opt);

                    w.current_task_id.store(task.id);
                    monitor_.emit_task_started(wid, task.id, task.complexity,
                                               task.estimated_cost,
                                               static_cast<int>(w.queue.size()));
                    monitor_.emit_cost_estimated(wid, task.id, task.complexity,
                                                 task.estimated_cost);

                    task.execute(w.speed_factor.load());
                    task.executed_by_worker = wid;
                    w.current_task_id.store(0);

                    // Update estimator with normalized duration (relative to 1.0x baseline)
                    double norm_duration = task.actual_duration * w.speed_factor.load();
                    estimator_.update(task.complexity, norm_duration);

                    double busy = w.total_busy_ms.load();
                    while (!w.total_busy_ms.compare_exchange_weak(busy, busy + task.actual_duration)) {}
                    w.tasks_completed.fetch_add(1);
                    w.record_task_completed(task.actual_duration);
                    if (tasks_remaining.fetch_sub(1) == 1) {
                        stop_coordinator.store(true);
                        coord_cv.notify_all();
                    }

                    monitor_.emit_task_completed(wid, task.id, task.actual_duration,
                                                 task.estimated_cost,
                                                 static_cast<int>(w.queue.size()));

                    my_executed.push_back(task);
                    continue;
                }

                // Own queue empty - consider work-stealing (A2WS)
                if (tasks_remaining.load() > 0) {
                    w.state.store(WorkerState::Stealing);

                    auto overhead_start = clock::now();
                    double thief_remaining = w.queue.estimated_remaining_cost();
                    StealDecision decision = stealing_policy.decide(queue_ptrs, wid, thief_remaining, speeds);
                    double overhead_ms = ms_d(clock::now() - overhead_start).count();

                    double cur = total_sched_overhead_ms_.load();
                    while (!total_sched_overhead_ms_.compare_exchange_weak(cur, cur + overhead_ms)) {}

                    if (decision.victim_id == -1) {
                        // No slower/equal victim with tasks currently available.
                        // Back off briefly to prevent busy-spinning and event flood.
                        if (tasks_remaining.load() > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                        continue;
                    }

                    total_steal_attempts_.fetch_add(1);
                    w.steal_attempts.fetch_add(1);

                    if (decision.should_steal && workers[decision.victim_id]->is_alive.load()) {
                        int vid = decision.victim_id;
                        monitor_.emit_victim_selected(wid, vid, decision.expected_benefit,
                                                      static_cast<int>(w.queue.size()),
                                                      static_cast<int>(workers[vid]->queue.size()));
                        monitor_.emit_steal_attempted(wid, vid);

                        std::vector<Task> stolen = workers[vid]->queue.steal_batch(decision.batch_size);

                        if (stolen.empty()) {
                            monitor_.emit_steal_failed(wid, vid, "queue emptied before steal");
                        } else {
                            for (auto& st : stolen) {
                                st.assigned_worker = wid;
                                st.transfer_count++;
                                st.estimated_cost = estimator_.estimate(st.complexity);
                            }
                            w.queue.push_batch_front(stolen);

                            int batch = static_cast<int>(stolen.size());
                            w.steal_successes.fetch_add(1);
                            total_steal_successes_.fetch_add(1);
                            total_tasks_stolen_.fetch_add(batch);
                            w.tasks_stolen_by.fetch_add(batch);
                            workers[vid]->tasks_stolen_from.fetch_add(batch);
                            w.tasks_redistributed_in.fetch_add(batch);
                            workers[vid]->tasks_redistributed_out.fetch_add(batch);
                            total_redistributions_.fetch_add(1);

                            monitor_.emit_steal_succeeded(wid, vid, batch,
                                                          decision.victim_remaining_cost);
                            monitor_.emit_batch_stolen(wid, vid, batch,
                                                       decision.estimated_overhead);
                        }
                    } else {
                        monitor_.emit_steal_failed(wid, decision.victim_id, decision.reason);
                    }

                    if (tasks_remaining.load() > 0) {
                        std::this_thread::yield();
                    }
                } else {
                    break;
                }
            }

            w.state.store(WorkerState::Stopped);
            monitor_.emit_worker_idle(wid, 0);
        });
    }

    for (auto& th : threads) th.join();
    auto wall_end = clock::now();
    double wall_ms = ms_d(wall_end - wall_start).count();

    stop_coordinator.store(true);
    coord_cv.notify_all();
    if (coordinator.joinable()) coordinator.join();

    monitor_.emit_experiment_completed(cfg_.experiment_id, wall_ms, num_tasks);

    // Compute MAPE across all executed tasks
    double mape = 0.0;
    int mape_count = 0;
    for (auto& vec : executed_per_worker) {
        for (auto& t : vec) {
            if (t.actual_duration > 0.0) {
                double err = std::abs(t.estimated_cost - t.actual_duration) / t.actual_duration;
                mape += err;
                ++mape_count;
            }
        }
    }
    if (mape_count > 0) mape = (mape / mape_count) * 100.0;

    // Collect per-worker metrics
    std::vector<WorkerMetrics> wm;
    wm.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        Worker& w = *workers[i];
        WorkerMetrics m{};
        m.worker_id               = i;
        m.node_category           = to_string(w.category.load());
        m.speed_factor            = w.speed_factor.load();
        m.initial_assigned        = w.initial_assigned_tasks.load();
        m.tasks_completed         = w.tasks_completed.load();
        m.tasks_failed            = w.tasks_failed.load();
        m.tasks_redistributed_in  = w.tasks_redistributed_in.load();
        m.tasks_redistributed_out = w.tasks_redistributed_out.load();
        m.tasks_stolen_from       = w.tasks_stolen_from.load();
        m.tasks_stolen_by         = w.tasks_stolen_by.load();
        m.steal_attempts          = w.steal_attempts.load();
        m.steal_successes         = w.steal_successes.load();
        m.busy_ms                 = w.total_busy_ms.load();
        m.idle_ms                 = w.total_idle_ms.load();
        m.utilization             = w.utilization(wall_ms);
        m.avg_task_duration_ms    = w.avg_task_duration_ms.load();
        m.throughput_tasks_per_sec= w.throughput_tasks_per_sec.load();
        wm.push_back(m);
    }

    return ExperimentMetrics::compute(
        cfg_.experiment_id, "adaptive", num_workers, num_tasks,
        cfg_.workload_type, cfg_.random_seed,
        wall_ms, sequential_ms, wm,
        total_steal_attempts_.load(),
        total_steal_successes_.load(),
        total_tasks_stolen_.load(),
        total_sched_overhead_ms_.load(),
        mape,
        total_redistributions_.load()
    );
}
