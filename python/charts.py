"""Plotly chart factories for the Adaptive Scheduler dashboard."""
from typing import List, Dict, Any, Optional
import plotly.graph_objects as go
import plotly.express as px
import pandas as pd
import numpy as np

COLORS = px.colors.qualitative.Set2
ALGO_COLORS = {
    "static": "#FF6B6B",
    "dynamic": "#FFD700",
    "work_stealing": "#4ECDC4",
    "adaptive": "#96CEB4",
}
DARK_TEMPLATE = "plotly_dark"


def _empty_fig(title: str) -> go.Figure:
    fig = go.Figure()
    fig.add_annotation(
        text="No data available yet",
        xref="paper", yref="paper", x=0.5, y=0.5,
        showarrow=False, font=dict(size=16, color="#666"),
    )
    fig.update_layout(template=DARK_TEMPLATE, title=title,
                      paper_bgcolor="#0e1117", plot_bgcolor="#0e1117")
    return fig


def worker_utilization_bar(workers: List[dict], wall_ms: float) -> go.Figure:
    """Bar chart of worker utilization %."""
    if not workers or wall_ms <= 0:
        return _empty_fig("Worker Utilization")
    names = [f"W{w.get('worker_id', i)}" for i, w in enumerate(workers)]
    utils = [min(100.0, w.get("busy_ms", 0.0) / wall_ms * 100) for w in workers]
    fig = go.Figure(go.Bar(
        x=names, y=utils,
        marker_color=COLORS[:len(names)],
        text=[f"{u:.1f}%" for u in utils],
        textposition="auto",
    ))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Worker Utilization (%)",
        xaxis_title="Worker", yaxis_title="Utilization (%)",
        yaxis_range=[0, 110],
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def queue_length_bar(snapshot: dict) -> go.Figure:
    """Current queue lengths per worker from a live snapshot."""
    workers = snapshot.get("workers", {})
    if not workers:
        return _empty_fig("Queue Lengths")
    sorted_ids = sorted(workers.keys())
    names = [f"W{wid}" for wid in sorted_ids]
    lengths = [workers[wid].get("queue_len", 0) for wid in sorted_ids]
    fig = go.Figure(go.Bar(x=names, y=lengths, marker_color="#4ECDC4"))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Current Queue Lengths",
        xaxis_title="Worker", yaxis_title="Tasks in Queue",
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def task_completion_timeline(task_records: List[dict]) -> go.Figure:
    """Cumulative task completions over time."""
    if not task_records:
        return _empty_fig("Task Completion Timeline")
    df = pd.DataFrame(task_records).sort_values("ts")
    df["cumulative"] = range(1, len(df) + 1)
    fig = go.Figure(go.Scatter(
        x=df["ts"], y=df["cumulative"],
        mode="lines", line=dict(color="#FF6B6B", width=2),
        fill="tozeroy", fillcolor="rgba(255,107,107,0.1)",
    ))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Cumulative Task Completions",
        xaxis_title="Time (ms epoch)", yaxis_title="Tasks Completed",
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def tasks_per_worker_pie(workers: List[dict]) -> go.Figure:
    """Donut chart of task distribution across workers."""
    labels = [f"W{w.get('worker_id', i)}" for i, w in enumerate(workers)]
    values = [w.get("tasks_completed", 0) for w in workers]
    if not any(values):
        return _empty_fig("Task Distribution")
    fig = go.Figure(go.Pie(labels=labels, values=values, hole=0.45,
                            marker_colors=COLORS[:len(labels)]))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Task Distribution by Worker",
        paper_bgcolor="#0e1117",
    )
    return fig


