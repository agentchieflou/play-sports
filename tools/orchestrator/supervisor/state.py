"""Versioned run state for supervisor graph runs (Epic 138).

One JSON file per run at eval/runs/<run-id>.json, the contract sketched in
roadmap/agent-orchestration.md:

    {version, run_id, mode: graph|duel|single, started_at, parallel_matrix_sha,
     stories: {"<epic>.<story>": {status, worker, worktree, branch, attempts,
                                  pr, score, events[]}},
     duels[], escalations[]}

plus `halted` (the hard-stop reason, empty while the run may continue) and
`updated_at`. Every change is written straight away and atomically (temp file,
fsync, os.replace), so a crash leaves the last complete state on disk. Worker
threads share one RunState; its lock serializes changes and writes.
"""

from __future__ import annotations

import json
import os
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path

STATE_VERSION = 1
STATUSES = ("pending", "assigned", "coding", "testing", "review", "pr_open",
            "merged", "failed", "escalated")
IN_FLIGHT = ("assigned", "coding", "testing")
# A story in one of these won't be dispatched again by this run.
SETTLED = ("review", "pr_open", "merged", "failed", "escalated")
MODES = ("graph", "duel", "single")


class RunStateError(RuntimeError):
    pass


def default_run_dir(repo_root: Path) -> Path:
    return Path(repo_root) / "eval" / "runs"


def new_run_id() -> str:
    return time.strftime("%Y%m%d-%H%M%S", time.gmtime())


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def atomic_write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    with open(temp, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, sort_keys=True)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temp, path)


@dataclass
class RunState:
    path: Path
    data: dict = field(default_factory=dict)
    _lock: threading.RLock = field(default_factory=threading.RLock, repr=False)

    # -- lifecycle -----------------------------------------------------------------------

    @classmethod
    def create(cls, run_dir: Path, mode: str = "graph", matrix_sha: str = "",
               run_id: str | None = None) -> "RunState":
        if mode not in MODES:
            raise RunStateError(f"unknown mode {mode!r}")
        run_id = run_id or new_run_id()
        path = Path(run_dir) / f"{run_id}.json"
        if path.exists():
            raise RunStateError(f"run {run_id} already exists at {path}")
        state = cls(path=path, data={
            "version": STATE_VERSION,
            "run_id": run_id,
            "mode": mode,
            "started_at": _now(),
            "updated_at": _now(),
            "parallel_matrix_sha": matrix_sha,
            "halted": "",
            "stories": {},
            "duels": [],
            "escalations": [],
        })
        state.save()
        return state

    @classmethod
    def load(cls, path: Path) -> "RunState":
        try:
            data = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise RunStateError(f"can't read run state {path}: {error}") from error
        if data.get("version") != STATE_VERSION:
            raise RunStateError(f"{path}: state version {data.get('version')!r}, "
                                f"this code reads {STATE_VERSION}")
        for key in ("run_id", "mode", "stories", "escalations"):
            if key not in data:
                raise RunStateError(f"{path}: missing {key!r}")
        data.setdefault("halted", "")
        data.setdefault("duels", [])
        return cls(path=Path(path), data=data)

    @classmethod
    def load_run(cls, run_dir: Path, run_id: str | None = None) -> "RunState":
        """The named run, or the most recent one in run_dir."""
        run_dir = Path(run_dir)
        if run_id:
            return cls.load(run_dir / f"{run_id}.json")
        runs = sorted(run_dir.glob("*.json")) if run_dir.is_dir() else []
        if not runs:
            raise RunStateError(f"no runs in {run_dir}")
        return cls.load(runs[-1])

    def save(self) -> None:
        with self._lock:
            self.data["updated_at"] = _now()
            atomic_write_json(self.path, self.data)

    # -- reads ---------------------------------------------------------------------------

    @property
    def run_id(self) -> str:
        return self.data["run_id"]

    @property
    def halted(self) -> str:
        return self.data.get("halted", "")

    @property
    def stories(self) -> dict:
        return self.data["stories"]

    @property
    def escalations(self) -> list:
        return self.data["escalations"]

    def status_of(self, story_id: str) -> str:
        return self.stories.get(story_id, {}).get("status", "")

    def in_flight(self) -> list[str]:
        with self._lock:
            return [sid for sid, s in self.stories.items() if s["status"] in IN_FLIGHT]

    # -- changes -------------------------------------------------------------------------

    def set_status(self, story_id: str, status: str, note: str = "", **fields) -> None:
        if status not in STATUSES:
            raise RunStateError(f"unknown status {status!r}")
        with self._lock:
            entry = self.stories.setdefault(story_id, {
                "status": "pending", "worker": "", "worktree": "", "branch": "",
                "attempts": 0, "pr": "", "score": None, "events": [],
            })
            entry["status"] = status
            entry.update(fields)
            event = {"t": _now(), "status": status}
            if note:
                event["note"] = note[:2000]
            entry["events"].append(event)
            self.save()

    def bump_attempts(self, story_id: str) -> int:
        with self._lock:
            entry = self.stories[story_id]
            entry["attempts"] = entry.get("attempts", 0) + 1
            self.save()
            return entry["attempts"]

    def escalate(self, story_id: str, stage: str, reason: str,
                 status: str = "escalated") -> None:
        """Records an escalation for a person and settles the story (status
        "escalated", or "failed" for a hard stop)."""
        with self._lock:
            self.escalations.append({"t": _now(), "story": story_id, "stage": stage,
                                     "reason": reason[:2000]})
            self.set_status(story_id, status, note=f"{stage}: {reason}")

    def halt(self, reason: str) -> None:
        with self._lock:
            if not self.data.get("halted"):
                self.data["halted"] = reason
            self.save()

    def prepare_resume(self) -> list[str]:
        """Stories caught mid-flight by a crash go back to pending (their
        worktrees are gone or stale); a halt is cleared so the run continues.
        Returns the story IDs reset."""
        with self._lock:
            reset = []
            for story_id, entry in self.stories.items():
                if entry["status"] in IN_FLIGHT:
                    reset.append(story_id)
                    entry["status"] = "pending"
                    entry["events"].append({"t": _now(), "status": "pending",
                                            "note": "reset on resume"})
            self.data["halted"] = ""
            self.save()
            return reset

    # -- report --------------------------------------------------------------------------

    def summary_lines(self) -> list[str]:
        with self._lock:
            lines = [f"run {self.run_id} ({self.data['mode']}), started {self.data['started_at']}, "
                     f"updated {self.data.get('updated_at', '')}"]
            if self.halted:
                lines.append(f"HALTED: {self.halted}")
            for story_id, entry in sorted(self.stories.items()):
                extras = [f"attempts {entry.get('attempts', 0)}"]
                if entry.get("worker"):
                    extras.append(f"worker {entry['worker']}")
                if entry.get("branch"):
                    extras.append(f"branch {entry['branch']}")
                if entry.get("pr"):
                    extras.append(f"PR {entry['pr']}")
                lines.append(f"  {story_id}: {entry['status']} ({', '.join(extras)})")
            if self.escalations:
                lines.append("escalations:")
                for item in self.escalations:
                    lines.append(f"  {item['story']} [{item['stage']}]: {item['reason'][:300]}")
            return lines
