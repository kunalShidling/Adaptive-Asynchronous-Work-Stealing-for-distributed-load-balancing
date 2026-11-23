#include "Task.h"
#include <chrono>

void Task::execute() {
    auto start = std::chrono::high_resolution_clock::now();
    status = TaskStatus::Running;

    // Synthetic CPU-bound work: calibrated so complexity ~1000 is ~0.5-1.0ms
    volatile double x = 1.0;
    for (int i = 0; i < complexity; ++i) {
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
