"""Epic 138: the roadmap crawl and the PARALLEL.md drift check, the run state,
candidate selection, and graph runs end to end with scripted models in temp git
repos (including a two-worker smoke run that proves the workers overlap). No
network."""

import json
import re
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

from tools.orchestrator.config import REPO_ROOT
from tools.orchestrator.models.base import ChatResponse, ToolCall, Usage
from tools.orchestrator.supervisor.board import (
    check_parallel,
    crawl,
    globs_overlap,
    load_matrix,
    parse_depends,
)
from tools.orchestrator.supervisor.graph import (
    MAX_ATTEMPTS,
    GraphRunner,
    find_candidates,
    parse_assignment,
    resume_cleanup,
    select_batch,
)
from tools.orchestrator.supervisor.state import RunState, RunStateError

ROADMAP = """\
# Fixture roadmap

### Epic 900: Docs A

**Size/Mode:** S / code
**Depends on:** —

- [ ] Write docs/a.md with one line about A

### Epic 901: Docs B

**Size/Mode:** S / code
**Depends on:** — (independent of 900)

- [ ] Write docs/b.md with one line about B

### Epic 902: Follow-up

**Size/Mode:** S / code
**Depends on:** Epic 900 (needs A first)

- [ ] Write docs/c.md after A
"""


def matrix_json(**overrides) -> dict:
    matrix = {
        "version": 1,
        "track_scopes": {"core": ["docs/**"]},
        "epics": {
            "900": {"track": "core", "mode": "code", "status": "open", "depends_on": [],
                    "scope": ["docs/a.md"]},
            "901": {"track": "core", "mode": "code", "status": "open", "depends_on": [],
                    "scope": ["docs/b.md"]},
            "902": {"track": "core", "mode": "code", "status": "open", "depends_on": ["900"],
                    "scope": ["docs/c.md"]},
        },
        "groups": [],
        "conflicts": [],
    }
    matrix.update(overrides)
    return matrix


def write_matrix(repo: Path, matrix: dict) -> None:
    (repo / "roadmap").mkdir(exist_ok=True)
    (repo / "roadmap" / "PARALLEL.md").write_text(
        "# Parallel\n\n```json parallel-matrix\n" + json.dumps(matrix, indent=1) + "\n```\n",
        encoding="utf-8")


def git(repo: Path, *args) -> None:
    subprocess.run(["git", *args], cwd=str(repo), check=True, capture_output=True)


def make_repo(root: Path, matrix: dict | None = None, roadmap: str = ROADMAP) -> Path:
    repo = root / "repo"
    repo.mkdir()
    git(repo, "init", "-q", "-b", "main")
    git(repo, "config", "user.email", "test@test.local")
    git(repo, "config", "user.name", "test")
    (repo / "ROADMAP.md").write_text(roadmap, encoding="utf-8")
    write_matrix(repo, matrix or matrix_json())
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", "init")
    return repo


class DocWriter:
    """A scripted worker: writes the file its story names, then finishes.
    `barrier` makes two workers prove they run at the same time; `fail_first`
    stops without finishing on its first call (a budget-exhausted worker)."""

    def __init__(self, barrier=None, never_finish=False, summary="done"):
        self.barrier = barrier
        self.never_finish = never_finish
        self.summary = summary
        self.label = "fake:doc-writer"
        self.prompts = []
        self._step = 0

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.prompts.append(messages[1].content)
        self._step += 1
        if self.never_finish:
            return ChatResponse(text="thinking", usage=Usage(1, 1))
        if self._step == 1:
            if self.barrier is not None:
                self.barrier.wait()
            path = re.search(r"Write (\S+\.md)", messages[1].content).group(1)
            return ChatResponse(text="", usage=Usage(1, 1), tool_calls=[
                ToolCall(id="w", name="write_file", arguments={"path": path, "content": "text\n"})])
        return ChatResponse(text="", usage=Usage(1, 1), tool_calls=[
            ToolCall(id="f", name="finish", arguments={"summary": self.summary})])


