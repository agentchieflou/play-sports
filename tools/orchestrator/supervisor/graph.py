"""Supervisor graph loop (Epic 138).

Each round the supervisor crawls the roadmap, finds the stories it may start
(dependencies done, file scopes free, one story per epic, one per serialized
group), lets the supervisor model rank them, and hands the winners to a pool
of worker threads. Each worker runs one Epic 136 harness in its own worktree.
Code, not the prompt, enforces the rules:

- one story = one run: an epic, or a serialize_within group, has at most one
  unmerged story in a run, and a settled story is never dispatched again;
- disjointness: two concurrent stories never share a file scope (PARALLEL.md
  rule 3) or a `conflicts` entry;
- failure routing (supervisor-orchestrator skill): a stage failure gets one
  fresh worker with the findings; a second failure is an escalation entry;
- hard stops (phase-runner skill) end dispatching: a repo-state surprise
  (tracked changes in the primary checkout, a git or push failure, an
  unreadable roadmap), a PR that conflicts, or a worker reporting a story
  BLOCKED (mis-sized or needing a decision). Running workers finish; nothing
  new starts.

Everything is recorded in the run state (state.py), so `resume` continues a
run after a crash or a hard stop.
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
import threading
from concurrent.futures import FIRST_COMPLETED, ThreadPoolExecutor, wait
from dataclasses import dataclass
from pathlib import Path

from ..models.base import Message, ProviderError
from ..worker.harness import WorkerHarness
from ..worker.prompts import StoryAssignment
from ..worker.run import open_pr
from ..worker.stories import default_branch_name
from ..worker.tools import WorkerTools
from ..worker.workspace import GitError, Workspace, run_git
from .board import Board, Epic, Matrix, Story, crawl, load_matrix, scopes_overlap
from .state import IN_FLIGHT, RunState

MAX_ATTEMPTS = 2
SKIPPED_MODES = ("editor",)
# Git operations that touch the shared repository (worktree add/remove, branch
# refs, push) run one at a time; the model loops run in parallel.
GIT_LOCK = threading.Lock()

SUPERVISOR_SYSTEM = """\
You are the supervisor of a team of coding agents working through a game's
roadmap. You never write code. Each round you get the stories that may start
now; the program has already checked their dependencies, file scopes and the
free worker slots. Choose which to start, most valuable first: stories that
unblock many other epics, earlier phases before later ones, small before large.
Assign at most `capacity` stories. You may assign fewer when a story looks
risky to run unattended.

