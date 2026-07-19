"""Importable scoring core shared by score_agent_run.py (Epic 120 CLI) and the
orchestrator's duel mode (Track P, Epic 137).

Two paths:
  score_pr_facts()   - the original PR-based composite (0-100, CI worth 40)
  score_local_diff() - pre-PR scoring for a local branch/worktree: the same
                       rubric minus CI (0-60), computable without `gh`
"""

from __future__ import annotations

import fnmatch
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TASK_DIR = REPO / "eval" / "tasks"

LOCAL_MAX = 60  # composite ceiling when CI (40 pts) is not yet available


def load_task(task_id: str, task_dir: Path | None = None) -> dict:
    """Parse an eval/tasks/<id>.md frontmatter block (allowed_paths,
    requires_tests). Exits on unknown task, matching the original CLI."""
    path = (task_dir or TASK_DIR) / f"{task_id}.md"
    if not path.is_file():
        sys.exit(f"unknown task '{task_id}' (no {path})")
    allowed, requires_tests, in_fm, key = [], False, False, None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() == "---":
            if in_fm:
                break
            in_fm = True
            continue
        if not in_fm:
            continue
        if line.startswith("- ") and key == "allowed_paths":
            allowed.append(line[2:].strip())
        elif ":" in line:
            key, _, value = line.partition(":")
            key = key.strip()
            if key == "requires_tests":
                requires_tests = value.strip().lower() == "true"
    return {"allowed_paths": allowed, "requires_tests": requires_tests}


def run_tool(script: str, repo_root: Path | None = None) -> bool:
    root = repo_root or REPO
    proc = subprocess.run([sys.executable, str(root / "tools" / script)],
                          capture_output=True, text=True, cwd=root)
    return proc.returncode == 0


def _rubric(files: list[str], commits: int, ci_green: bool | None,
            task: dict | None, repo_root: Path | None) -> tuple[dict, int]:
    in_scope = True
    if task and task["allowed_paths"]:
        in_scope = all(
            any(fnmatch.fnmatch(f, pat) for pat in task["allowed_paths"])
            for f in files)
    tests_added = any("/Tests/" in f
                      or f.startswith("Source") and "Tests" in f
                      for f in files)
    tests_ok = tests_added if (task and task["requires_tests"]) else True
    lint_ok = run_tool("lint_conventions.py", repo_root)
    data_ok = run_tool("validate_data.py", repo_root)
    tick_ok = any(f == "ROADMAP.md" or f.startswith("roadmap/") for f in files)
    efficiency = max(0, 10 - 2 * (commits - 1))
    composite = (
        (40 if ci_green else 0) + (20 if in_scope else 0)
        + (10 if tests_ok else 0) + (10 if lint_ok and data_ok else 0)
        + (10 if tick_ok else 0) + efficiency)
    scores = {"ci_green": ci_green, "in_scope": in_scope, "tests_ok": tests_ok,
              "lint_ok": lint_ok, "data_ok": data_ok, "roadmap_tick": tick_ok,
              "commits": commits, "efficiency": efficiency}
    return scores, composite


def score_manual_recall(manual_recall: str) -> tuple[dict, int]:
    """Review-type tasks: composite from a found/seeded recall block."""
    found, _, seeded = manual_recall.partition("/")
    recall = int(found) / max(1, int(seeded))
    return {"manual_recall": manual_recall, "recall": recall}, round(recall * 100)


def score_pr_facts(files: list[str], commits: int, ci_green: bool,
                   task: dict | None = None,
                   repo_root: Path | None = None) -> tuple[dict, int]:
    """The Epic 120 composite (0-100), from already-fetched PR facts."""
    return _rubric(files, commits, ci_green, task, repo_root)


def score_local_diff(files: list[str], commits: int = 1,
                     task: dict | None = None,
                     repo_root: Path | None = None) -> tuple[dict, int]:
    """Pre-PR path (Epic 137): same rubric with CI unknown -> composite is out
    of LOCAL_MAX (60). scores["ci_green"] is None to make that explicit."""
    scores, composite = _rubric(files, commits, None, task, repo_root)
    scores["local_max"] = LOCAL_MAX
    return scores, composite
