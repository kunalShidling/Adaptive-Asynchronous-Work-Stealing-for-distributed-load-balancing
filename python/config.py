"""Configuration management for the scheduler dashboard."""
import json
import os
from dataclasses import dataclass, field, asdict
from typing import Optional

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESULTS_DIR = os.path.join(PROJECT_ROOT, "results", "experiments")
REPORTS_DIR = os.path.join(PROJECT_ROOT, "results", "reports")
EXECUTABLE = os.path.join(PROJECT_ROOT, "build", "scheduler.exe")
CONFIGS_DIR = os.path.join(PROJECT_ROOT, "configs")

os.makedirs(RESULTS_DIR, exist_ok=True)
os.makedirs(REPORTS_DIR, exist_ok=True)
os.makedirs(CONFIGS_DIR, exist_ok=True)

ALGORITHMS = {
    "static": "Static Scheduling",
    "dynamic": "Centralized Dynamic Scheduling",
    "work_stealing": "Traditional Random Work-Stealing",
    "adaptive": "Cost-Aware Adaptive Work-Stealing",
    "all": "All Algorithms (Comparison)",
}

WORKLOAD_TYPES = {
    "uniform": "Uniform (similar task costs)",
    "heterogeneous": "Heterogeneous (varied task costs)",
    "imbalanced": "Initially Imbalanced",
}


@dataclass
class StealingConfig:
    ewma_alpha: float = 0.3
    min_benefit_ms: float = 0.001
    overhead_ms: float = 0.001
    max_batch_size: int = 16
    min_victim_queue: int = 1
    victim_policy: str = "max_remaining_cost"


@dataclass
class ExperimentConfig:
    experiment_id: int = 0
    algorithm: str = "adaptive"
    num_workers: int = 4
    num_tasks: int = 200
    workload_type: str = "heterogeneous"
    min_complexity: int = 500
    max_complexity: int = 10000
    random_seed: int = 42
    results_dir: str = RESULTS_DIR
    stealing: StealingConfig = field(default_factory=StealingConfig)

    # Heterogeneous Node Architecture
    heterogeneous_nodes: bool = True
    worker_speeds: list = field(default_factory=list)
    dynamic_redistribution: bool = True
    redistribution_interval_ms: float = 5.0
    imbalance_threshold: float = 0.15

    # Simulation hooks (slowdown & failure)
    simulate_slowdown_worker: int = -1
    simulate_slowdown_factor: float = 0.5
    simulate_slowdown_after_tasks: int = 10
    simulate_fail_worker: int = -1
    simulate_fail_after_tasks: int = 10

    def to_json(self) -> str:
        d = asdict(self)
        return json.dumps(d, indent=2)

    def validate(self) -> list:
        """Return list of validation error strings, empty if valid."""
        errors = []
        if self.num_workers < 1 or self.num_workers > 64:
            errors.append("num_workers must be 1–64")
        if self.num_tasks < 1 or self.num_tasks > 100000:
            errors.append("num_tasks must be 1–100000")
        if self.min_complexity < 100:
            errors.append("min_complexity must be >= 100")
        if self.max_complexity <= self.min_complexity:
            errors.append("max_complexity must be > min_complexity")
        if self.algorithm not in ALGORITHMS:
            errors.append(f"Unknown algorithm: {self.algorithm}")
        if self.workload_type not in WORKLOAD_TYPES:
            errors.append(f"Unknown workload type: {self.workload_type}")
        if not (0.0 < self.stealing.ewma_alpha <= 1.0):
            errors.append("ewma_alpha must be in (0, 1]")
        if self.stealing.max_batch_size < 1:
            errors.append("max_batch_size must be >= 1")
        return errors


def next_experiment_id() -> int:
    """Return next unused experiment ID based on results directory."""
    if not os.path.exists(RESULTS_DIR):
        return 1
    existing = [d for d in os.listdir(RESULTS_DIR) if os.path.isdir(os.path.join(RESULTS_DIR, d))]
    ids = []
    for name in existing:
        try:
            ids.append(int(name))
        except ValueError:
            pass
    return max(ids, default=0) + 1