Reply with EXACTLY one JSON object, no prose around it:
{"assign": ["<story id>", ...], "reasoning": "<one or two sentences>"}
"""


@dataclass
class Candidate:
    story: Story
    epic: Epic
    scope: list[str]
    group: str = ""
    serialized: bool = False
    unblocks: int = 0

    @property
    def story_id(self) -> str:
        return self.story.story_id


@dataclass
class JobOutcome:
    """How one worker attempt ended.

    status: "review" (dry run, diff ready), "pr_open", "stage_failed" (retry
    or escalate), "blocked" (the worker says the story needs a person: a hard
    stop) or "hard_stop" (merge conflict and the like).
    """

    status: str
    stage: str = ""
    findings: str = ""
    pr: str = ""
    diff: str = ""


# --- candidate selection ------------------------------------------------------------------

def _dependency_done(board: Board, matrix: Matrix, dep: str) -> bool:
    epic = board.epic(dep)
    if epic is not None:
        return epic.status == "done"
    return matrix.epics.get(dep, {}).get("status") == "done"


def _epic_done(board: Board, matrix: Matrix, epic_id: str) -> bool:
    return _dependency_done(board, matrix, epic_id)


def find_candidates(board: Board, matrix: Matrix, state: RunState,
                    allow_xl: bool = False) -> tuple[list[Candidate], list[str]]:
    """Stories that may start now, in roadmap order, and a note for each epic
    held back for a reason a person may want to see (XL, editor mode, not in
    the matrix)."""
    notes: list[str] = []
    busy_epics: set[str] = set()
    busy_groups: set[str] = set()
    for story_id, entry in list(state.stories.items()):
        if entry["status"] in ("pending", "merged"):
            continue
        epic_id = story_id.split(".")[0]
        busy_epics.add(epic_id)
        group = matrix.group_of(epic_id)
        if group and group.get("serialize_within"):
            busy_groups.add(group["id"])

    dependents: dict[str, int] = {}
    for entry in matrix.epics.values():
        for dep in entry.get("depends_on", []):
            dependents[dep] = dependents.get(dep, 0) + 1

    candidates: list[Candidate] = []
    for epic in board.epics.values():
        if epic.status == "done" or epic.id in busy_epics:
            continue
        entry = matrix.epics.get(epic.id)
        if entry is None:
            notes.append(f"Epic {epic.id} is not in PARALLEL.md's matrix; not dispatched")
            continue
        deps = sorted(set(epic.depends_on) | set(entry.get("depends_on", [])))
        if not all(_dependency_done(board, matrix, dep) for dep in deps):
            continue
        mode = epic.mode or entry.get("mode", "")
        if mode in SKIPPED_MODES:
            notes.append(f"Epic {epic.id} is {mode}-mode work: needs an editor session")
            continue
        if epic.size == "XL" and not allow_xl:
            notes.append(f"Epic {epic.id} is XL: a person should see it before dispatch (--allow-xl)")
            continue
        group = matrix.group_of(epic.id)
        serialized = bool(group and group.get("serialize_within"))
        if serialized:
            if group["id"] in busy_groups:
                continue
            first_open = next((member for member in group.get("epics", [])
                               if not _epic_done(board, matrix, member)), None)
            if first_open != epic.id:
                continue
        open_stories = epic.open_stories()
        if not open_stories:
            continue
        story = open_stories[0]
        if state.status_of(story.story_id) not in ("", "pending"):
            continue
        candidates.append(Candidate(story=story, epic=epic, scope=matrix.scope(epic.id),
                                    group=group["id"] if group else "",
                                    serialized=serialized,
                                    unblocks=dependents.get(epic.id, 0)))
    return candidates, notes


def candidates_conflict(a: Candidate, b: Candidate, matrix: Matrix) -> bool:
    if a.epic.id == b.epic.id:
        return True
    if a.group and a.group == b.group and (a.serialized or b.serialized):
        return True
    for conflict in matrix.conflicts:
        members = conflict.get("epics", [])
        if a.epic.id in members and b.epic.id in members:
            return True
    return scopes_overlap(a.scope, b.scope)


def select_batch(candidates: list[Candidate], running: list[Candidate], capacity: int,
                 matrix: Matrix, preferred: list[str] | None = None) -> list[Candidate]:
    """Up to `capacity` candidates that conflict neither with the running
    stories nor with each other. `preferred` (the supervisor model's ranking)
    decides the order when it names valid candidates; otherwise the epics
    that unblock the most go first, in roadmap order."""
    by_id = {candidate.story_id: candidate for candidate in candidates}
    ranked = [by_id[story_id] for story_id in (preferred or []) if story_id in by_id]
    if not ranked:
        ranked = sorted(candidates, key=lambda c: -c.unblocks)
    chosen: list[Candidate] = []
    for candidate in ranked:
        if len(chosen) >= capacity:
            break
        if any(candidates_conflict(candidate, other, matrix) for other in running + chosen):
            continue
        chosen.append(candidate)
    return chosen


def parse_assignment(text: str) -> tuple[list[str], str]:
    match = re.search(r"\{.*\}", text or "", re.S)
    if not match:
        return [], f"unparseable supervisor reply: {(text or '')[:200]}"
    try:
        data = json.loads(match.group(0))
    except json.JSONDecodeError:
        return [], f"unparseable supervisor JSON: {(text or '')[:200]}"
    assign = data.get("assign", [])
    if not isinstance(assign, list):
        return [], "supervisor reply has no assign list"
    return [str(item) for item in assign], str(data.get("reasoning", ""))


def ask_supervisor(client, candidates: list[Candidate], running: list[Candidate],
                   capacity: int, matrix: Matrix | None = None) -> tuple[list[str], str]:
    """The supervisor model's ranking of the candidates; ([], why) when there
    is no model or it fails, and the program's own order applies. The model
    sees PARALLEL.md's groups and the board state: what runs, what may start."""
    if client is None:
        return [], "no supervisor model configured; using the program's order"
    payload = {
        "capacity": capacity,
        "groups": [{"id": g.get("id"), "label": g.get("label"), "epics": g.get("epics", []),
                    "serialize_within": g.get("serialize_within", False)}
                   for g in (matrix.groups if matrix else [])],
        "running": [{"story": c.story_id, "epic": c.epic.title} for c in running],
        "candidates": [{
            "story": c.story_id,
            "epic": f"Epic {c.epic.id}: {c.epic.title}",
            "roadmap_file": c.epic.file,
            "size": c.epic.size, "mode": c.epic.mode,
            "group": c.group,
            "unblocks_epics": c.unblocks,
            "text": c.story.text[:600],
        } for c in candidates],
    }
    messages = [Message(role="system", content=SUPERVISOR_SYSTEM),
                Message(role="user", content=json.dumps(payload, indent=1))]
    try:
        response = client.chat(messages, temperature=0.0, max_tokens=2048)
    except ProviderError as error:
        return [], f"supervisor model unavailable ({error}); using the program's order"
    return parse_assignment(response.text)


# --- checks and repo state ----------------------------------------------------------------

def default_checks(worktree: Path, changed: list[str]) -> list[tuple[str, bool, str]]:
    """The tester stage: the repo's own checks, run in the worktree."""
    results = []
    for script in ("tools/lint_conventions.py", "tools/validate_data.py"):
        if (worktree / script).is_file():
            proc = subprocess.run([sys.executable, script], cwd=str(worktree),
                                  capture_output=True, text=True, timeout=600)
            results.append((script, proc.returncode == 0, (proc.stdout + proc.stderr)[-3000:]))
    if any(path.startswith("tools/orchestrator/") for path in changed) \
            and (worktree / "tools" / "orchestrator" / "tests").is_dir():
        proc = subprocess.run([sys.executable, "-m", "unittest", "discover", "-s",
                               "tools/orchestrator/tests", "-t", "."],
                              cwd=str(worktree), capture_output=True, text=True, timeout=900)
        results.append(("orchestrator unittest", proc.returncode == 0,
                        (proc.stdout + proc.stderr)[-3000:]))
    return results


def tracked_changes(repo_root: Path) -> str:
    """A repo-state surprise: tracked files changed in the primary checkout
    while the graph runs (someone else is working in it). Empty when clean."""
    try:
        # The run's own state files (eval/runs) are expected to change.
        output = run_git(["status", "--porcelain", "--untracked-files=no", "--", ".",
                          ":(exclude)eval/runs"], cwd=repo_root)
    except GitError as error:
        return str(error)
    return f"tracked changes in the primary checkout:\n{output.strip()}" if output.strip() else ""


def pr_mergeable(pr_url: str) -> str:
    """GitHub's mergeable state for a PR (MERGEABLE, CONFLICTING or UNKNOWN)."""
    try:
        proc = subprocess.run(["gh", "pr", "view", pr_url, "--json", "mergeable", "-q", ".mergeable"],
                              capture_output=True, text=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return "UNKNOWN"
    return proc.stdout.strip() or "UNKNOWN"


# --- the runner ---------------------------------------------------------------------------

class GraphRunner:
    """Runs one supervisor graph over the roadmap with up to max_workers stories
    at a time. worker_client_factory(assignment) returns a fresh model client
    for each attempt (a "fresh worker")."""

    def __init__(self, repo_root: Path, state: RunState, worker_client_factory,
                 supervisor_client=None, max_workers: int = 2, dry_run: bool = False,
                 max_stories: int | None = None, max_iterations: int | None = None,
                 allow_xl: bool = False, check_runner=default_checks,
                 surprise_check=tracked_changes, mergeable_check=pr_mergeable,
                 transcript_dir: Path | None = None, log=print):
        self.repo_root = Path(repo_root)
        self.state = state
        self.worker_client_factory = worker_client_factory
        self.supervisor_client = supervisor_client
        self.max_workers = max(1, max_workers)
        self.dry_run = dry_run
        self.max_stories = max_stories
        self.max_iterations = max_iterations
        self.allow_xl = allow_xl
        self.check_runner = check_runner
        self.surprise_check = surprise_check
        self.mergeable_check = mergeable_check
        self.transcript_dir = transcript_dir
        self.log = log

    def run(self) -> RunState:
        futures: dict = {}
        started = 0
        reported_notes: set[str] = set()
        with ThreadPoolExecutor(max_workers=self.max_workers) as pool:
            while True:
                room = self.max_workers - len(futures)
                if self.max_stories is not None:
                    room = min(room, self.max_stories - started)
                if not self.state.halted and room > 0:
                    for candidate in self._next_batch(list(futures.values()), room, reported_notes):
                        self.state.set_status(candidate.story_id, "assigned",
                                              note=f"Epic {candidate.epic.id}: {candidate.epic.title}")
                        futures[pool.submit(self._attempt, candidate)] = candidate
                        started += 1
                        self.log(f"dispatched {candidate.story_id}: {candidate.story.text[:80]}")
                if not futures:
                    break
                done, _ = wait(list(futures), return_when=FIRST_COMPLETED)
                for future in done:
                    candidate = futures.pop(future)
                    try:
                        outcome = future.result()
                    except Exception as error:  # a git failure or another surprise
                        outcome = JobOutcome(status="hard_stop", stage="harness",
                                             findings=f"repo-state surprise: {error}")
                    self._settle(candidate, outcome)
        if self.state.halted:
            self.log(f"halted: {self.state.halted}")
        return self.state

    def _next_batch(self, running: list[Candidate], room: int,
                    reported_notes: set[str]) -> list[Candidate]:
        surprise = self.surprise_check(self.repo_root) if self.surprise_check else ""
        if surprise:
            self.state.halt(f"repo-state surprise: {surprise}")
            return []
        try:
            board = crawl(self.repo_root)
            matrix = load_matrix(self.repo_root)
        except (OSError, ValueError) as error:
            self.state.halt(f"repo-state surprise: can't read the roadmap: {error}")
            return []
        if not self.state.data.get("parallel_matrix_sha"):
            self.state.data["parallel_matrix_sha"] = matrix.sha256
        # A story whose box is now ticked in the checkout has landed: its epic or
        # group may move on to the next story.
        for story_id, entry in list(self.state.stories.items()):
            story = board.story(story_id)
            if entry["status"] in ("review", "pr_open") and story is not None and story.done:
                self.state.set_status(story_id, "merged", note="ticked in the roadmap")
        candidates, notes = find_candidates(board, matrix, self.state, self.allow_xl)
        for note in notes:
            if note not in reported_notes:
                reported_notes.add(note)
                self.log(note)
        if not candidates:
            return []
        preferred, reasoning = ask_supervisor(self.supervisor_client, candidates, running, room, matrix)
        if reasoning:
            self.log(f"supervisor: {reasoning[:300]}")
        return select_batch(candidates, running, room, matrix, preferred)

    def _attempt(self, candidate: Candidate) -> JobOutcome:
        story_id = candidate.story_id
        self.state.bump_attempts(story_id)
        findings = self.state.stories[story_id].get("findings", "")
        assignment = StoryAssignment(epic=candidate.epic.id, story_index=candidate.story.index,
                                     story_text=candidate.story.text,
                                     epic_title=candidate.epic.title,
                                     track_file=candidate.epic.file, findings=findings)
        branch = default_branch_name(assignment)
        assignment = StoryAssignment(**{**assignment.__dict__, "branch": branch})
        client = self.worker_client_factory(assignment)
        workspace = Workspace(self.repo_root, branch=branch)
        with GIT_LOCK:
            if workspace.path.exists():
                workspace.remove(delete_branch=True)  # a crashed attempt's leftovers
            else:
                run_git(["branch", "-D", branch], cwd=self.repo_root, check=False)
            workspace.create()
        self.state.set_status(story_id, "coding", worker=getattr(client, "label", "worker"),
                              branch=branch, worktree=str(workspace.path))
        keep_branch = False
        try:
            harness_kwargs = {"transcript_dir": self.transcript_dir} if self.transcript_dir else {}
            if self.max_iterations:
                harness_kwargs["max_iterations"] = self.max_iterations
            result = WorkerHarness(client, WorkerTools(workspace.path), repo_root=self.repo_root,
                                   **harness_kwargs).run(assignment)
            self.state.set_status(story_id, "coding", transcript=result.transcript_path,
                                  note=f"worker {result.status}")
            if result.status != "finished":
                return JobOutcome(status="stage_failed", stage="coding",
                                  findings=f"The previous worker stopped without finishing "
                                           f"({result.status}). {result.summary}".strip())
            if result.summary.strip().upper().startswith("BLOCKED"):
                return JobOutcome(status="blocked", stage="coding", findings=result.summary)
            with GIT_LOCK:
                committed = workspace.commit_all(
                    f"Epic {assignment.epic} story {assignment.story_index} (graph run)\n\n"
                    f"{result.summary[:2000]}")
            if not committed:
                return JobOutcome(status="stage_failed", stage="coding",
                                  findings="The previous worker finished without changing any file.")

            self.state.set_status(story_id, "testing")
            checks = self.check_runner(workspace.path, workspace.changed_files())
            failures = [(name, output) for name, ok, output in checks if not ok]
            if failures:
                return JobOutcome(status="stage_failed", stage="testing",
                                  findings="\n\n".join(f"{name} failed:\n{output}" for name, output in failures))

            diff = workspace.diff_against_base()
            if self.dry_run:
                return JobOutcome(status="review", diff=diff, findings=result.summary)
            with GIT_LOCK:
                workspace.push()
            pr_url = open_pr(workspace, assignment, result.summary)
            keep_branch = True
            if self.mergeable_check and self.mergeable_check(pr_url) == "CONFLICTING":
                return JobOutcome(status="hard_stop", stage="pr", pr=pr_url,
                                  findings=f"merge conflict: {pr_url} conflicts with main")
            return JobOutcome(status="pr_open", pr=pr_url, findings=result.summary)
        finally:
            with GIT_LOCK:
                workspace.remove(delete_branch=not keep_branch)

    def _settle(self, candidate: Candidate, outcome: JobOutcome) -> None:
        story_id = candidate.story_id
        state = self.state
        if outcome.status in ("review", "pr_open"):
            state.set_status(story_id, outcome.status, note=outcome.findings[:500], pr=outcome.pr,
                             diff_lines=len(outcome.diff.splitlines()))
            self.log(f"{story_id}: {outcome.status}{' ' + outcome.pr if outcome.pr else ''}")
        elif outcome.status == "stage_failed":
            attempts = state.stories[story_id].get("attempts", 0)
            if attempts >= MAX_ATTEMPTS:
                state.escalate(story_id, outcome.stage,
                               f"failed {attempts} attempts; last: {outcome.findings}")
                self.log(f"{story_id}: escalated after {attempts} attempts ({outcome.stage})")
            else:
                state.set_status(story_id, "pending", note=f"{outcome.stage} failed; a fresh worker retries",
                                 findings=outcome.findings[:6000])
                self.log(f"{story_id}: {outcome.stage} failed; retrying with a fresh worker")
        elif outcome.status == "blocked":
            state.escalate(story_id, outcome.stage,
                           f"the worker reports the story needs a person: {outcome.findings}")
            state.halt(f"story {story_id} is mis-sized or needs a decision")
        else:
            state.escalate(story_id, outcome.stage, outcome.findings, status="failed")
            if outcome.pr:
                state.set_status(story_id, "failed", pr=outcome.pr)
            state.halt(outcome.findings.splitlines()[0] if outcome.findings else "hard stop")


def resume_cleanup(repo_root: Path, state: RunState) -> list[str]:
    """Before resuming: in-flight stories go back to pending and their stale
    worktrees are removed."""
    stale = [(sid, state.stories[sid].get("branch", "")) for sid in state.in_flight()]
    reset = state.prepare_resume()
    for _, branch in stale:
        if branch:
            with GIT_LOCK:
                Workspace(repo_root, branch=branch).remove(delete_branch=True)
    return reset


__all__ = [
    "Candidate", "GraphRunner", "JobOutcome", "MAX_ATTEMPTS", "ask_supervisor",
    "candidates_conflict", "default_checks", "find_candidates", "parse_assignment",
    "resume_cleanup", "select_batch", "tracked_changes", "IN_FLIGHT",
]
