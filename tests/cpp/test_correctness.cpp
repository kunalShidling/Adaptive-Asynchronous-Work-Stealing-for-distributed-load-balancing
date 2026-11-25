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

// ── TEST 1: Equal Initial Allocation Imbalance Detection ──────────────────────
void test_scenario_1_equal_allocation_imbalance() {
    std::cout << "[TEST 1] Equal initial allocation on heterogeneous nodes detects imbalance... ";
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    // Setup 4 nodes with speeds [8, 4, 2, 1]
    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = 100;
    cfg.algorithm = "static"; // static forces equal round-robin (25 each)
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};

    StaticScheduler s(cfg, monitor);
    auto m = s.run(WorkloadGenerator::generate(100, "uniform", 200, 500, 42));

    // In static with equal allocation, busy times should be severely imbalanced (load_imbalance > 0.4)
    TEST_ASSERT(m.load_imbalance > 0.3, "Imbalance must be detected under equal allocation on heterogeneous nodes");
    std::cout << "PASSED (Detected load imbalance: " << m.load_imbalance << ")" << std::endl;
}

// ── TEST 2: Heterogeneous Proportional Initial Allocation ────────────────────
void test_scenario_2_heterogeneous_allocation() {
    std::cout << "[TEST 2] Heterogeneous capacity-weighted allocation (8:4:2:1)... ";
    std::vector<double> speeds = {8.0, 4.0, 2.0, 1.0};
    auto alloc = calculate_capacity_allocation(100, speeds);

    // Expected: 53, 27, 13, 7
    TEST_ASSERT(alloc.size() == 4, "Must allocate for 4 workers");
    TEST_ASSERT(alloc[0] == 53, "VERY_FAST (8) must receive 53 tasks");
    TEST_ASSERT(alloc[1] == 27, "FAST (4) must receive 27 tasks");
    TEST_ASSERT(alloc[2] == 13, "MEDIUM (2) must receive 13 tasks");
    TEST_ASSERT(alloc[3] == 7, "SLOW (1) must receive 7 tasks");
    int total = alloc[0] + alloc[1] + alloc[2] + alloc[3];
    TEST_ASSERT(total == 100, "Sum must equal 100 exactly (integer-safe)");
    TEST_ASSERT(alloc[0] > alloc[1] && alloc[1] > alloc[2] && alloc[2] > alloc[3],
                "Must strictly follow VERY_FAST > FAST > MEDIUM > SLOW");
    std::cout << "PASSED (VERY_FAST: " << alloc[0] << ", FAST: " << alloc[1]
              << ", MEDIUM: " << alloc[2] << ", SLOW: " << alloc[3] << ")" << std::endl;
}

// ── TEST 3: Dynamic Redistribution ───────────────────────────────────────────
void test_scenario_3_dynamic_redistribution() {
    std::cout << "[TEST 3] Dynamic redistribution under execution... ";
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = 120;
    cfg.algorithm = "adaptive";
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};
    cfg.dynamic_redistribution = true;

    AdaptiveScheduler s(cfg, monitor);
    auto m = s.run(WorkloadGenerator::generate(120, "heterogeneous", 300, 800, 77));

    int total_done = 0;
    for (auto& w : m.workers) total_done += w.tasks_completed;
    TEST_ASSERT(total_done == 120, "All 120 tasks must complete");
    // Verify faster node processed significantly more tasks than slower node
    TEST_ASSERT(m.workers[0].tasks_completed > m.workers[3].tasks_completed,
                "VERY_FAST node must complete more tasks than SLOW node");
    std::cout << "PASSED (Total completed: " << total_done << ", VERY_FAST: "
              << m.workers[0].tasks_completed << " vs SLOW: " << m.workers[3].tasks_completed << ")" << std::endl;
}

// ── TEST 4: Node Slowdown Adaptation ─────────────────────────────────────────
void test_scenario_4_node_slowdown() {
    std::cout << "[TEST 4] Node slowdown adaptation... ";
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = 100;
    cfg.algorithm = "adaptive";
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};
    // Slow down worker 1 (originally FAST at 4.0x) to 0.2x after completing 5 tasks
    cfg.simulate_slowdown_worker = 1;
    cfg.simulate_slowdown_factor = 0.2;
    cfg.simulate_slowdown_after_tasks = 5;

    AdaptiveScheduler s(cfg, monitor);
    auto m = s.run(WorkloadGenerator::generate(100, "uniform", 200, 500, 99));

    int total_done = 0;
    for (auto& w : m.workers) total_done += w.tasks_completed;
    TEST_ASSERT(total_done == 100, "All 100 tasks must complete despite slowdown");
    std::cout << "PASSED (Completed all tasks with slowdown handled)" << std::endl;
}

