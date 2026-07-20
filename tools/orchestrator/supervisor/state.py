"""Versioned, crash-safe run state for graph/duel/single runs (Epic 138).

Stored at eval/runs/<run-id>.json per the Track P schema sketch. Writes are
atomic (tmp file + replace) so a crash mid-write never corrupts the state.
"""

from __future__ import annotations

import json
import os
import time
from pathlib import Path

STATE_VERSION = 1
STORY_STATUSES = ("pending", "assigned", "coding", "testing", "review",
                  "pr_open", "merged", "failed", "escalated")


def _now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%S")


class RunState:
    def __init__(self, path: Path, data: dict):
        self.path = Path(path)
        self.data = data

    # -- lifecycle ----------------------------------------------------------

    @classmethod
    def new(cls, runs_dir: Path, mode: str,
            parallel_matrix_sha: str = "") -> "RunState":
        run_id = time.strftime("%Y%m%d-%H%M%S") + f"-{os.getpid() % 10000:04d}"
        data = {
            "version": STATE_VERSION,
            "run_id": run_id,
            "mode": mode,
            "started_at": _now(),
            "parallel_matrix_sha": parallel_matrix_sha,
            "stories": {},
            "duels": [],
            "escalations": [],
        }
        state = cls(Path(runs_dir) / f"{run_id}.json", data)
        state.save()
        return state

    @classmethod
    def load(cls, path: Path) -> "RunState":
        data = json.loads(Path(path).read_text(encoding="utf-8"))
        if data.get("version") != STATE_VERSION:
            raise ValueError(
                f"run state version {data.get('version')} != {STATE_VERSION}")
        return cls(Path(path), data)

    @classmethod
    def latest(cls, runs_dir: Path) -> "RunState | None":
        runs = sorted(Path(runs_dir).glob("*.json"))
        return cls.load(runs[-1]) if runs else None

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.data, indent=2) + "\n", encoding="utf-8")
        os.replace(tmp, self.path)

    # -- stories ------------------------------------------------------------

    def story(self, story_id: str) -> dict:
        return self.data["stories"].setdefault(story_id, {
            "status": "pending", "worker": None, "worktree": "", "branch": "",
            "attempts": 0, "pr": None, "score": None, "events": [],
        })

    def set_status(self, story_id: str, status: str, **extra) -> None:
        if status not in STORY_STATUSES:
            raise ValueError(f"bad story status {status!r}")
        entry = self.story(story_id)
        entry["status"] = status
        entry.update(extra)
        entry["events"].append({"t": _now(), "event": status, **{
            k: v for k, v in extra.items() if isinstance(v, (str, int, float))}})
        self.save()

    def escalate(self, story_id: str, reason: str) -> None:
        self.data["escalations"].append(
            {"t": _now(), "story": story_id, "reason": reason})
        self.set_status(story_id, "escalated")

    def in_flight(self) -> list[str]:
        return [sid for sid, entry in self.data["stories"].items()
                if entry["status"] in ("assigned", "coding", "testing", "review")]

    def summary(self) -> str:
        counts: dict[str, int] = {}
        for entry in self.data["stories"].values():
            counts[entry["status"]] = counts.get(entry["status"], 0) + 1
        parts = [f"run {self.data['run_id']} ({self.data['mode']})"]
        parts.extend(f"{status}={count}" for status, count in sorted(counts.items()))
        if self.data["escalations"]:
            parts.append(f"escalations={len(self.data['escalations'])}")
        return " ".join(parts)
