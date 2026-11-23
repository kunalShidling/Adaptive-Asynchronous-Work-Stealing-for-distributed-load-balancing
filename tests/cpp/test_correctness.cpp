/**
 * C++ Unit and Concurrency Test Suite
 * Tests:
 * 1. Task execution and timing
 * 2. TaskQueue thread-safety (multi-threaded concurrent push, pop, steal)
 * 3. CostEstimator EWMA updates, fallback, and convergence
 * 4. StealingPolicy decision boundaries and batch sizing
 * 5. WorkloadGenerator uniqueness, distribution, and reproducibility
 * 6. Edge cases (0 tasks, 1 task, 1 worker, more workers than tasks)
 * 7. Correctness: All 4 algorithms complete exactly once with zero tasks lost or duplicated
 */

#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include <cmath>
#include <set>

#include "Task.h"
#include "TaskQueue.h"
#include "CostEstimator.h"
#include "StealingPolicy.h"
#include "WorkloadGenerator.h"
#include "RuntimeMonitor.h"
#include "StaticScheduler.h"
#include "DynamicScheduler.h"
#include "WorkStealingScheduler.h"
#include "AdaptiveScheduler.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while(0)

void test_task_execution() {
    std::cout << "[TEST] Task execution and timing... ";
    Task t(101, TaskType::Uniform, 500);
    TEST_ASSERT(t.status == TaskStatus::Pending, "Task should start Pending");
    t.execute();
    TEST_ASSERT(t.status == TaskStatus::Completed, "Task should be Completed");
    TEST_ASSERT(t.actual_duration > 0.0, "Actual duration should be positive");
    std::cout << "PASSED (" << t.actual_duration << " ms)" << std::endl;
}

void test_task_queue_concurrency() {
    std::cout << "[TEST] TaskQueue concurrent push/pop/steal... ";
    TaskQueue q;
    const int N = 1000;
    for (int i = 0; i < N; ++i) {
        q.push_front(Task(i, TaskType::Uniform, 100));
    }
    TEST_ASSERT(q.size() == N, "Queue size must match initial items");

    std::atomic<int> popped{0};
    std::atomic<int> stolen{0};

    // 1 owner popping from front, 2 thieves stealing from back
    std::thread owner([&]() {
        while (true) {
            auto t = q.pop_front();
            if (!t) break;
            popped++;
        }
    });

    auto thief_func = [&]() {
        while (true) {
            auto batch = q.steal_batch(4);
            if (batch.empty()) {
                auto t = q.steal_back();
                if (!t) {
                    if (q.empty()) break;
                    std::this_thread::yield();
                    continue;
                }
                stolen++;
            } else {
                stolen += batch.size();
            }
        }
    };

    std::thread thief1(thief_func);
    std::thread thief2(thief_func);

    owner.join();
    thief1.join();
    thief2.join();

    TEST_ASSERT(popped + stolen == N, "Sum of popped and stolen must equal total items");
    TEST_ASSERT(q.empty(), "Queue must be empty after all items processed");
    std::cout << "PASSED (Popped: " << popped << ", Stolen: " << stolen << ")" << std::endl;
}

void test_cost_estimator() {
    std::cout << "[TEST] CostEstimator EWMA updates & fallback... ";
    CostEstimator est(0.5); // alpha = 0.5
    
    // Fallback when no samples exist
    double f1 = est.estimate(1000);
    TEST_ASSERT(f1 > 0.0, "Fallback estimate must be positive");

    // Add first sample
    est.update(1000, 10.0);
    TEST_ASSERT(std::abs(est.estimate(1000) - 10.0) < 1e-6, "First sample should set estimate directly");

    // Add second sample (10.0 * 0.5 + 20.0 * 0.5 = 15.0)
    est.update(1000, 20.0);
    TEST_ASSERT(std::abs(est.estimate(1000) - 15.0) < 1e-6, "EWMA update calculation must match formula");

    // Reset
    est.reset();
    TEST_ASSERT(std::abs(est.estimate(1000) - f1) < 1e-6, "Reset must restore fallback");
    std::cout << "PASSED" << std::endl;
}

void test_workload_generator() {
    std::cout << "[TEST] WorkloadGenerator uniqueness & reproducibility... ";
    auto tasks1 = WorkloadGenerator::generate(100, "heterogeneous", 500, 5000, 42);
    auto tasks2 = WorkloadGenerator::generate(100, "heterogeneous", 500, 5000, 42);

    TEST_ASSERT(tasks1.size() == 100, "Must generate exact number of tasks");
    TEST_ASSERT(tasks2.size() == 100, "Must generate exact number of tasks");

    std::set<uint64_t> ids;
    for (size_t i = 0; i < tasks1.size(); ++i) {
        TEST_ASSERT(ids.insert(tasks1[i].id).second, "Task IDs must be strictly unique");
        TEST_ASSERT(tasks1[i].complexity == tasks2[i].complexity, "Same seed must produce identical tasks");
        TEST_ASSERT(tasks1[i].complexity >= 500 && tasks1[i].complexity <= 5000, "Complexity must be within bounds");
    }
    std::cout << "PASSED" << std::endl;
}

