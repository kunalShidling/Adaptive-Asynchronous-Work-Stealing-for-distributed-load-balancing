#pragma once
#include <string>
#include <chrono>
#include <cstdint>
#include <functional>

enum class TaskStatus { Pending, Running, Completed };
enum class TaskType { Uniform, Heterogeneous, Imbalanced };

struct Task {
    uint64_t id;
    TaskType type;
    int complexity;          // workload size (controls synthetic CPU work)
    double estimated_cost;   // ms, from cost estimator
    double actual_duration;  // ms, measured at runtime
    TaskStatus status;
    int assigned_worker;     // -1 = unassigned
    int executed_by_worker;  // which worker actually ran it

    Task() : id(0), type(TaskType::Uniform), complexity(1000),
             estimated_cost(0.0), actual_duration(0.0),
             status(TaskStatus::Pending), assigned_worker(-1), executed_by_worker(-1) {}

    Task(uint64_t id_, TaskType type_, int complexity_)
        : id(id_), type(type_), complexity(complexity_),
          estimated_cost(0.0), actual_duration(0.0),
          status(TaskStatus::Pending), assigned_worker(-1), executed_by_worker(-1) {}

    // Execute synthetic CPU-bound work
    void execute();
};
