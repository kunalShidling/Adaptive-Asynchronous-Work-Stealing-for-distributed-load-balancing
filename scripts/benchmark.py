"""
Benchmark runner: Automates performance comparisons across all 4 schedulers.
Runs identical task sets across Static, Dynamic, Work-Stealing, and Adaptive algorithms.
Calculates execution time, speedup, efficiency, worker utilization, imbalance, and MAPE.
Saves results into CSV and JSON reports in results/reports/
"""

import os
import sys
import json
import time
import subprocess
import pandas as pd
import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "python"))

from config import EXECUTABLE, RESULTS_DIR, REPORTS_DIR

def run_experiment(config: dict) -> dict:
    exp_id = config.get("experiment_id", int(time.time() * 1000) % 100000)
    exp_dir = os.path.join(RESULTS_DIR, str(exp_id))
    os.makedirs(exp_dir, exist_ok=True)
    config["results_dir"] = RESULTS_DIR

    cfg_str = json.dumps(config)
    
    proc = subprocess.Popen(
        [EXECUTABLE],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace"
    )
    stdout, stderr = proc.communicate(input=cfg_str)
    
    if proc.returncode != 0:
        raise RuntimeError(f"Engine failed ({proc.returncode}): {stderr}")
    
    # Read summary or comparison
    comp_file = os.path.join(exp_dir, "comparison.json")
    sum_file = os.path.join(exp_dir, "summary.json")
    
    if os.path.exists(comp_file):
        with open(comp_file, "r", encoding="utf-8") as f:
            return json.load(f)
    elif os.path.exists(sum_file):
        with open(sum_file, "r", encoding="utf-8") as f:
            return [json.load(f)]
    else:
        raise FileNotFoundError(f"Neither comparison.json nor summary.json found in {exp_dir}")

def run_suite():
    print("=" * 70)
    print("Starting Automated Benchmark Suite")
    print(f"Engine: {EXECUTABLE}")
    print("=" * 70)
    
    if not os.path.exists(EXECUTABLE):
        print(f"Error: Executable not found at {EXECUTABLE}")
        sys.exit(1)

    workloads = ["uniform", "heterogeneous", "imbalanced"]
    worker_counts = [2, 4, 8]
    task_count = 300
    
    records = []
    
    for wl in workloads:
        for workers in worker_counts:
            print(f"\n--> Running benchmark: Workload={wl}, Workers={workers}, Tasks={task_count}")
            cfg = {
                "experiment_id": int(time.time() * 100) % 100000,
                "algorithm": "all",
                "num_workers": workers,
                "num_tasks": task_count,
                "workload_type": wl,
                "min_complexity": 500,
                "max_complexity": 10000,
                "random_seed": 42,
                "stealing": {
                    "ewma_alpha": 0.3,
                    "min_benefit_ms": 0.5,
                    "overhead_ms": 0.1,
                    "max_batch_size": 8,
                    "min_victim_queue": 2,
                    "victim_policy": "max_remaining_cost"
                }
            }
            
            try:
                results = run_experiment(cfg)
                for res in results:
                    records.append({
                        "workload": wl,
                        "workers": workers,
                        "tasks": task_count,
                        "algorithm": res.get("algorithm"),
                        "wall_ms": round(res.get("wall_ms", 0), 2),
                        "sequential_ms": round(res.get("sequential_ms", 0), 2),
                        "speedup": round(res.get("speedup", 0), 3),
                        "efficiency": round(res.get("efficiency", 0), 3),
                        "utilization": round(res.get("avg_utilization", 0) * 100, 1),
                        "imbalance": round(res.get("load_imbalance", 0), 4),
                        "steal_attempts": res.get("total_steals_attempted", 0),
                        "steal_successes": res.get("total_steals_succeeded", 0),
                        "steal_rate": round(res.get("steal_success_rate", 0) * 100, 1),
                        "tasks_stolen": res.get("total_tasks_stolen", 0),
                        "sched_overhead_ms": round(res.get("scheduling_overhead_ms", 0), 3),
                        "mape": round(res.get("cost_estimation_mape", 0), 2)
                    })
                    print(f"    [{res.get('algorithm'):<14}] Wall: {res.get('wall_ms', 0):6.1f}ms | Speedup: {res.get('speedup', 0):4.2f}x | Efficiency: {res.get('efficiency', 0)*100:4.1f}% | Imbalance: {res.get('load_imbalance', 0):.3f}")
            except Exception as e:
                print(f"    Failed: {e}")

    df = pd.DataFrame(records)
    out_csv = os.path.join(REPORTS_DIR, "benchmark_summary.csv")
    df.to_csv(out_csv, index=False)
    print("\n" + "=" * 70)
    print(f"Benchmark completed! Report saved to {out_csv}")
    print("=" * 70)
    print(df.to_string())

if __name__ == "__main__":
    run_suite()
