#pragma once
#include "Task.h"
#include <vector>
#include <string>

class WorkloadGenerator {
public:
    // Generate tasks according to workload type and seed.
    // workload_type: "uniform", "heterogeneous", "imbalanced"
    static std::vector<Task> generate(
        int num_tasks,
        const std::string& workload_type,
        int min_complexity,
        int max_complexity,
        int seed
    );
};
