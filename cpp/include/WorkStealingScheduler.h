#pragma once
#include "Scheduler.h"

class WorkStealingScheduler : public Scheduler {
public:
    using Scheduler::Scheduler;
    ExperimentMetrics run(std::vector<Task> tasks) override;
};
