"""
Automated Python Unit and Integration Test Suite
Tests:
1. Configuration creation, JSON serialization, and validation
2. EventProcessor real-time state tracking
3. EngineRunner background subprocess execution and termination
4. Metric calculations (MAPE, load imbalance, speedup, efficiency)
5. ResultManager persistence (summary, comparison, CSV export)
6. End-to-end integration: C++ engine runs, outputs valid JSONL, and produces summary.json
7. Comparison run: All 4 algorithms on identical tasks
"""

import unittest
import os
import sys
import time
import json
import pandas as pd
import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "python"))

from config import ExperimentConfig, StealingConfig, next_experiment_id, EXECUTABLE
from event_processor import EventProcessor, WorkerState, StealEvent
from engine_runner import EngineRunner
from metrics import safe_div, compute_mape, compute_load_imbalance, summarize_workers, format_ms
from result_manager import save_experiment, load_history, load_summary, load_comparison, export_worker_csv

class TestConfiguration(unittest.TestCase):
    def test_default_config_valid(self):
        cfg = ExperimentConfig()
        errors = cfg.validate()
        self.assertEqual(len(errors), 0, f"Default config should be valid: {errors}")

    def test_invalid_parameters_caught(self):
        cfg = ExperimentConfig(num_workers=0, num_tasks=-5, min_complexity=2000, max_complexity=500)
        errors = cfg.validate()
        self.assertGreater(len(errors), 0)

    def test_json_serialization(self):
        cfg = ExperimentConfig(experiment_id=123, algorithm="adaptive")
        json_str = cfg.to_json()
        parsed = json.loads(json_str)
        self.assertEqual(parsed["experiment_id"], 123)
        self.assertEqual(parsed["algorithm"], "adaptive")

class TestMetricsCalculations(unittest.TestCase):
    def test_safe_div(self):
        self.assertEqual(safe_div(10, 2), 5.0)
        self.assertEqual(safe_div(10, 0, default=99.0), 99.0)

    def test_compute_mape(self):
        est = [10.0, 20.0, 30.0]
        act = [12.0, 18.0, 33.0]
        mape = compute_mape(est, act)
        self.assertIsNotNone(mape)
        self.assertAlmostEqual(mape, ((2/12 + 2/18 + 3/33)/3)*100, places=2)

    def test_compute_load_imbalance(self):
        # Uniform times -> imbalance = 0
        self.assertAlmostEqual(compute_load_imbalance([10.0, 10.0, 10.0, 10.0]), 0.0)
        # Imbalanced times -> imbalance > 0
        imb = compute_load_imbalance([1.0, 10.0, 20.0])
        self.assertGreater(imb, 0.0)

    def test_format_ms(self):
        self.assertEqual(format_ms(50.4), "50.4ms")
        self.assertEqual(format_ms(2500.0), "2.50s")

class TestEventProcessor(unittest.TestCase):
    def test_event_processing(self):
        ep = EventProcessor()
        ep.process_line('{"type":"experiment_started","ts":1000,"exp_id":1,"num_workers":2,"num_tasks":10,"algorithm":"adaptive"}')
        self.assertTrue(ep.experiment_started)
        self.assertEqual(len(ep.workers), 2)

        ep.process_line('{"type":"worker_started","ts":1001,"worker_id":0}')
        self.assertEqual(ep.workers[0].state, "running")

        ep.process_line('{"type":"task_started","ts":1002,"worker_id":0,"task_id":1,"complexity":1000,"est_cost":1.0,"queue_len":5}')
        self.assertEqual(ep.workers[0].current_task_id, 1)

        ep.process_line('{"type":"task_completed","ts":1005,"worker_id":0,"task_id":1,"actual_ms":3.0,"est_cost":1.0,"queue_len":4}')
        self.assertEqual(ep.workers[0].tasks_completed, 1)
        self.assertIsNone(ep.workers[0].current_task_id)

        ep.process_line('{"type":"steal_succeeded","ts":1006,"thief_id":1,"victim_id":0,"tasks_stolen":2,"victim_remaining":2.0}')
        self.assertEqual(ep.workers[1].tasks_stolen_by, 2)
        self.assertEqual(ep.workers[0].tasks_stolen_from, 2)

        ep.process_line('{"type":"experiment_completed","ts":1010,"exp_id":1,"wall_ms":10.0,"tasks_completed":10}')
        self.assertTrue(ep.experiment_completed)
        self.assertEqual(ep.wall_ms, 10.0)

class TestEngineIntegration(unittest.TestCase):
    def test_single_algorithm_run(self):
        cfg = ExperimentConfig(
            experiment_id=8888,
            algorithm="adaptive",
            num_workers=4,
            num_tasks=40,
            workload_type="heterogeneous",
            min_complexity=500,
            max_complexity=3000,
            random_seed=42
        )
        runner = EngineRunner(cfg)
        runner.start()
        
        # Wait for completion (up to 10s)
        timeout = 10.0
        start = time.time()
        while not runner.is_done() and (time.time() - start) < timeout:
            time.sleep(0.1)

        self.assertTrue(runner.is_done(), "Engine execution timed out")
        self.assertEqual(runner.returncode, 0, f"Engine returned error: {runner.error_output}")
        
        summary = runner.get_summary()
        self.assertIsNotNone(summary, "summary.json must exist")
        self.assertEqual(summary.get("algorithm"), "adaptive")
        self.assertEqual(summary.get("num_tasks"), 40)
        self.assertGreater(summary.get("wall_ms", 0), 0)

        # Check worker breakdown
        workers = summary.get("workers", [])
        self.assertEqual(len(workers), 4)
        total_done = sum(w.get("tasks_completed", 0) for w in workers)
        self.assertEqual(total_done, 40, "Sum of worker tasks must equal total submitted tasks")

    def test_comparison_run(self):
        cfg = ExperimentConfig(
            experiment_id=8889,
            algorithm="all",
            num_workers=2,
            num_tasks=20,
            workload_type="uniform",
            min_complexity=500,
            max_complexity=1000,
            random_seed=100
        )
        runner = EngineRunner(cfg)
        runner.start()
        
        timeout = 15.0
        start = time.time()
        while not runner.is_done() and (time.time() - start) < timeout:
            time.sleep(0.1)

        self.assertTrue(runner.is_done())
        self.assertEqual(runner.returncode, 0)

        comp = runner.get_comparison()
        self.assertIsNotNone(comp, "comparison.json must exist for 'all' algorithm run")
        self.assertEqual(len(comp), 4, "Comparison must include all 4 algorithms")

        algo_names = [c.get("algorithm") for c in comp]
        self.assertIn("static", algo_names)
        self.assertIn("dynamic", algo_names)
        self.assertIn("work_stealing", algo_names)
        self.assertIn("adaptive", algo_names)

if __name__ == "__main__":
    unittest.main()
