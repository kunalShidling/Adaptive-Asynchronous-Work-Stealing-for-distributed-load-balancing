#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "WorkloadGenerator.h"
#include "RuntimeMonitor.h"
#include "Scheduler.h"
#include "StaticScheduler.h"
#include "DynamicScheduler.h"
#include "WorkStealingScheduler.h"
#include "AdaptiveScheduler.h"
#include "Metrics.h"
#include "StealingPolicy.h"

#include "vendor/nlohmann/json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

static SchedulerConfig parse_config(const json& j) {
    SchedulerConfig cfg;
    cfg.experiment_id  = j.value("experiment_id",  static_cast<uint64_t>(1));
    cfg.algorithm      = j.value("algorithm",       std::string("adaptive"));
    cfg.num_workers    = j.value("num_workers",     4);
    cfg.num_tasks      = j.value("num_tasks",       100);
    cfg.workload_type  = j.value("workload_type",   std::string("uniform"));
    cfg.min_complexity = j.value("min_complexity",  500);
    cfg.max_complexity = j.value("max_complexity",  5000);
    cfg.random_seed    = j.value("random_seed",     42);

    if (j.contains("stealing")) {
        const auto& s = j["stealing"];
        cfg.stealing.ewma_alpha      = s.value("ewma_alpha",      0.3);
        cfg.stealing.min_benefit_ms  = s.value("min_benefit_ms",  0.5);
        cfg.stealing.overhead_ms     = s.value("overhead_ms",     0.1);
        cfg.stealing.max_batch_size  = s.value("max_batch_size",  8);
        cfg.stealing.min_victim_queue= s.value("min_victim_queue",2);

        std::string vp = s.value("victim_policy", std::string("max_remaining_cost"));
        if (vp == "random")               cfg.stealing.policy = VictimSelectionPolicy::Random;
        else if (vp == "max_queue")       cfg.stealing.policy = VictimSelectionPolicy::MaxQueue;
        else                               cfg.stealing.policy = VictimSelectionPolicy::MaxRemainingCost;
    }
    return cfg;
}

static void ensure_dir(const fs::path& p) {
    if (!fs::exists(p)) fs::create_directories(p);
}

static void write_summary(const fs::path& results_root, uint64_t exp_id, const ExperimentMetrics& m) {
    fs::path dir = results_root / std::to_string(exp_id);
    ensure_dir(dir);
    fs::path out_path = dir / "summary.json";
    std::ofstream f(out_path);
    if (!f) throw std::runtime_error("Cannot open " + out_path.string());
    f << m.to_json() << "\n";
}

static void write_comparison(const fs::path& results_root, uint64_t exp_id,
                              const std::vector<ExperimentMetrics>& all) {
    fs::path dir = results_root / std::to_string(exp_id);
    ensure_dir(dir);
    fs::path out_path = dir / "comparison.json";
    std::ofstream f(out_path);
    if (!f) throw std::runtime_error("Cannot open " + out_path.string());

    f << "[\n";
    for (size_t i = 0; i < all.size(); ++i) {
        f << all[i].to_json();
        if (i + 1 < all.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

int main(int argc, char* argv[]) {
    // Determine config source
    json config_json;
    try {
        if (argc >= 3 && std::string(argv[1]) == "--config") {
            std::ifstream cfg_file(argv[2]);
            if (!cfg_file) {
                std::cerr << "Cannot open config file: " << argv[2] << "\n";
                return 1;
            }
            cfg_file >> config_json;
        } else {
            std::cin >> config_json;
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config JSON: " << e.what() << "\n";
        return 1;
    }

    SchedulerConfig base_cfg = parse_config(config_json);
    std::string results_dir  = config_json.value("results_dir", std::string("results/experiments"));

    // Resolve results directory relative to executable or cwd
    fs::path results_root;
    if (fs::path(results_dir).is_absolute()) {
        results_root = results_dir;
    } else {
        // Try relative to executable's parent
        fs::path exe_path(argv[0]);
        fs::path exe_parent = exe_path.parent_path();
        results_root = exe_parent / results_dir;
        // Fall back to cwd if that doesn't make sense
        if (!fs::exists(exe_parent)) {
            results_root = fs::current_path() / results_dir;
        }
    }
    ensure_dir(results_root);

    // RuntimeMonitor writes JSONL events to stdout
    RuntimeMonitor monitor(std::cout);

    // Generate tasks once with the given seed
    std::vector<Task> base_tasks = WorkloadGenerator::generate(
        base_cfg.num_tasks,
        base_cfg.workload_type,
        base_cfg.min_complexity,
        base_cfg.max_complexity,
        base_cfg.random_seed
    );

    std::string algo = base_cfg.algorithm;

    auto run_one = [&](const std::string& name) -> ExperimentMetrics {
        SchedulerConfig cfg = base_cfg;
        cfg.algorithm = name;
        std::vector<Task> tasks_copy = base_tasks; // deep copy

        if (name == "static") {
            StaticScheduler s(cfg, monitor);
            return s.run(tasks_copy);
        } else if (name == "dynamic") {
            DynamicScheduler s(cfg, monitor);
            return s.run(tasks_copy);
        } else if (name == "work_stealing") {
            WorkStealingScheduler s(cfg, monitor);
            return s.run(tasks_copy);
        } else if (name == "adaptive") {
            AdaptiveScheduler s(cfg, monitor);
            return s.run(tasks_copy);
        } else {
            throw std::runtime_error("Unknown algorithm: " + name);
        }
    };

    if (algo == "all") {
        std::vector<ExperimentMetrics> all_results;
        std::vector<std::string> names = {"static", "dynamic", "work_stealing", "adaptive"};
        for (const auto& name : names) {
            ExperimentMetrics m = run_one(name);
            all_results.push_back(m);
            // Write individual summary
            write_summary(results_root, base_cfg.experiment_id, m);
        }
        write_comparison(results_root, base_cfg.experiment_id, all_results);
    } else {
        ExperimentMetrics m = run_one(algo);
        write_summary(results_root, base_cfg.experiment_id, m);
    }

    return 0;
}
