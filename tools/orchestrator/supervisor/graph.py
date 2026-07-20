"""The supervisor loop (Epic 138).

The supervisor model (Gemini Flash high) is consulted for assignment choices,
but every rule that matters - one story = one run, dependency gating, scope
disjointness, conflict ordering, retry-then-escalate - is enforced in code.
A bad or unavailable supervisor degrades to deterministic first-N dispatch,
never to rule-breaking.

Hard stops mirror the phase-runner skill: dirty repo state at start, worktree
or story-lookup failures, and any story failing twice at the same stage stop
dispatch (escalation entry) rather than plowing on.
"""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

from ..models.base import Message, ProviderError
from ..worker.run import run_story
from .board import check_parallel, crawl, epic_scope, globs_overlap, load_matrix
from .state import RunState

MAX_ATTEMPTS = 2

SUPERVISOR_SYSTEM = """\
You are the supervisor of a multi-agent roadmap pipeline. You are given
candidate stories that are already dependency-unblocked and scope-disjoint
(the harness enforced that - you cannot add candidates). Choose which to
dispatch now, up to the worker limit, prioritizing launch-critical work and
sensible sequencing.

Reply with EXACTLY one JSON object, no prose:
{"assign": ["<story_id>", ...], "reasoning": "<1-3 sentences>"}
"""


@dataclass
class Candidate:
    story_id: str      # "<epic>.<index>"
    epic_id: str
    group_id: str
    scope: list[str]