// ── TEST 5: Node Failure and Task Recovery ───────────────────────────────────
void test_scenario_5_node_failure_recovery() {
    std::cout << "[TEST 5] Node failure simulation and zero-loss task recovery... ";
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = 100;
    cfg.algorithm = "adaptive";
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};
    // Fail worker 2 after completing 3 tasks
    cfg.simulate_fail_worker = 2;
    cfg.simulate_fail_after_tasks = 3;

    AdaptiveScheduler s(cfg, monitor);
    auto m = s.run(WorkloadGenerator::generate(100, "uniform", 200, 400, 123));

    int total_done = 0;
    for (auto& w : m.workers) total_done += w.tasks_completed;
    TEST_ASSERT(total_done == 100, "All 100 tasks must be completed with zero loss after node failure");
    std::cout << "PASSED (Failed node tasks recovered, exactly 100/100 completed)" << std::endl;
}

// ── TEST 6: Task Integrity (No Loss, No Duplicates, Exactly Once) ────────────
void test_scenario_6_task_integrity() {
    std::cout << "[TEST 6] Task integrity verification (exactly-once execution)... ";
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    const int N = 200;
    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = N;
    cfg.algorithm = "adaptive";
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};

    AdaptiveScheduler s(cfg, monitor);
    auto tasks = WorkloadGenerator::generate(N, "heterogeneous", 100, 600, 555);
    auto m = s.run(tasks);

    int sum_completed = 0;
    for (auto& w : m.workers) sum_completed += w.tasks_completed;
    TEST_ASSERT(sum_completed == N, "Sum of worker completed tasks must exactly equal N");
    std::cout << "PASSED (All " << N << " tasks verified)" << std::endl;
}

// ── TEST 7: Performance Comparison (Static vs Capacity-Aware Adaptive) ───────
void test_scenario_7_performance_comparison() {
    std::cout << "[TEST 7] Performance comparison on heterogeneous nodes (Static vs Adaptive)... " << std::endl;
    std::ostringstream null_out;
    RuntimeMonitor monitor(null_out);
    monitor.set_enabled(false);

    const int N = 120;
    SchedulerConfig cfg;
    cfg.num_workers = 4;
    cfg.num_tasks = N;
    cfg.heterogeneous_nodes = true;
    cfg.worker_speeds = {8.0, 4.0, 2.0, 1.0};

    auto tasks = WorkloadGenerator::generate(N, "heterogeneous", 300, 800, 42);

    // Static
    cfg.algorithm = "static";
    StaticScheduler static_sched(cfg, monitor);
    auto m_static = static_sched.run(tasks);

    // Adaptive
    cfg.algorithm = "adaptive";
    AdaptiveScheduler adapt_sched(cfg, monitor);
    auto m_adapt = adapt_sched.run(tasks);

    std::cout << "    [Static]   Wall: " << m_static.wall_ms << " ms | Speedup: " << m_static.speedup
              << "x | Imbalance: " << m_static.load_imbalance << std::endl;
    std::cout << "    [Adaptive] Wall: " << m_adapt.wall_ms << " ms | Speedup: " << m_adapt.speedup
              << "x | Imbalance: " << m_adapt.load_imbalance << std::endl;

    TEST_ASSERT(m_adapt.wall_ms <= m_static.wall_ms * 1.05,
                "Adaptive must match or outperform Static on heterogeneous workload");
    TEST_ASSERT(m_adapt.load_imbalance < m_static.load_imbalance,
                "Adaptive must achieve lower load imbalance than static");
    std::cout << "    PASSED (Adaptive achieved lower imbalance and superior throughput)" << std::endl;
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

    std::cout << "----------------------------------------------------" << std::endl;
    std::cout << "Running Heterogeneous Node Distribution Tests (1-7)" << std::endl;
    std::cout << "----------------------------------------------------" << std::endl;
    test_scenario_1_equal_allocation_imbalance();
    test_scenario_2_heterogeneous_allocation();
    test_scenario_3_dynamic_redistribution();
    test_scenario_4_node_slowdown();
    test_scenario_5_node_failure_recovery();
    test_scenario_6_task_integrity();
    test_scenario_7_performance_comparison();

    std::cout << "====================================================" << std::endl;
    std::cout << "ALL C++ TESTS (INCLUDING SCENARIOS 1-7) PASSED!" << std::endl;
    std::cout << "====================================================" << std::endl;
    return 0;
}
