#!/usr/bin/env python
"""Agent-run scorer for play-sports (Epic 120).

Turns a completed PR into an objective scorecard JSON in eval/scorecards/.
Uses `gh` for PR facts (files, commits, check rollup); runs the local linter
and data validator for hygiene scores.

Usage (repo root, gh authenticated):
  python tools/score_agent_run.py --pr 28 --agent claude --model sonnet-5 \
      --archetype gameplay-cpp-story [--task cpp-system-story] \
      [--manual-recall 2/3]

Composite (0-100): CI green 40, scope 20, tests 10, lint 10, roadmap tick 10,
efficiency 10 (10 - 2*(commits-1), floor 0). Review tasks (no diff) score from
the manual block instead.
"""

import argparse
import fnmatch
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCORECARD_DIR = REPO / "eval" / "scorecards"
TASK_DIR = REPO / "eval" / "tasks"


def gh_pr(pr):
    out = subprocess.run(
        ["gh", "pr", "view", str(pr), "--json",
         "number,title,headRefName,files,commits,statusCheckRollup,mergedAt"],
        capture_output=True, text=True, check=True, cwd=REPO)
    return json.loads(out.stdout)


def load_task(task_id):
    path = TASK_DIR / f"{task_id}.md"
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


def run_tool(script):
    proc = subprocess.run([sys.executable, str(REPO / "tools" / script)],
                          capture_output=True, text=True, cwd=REPO)
    return proc.returncode == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pr", type=int, required=True)
    ap.add_argument("--agent", default="unknown")
    ap.add_argument("--model", default="unknown")
    ap.add_argument("--archetype", default="unknown")
    ap.add_argument("--task", default=None)
    ap.add_argument("--manual-recall", default=None, help="review tasks: found/seeded, e.g. 2/3")
    args = ap.parse_args()

    pr = gh_pr(args.pr)
    files = [f["path"] for f in pr.get("files", [])]
    commits = len(pr.get("commits", []))
    rollup = pr.get("statusCheckRollup") or []
    ci_green = bool(rollup) and all(
        (c.get("conclusion") or c.get("state") or "").upper() in ("SUCCESS", "NEUTRAL", "SKIPPED")
        for c in rollup)

    task = load_task(args.task) if args.task else None

    if args.manual_recall:
        found, _, seeded = args.manual_recall.partition("/")
        recall = int(found) / max(1, int(seeded))
        composite = round(recall * 100)
        scores = {"manual_recall": args.manual_recall, "recall": recall}
    else:
        in_scope = True
        if task and task["allowed_paths"]:
            in_scope = all(
                any(fnmatch.fnmatch(f, pat) for pat in task["allowed_paths"])
                for f in files)
        tests_added = any("/Tests/" in f or f.startswith("Source") and "Tests" in f for f in files)
        tests_ok = tests_added if (task and task["requires_tests"]) else True
        lint_ok = run_tool("lint_conventions.py")
        data_ok = run_tool("validate_data.py")
        tick_ok = any(f == "ROADMAP.md" or f.startswith("roadmap/") for f in files)
        efficiency = max(0, 10 - 2 * (commits - 1))
        composite = (
            (40 if ci_green else 0) + (20 if in_scope else 0) + (10 if tests_ok else 0)
            + (10 if lint_ok and data_ok else 0) + (10 if tick_ok else 0) + efficiency)
        scores = {"ci_green": ci_green, "in_scope": in_scope, "tests_ok": tests_ok,
                  "lint_ok": lint_ok, "data_ok": data_ok, "roadmap_tick": tick_ok,
                  "commits": commits, "efficiency": efficiency}

    card = {
        "pr": pr["number"],
        "title": pr["title"],
        "branch": pr["headRefName"],
        "agent": args.agent,
        "model": args.model,
        "archetype": args.archetype,
        "task": args.task,
        "scored_at": datetime.now(timezone.utc).isoformat(),
        "merged_at": pr.get("mergedAt"),
        "scores": scores,
        "composite": composite,
    }

    SCORECARD_DIR.mkdir(parents=True, exist_ok=True)
    out = SCORECARD_DIR / f"pr{pr['number']:04d}.json"
    out.write_text(json.dumps(card, indent=2) + "\n", encoding="utf-8")
    print(f"scorecard written: {out.relative_to(REPO)} composite={composite}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
