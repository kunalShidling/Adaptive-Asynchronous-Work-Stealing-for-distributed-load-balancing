#include "Task.h"
#include <chrono>
#include <algorithm>
#include <cmath>

void Task::execute(double speed_factor) {
    auto start = std::chrono::high_resolution_clock::now();
    status = TaskStatus::Running;

    if (speed_factor <= 0.0) speed_factor = 1.0;
    int eff_complexity = std::max(1, static_cast<int>(std::round(static_cast<double>(complexity) / speed_factor)));

    // Synthetic CPU-bound work: calibrated so complexity ~1000 is ~0.5-1.0ms on 1.0x
    volatile double x = 1.0;
    for (int i = 0; i < eff_complexity; ++i) {
        for (int j = 0; j < 50; ++j) {
            x = x * 1.000001 + 0.000001;
            if (j % 10 == 0) x = x / 1.0001;
        }
    }
    (void)x; // prevent optimization

    auto end = std::chrono::high_resolution_clock::now();
    actual_duration = std::chrono::duration<double, std::milli>(end - start).count();
    status = TaskStatus::Completed;
}