class FakeSupervisor:
    label = "fake:supervisor"

    def __init__(self, reply):
        self.reply = reply
        self.payloads = []

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.payloads.append(json.loads(messages[1].content))
        return ChatResponse(text=self.reply, usage=Usage(1, 1))


def no_checks(worktree, changed):
    return []


class BoardTests(unittest.TestCase):
    def test_parse_depends(self):
        self.assertEqual(parse_depends("Epics 6, 7, 9"), ["6", "7", "9"])
        self.assertEqual(parse_depends("Core 19, Core 20, 121–122 (Track L generators)"),
                         ["19", "20", "121", "122"])
        self.assertEqual(parse_depends("Core 20's quick-sim, Core 24"), ["20", "24"])
        self.assertEqual(parse_depends("C3, C4, Core 14, Core 16"), ["C3", "C4", "14", "16"])
        self.assertEqual(parse_depends("136 (extends Epic 120)"), ["136"])
        self.assertEqual(parse_depends("— (seed of Epic 119; Core 25's router story)"), [])
        self.assertEqual(parse_depends("starts alongside Phase 1 and grows with every Epic"), [])

    def test_crawl(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = make_repo(Path(tmp))
            board = crawl(repo)
            self.assertEqual(sorted(board.epics), ["900", "901", "902"])
            self.assertEqual(board.epics["902"].depends_on, ["900"])
            self.assertEqual(board.epics["901"].depends_on, [])
            self.assertEqual((board.epics["900"].size, board.epics["900"].mode), ("S", "code"))
            self.assertEqual(board.epics["900"].status, "open")
            self.assertEqual(board.story("900.1").text, "Write docs/a.md with one line about A")
            self.assertEqual(check_parallel(board, load_matrix(repo)), [])

    def test_check_parallel_finds_drift(self):
        with tempfile.TemporaryDirectory() as tmp:
            roadmap = ROADMAP.replace("- [ ] Write docs/b.md", "- [x] Write docs/b.md")
            matrix = matrix_json()
            matrix["epics"]["902"]["depends_on"] = []
            matrix["epics"]["777"] = {"track": "core", "mode": "code", "status": "open", "depends_on": ["555"]}
            matrix["groups"] = [{"id": "G", "epics": ["900", "404"], "serialize_within": True}]
            repo = make_repo(Path(tmp), matrix=matrix, roadmap=roadmap)
            problems = check_parallel(crawl(repo), load_matrix(repo))
            joined = "\n".join(problems)
            self.assertIn("Epic 901: matrix status 'open', roadmap checkboxes say 'done'", joined)
            self.assertIn("Epic 902: roadmap depends on 900", joined)
            self.assertIn("Matrix epic 777 is not in the roadmap", joined)
            self.assertIn("Matrix epic 777 depends on unknown 555", joined)
            self.assertIn("Group G: unknown epic 404", joined)

    def test_globs_overlap(self):
        self.assertTrue(globs_overlap("Source/PlaySports/**", "Source/PlaySports/**/PSOverlay*"))
        self.assertFalse(globs_overlap("Source/PlaySports/**/PSOverlay*", "Source/PlaySports/**/PSCamera*"))
        self.assertTrue(globs_overlap("tools/**", "tools/orchestrator/**"))
        self.assertFalse(globs_overlap("tools/playbook_scraper/**", "tools/orchestrator/**"))
        self.assertTrue(globs_overlap("Data/**", "Data/input_glyphs*"))
        self.assertFalse(globs_overlap("Data/ui_*", "Data/play_call*"))
        self.assertFalse(globs_overlap(".mcp.json", ".vscode/mcp.json"))
        self.assertTrue(globs_overlap("Plugins/**", "Plugins/AgenticLink/**"))

    def test_real_repo_crawls(self):
        board = crawl(REPO_ROOT)
        matrix = load_matrix(REPO_ROOT)
        self.assertGreater(len(board.epics), 100)
        self.assertIn("138", board.epics)
        self.assertIsInstance(check_parallel(board, matrix), list)


class StateTests(unittest.TestCase):
    def test_roundtrip_and_resume(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = RunState.create(Path(tmp), matrix_sha="abc", run_id="r1")
            state.set_status("900.1", "assigned")
            state.set_status("900.1", "coding", worker="w", branch="epic-900/x")
            state.set_status("901.1", "review")
            self.assertEqual(state.bump_attempts("900.1"), 1)
            self.assertFalse(list(Path(tmp).glob("*.tmp")))

            loaded = RunState.load_run(Path(tmp))
            self.assertEqual(loaded.run_id, "r1")
            self.assertEqual(loaded.stories["900.1"]["status"], "coding")
            self.assertEqual(loaded.stories["900.1"]["branch"], "epic-900/x")
            self.assertEqual([e["status"] for e in loaded.stories["900.1"]["events"]], ["assigned", "coding"])
            self.assertEqual(loaded.in_flight(), ["900.1"])

            loaded.halt("stop")
            self.assertEqual(loaded.prepare_resume(), ["900.1"])
            again = RunState.load(loaded.path)
            self.assertEqual(again.status_of("900.1"), "pending")
            self.assertEqual(again.status_of("901.1"), "review")
            self.assertEqual(again.halted, "")

    def test_rejects_bad_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = RunState.create(Path(tmp), run_id="r1")
            with self.assertRaises(RunStateError):
                state.set_status("900.1", "done-ish")
            with self.assertRaises(RunStateError):
                RunState.create(Path(tmp), run_id="r1")
            data = json.loads(state.path.read_text(encoding="utf-8"))
            data["version"] = 99
            state.path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(RunStateError):
                RunState.load(state.path)


class SelectionTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def candidates(self, matrix=None, roadmap=ROADMAP, allow_xl=False):
        root = self.tmp / f"case{len(list(self.tmp.iterdir()))}"
        root.mkdir()
        repo = make_repo(root, matrix=matrix, roadmap=roadmap)
        state = RunState.create(root / "runs", run_id="r")
        board, matrix_obj = crawl(repo), load_matrix(repo)
        found, notes = find_candidates(board, matrix_obj, state, allow_xl=allow_xl)
        return found, notes, matrix_obj, state

    def test_dependencies_gate(self):
        found, _, _, _ = self.candidates()
        self.assertEqual([c.story_id for c in found], ["900.1", "901.1"])

    def test_serialized_group_runs_its_first_member_only(self):
        found, _, _, _ = self.candidates(matrix_json(groups=[
            {"id": "G", "epics": ["901", "900"], "serialize_within": True}]))
        self.assertEqual([c.story_id for c in found], ["901.1"])

    def test_overlapping_scopes_and_conflicts_never_run_together(self):
        matrix = matrix_json()
        matrix["epics"]["901"]["scope"] = ["docs/**"]
        found, _, matrix_obj, _ = self.candidates(matrix)
        self.assertEqual(len(select_batch(found, [], 2, matrix_obj)), 1)

        matrix = matrix_json(conflicts=[{"epics": ["900", "901"], "reason": "shared file"}])
        found, _, matrix_obj, _ = self.candidates(matrix)
        self.assertEqual(len(select_batch(found, [], 2, matrix_obj)), 1)

        found, _, matrix_obj, _ = self.candidates()
        batch = select_batch(found, [], 2, matrix_obj, preferred=["901.1", "900.1"])
        self.assertEqual([c.story_id for c in batch], ["901.1", "900.1"])
        self.assertEqual([c.story_id for c in select_batch(found, found[:1], 2, matrix_obj)], ["901.1"])

    def test_xl_and_editor_epics_wait_for_a_person(self):
        roadmap = ROADMAP.replace("**Size/Mode:** S / code\n**Depends on:** —\n",
                                  "**Size/Mode:** XL / code\n**Depends on:** —\n", 1)
        matrix = matrix_json()
        matrix["epics"]["901"]["mode"] = "editor"
        roadmap = roadmap.replace("**Size/Mode:** S / code\n**Depends on:** — (independent",
                                  "**Size/Mode:** S / editor\n**Depends on:** — (independent")
        found, notes, _, _ = self.candidates(matrix, roadmap)
        self.assertEqual(found, [])
        self.assertTrue(any("XL" in note for note in notes))
        self.assertTrue(any("editor" in note for note in notes))
        found, _, _, _ = self.candidates(matrix, roadmap, allow_xl=True)
        self.assertEqual([c.story_id for c in found], ["900.1"])

    def test_parse_assignment(self):
        self.assertEqual(parse_assignment('ok {"assign": ["1.2"], "reasoning": "r"}'), (["1.2"], "r"))
        self.assertEqual(parse_assignment("nothing")[0], [])


class GraphRunTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.repo = make_repo(self.tmp)
        self.logs = []

    def tearDown(self):
        self._tmp.cleanup()

    def runner(self, factory, state=None, **kwargs):
        state = state or RunState.create(self.tmp / "runs", run_id="smoke")
        options = dict(supervisor_client=None, max_workers=2, dry_run=True, max_iterations=4,
                       check_runner=no_checks, transcript_dir=self.tmp / "transcripts",
                       log=self.logs.append)
        options.update(kwargs)
        return GraphRunner(self.repo, state, worker_client_factory=factory, **options)

    def assertNoWorktreesLeft(self):
        worktrees = self.repo / ".worktrees"
        self.assertFalse(worktrees.exists() and any(worktrees.iterdir()))

    def test_two_worker_smoke_run(self):
        """End-to-end smoke: two trivially disjoint doc-only stories run at the
        same time (each worker waits for the other at a barrier), the blocked
        follow-up is left alone, and the run state records it all."""
        barrier = threading.Barrier(2, timeout=10)
        supervisor = FakeSupervisor('{"assign": ["901.1", "900.1"], "reasoning": "both are free"}')
        workers = []

        def factory(assignment):
            workers.append(DocWriter(barrier=barrier))
            return workers[-1]

        state = self.runner(factory, supervisor_client=supervisor).run()
        self.assertEqual(state.halted, "")
        self.assertEqual(state.status_of("900.1"), "review")
        self.assertEqual(state.status_of("901.1"), "review")
        self.assertNotIn("902.1", state.stories)  # 900 isn't merged, so 902 stays blocked
        self.assertEqual(len(workers), 2)
        self.assertEqual(sorted(c["story"] for c in supervisor.payloads[0]["candidates"]), ["900.1", "901.1"])
        self.assertEqual(supervisor.payloads[0]["capacity"], 2)
        self.assertIn("groups", supervisor.payloads[0])
        on_disk = RunState.load(state.path)
        self.assertEqual(on_disk.stories["900.1"]["attempts"], 1)
        self.assertTrue(on_disk.stories["901.1"]["branch"].startswith("epic-901/"))
        self.assertTrue(any("900.1: review" in line for line in on_disk.summary_lines()))
        self.assertNoWorktreesLeft()

    def test_failed_stage_gets_a_fresh_worker_with_findings(self):
        made = []

        def factory(assignment):
            made.append(DocWriter(never_finish=not made))
            return made[-1]

        state = self.runner(factory, max_workers=1, max_stories=2).run()
        self.assertEqual(state.status_of("900.1"), "review")
        self.assertEqual(state.stories["900.1"]["attempts"], 2)
        self.assertIn("Findings from the previous attempt", made[1].prompts[0])
        self.assertNotIn("Findings from the previous attempt", made[0].prompts[0])

    def test_second_failure_escalates_and_the_run_goes_on(self):
        def factory(assignment):
            return DocWriter(never_finish=assignment.epic == "900")

        state = self.runner(factory).run()
        self.assertEqual(state.status_of("900.1"), "escalated")
        self.assertEqual(state.stories["900.1"]["attempts"], MAX_ATTEMPTS)
        self.assertEqual(state.status_of("901.1"), "review")
        self.assertEqual([item["story"] for item in state.escalations], ["900.1"])
        self.assertEqual(state.halted, "")

    def test_failed_checks_retry_then_pass(self):
        calls = []

        def checks(worktree, changed):
            calls.append(changed)
            return [("lint", False, "brace on the wrong line")] if len(calls) == 1 else []

        state = self.runner(lambda a: DocWriter(), max_workers=1, max_stories=2,
                            check_runner=checks).run()
        self.assertEqual(state.status_of("900.1"), "review")
        self.assertEqual(state.stories["900.1"]["attempts"], 2)
        self.assertIn("brace on the wrong line", state.stories["900.1"]["findings"])
        self.assertEqual(calls[0], ["docs/a.md"])

    def test_blocked_story_is_a_hard_stop(self):
        state = self.runner(lambda a: DocWriter(summary="BLOCKED: needs a design decision"),
                            max_workers=1).run()
        self.assertIn("needs a decision", state.halted)
        self.assertEqual(state.status_of("900.1"), "escalated")
        self.assertNotIn("901.1", state.stories)  # nothing new starts after a hard stop

    def test_repo_state_surprise_halts_before_dispatch(self):
        state = self.runner(lambda a: DocWriter(),
                            surprise_check=lambda repo: "tracked changes in the primary checkout").run()
        self.assertIn("repo-state surprise", state.halted)
        self.assertEqual(state.stories, {})

    def test_a_landed_story_lets_its_epic_move_on(self):
        state = RunState.create(self.tmp / "runs", run_id="landed")
        state.set_status("900.1", "pr_open", pr="https://example/pr/1")
        roadmap = ROADMAP.replace("- [ ] Write docs/a.md", "- [x] Write docs/a.md")
        (self.repo / "ROADMAP.md").write_text(roadmap, encoding="utf-8")
        git(self.repo, "commit", "-q", "-am", "900.1 landed")
        state = self.runner(lambda a: DocWriter(), state=state).run()
        self.assertEqual(state.status_of("900.1"), "merged")
        self.assertEqual(state.status_of("902.1"), "review")  # 900 is done, so 902 is unblocked
        self.assertEqual(state.status_of("901.1"), "review")

    def test_tracked_changes_are_a_surprise_but_run_state_is_not(self):
        from tools.orchestrator.supervisor.graph import tracked_changes

        self.assertEqual(tracked_changes(self.repo), "")
        (self.repo / "eval" / "runs").mkdir(parents=True)
        (self.repo / "eval" / "runs" / "r.json").write_text("{}", encoding="utf-8")
        git(self.repo, "add", "-A")
        git(self.repo, "commit", "-q", "-m", "a committed run")
        (self.repo / "eval" / "runs" / "r.json").write_text('{"a": 1}', encoding="utf-8")
        self.assertEqual(tracked_changes(self.repo), "")
        (self.repo / "ROADMAP.md").write_text("changed\n", encoding="utf-8")
        self.assertIn("ROADMAP.md", tracked_changes(self.repo))

    def test_resume_finishes_a_crashed_run(self):
        state = RunState.create(self.tmp / "runs", run_id="crashed")
        state.set_status("900.1", "coding", branch="epic-900/write-docsamd-with-one-line")
        state.halt("crash")
        reset = resume_cleanup(self.repo, state)
        self.assertEqual(reset, ["900.1"])
        state = self.runner(lambda a: DocWriter(), state=state).run()
        self.assertEqual(state.status_of("900.1"), "review")
        self.assertEqual(state.status_of("901.1"), "review")
        self.assertNoWorktreesLeft()


if __name__ == "__main__":
    unittest.main()
