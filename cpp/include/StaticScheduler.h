#pragma once
#include "Scheduler.h"

class StaticScheduler : public Scheduler {
public:
    using Scheduler::Scheduler;
    ExperimentMetrics run(std::vector<Task> tasks) override;
};