def algorithm_comparison_bar(comparison_list: list, metric: str, title: str, y_label: str = "") -> go.Figure:
    """Bar chart comparing algorithms on a single metric from comparison JSON list."""
    algos = []
    values = []
    colors = []
    for entry in comparison_list:
        if not isinstance(entry, dict):
            continue
        algo = entry.get("algorithm", "?")
        val = entry.get(metric)
        if val is not None:
            algos.append(algo.replace("_", " ").title())
            values.append(float(val))
            colors.append(ALGO_COLORS.get(algo, "#888"))
    if not algos:
        return _empty_fig(title)
    fig = go.Figure(go.Bar(
        x=algos, y=values,
        marker_color=colors,
        text=[f"{v:.3g}" for v in values],
        textposition="auto",
    ))
    fig.update_layout(
        template=DARK_TEMPLATE, title=title,
        xaxis_title="Algorithm", yaxis_title=y_label or title,
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def steal_timeline(steal_events: List[dict]) -> go.Figure:
    """Timeline of stealing events (successes vs failures)."""
    if not steal_events:
        return _empty_fig("Stealing Events Timeline")
    successes = [e for e in steal_events if e.get("success")]
    failures = [e for e in steal_events if not e.get("success")]
    fig = go.Figure()
    if successes:
        fig.add_trace(go.Scatter(
            x=[e.get("ts", 0) for e in successes],
            y=[e.get("tasks_stolen", 1) for e in successes],
            mode="markers", name="Successful Steal",
            marker=dict(color="#96CEB4", size=10, symbol="circle"),
            hovertemplate="<b>Steal Success</b><br>Thief: W%{customdata[0]}<br>Victim: W%{customdata[1]}<br>Tasks: %{y}<extra></extra>",
            customdata=[[e.get("thief_id", "?"), e.get("victim_id", "?")] for e in successes],
        ))
    if failures:
        fig.add_trace(go.Scatter(
            x=[e.get("ts", 0) for e in failures],
            y=[0] * len(failures),
            mode="markers", name="Failed Steal",
            marker=dict(color="#FF6B6B", size=8, symbol="x"),
            hovertemplate="<b>Steal Failed</b><br>Reason: %{customdata}<extra></extra>",
            customdata=[e.get("reason", "?") for e in failures],
        ))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Stealing Events Timeline",
        xaxis_title="Time (ms epoch)", yaxis_title="Tasks Stolen",
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def cost_estimation_scatter(task_records: List[dict]) -> go.Figure:
    """Scatter plot: estimated vs actual task execution cost."""
    if not task_records:
        return _empty_fig("Cost Estimation Accuracy")
    df = pd.DataFrame(task_records)
    df = df[(df["actual_ms"] > 0) & (df["est_cost"] >= 0)]
    if df.empty:
        return _empty_fig("Cost Estimation Accuracy")
    max_val = max(df["actual_ms"].max(), df["est_cost"].max()) * 1.1
    fig = go.Figure()
    fig.add_trace(go.Scatter(
        x=df["actual_ms"], y=df["est_cost"],
        mode="markers", name="Tasks",
        marker=dict(color="#45B7D1", opacity=0.6, size=5),
        hovertemplate="Actual: %{x:.2f}ms<br>Estimated: %{y:.2f}ms<extra></extra>",
    ))
    # Perfect prediction diagonal
    fig.add_trace(go.Scatter(
        x=[0, max_val], y=[0, max_val],
        mode="lines", name="Perfect Prediction",
        line=dict(color="#FFD700", dash="dash", width=1.5),
    ))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Cost Estimation: Estimated vs Actual (ms)",
        xaxis_title="Actual Duration (ms)", yaxis_title="Estimated Cost (ms)",
        paper_bgcolor="#0e1117", plot_bgcolor="#161b2e",
    )
    return fig


def worker_activity_heatmap(task_records: List[dict], num_workers: int) -> go.Figure:
    """Heatmap of task completions per worker over time buckets."""
    if not task_records or num_workers < 1:
        return _empty_fig("Worker Activity Heatmap")
    df = pd.DataFrame(task_records)
    df = df.sort_values("ts")
    if df.empty or "ts" not in df.columns:
        return _empty_fig("Worker Activity Heatmap")
    ts_min, ts_max = df["ts"].min(), df["ts"].max()
    if ts_max <= ts_min:
        return _empty_fig("Worker Activity Heatmap")
    n_buckets = min(20, len(df))
    df["bucket"] = pd.cut(df["ts"], bins=n_buckets, labels=False)
    pivot = df.groupby(["worker_id", "bucket"]).size().unstack(fill_value=0)
    fig = go.Figure(go.Heatmap(
        z=pivot.values,
        x=[f"T{i}" for i in range(pivot.shape[1])],
        y=[f"W{wid}" for wid in pivot.index],
        colorscale="Teal",
        hovertemplate="Worker: %{y}<br>Time Bucket: %{x}<br>Tasks: %{z}<extra></extra>",
    ))
    fig.update_layout(
        template=DARK_TEMPLATE, title="Worker Activity Over Time",
        xaxis_title="Time Bucket", yaxis_title="Worker",
        paper_bgcolor="#0e1117",
    )
    return fig
