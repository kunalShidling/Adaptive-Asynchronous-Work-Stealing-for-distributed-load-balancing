"""Parse JSONL events from the C++ engine and maintain live state."""
import json
import threading
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Any


@dataclass
class WorkerState:
    worker_id: int
    category: str = "MEDIUM"   # VERY_FAST, FAST, MEDIUM, SLOW
    speed_factor: float = 1.0
    state: str = "idle"        # idle, running, stealing, stopped, failed
    queue_len: int = 0
    initial_assigned: int = 0
    tasks_completed: int = 0
    tasks_failed: int = 0
    tasks_redistributed_in: int = 0
    tasks_redistributed_out: int = 0
    tasks_stolen_by: int = 0
    tasks_stolen_from: int = 0
    steal_attempts: int = 0
    steal_successes: int = 0
    current_task_id: Optional[int] = None
    total_busy_ms: float = 0.0
    avg_task_duration_ms: float = 0.0
    throughput_tasks_per_sec: float = 0.0


@dataclass
class StealEvent:
    ts: float
    thief_id: int
    victim_id: int
    tasks_stolen: int
    success: bool
    batch_size: int = 0
    expected_benefit: float = 0.0
    victim_remaining: float = 0.0
    reason: str = ""


class EventProcessor:
    """Processes JSONL events from the C++ engine and maintains live state."""

    def __init__(self):
        self._lock = threading.Lock()
        self.workers: Dict[int, WorkerState] = {}
        self.events: List[Dict[str, Any]] = []
        self.steal_events: List[StealEvent] = []
        self.redistribution_events: List[Dict[str, Any]] = []
        self.initial_allocations: Dict[int, int] = {}
        self.task_records: List[Dict[str, Any]] = []
        self.activity_feed: List[str] = []
        self.experiment_started: bool = False
        self.experiment_completed: bool = False
        self.wall_ms: float = 0.0
        self.tasks_completed_total: int = 0
        self.redistribution_count: int = 0
        self.error: Optional[str] = None

    def process_line(self, line: str):
        """Process one JSONL line from the C++ engine."""
        line = line.strip()
        if not line:
            return
        try:
            event = json.loads(line)
        except json.JSONDecodeError:
            return

        with self._lock:
            self.events.append(event)
            self._handle_event(event)

    def _handle_event(self, e: dict):
        """Update state from event (called with lock held)."""
        t = e.get("type", "")
        ts = e.get("ts", 0.0)

        if t == "experiment_started":
            self.experiment_started = True
            nw = e.get("num_workers", 0)
            for i in range(nw):
                if i not in self.workers:
                    self.workers[i] = WorkerState(worker_id=i)
            algo = e.get("algorithm", "?")
            self.activity_feed.append(
                f"Experiment started: {algo} | {nw} workers | {e.get('num_tasks','?')} tasks"
            )

        elif t == "node_classified":
            wid = e.get("worker_id", 0)
            cat = e.get("category", "MEDIUM")
            speed = e.get("speed_factor", 1.0)
            tp = e.get("throughput", 0.0)
            if wid not in self.workers:
                self.workers[wid] = WorkerState(worker_id=wid)
            self.workers[wid].category = cat
            self.workers[wid].speed_factor = speed
            if tp > 0.0:
                self.workers[wid].throughput_tasks_per_sec = tp
            self.activity_feed.append(
                f"Node {wid} classified: [{cat}] speed factor: {speed:.1f}x"
            )

        elif t == "initial_allocation":
            wid = e.get("worker_id", 0)
            count = e.get("count", 0)
            cat = e.get("category", "MEDIUM")
            if wid not in self.workers:
                self.workers[wid] = WorkerState(worker_id=wid)
            self.workers[wid].initial_assigned = count
            self.workers[wid].category = cat
            self.initial_allocations[wid] = count
            self.activity_feed.append(
                f"Initial Allocation -> Node {wid} [{cat}]: {count} tasks"
            )

        elif t == "redistribution_performed":
            donor = e.get("donor_id", -1)
            recipient = e.get("recipient_id", -1)
            count = e.get("tasks_transferred", 0)
            if donor in self.workers:
                self.workers[donor].tasks_redistributed_out += count
            if recipient in self.workers:
                self.workers[recipient].tasks_redistributed_in += count
            self.redistribution_count += 1
            self.redistribution_events.append({
                "ts": ts,
                "donor": donor,
                "recipient": recipient,
                "count": count,
                "donor_rem": e.get("donor_rem", 0.0),
                "recipient_rem": e.get("recipient_rem", 0.0),
            })
            self.steal_events.append(StealEvent(
                ts=ts, thief_id=recipient, victim_id=donor,
                tasks_stolen=count, success=True,
                batch_size=count,
                victim_remaining=e.get("donor_rem", 0.0),
            ))
            self.activity_feed.append(
                f"⚡ Dynamic Redistribution: Moved {count} tasks from Node {donor} to Node {recipient}"
            )

        elif t == "node_slowdown":
            wid = e.get("worker_id", -1)
            new_speed = e.get("new_speed_factor", 0.5)
            if wid in self.workers:
                self.workers[wid].speed_factor = new_speed
            self.activity_feed.append(
                f"⚠️ Node {wid} slowdown detected! New speed factor: {new_speed:.2f}x"
            )

        elif t == "node_failed":
            wid = e.get("worker_id", -1)
            rec = e.get("recovered_tasks", 0)
            if wid in self.workers:
                self.workers[wid].state = "failed"
                self.workers[wid].tasks_failed += rec
            self.activity_feed.append(
                f"🚨 Node {wid} FAILED! {rec} orphan tasks safely reclaimed and reassigned"
            )

        elif t == "worker_started":
            wid = e.get("worker_id", 0)
            if wid not in self.workers:
                self.workers[wid] = WorkerState(worker_id=wid)
            self.workers[wid].state = "running"

        elif t == "task_started":
            wid = e.get("worker_id", 0)
            tid = e.get("task_id", 0)
            if wid in self.workers:
                self.workers[wid].state = "running"
                self.workers[wid].current_task_id = tid
                self.workers[wid].queue_len = e.get("queue_len", 0)

        elif t == "task_completed":
            wid = e.get("worker_id", 0)
            tid = e.get("task_id", 0)
            actual_ms = e.get("actual_ms", 0.0)
            est_cost = e.get("est_cost", 0.0)
            if wid in self.workers:
                self.workers[wid].tasks_completed += 1
                self.workers[wid].current_task_id = None
                self.workers[wid].queue_len = e.get("queue_len", 0)
                self.workers[wid].total_busy_ms += actual_ms
                if actual_ms > 0:
                    prev_avg = self.workers[wid].avg_task_duration_ms
                    self.workers[wid].avg_task_duration_ms = (
                        actual_ms if prev_avg == 0 else 0.25 * actual_ms + 0.75 * prev_avg
                    )
                    self.workers[wid].throughput_tasks_per_sec = (
                        1000.0 / self.workers[wid].avg_task_duration_ms
                    )
            self.tasks_completed_total += 1
            self.task_records.append({
                "task_id": tid,
                "worker_id": wid,
                "actual_ms": actual_ms,
                "est_cost": est_cost,
                "ts": ts,
            })
            self.activity_feed.append(
                f"Worker {wid} completed Task {tid} ({actual_ms:.2f}ms)"
            )

        elif t == "worker_idle":
            wid = e.get("worker_id", 0)
            if wid in self.workers:
                if self.workers[wid].state != "failed":
                    self.workers[wid].state = "idle"
                self.workers[wid].current_task_id = None
                self.workers[wid].queue_len = 0
            self.activity_feed.append(f"Worker {wid} became idle")

        elif t == "steal_attempted":
            thief = e.get("thief_id", -1)
            if thief in self.workers:
                self.workers[thief].state = "stealing"
                self.workers[thief].steal_attempts += 1

        elif t == "steal_succeeded":
            thief = e.get("thief_id", -1)
            victim = e.get("victim_id", -1)
            stolen = e.get("tasks_stolen", 0)
            if thief in self.workers:
                self.workers[thief].steal_successes += 1
                self.workers[thief].tasks_stolen_by += stolen
                self.workers[thief].tasks_redistributed_in += stolen
            if victim in self.workers:
                self.workers[victim].tasks_stolen_from += stolen
                self.workers[victim].tasks_redistributed_out += stolen
            self.redistribution_count += 1
            # Avoid duplicate if just added by redistribution_performed
            if not (self.steal_events and self.steal_events[-1].success and
                    self.steal_events[-1].thief_id == thief and
                    self.steal_events[-1].victim_id == victim and
                    abs(self.steal_events[-1].ts - ts) < 5.0):
                self.steal_events.append(StealEvent(
                    ts=ts, thief_id=thief, victim_id=victim,
                    tasks_stolen=stolen, success=True,
                    batch_size=stolen,
                    victim_remaining=e.get("victim_remaining", 0.0),
                ))
            self.activity_feed.append(
                f"Worker {thief} stole {stolen} task(s) from Worker {victim}"
            )

        elif t == "steal_failed":
            thief = e.get("thief_id", -1)
            victim = e.get("victim_id", -1)
            reason = e.get("reason", "unknown")
            self.steal_events.append(StealEvent(
                ts=ts, thief_id=thief, victim_id=victim,
                tasks_stolen=0, success=False, reason=reason,
            ))

        elif t == "victim_selected":
            thief = e.get("thief_id", -1)
            victim = e.get("victim_id", -1)
            score = e.get("victim_score", 0.0)
            self.activity_feed.append(
                f"Worker {thief} selected Worker {victim} as victim (score: {score:.2f}ms)"
            )

        elif t == "batch_stolen":
            thief = e.get("thief_id", -1)
            victim = e.get("victim_id", -1)
            batch = e.get("batch_size", 0)
            self.steal_events.append(StealEvent(
                ts=ts, thief_id=thief, victim_id=victim,
                tasks_stolen=batch, success=True,
                batch_size=batch,
                expected_benefit=e.get("overhead_est", 0.0),
            ))

        elif t == "experiment_completed":
            self.experiment_completed = True
            self.wall_ms = e.get("wall_ms", 0.0)
            done = e.get("tasks_completed", self.tasks_completed_total)
            self.tasks_completed_total = done
            self.activity_feed.append(
                f"Experiment completed in {self.wall_ms:.1f}ms | {done} tasks done"
            )

        # Cap feed at 300 entries
        if len(self.activity_feed) > 300:
            self.activity_feed = self.activity_feed[-300:]

    def get_snapshot(self) -> dict:
        """Return a thread-safe snapshot of current state."""
        with self._lock:
            # Current allocation after redistribution: initial - transferred_out + received
            current_alloc = {}
            for wid, w in self.workers.items():
                init = self.initial_allocations.get(wid, w.initial_assigned)
                current_alloc[wid] = max(0, init - w.tasks_redistributed_out + w.tasks_redistributed_in)

            # Cluster throughput
            total_tp = sum(w.throughput_tasks_per_sec for w in self.workers.values())
            pending_total = sum(w.queue_len for w in self.workers.values())

            # Preserve all successful steals so they always render in the UI timeline,
            # along with recent failed steal checks (up to 80 failures)
            succ_steals = [vars(s) for s in self.steal_events if s.success]
            fail_steals = [vars(s) for s in self.steal_events if not s.success][-80:]
            sampled_steals = sorted(succ_steals + fail_steals, key=lambda x: x["ts"])

            return {
                "workers": {k: vars(v) for k, v in self.workers.items()},
                "initial_allocations": dict(self.initial_allocations),
                "current_allocations": current_alloc,
                "events_count": len(self.events),
                "steal_events": sampled_steals,
                "redistribution_events": self.redistribution_events[-100:],
                "redistribution_count": self.redistribution_count,
                "task_records": self.task_records[-200:],
                "activity_feed": self.activity_feed[-50:],
                "experiment_started": self.experiment_started,
                "experiment_completed": self.experiment_completed,
                "wall_ms": self.wall_ms,
                "tasks_completed_total": self.tasks_completed_total,
                "pending_total": pending_total,
                "cluster_throughput": total_tp,
                "error": self.error,
            }