class GraphSupervisor:
    def __init__(self, repo_root: Path, worker_client_factory,
                 supervisor_client=None, max_workers: int = 2,
                 dry_run: bool = True, state: RunState | None = None,
                 max_dispatches: int | None = None):
        """worker_client_factory: () -> fresh model client per attempt (the
        failure-routing rule: a retry gets a FRESH worker, never the failed
        session's context)."""
        self.repo_root = Path(repo_root)
        self.worker_client_factory = worker_client_factory
        self.supervisor_client = supervisor_client
        self.max_workers = max_workers
        self.dry_run = dry_run
        self.max_dispatches = max_dispatches
        self.state = state
        self.stop_reason = ""

    # -- preflight ----------------------------------------------------------

    def preflight(self) -> list[str]:
        problems = []
        # untracked files (run state, transcripts) are fine; modified tracked
        # files are the phase-runner "repo-state surprise" hard stop
        status = subprocess.run(["git", "status", "--porcelain",
                                 "--untracked-files=no"],
                                cwd=str(self.repo_root), capture_output=True,
                                text=True)
        if status.stdout.strip():
            problems.append("repo working tree is dirty (hard stop per phase-runner)")
        problems.extend(check_parallel(self.repo_root))
        return problems

    # -- candidate math (code-enforced rules) --------------------------------

    def candidates(self, board=None, matrix=None,
                   in_flight: list["Candidate"] | None = None) -> list[Candidate]:
        board = board or crawl(self.repo_root)
        matrix = matrix or load_matrix(self.repo_root)
        in_flight = in_flight or []
        epics = matrix.get("epics", {})
        done = {eid for eid, e in epics.items() if e.get("status") == "done"}
        # crawl truth wins over matrix staleness
        done.update(eid for eid, epic in board.items() if epic.status == "done")

        blocked_by_conflict: set[str] = set()
        flight_epics = {c.epic_id for c in in_flight}
        for conflict in matrix.get("conflicts", []):
            pair = conflict.get("epics", [])
            for epic_id in pair:
                others = [p for p in pair if p != epic_id]
                if any(o in flight_epics for o in others):
                    blocked_by_conflict.add(epic_id)
                # ordering: later entries wait for earlier entries to be done
                if others and pair.index(epic_id) > 0 and not all(
                        o in done for o in pair[:pair.index(epic_id)]):
                    blocked_by_conflict.add(epic_id)

        found: list[Candidate] = []
        busy_scopes = [c.scope for c in in_flight]
        for group in matrix.get("groups", []):
            group_epics = [e for e in group.get("epics", []) if e in board]
            if group.get("serialize_within", True) and any(
                    c.group_id == group.get("id") for c in in_flight):
                continue
            for epic_id in group_epics:
                epic = board[epic_id]
                if epic.status == "done":
                    continue
                if epic_id in blocked_by_conflict:
                    break  # serialized group: later epics must not leapfrog
                deps = epics.get(epic_id, {}).get("depends_on", epic.depends_on)
                external = [d for d in deps if d not in group_epics]
                if not all(d in done for d in external):
                    break  # serialized group: later epics can't leapfrog
                open_indices = epic.open_story_indices()
                if not open_indices:
                    continue
                scope = epic_scope(matrix, epic_id)
                if any(globs_overlap(scope, busy) for busy in busy_scopes):
                    break
                found.append(Candidate(
                    story_id=f"{epic_id}.{open_indices[0]}",
                    epic_id=epic_id, group_id=group.get("id", "?"),
                    scope=scope))
                break  # one candidate per group per wave
        return found

    # -- supervisor consultation --------------------------------------------

    def choose(self, candidates: list[Candidate], slots: int) -> list[Candidate]:
        if not candidates or slots <= 0:
            return []
        default = candidates[:slots]
        if self.supervisor_client is None:
            return default
        by_id = {c.story_id: c for c in candidates}
        try:
            reply = self.supervisor_client.chat(
                [Message(role="system", content=SUPERVISOR_SYSTEM),
                 Message(role="user", content=json.dumps({
                     "worker_limit": slots,
                     "candidates": [{"story": c.story_id, "group": c.group_id}
                                    for c in candidates],
                 }))],
                max_tokens=512)
            match = re.search(r"\{.*\}", reply.text, re.S)
            picks = json.loads(match.group(0))["assign"] if match else []
            chosen = [by_id[p] for p in picks if p in by_id][:slots]
            return chosen or default
        except (ProviderError, KeyError, json.JSONDecodeError, TypeError):
            return default

    # -- dispatch -----------------------------------------------------------

    def _attempt(self, candidate: Candidate, findings: str = ""):
        client = self.worker_client_factory()
        return run_story(self.repo_root, candidate.story_id, client,
                         dry_run=self.dry_run, extra_context=findings)

    def run(self) -> RunState:
        state = self.state or RunState.new(
            self.repo_root / "eval" / "runs", mode="graph",
            parallel_matrix_sha=self._matrix_sha())
        self.state = state

        problems = self.preflight()
        if problems:
            self.stop_reason = "; ".join(problems)
            state.data["escalations"].append(
                {"story": None, "reason": self.stop_reason})
            state.save()
            return state

        dispatched = 0
        while True:
            if self.max_dispatches and dispatched >= self.max_dispatches:
                self.stop_reason = "dispatch budget reached"
                break
            wave = self.choose(self.candidates(), self.max_workers)
            if not wave:
                self.stop_reason = self.stop_reason or "no dispatchable candidates"
                break
            for candidate in wave:
                state.set_status(candidate.story_id, "assigned",
                                 branch="", group=candidate.group_id)

            with ThreadPoolExecutor(max_workers=self.max_workers) as pool:
                futures = {pool.submit(self._run_with_retry, c): c
                           for c in wave}
                for future in as_completed(futures):
                    future.result()  # state updated inside; surface crashes
            dispatched += len(wave)
            if self.dry_run:
                # dry runs don't tick checkboxes, so the board never advances;
                # one wave is the honest stopping point
                self.stop_reason = "dry run: one wave dispatched"
                break
        state.save()
        return state

    def _run_with_retry(self, candidate: Candidate) -> None:
        state = self.state
        findings = ""
        for attempt in range(1, MAX_ATTEMPTS + 1):
            state.set_status(candidate.story_id, "coding", attempts=attempt)
            try:
                outcome = self._attempt(candidate, findings)
            except Exception as error:  # worktree/lookup failures = hard stop
                state.escalate(candidate.story_id, f"hard stop: {error}")
                return
            if outcome.harness.status == "finished":
                status = "review" if self.dry_run else (
                    "pr_open" if outcome.pr_url else "review")
                state.set_status(candidate.story_id, status,
                                 branch=outcome.branch,
                                 pr=outcome.pr_url or None)
                return
            findings = (f"previous attempt {attempt} ended "
                        f"{outcome.harness.status}: {outcome.harness.summary}")
            state.set_status(candidate.story_id, "failed", attempts=attempt)
        state.escalate(candidate.story_id,
                       f"failed {MAX_ATTEMPTS} attempts at coding stage")

    def _matrix_sha(self) -> str:
        path = self.repo_root / "roadmap" / "PARALLEL.md"
        if not path.is_file():
            return ""
        return hashlib.sha256(path.read_bytes()).hexdigest()[:12]
