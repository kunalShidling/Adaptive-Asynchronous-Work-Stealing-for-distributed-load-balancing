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
    total_sched_overhead_ms_.store(0.0);

    // Initialize cost estimator
    estimator_.set_alpha(cfg_.stealing.ewma_alpha);
    estimator_.reset();

    // Sequential baseline
    std::vector<Task> seq_tasks = tasks;
    auto seq_start = clock::now();
    for (auto& t : seq_tasks) t.execute();
    auto seq_end = clock::now();
    double sequential_ms = ms_d(seq_end - seq_start).count();

    // Create StealingPolicy
    StealingPolicy stealing_policy(cfg_.stealing, estimator_);

    // Create workers
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) workers.emplace_back(std::make_unique<Worker>(i));

    // Build queue pointer list for the stealing policy
    std::vector<TaskQueue*> queue_ptrs;
    queue_ptrs.reserve(num_workers);
    for (auto& w : workers) queue_ptrs.push_back(&w->queue);

    // Distribute tasks round-robin, pre-estimate cost
    for (int i = 0; i < num_tasks; ++i) {
        tasks[i].assigned_worker = i % num_workers;
        tasks[i].estimated_cost  = estimator_.estimate(tasks[i].complexity);
        workers[i % num_workers]->queue.push_front(tasks[i]);
    }

    // Shared task counter
    std::atomic<int> tasks_remaining{num_tasks};

    // Keep all executed tasks for MAPE computation
    // Use per-worker vectors to avoid lock contention, then merge
    std::vector<std::vector<Task>> executed_per_worker(num_workers);

    monitor_.emit_experiment_started(num_workers, num_tasks, "adaptive", cfg_.experiment_id);
    auto wall_start = clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_workers);

    for (int wid = 0; wid < num_workers; ++wid) {
        threads.emplace_back([&, wid]() {
            Worker& w = *workers[wid];
            monitor_.emit_worker_started(wid);
            w.state.store(WorkerState::Running);

            auto& my_executed = executed_per_worker[wid];

            while (tasks_remaining.load() > 0 || !w.queue.empty()) {
                // Try own queue
                auto opt = w.queue.pop_front();
                if (opt) {
                    Task task = std::move(*opt);

                    monitor_.emit_task_started(wid, task.id, task.complexity,
                                               task.estimated_cost,
                                               static_cast<int>(w.queue.size()));
                    monitor_.emit_cost_estimated(wid, task.id, task.complexity,
                                                 task.estimated_cost);

                    task.execute();

                    // Update estimator with actual measurement
                    estimator_.update(task.complexity, task.actual_duration);
                    task.executed_by_worker = wid;

                    double busy = w.total_busy_ms.load();
                    while (!w.total_busy_ms.compare_exchange_weak(busy, busy + task.actual_duration)) {}
                    w.tasks_completed.fetch_add(1);
                    tasks_remaining.fetch_sub(1);

                    monitor_.emit_task_completed(wid, task.id, task.actual_duration,
                                                 task.estimated_cost,
                                                 static_cast<int>(w.queue.size()));

                    my_executed.push_back(task);
                    continue;
                }

                // Own queue empty - consider stealing
                if (tasks_remaining.load() > 0) {
                    w.state.store(WorkerState::Stealing);

                    auto overhead_start = clock::now();
                    double thief_remaining = w.queue.estimated_remaining_cost();
                    StealDecision decision = stealing_policy.decide(queue_ptrs, wid, thief_remaining);
                    double overhead_ms = ms_d(clock::now() - overhead_start).count();

                    // Accumulate overhead (atomic double add via CAS)
                    double cur = total_sched_overhead_ms_.load();
                    while (!total_sched_overhead_ms_.compare_exchange_weak(cur, cur + overhead_ms)) {}

                    total_steal_attempts_.fetch_add(1);
                    w.steal_attempts.fetch_add(1);

                    if (decision.should_steal) {
                        int vid = decision.victim_id;
                        monitor_.emit_victim_selected(wid, vid, decision.expected_benefit,
                                                      static_cast<int>(w.queue.size()),
                                                      static_cast<int>(workers[vid]->queue.size()));
                        monitor_.emit_steal_attempted(wid, vid);

                        std::vector<Task> stolen = workers[vid]->queue.steal_batch(decision.batch_size);

                        if (stolen.empty()) {
                            monitor_.emit_steal_failed(wid, vid, "queue emptied before steal");
                        } else {
                            // Re-estimate stolen tasks
                            for (auto& st : stolen) {
                                st.estimated_cost = estimator_.estimate(st.complexity);
                            }
                            // Push onto own queue (front for LIFO locality)
                            for (auto it = stolen.rbegin(); it != stolen.rend(); ++it) {
                                w.queue.push_front(*it);
                            }

                            int batch = static_cast<int>(stolen.size());
                            w.steal_successes.fetch_add(1);
                            total_steal_successes_.fetch_add(1);
                            total_tasks_stolen_.fetch_add(batch);
                            w.tasks_stolen_by.fetch_add(batch);
                            workers[vid]->tasks_stolen_from.fetch_add(batch);

                            monitor_.emit_steal_succeeded(wid, vid, batch,
                                                          decision.victim_remaining_cost);
                            monitor_.emit_batch_stolen(wid, vid, batch,
                                                       decision.estimated_overhead);
                        }
                    } else {
                        int fail_id = (decision.victim_id >= 0) ? decision.victim_id : wid;
                        monitor_.emit_steal_failed(wid, fail_id, decision.reason);
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
        m.worker_id         = i;
        m.tasks_completed   = w.tasks_completed.load();
        m.tasks_stolen_from = w.tasks_stolen_from.load();
        m.tasks_stolen_by   = w.tasks_stolen_by.load();
        m.steal_attempts    = w.steal_attempts.load();
        m.steal_successes   = w.steal_successes.load();
        m.busy_ms           = w.total_busy_ms.load();
        m.idle_ms           = w.total_idle_ms.load();
        m.utilization       = w.utilization(wall_ms);
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
        mape
    );
}
