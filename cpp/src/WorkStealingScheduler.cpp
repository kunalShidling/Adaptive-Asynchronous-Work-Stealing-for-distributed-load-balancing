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

    // Create workers with heterogeneous speeds
    std::vector<double> speeds = get_cluster_speeds(cfg_);
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) workers.emplace_back(std::make_unique<Worker>(i, speeds[i]));

    // Capacity-aware proportional initial distribution (NOT round-robin)
    // VERY_FAST nodes receive the most tasks, SLOW the fewest
    std::vector<int> initial_alloc = calculate_capacity_allocation(num_tasks, speeds);
    int task_idx = 0;
    for (int wid = 0; wid < num_workers; ++wid) {
        int count = initial_alloc[wid];
        workers[wid]->initial_assigned_tasks.store(count);
        for (int k = 0; k < count && task_idx < num_tasks; ++k, ++task_idx) {
            tasks[task_idx].assigned_worker = wid;
            tasks[task_idx].initial_assigned_worker = wid;
            workers[wid]->queue.push_front(tasks[task_idx]);
        }
    }

    // Global counters
    std::atomic<int> tasks_remaining{num_tasks};
    std::atomic<int> total_steal_attempted{0};
    std::atomic<int> total_steal_succeeded{0};
    std::atomic<int> total_tasks_stolen{0};

    monitor_.emit_experiment_started(num_workers, num_tasks, "work_stealing", cfg_.experiment_id);
    for (int i = 0; i < num_workers; ++i) {
        monitor_.emit_node_classified(i, to_string(workers[i]->category.load()),
                                      workers[i]->speed_factor.load(), 0.0);
        monitor_.emit_initial_allocation(i, workers[i]->initial_assigned_tasks.load(),
                                         to_string(workers[i]->category.load()));
    }
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
                    w.current_task_id.store(task.id);
                    monitor_.emit_task_started(wid, task.id, task.complexity, task.estimated_cost,
                                               static_cast<int>(w.queue.size()));
                    task.execute(w.speed_factor.load());
                    task.executed_by_worker = wid;
                    w.current_task_id.store(0);

                    double busy = w.total_busy_ms.load();
                    while (!w.total_busy_ms.compare_exchange_weak(busy, busy + task.actual_duration)) {}
                    w.tasks_completed.fetch_add(1);
                    w.record_task_completed(task.actual_duration);
                    tasks_remaining.fetch_sub(1);

                    monitor_.emit_task_completed(wid, task.id, task.actual_duration, task.estimated_cost,
                                                 static_cast<int>(w.queue.size()));
                    continue;
                }

                // Own queue empty - check termination
                if (tasks_remaining.load() == 0) break;

                // Try stealing from a random victim
                w.state.store(WorkerState::Stealing);

                // Pick a victim that is equal or slower:
                // Slower workers must NEVER steal from faster workers!
                // Tasks flow in descending order: VERY_FAST -> FAST -> MEDIUM -> SLOW
                int victim_id = -1;
                double my_speed = w.speed_factor.load();
                for (int attempt = 0; attempt < num_workers * 4; ++attempt) {
                    int v = dist(rng);
                    if (v != wid && !workers[v]->queue.empty()) {
                        if (my_speed >= workers[v]->speed_factor.load()) {
                            victim_id = v;
                            break;
                        }
                    }
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
                    workers[victim_id]->tasks_redistributed_out.fetch_add(1);
                    w.tasks_redistributed_in.fetch_add(1);
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
        m.worker_id               = i;
        m.node_category           = to_string(w.category.load());
        m.speed_factor            = w.speed_factor.load();
        m.initial_assigned        = w.initial_assigned_tasks.load();
        m.tasks_completed         = w.tasks_completed.load();
        m.tasks_failed            = w.tasks_failed.load();
        m.tasks_redistributed_in  = 0;
        m.tasks_redistributed_out = 0;
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
        cfg_.experiment_id, "work_stealing", num_workers, num_tasks,
        cfg_.workload_type, cfg_.random_seed,
        wall_ms, sequential_ms, wm,
        total_steal_attempted.load(),
        total_steal_succeeded.load(),
        total_tasks_stolen.load(),
        0.0, 0.0
    );
}
