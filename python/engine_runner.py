"""Launch and manage the C++ scheduler engine subprocess."""
import subprocess
import threading
import json
import os
import time
from typing import Optional, Callable

import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from config import ExperimentConfig, EXECUTABLE, RESULTS_DIR
from event_processor import EventProcessor


class EngineRunner:
    """Manages a single C++ engine subprocess run."""

    def __init__(self, config: ExperimentConfig, on_event: Optional[Callable] = None):
        self.config = config
        self.on_event = on_event
        self.processor = EventProcessor()
        self._process: Optional[subprocess.Popen] = None
        self._thread: Optional[threading.Thread] = None
        self._done = threading.Event()
        self.start_time: float = 0.0
        self.end_time: float = 0.0
        self.returncode: Optional[int] = None
        self.error_output: str = ""
        self._exp_dir = os.path.join(RESULTS_DIR, str(config.experiment_id))
        os.makedirs(self._exp_dir, exist_ok=True)

    def start(self):
        """Launch the C++ engine in a background thread."""
        if not os.path.exists(EXECUTABLE):
            self.processor.error = (
                f"Executable not found: {EXECUTABLE}\n"
                "Build the C++ engine first (see scripts/build.bat)."
            )
            self._done.set()
            return

        # Inject the absolute results dir into the config
        cfg_dict = json.loads(self.config.to_json())
        cfg_dict["results_dir"] = RESULTS_DIR
        config_json = json.dumps(cfg_dict)

        self._thread = threading.Thread(
            target=self._run, args=(config_json,), daemon=True
        )
        self._thread.start()

    def _run(self, config_json: str):
        self.start_time = time.time()
        env = os.environ.copy()
        mingw_bin = "d:\\mingw64\\bin"
        if os.path.exists(mingw_bin) and mingw_bin not in env.get("PATH", ""):
            env["PATH"] = mingw_bin + os.pathsep + env.get("PATH", "")

        try:
            self._process = subprocess.Popen(
                [EXECUTABLE],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env,
                text=True,
                encoding="utf-8",
                errors="replace",
            )
            # Write JSON config to stdin and close to signal EOF
            self._process.stdin.write(config_json)
            self._process.stdin.close()

            # Stream stdout line by line and process events
            for line in self._process.stdout:
                self.processor.process_line(line)
                if self.on_event:
                    try:
                        self.on_event(line)
                    except Exception:
                        pass

            self._process.wait()
            self.returncode = self._process.returncode
            self.error_output = self._process.stderr.read()
            self.end_time = time.time()

            if self.returncode != 0:
                self.processor.error = (
                    f"C++ engine exited with code {self.returncode}.\n"
                    f"Stderr: {self.error_output[:800]}"
                )

        except FileNotFoundError:
            self.processor.error = f"Executable not found: {EXECUTABLE}"
        except Exception as exc:
            self.processor.error = f"Runner error: {exc}"
        finally:
            self._done.set()

    def is_running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    def is_done(self) -> bool:
        return self._done.is_set()

    def stop(self):
        """Attempt to terminate the subprocess gracefully."""
        if self._process and self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()

    def get_summary(self) -> Optional[dict]:
        """Read the final summary JSON written by the C++ engine."""
        summary_path = os.path.join(self._exp_dir, "summary.json")
        if os.path.exists(summary_path):
            try:
                with open(summary_path, encoding="utf-8") as f:
                    return json.load(f)
            except Exception:
                return None
        return None

    def get_comparison(self) -> Optional[dict]:
        """Read comparison JSON (populated when algorithm == 'all')."""
        path = os.path.join(self._exp_dir, "comparison.json")
        if os.path.exists(path):
            try:
                with open(path, encoding="utf-8") as f:
                    return json.load(f)
            except Exception:
                return None
        return None
