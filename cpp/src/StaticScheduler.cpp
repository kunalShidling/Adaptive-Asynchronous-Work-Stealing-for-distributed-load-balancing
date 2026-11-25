#include "StaticScheduler.h"
#include "Worker.h"
#include "Metrics.h"
#include <thread>
#include <vector>
#include <chrono>

ExperimentMetrics StaticScheduler::run(std::vector<Task> tasks) {
    using clock = std::chrono::high_resolution_clock;

    int num_workers = cfg_.num_workers;
    int num_tasks = static_cast<int>(tasks.size());

    // Sequential baseline: run all tasks single-threaded on copies
    std::vector<Task> seq_tasks = tasks;
    auto seq_start = clock::now();
    for (auto& t : seq_tasks) {
        t.execute();
    }
    auto seq_end = clock::now();
    double sequential_ms = std::chrono::duration<double, std::milli>(seq_end - seq_start).count();

    // Create workers with configured/detected speeds
    std::vector<double> speeds = get_cluster_speeds(cfg_);
    std::vector<std::unique_ptr<Worker>> workers;
    workers.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        workers.emplace_back(std::make_unique<Worker>(i, speeds[i]));
    }

    // Distribute tasks round-robin (static baseline distributes equally)
    for (int i = 0; i < num_tasks; ++i) {
        int wid = i % num_workers;
        tasks[i].assigned_worker = wid;
        tasks[i].initial_assigned_worker = wid;
        workers[wid]->queue.push_front(tasks[i]);
        workers[wid]->initial_assigned_tasks.fetch_add(1);
    }

    monitor_.emit_experiment_started(num_workers, num_tasks, "static", cfg_.experiment_id);
    for (int i = 0; i < num_workers; ++i) {
        monitor_.emit_node_classified(i, to_string(workers[i]->category.load()),
                                      workers[i]->speed_factor.load(), 0.0);
        monitor_.emit_initial_allocation(i, workers[i]->initial_assigned_tasks.load(),
                                         to_string(workers[i]->category.load()));
    }
    auto wall_start = clock::now();

    // Launch worker threads
    std::vector<std::thread> threads;
    threads.reserve(num_workers);

    for (int wid = 0; wid < num_workers; ++wid) {
        threads.emplace_back([&, wid]() {
            Worker& w = *workers[wid];
            monitor_.emit_worker_started(wid);
            w.state.store(WorkerState::Running);

            while (true) {
                auto opt = w.queue.pop_front();
                if (!opt) break;
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
                monitor_.emit_task_completed(wid, task.id, task.actual_duration, task.estimated_cost,
                                             static_cast<int>(w.queue.size()));
            }

            w.state.store(WorkerState::Stopped);
            monitor_.emit_worker_idle(wid, 0);
        });
    }

    for (auto& th : threads) th.join();

    auto wall_end = clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    monitor_.emit_experiment_completed(cfg_.experiment_id, wall_ms, num_tasks);

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
        cfg_.experiment_id, "static", num_workers, num_tasks,
        cfg_.workload_type, cfg_.random_seed,
        wall_ms, sequential_ms, wm,
        0, 0, 0, 0.0, 0.0
    );
}
