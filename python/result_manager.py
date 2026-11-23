"""Persist and load experiment results."""
import json
import os
import csv
from datetime import datetime
from typing import Optional, List, Dict, Any

import pandas as pd

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from config import RESULTS_DIR, REPORTS_DIR

HISTORY_FILE = os.path.join(REPORTS_DIR, "experiment_history.csv")

HISTORY_COLUMNS = [
    "experiment_id", "timestamp", "algorithm", "workload_type",
    "num_workers", "num_tasks", "random_seed",
    "wall_ms", "sequential_ms", "speedup", "efficiency",
    "avg_utilization", "load_imbalance",
    "total_steals_attempted", "total_steals_succeeded", "steal_success_rate",
    "total_tasks_stolen", "scheduling_overhead_ms", "cost_estimation_mape",
]


def save_experiment(summary: dict):
    """Append a completed experiment summary to the history CSV."""
    os.makedirs(REPORTS_DIR, exist_ok=True)
    row = {col: summary.get(col, "") for col in HISTORY_COLUMNS}
    row["timestamp"] = datetime.now().isoformat()

    file_exists = os.path.exists(HISTORY_FILE)
    with open(HISTORY_FILE, "a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=HISTORY_COLUMNS)
        if not file_exists:
            writer.writeheader()
        writer.writerow(row)


def load_history() -> pd.DataFrame:
    """Load experiment history as a DataFrame."""
    if not os.path.exists(HISTORY_FILE):
        return pd.DataFrame(columns=HISTORY_COLUMNS)
    try:
        return pd.read_csv(HISTORY_FILE)
    except Exception:
        return pd.DataFrame(columns=HISTORY_COLUMNS)


def load_summary(experiment_id: int) -> Optional[dict]:
    """Load a single experiment's summary.json."""
    path = os.path.join(RESULTS_DIR, str(experiment_id), "summary.json")
    if os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None
    return None


def load_comparison(experiment_id: int) -> Optional[list]:
    """Load comparison.json (produced for 'all' algorithm runs)."""
    path = os.path.join(RESULTS_DIR, str(experiment_id), "comparison.json")
    if os.path.exists(path):
        try:
            with open(path, encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None
    return None


def list_experiments() -> List[int]:
    """Return sorted list of experiment IDs that have a results directory."""
    if not os.path.exists(RESULTS_DIR):
        return []
    dirs = []
    for d in os.listdir(RESULTS_DIR):
        try:
            dirs.append(int(d))
        except ValueError:
            pass
    return sorted(dirs)


def export_worker_csv(experiment_id: int) -> Optional[str]:
    """Export per-worker metrics for an experiment as a CSV file."""
    summary = load_summary(experiment_id)
    if not summary:
        return None
    workers = summary.get("workers", [])
    if not workers:
        return None
    path = os.path.join(REPORTS_DIR, f"exp_{experiment_id}_workers.csv")
    os.makedirs(REPORTS_DIR, exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=workers[0].keys())
        writer.writeheader()
        writer.writerows(workers)
    return path
