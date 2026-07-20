"""Single-story run pipeline (Epic 136): worktree -> harness -> commit ->
push/PR (or dry-run diff). The harness owns git and `gh`; the model never
touches either."""

from __future__ import annotations

import subprocess
from dataclasses import dataclass
from pathlib import Path

from .harness import HarnessResult, WorkerHarness
from .prompts import StoryAssignment
from .stories import default_branch_name, find_story
from .tools import WorkerTools
from .workspace import Workspace


@dataclass
class RunOutcome:
    assignment: StoryAssignment
    harness: HarnessResult
    branch: str
    committed: bool = False
    diff: str = ""
    pr_url: str = ""


def open_pr(workspace: Workspace, assignment: StoryAssignment,
            summary: str) -> str:
    body = (
        f"## Summary\n\nStory {assignment.story_id}: {assignment.story_text}\n\n"
        f"Worker summary:\n\n{summary}\n\n"
        "Automated worker run via tools/orchestrator (Track P). "
        "Reviewer + CI gates apply as usual.\n\n"
        "🤖 Generated with [Claude Code](https://claude.com/claude-code)\n"
    )
    result = subprocess.run(
        ["gh", "pr", "create",
         "--title", f"Epic {assignment.epic}: {assignment.story_text[:60]}",
         "--body", body,
         "--head", workspace.branch],
        cwd=str(workspace.path), capture_output=True, text=True, timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"gh pr create failed: {result.stderr.strip()}")
    return result.stdout.strip().splitlines()[-1]


def run_story(repo_root: Path, story_id: str, client,
              branch: str | None = None,
              specialization: str | None = None,
              dry_run: bool = False,
              max_iterations: int | None = None,
              keep_worktree: bool = False,
              extra_context: str = "") -> RunOutcome:
    assignment = find_story(repo_root, story_id)
    if specialization:
        assignment = StoryAssignment(
            **{**assignment.__dict__, "specialization": specialization})
    branch = branch or default_branch_name(assignment)
    assignment = StoryAssignment(**{**assignment.__dict__, "branch": branch})

    workspace = Workspace(repo_root, branch=branch)
    workspace.create()
    try:
        tools = WorkerTools(workspace.path)
        harness_kwargs = {}
        if max_iterations:
            harness_kwargs["max_iterations"] = max_iterations
        harness = WorkerHarness(client, tools, repo_root=repo_root,
                                **harness_kwargs)
        harness_result = harness.run(assignment, extra_context=extra_context)

        outcome = RunOutcome(assignment=assignment, harness=harness_result,
                             branch=branch)
        if harness_result.status != "finished":
            return outcome

        outcome.committed = workspace.commit_all(
            f"Epic {assignment.epic} story {assignment.story_index} "
            f"(worker run)\n\n{harness_result.summary[:2000]}")
        outcome.diff = workspace.diff_against_base()

        if dry_run or not outcome.committed:
            return outcome

        workspace.push()
        outcome.pr_url = open_pr(workspace, assignment, harness_result.summary)
        return outcome
    finally:
        if not keep_worktree:
            # Branch (and any pushed PR) survives; only the local worktree goes.
            workspace.remove(delete_branch=dry_run)
