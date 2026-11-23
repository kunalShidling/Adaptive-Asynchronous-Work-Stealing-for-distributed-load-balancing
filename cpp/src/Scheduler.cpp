#include "Scheduler.h"
#include "WorkloadGenerator.h"

Scheduler::Scheduler(const SchedulerConfig& cfg, RuntimeMonitor& monitor)
    : cfg_(cfg), monitor_(monitor) {}

std::vector<Task> Scheduler::generate_tasks() const {
    return WorkloadGenerator::generate(
        cfg_.num_tasks,
        cfg_.workload_type,
        cfg_.min_complexity,
        cfg_.max_complexity,
        cfg_.random_seed
    );
}
