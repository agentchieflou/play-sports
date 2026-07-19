"""Benchmark duel mode (Epic 137): two workers race the same story in separate
worktrees; objective local scores plus a supervisor-model judgment pick the
branch that proceeds to PR. Epic 120's eval gym, made head-to-head."""

from __future__ import annotations

import importlib
import json
import re
import time
from dataclasses import dataclass, field
from pathlib import Path

from .models.base import Message, ProviderError
from .worker.harness import WorkerHarness
from .worker.prompts import StoryAssignment
from .worker.run import open_pr
from .worker.stories import default_branch_name, find_story
from .worker.tools import WorkerTools
from .worker.workspace import Workspace

score_lib = importlib.import_module("tools.score_lib")

JUDGE_SYSTEM = """\
You are the supervisor of a coding-agent benchmark duel. Two workers each
attempted the same roadmap story in isolated checkouts. Compare their diffs
against the story text and pick the better one.

Judge on: correctness for the story's scope, convention fit (4-space indent,
Allman braces, PS* prefixes), test coverage, and restraint (no out-of-scope
changes). A smaller correct diff beats a larger sprawling one.

Reply with EXACTLY one JSON object, no prose around it:
{"winner": "a" | "b" | "tie", "reasoning": "<2-4 sentences>"}
"""


@dataclass
class Contestant:
    key: str            # "a" | "b"
    client: object      # ModelClient-compatible
    branch: str = ""
    status: str = ""
    summary: str = ""
    diff: str = ""
    files: list[str] = field(default_factory=list)
    local_scores: dict = field(default_factory=dict)
    local_composite: int = 0
    transcript_path: str = ""
    cost_rank: int = 0  # lower = cheaper; wins ties


@dataclass
class DuelResult:
    duel_id: str
    story_id: str
    winner: str = ""    # "a" | "b" | "none"
    judge_reasoning: str = ""
    pr_url: str = ""
    record_path: str = ""


def parse_judge_verdict(text: str) -> tuple[str, str]:
    match = re.search(r"\{.*\}", text, re.S)
    if not match:
        return "tie", f"unparseable judge reply: {text[:200]}"
    try:
        data = json.loads(match.group(0))
    except json.JSONDecodeError:
        return "tie", f"unparseable judge JSON: {text[:200]}"
    winner = str(data.get("winner", "tie")).lower()
    if winner not in ("a", "b", "tie"):
        winner = "tie"
    return winner, str(data.get("reasoning", ""))


def decide_winner(a: Contestant, b: Contestant,
                  judge_winner: str) -> tuple[str, str]:
    """Combine objective scores with the judge verdict.

    Objective composite difference > 10 points is decisive on its own; inside
    that band the judge decides; a judge tie goes to the cheaper model."""
    finished = [c for c in (a, b) if c.status == "finished"]
    if len(finished) == 0:
        return "none", "neither worker finished"
    if len(finished) == 1:
        return finished[0].key, "only finisher"
    if abs(a.local_composite - b.local_composite) > 10:
        leader = a if a.local_composite > b.local_composite else b
        return leader.key, "objective score margin"
    if judge_winner in ("a", "b"):
        return judge_winner, "judge verdict"
    cheaper = a if a.cost_rank <= b.cost_rank else b
    return cheaper.key, "tie -> cheaper model"


def run_duel(repo_root: Path, story_id: str,
             worker_clients: list, judge_client,
             specialization: str | None = None,
             dry_run: bool = False,
             max_iterations: int | None = None,
             duel_dir: Path | None = None) -> DuelResult:
    if len(worker_clients) != 2:
        raise ValueError("a duel needs exactly two worker clients")

    assignment = find_story(repo_root, story_id)
    if specialization:
        assignment = StoryAssignment(
            **{**assignment.__dict__, "specialization": specialization})
    base_branch = default_branch_name(assignment)
    duel_id = f"{story_id.replace('.', '-')}-{time.strftime('%Y%m%d-%H%M%S')}"

    contestants = [
        Contestant(key="a", client=worker_clients[0], cost_rank=0),
        Contestant(key="b", client=worker_clients[1], cost_rank=1),
    ]
    workspaces: dict[str, Workspace] = {}

    try:
        for contestant in contestants:
            contestant.branch = f"{base_branch}-duel-{contestant.key}"
            run_assignment = StoryAssignment(
                **{**assignment.__dict__, "branch": contestant.branch})
            workspace = Workspace(repo_root, branch=contestant.branch)
            workspaces[contestant.key] = workspace
            workspace.create()

            harness_kwargs = {}
            if max_iterations:
                harness_kwargs["max_iterations"] = max_iterations
            harness = WorkerHarness(contestant.client,
                                    WorkerTools(workspace.path),
                                    repo_root=repo_root, **harness_kwargs)
            result = harness.run(run_assignment)
            contestant.status = result.status
            contestant.summary = result.summary
            contestant.transcript_path = result.transcript_path

            if result.status == "finished":
                workspace.commit_all(
                    f"Epic {assignment.epic} story {assignment.story_index} "
                    f"(duel worker {contestant.key})")
                contestant.diff = workspace.diff_against_base()
                contestant.files = workspace.changed_files()
                contestant.local_scores, contestant.local_composite = (
                    score_lib.score_local_diff(contestant.files,
                                               repo_root=repo_root))

        a, b = contestants
        judge_winner, judge_reasoning = "tie", "judge not consulted"
        if a.status == "finished" and b.status == "finished":
            try:
                judge_reply = judge_client.chat(
                    [Message(role="system", content=JUDGE_SYSTEM),
                     Message(role="user", content=(
                         f"Story: {assignment.story_text}\n\n"
                         f"## Worker A diff\n```diff\n{a.diff[:60000]}\n```\n\n"
                         f"## Worker B diff\n```diff\n{b.diff[:60000]}\n```\n"))],
                    max_tokens=1024)
                judge_winner, judge_reasoning = parse_judge_verdict(judge_reply.text)
            except ProviderError as error:
                judge_reasoning = f"judge unavailable: {error}"

        winner_key, decision_basis = decide_winner(a, b, judge_winner)
        result = DuelResult(duel_id=duel_id, story_id=story_id,
                            winner=winner_key,
                            judge_reasoning=f"{decision_basis}: {judge_reasoning}")

        if winner_key in ("a", "b") and not dry_run:
            winner = a if winner_key == "a" else b
            workspace = workspaces[winner_key]
            workspace.push()
            result.pr_url = open_pr(workspace,
                                    StoryAssignment(**{**assignment.__dict__,
                                                       "branch": winner.branch}),
                                    winner.summary)

        record = {
            "duel_id": duel_id,
            "story": story_id,
            "story_text": assignment.story_text,
            "winner": result.winner,
            "decision": result.judge_reasoning,
            "pr": result.pr_url,
            "contestants": [{
                "key": c.key,
                "model": getattr(c.client, "label", "unknown"),
                "status": c.status,
                "branch": c.branch,
                "files_changed": len(c.files),
                "local_composite": c.local_composite,
                "local_scores": c.local_scores,
                "transcript": c.transcript_path,
            } for c in contestants],
        }
        out_dir = duel_dir or (repo_root / "eval" / "duels")
        out_dir.mkdir(parents=True, exist_ok=True)
        record_file = out_dir / f"{duel_id}.json"
        record_file.write_text(json.dumps(record, indent=2) + "\n",
                               encoding="utf-8")
        result.record_path = str(record_file)
        return result
    finally:
        for workspace in workspaces.values():
            # Loser branches die with their worktrees (transcripts keep the
            # archive); the pushed winner branch survives on the remote.
            workspace.remove(delete_branch=True)
