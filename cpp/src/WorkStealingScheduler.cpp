#include "WorkStealingScheduler.h"
#include "Worker.h"
#include "Metrics.h"
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <random>

ExperimentMetrics WorkStealingScheduler::run(std::vector<Task> tasks) {
    using clock = std::chrono::high_resolution_clock;

    int num_workers = cfg_.num_workers;
    int num_tasks   = static_cast<int>(tasks.size());

    // Sequential baseline
    std::vector<Task> seq_tasks = tasks;
    auto seq_start = clock::now();
    for (auto& t : seq_tasks) t.execute();
    auto seq_end = clock::now();
    double sequential_ms = std::chrono::duration<double, std::milli>(seq_end - seq_start).count();

    // Create workers
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) workers.emplace_back(std::make_unique<Worker>(i));

    // Distribute tasks round-robin
    for (int i = 0; i < num_tasks; ++i) {
        tasks[i].assigned_worker = i % num_workers;
        workers[i % num_workers]->queue.push_front(tasks[i]);
    }

    // Global counters
    std::atomic<int> tasks_remaining{num_tasks};
    std::atomic<int> total_steal_attempted{0};
    std::atomic<int> total_steal_succeeded{0};
    std::atomic<int> total_tasks_stolen{0};

    monitor_.emit_experiment_started(num_workers, num_tasks, "work_stealing", cfg_.experiment_id);
    auto wall_start = clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_workers);

    for (int wid = 0; wid < num_workers; ++wid) {
        threads.emplace_back([&, wid]() {
            Worker& w = *workers[wid];
            monitor_.emit_worker_started(wid);
            w.state.store(WorkerState::Running);

            // Per-thread RNG for victim selection
            std::mt19937 rng(static_cast<unsigned>(cfg_.random_seed + wid));
            std::uniform_int_distribution<int> dist(0, num_workers - 1);

            while (true) {
                // Try own queue first
                auto opt = w.queue.pop_front();
                if (opt) {
                    Task task = std::move(*opt);
                    monitor_.emit_task_started(wid, task.id, task.complexity, task.estimated_cost,
                                               static_cast<int>(w.queue.size()));
                    task.execute();
                    task.executed_by_worker = wid;

                    double busy = w.total_busy_ms.load();
                    while (!w.total_busy_ms.compare_exchange_weak(busy, busy + task.actual_duration)) {}
                    w.tasks_completed.fetch_add(1);
                    tasks_remaining.fetch_sub(1);

                    monitor_.emit_task_completed(wid, task.id, task.actual_duration, task.estimated_cost,
                                                 static_cast<int>(w.queue.size()));
                    continue;
                }

                // Own queue empty - check termination
                if (tasks_remaining.load() == 0) break;

                // Try stealing from a random victim
                w.state.store(WorkerState::Stealing);

                // Pick a victim that is not self
                int victim_id = -1;
                for (int attempt = 0; attempt < num_workers * 2; ++attempt) {
                    int v = dist(rng);
                    if (v != wid) { victim_id = v; break; }
                }
                if (victim_id == -1) {
                    if (tasks_remaining.load() == 0) break;
                    std::this_thread::yield();
                    continue;
                }

                w.steal_attempts.fetch_add(1);
                total_steal_attempted.fetch_add(1);
                monitor_.emit_steal_attempted(wid, victim_id);

                auto stolen_opt = workers[victim_id]->queue.steal_back();
                if (stolen_opt) {
                    Task stolen = std::move(*stolen_opt);
                    workers[victim_id]->tasks_stolen_from.fetch_add(1);
                    w.tasks_stolen_by.fetch_add(1);
                    w.steal_successes.fetch_add(1);
                    total_steal_succeeded.fetch_add(1);
                    total_tasks_stolen.fetch_add(1);

                    monitor_.emit_steal_succeeded(wid, victim_id, 1,
                        workers[victim_id]->queue.estimated_remaining_cost());
                    w.queue.push_front(stolen);
                    w.state.store(WorkerState::Running);
                } else {
                    monitor_.emit_steal_failed(wid, victim_id, "victim queue empty");
                    if (tasks_remaining.load() == 0) break;
                    std::this_thread::yield();
                }
            }

            w.state.store(WorkerState::Stopped);
            monitor_.emit_worker_idle(wid, 0);
        });
    }

    for (auto& th : threads) th.join();

    auto wall_end = clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    monitor_.emit_experiment_completed(cfg_.experiment_id, wall_ms, num_tasks);

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
        cfg_.experiment_id, "work_stealing", num_workers, num_tasks,
        cfg_.workload_type, cfg_.random_seed,
        wall_ms, sequential_ms, wm,
        total_steal_attempted.load(),
        total_steal_succeeded.load(),
        total_tasks_stolen.load(),
        0.0, 0.0
    );
}
