"""Calculate and format performance metrics."""
from typing import Optional, List
import numpy as np
import pandas as pd


def safe_div(a: float, b: float, default: float = 0.0) -> float:
    """Division protected against zero denominator."""
    return a / b if b != 0 else default


def compute_mape(estimated: List[float], actual: List[float]) -> Optional[float]:
    """
    Mean Absolute Percentage Error between estimated and actual values.
    Returns None if no valid (actual > 0.001) pairs exist.
    Formula: mean(|est - act| / act) * 100
    """
    pairs = [(e, a) for e, a in zip(estimated, actual) if a > 0.001]
    if not pairs:
        return None
    return float(np.mean([abs(e - a) / a * 100 for e, a in pairs]))


def compute_load_imbalance(busy_times: List[float]) -> float:
    """
    Normalized std dev of worker busy times.
    Formula: std_dev(busy_times) / mean(busy_times)
    Returns 0.0 if fewer than 2 workers or zero mean.
    """
    if len(busy_times) < 2:
        return 0.0
    arr = np.array(busy_times, dtype=float)
    mean = np.mean(arr)
    if mean < 1e-9:
        return 0.0
    return float(np.std(arr) / mean)


def summarize_workers(workers_data: List[dict], wall_ms: float) -> pd.DataFrame:
    """Create a summary DataFrame from a list of worker metric dicts."""
    rows = []
    for w in workers_data:
        busy = w.get("busy_ms", 0.0)
        rows.append({
            "Worker": f"W{w.get('worker_id', '?')}",
            "Completed": w.get("tasks_completed", 0),
            "Stolen From": w.get("tasks_stolen_from", 0),
            "Stolen By": w.get("tasks_stolen_by", 0),
            "Steal Attempts": w.get("steal_attempts", 0),
            "Steal Successes": w.get("steal_successes", 0),
            "Busy (ms)": round(busy, 2),
            "Utilization (%)": round(safe_div(busy, wall_ms) * 100, 1) if wall_ms > 0 else 0.0,
        })
    return pd.DataFrame(rows)


def format_ms(ms: float) -> str:
    """Human-readable duration string."""
    if ms <= 0:
        return "N/A"
    if ms >= 60_000:
        return f"{ms/60000:.1f}min"
    if ms >= 1_000:
        return f"{ms/1000:.2f}s"
    return f"{ms:.1f}ms"
