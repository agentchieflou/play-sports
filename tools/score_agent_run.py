#!/usr/bin/env python
"""Agent-run scorer for play-sports (Epic 120).

Turns a completed PR into an objective scorecard JSON in eval/scorecards/.
Uses `gh` for PR facts (files, commits, check rollup); runs the local linter
and data validator for hygiene scores. Scoring math lives in tools/score_lib.py
(shared with the Track P duel runner, Epic 137).

Usage (repo root, gh authenticated):
  python tools/score_agent_run.py --pr 28 --agent claude --model sonnet-5 \
      --archetype gameplay-cpp-story [--task cpp-system-story] \
      [--manual-recall 2/3]

Composite (0-100): CI green 40, scope 20, tests 10, lint 10, roadmap tick 10,
efficiency 10 (10 - 2*(commits-1), floor 0). Review tasks (no diff) score from
the manual block instead.
"""

import argparse
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

from score_lib import load_task, score_manual_recall, score_pr_facts

REPO = Path(__file__).resolve().parent.parent
SCORECARD_DIR = REPO / "eval" / "scorecards"


def gh_pr(pr):
    out = subprocess.run(
        ["gh", "pr", "view", str(pr), "--json",
         "number,title,headRefName,files,commits,statusCheckRollup,mergedAt"],
        capture_output=True, text=True, check=True, cwd=REPO)
    return json.loads(out.stdout)


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
        scores, composite = score_manual_recall(args.manual_recall)
    else:
        scores, composite = score_pr_facts(files, commits, ci_green, task)

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
