#include "WorkloadGenerator.h"
#include <random>
#include <algorithm>
#include <cmath>

std::vector<Task> WorkloadGenerator::generate(
    int num_tasks,
    const std::string& workload_type,
    int min_complexity,
    int max_complexity,
    int seed)
{
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::vector<Task> tasks;
    tasks.reserve(num_tasks);

    uint64_t next_id = 1;

    if (workload_type == "uniform") {
        int mid = (min_complexity + max_complexity) / 2;
        int spread = (max_complexity - min_complexity) / 10;
        if (spread < 1) spread = 1;
        std::uniform_int_distribution<int> dist(mid - spread, mid + spread);

        for (int i = 0; i < num_tasks; ++i) {
            Task t(next_id++, TaskType::Uniform, dist(rng));
            tasks.push_back(t);
        }

    } else if (workload_type == "heterogeneous") {
        std::uniform_int_distribution<int> dist(min_complexity, max_complexity);

        for (int i = 0; i < num_tasks; ++i) {
            Task t(next_id++, TaskType::Heterogeneous, dist(rng));
            tasks.push_back(t);
        }

    } else { // "imbalanced"
        int heavy_count = static_cast<int>(std::ceil(num_tasks * 0.20));
        int light_count = num_tasks - heavy_count;

        int heavy_complexity = static_cast<int>(max_complexity * 0.9);
        int light_complexity = static_cast<int>(min_complexity * 1.1);
        if (light_complexity < 1) light_complexity = 1;

        std::vector<Task> heavy_tasks, light_tasks;
        heavy_tasks.reserve(heavy_count);
        light_tasks.reserve(light_count);

        for (int i = 0; i < heavy_count; ++i) {
            Task t(next_id++, TaskType::Imbalanced, heavy_complexity);
            heavy_tasks.push_back(t);
        }
        for (int i = 0; i < light_count; ++i) {
            Task t(next_id++, TaskType::Imbalanced, light_complexity);
            light_tasks.push_back(t);
        }

        // Shuffle within each group to simulate initial imbalance
        std::shuffle(heavy_tasks.begin(), heavy_tasks.end(), rng);
        std::shuffle(light_tasks.begin(), light_tasks.end(), rng);

        // Combine: heavy tasks first (front-loaded), then light
        for (auto& t : heavy_tasks) tasks.push_back(std::move(t));
        for (auto& t : light_tasks) tasks.push_back(std::move(t));
    }

    return tasks;
}