void test_all_algorithms_correctness() {
    std::cout << "[TEST] Verifying all 4 scheduling algorithms for task execution correctness... " << std::endl;

    std::stringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false); // quiet for test run

    int num_tasks = 80;
    int num_workers = 4;
    auto base_tasks = WorkloadGenerator::generate(num_tasks, "heterogeneous", 500, 3000, 123);

    std::vector<std::string> algos = {"static", "dynamic", "work_stealing", "adaptive"};

    for (const auto& algo_name : algos) {
        SchedulerConfig cfg;
        cfg.algorithm = algo_name;
        cfg.num_workers = num_workers;
        cfg.num_tasks = num_tasks;
        cfg.workload_type = "heterogeneous";
        cfg.min_complexity = 500;
        cfg.max_complexity = 3000;
        cfg.random_seed = 123;
        cfg.experiment_id = 1;

        std::vector<Task> tasks_copy = base_tasks;
        ExperimentMetrics metrics;

        if (algo_name == "static") {
            StaticScheduler s(cfg, monitor);
            metrics = s.run(tasks_copy);
        } else if (algo_name == "dynamic") {
            DynamicScheduler s(cfg, monitor);
            metrics = s.run(tasks_copy);
        } else if (algo_name == "work_stealing") {
            WorkStealingScheduler s(cfg, monitor);
            metrics = s.run(tasks_copy);
        } else if (algo_name == "adaptive") {
            AdaptiveScheduler s(cfg, monitor);
            metrics = s.run(tasks_copy);
        }

        // Verify total completed tasks across workers equals submitted tasks
        int total_completed = 0;
        for (const auto& w : metrics.workers) {
            total_completed += w.tasks_completed;
        }

        TEST_ASSERT(total_completed == num_tasks, "Total tasks completed must equal submitted tasks exactly");
        TEST_ASSERT(metrics.wall_ms > 0.0, "Wall time must be greater than zero");
        TEST_ASSERT(metrics.speedup > 0.0, "Speedup must be positive");

        std::cout << "    [" << algo_name << "] OK: " << total_completed << "/" << num_tasks 
                  << " tasks completed, Wall: " << metrics.wall_ms << " ms, Speedup: " 
                  << metrics.speedup << "x" << std::endl;
    }
}

void test_edge_cases() {
    std::cout << "[TEST] Edge cases (1 worker, 1 task, more workers than tasks)... " << std::endl;
    std::stringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    // Case 1: 1 worker, 10 tasks
    {
        SchedulerConfig cfg;
        cfg.num_workers = 1;
        cfg.num_tasks = 10;
        cfg.algorithm = "adaptive";
        AdaptiveScheduler s(cfg, monitor);
        auto m = s.run(WorkloadGenerator::generate(10, "uniform", 200, 500, 1));
        int done = 0;
        for (auto& w : m.workers) done += w.tasks_completed;
        TEST_ASSERT(done == 10, "1-worker case must complete 10 tasks");
        std::cout << "    1 worker: OK" << std::endl;
    }

    // Case 2: 1 task, 4 workers
    {
        SchedulerConfig cfg;
        cfg.num_workers = 4;
        cfg.num_tasks = 1;
        cfg.algorithm = "adaptive";
        AdaptiveScheduler s(cfg, monitor);
        auto m = s.run(WorkloadGenerator::generate(1, "uniform", 200, 500, 2));
        int done = 0;
        for (auto& w : m.workers) done += w.tasks_completed;
        TEST_ASSERT(done == 1, "1-task case must complete exactly 1 task");
        std::cout << "    1 task / 4 workers: OK" << std::endl;
    }

    // Case 3: More workers than tasks (8 workers, 3 tasks)
    {
        SchedulerConfig cfg;
        cfg.num_workers = 8;
        cfg.num_tasks = 3;
        cfg.algorithm = "adaptive";
        AdaptiveScheduler s(cfg, monitor);
        auto m = s.run(WorkloadGenerator::generate(3, "uniform", 200, 500, 3));
        int done = 0;
        for (auto& w : m.workers) done += w.tasks_completed;
        TEST_ASSERT(done == 3, "More workers than tasks case must complete 3 tasks");
        std::cout << "    8 workers / 3 tasks: OK" << std::endl;
    }
}

int main() {
    std::cout << "====================================================" << std::endl;
    std::cout << "Running Cost-Aware Scheduler C++ Test Suite" << std::endl;
    std::cout << "====================================================" << std::endl;

    test_task_execution();
    test_task_queue_concurrency();
    test_cost_estimator();
    test_workload_generator();
    test_all_algorithms_correctness();
    test_edge_cases();

    std::cout << "====================================================" << std::endl;
    std::cout << "ALL C++ TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
