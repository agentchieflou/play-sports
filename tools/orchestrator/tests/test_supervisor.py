"""Epic 138: board crawl, PARALLEL.md drift check, run-state persistence, and
an end-to-end 2-worker dry-run graph over disjoint doc-only stories. No
network; git runs in temp repos."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.orchestrator.models.base import ChatResponse, ToolCall, Usage
from tools.orchestrator.supervisor.board import (
    check_parallel,
    crawl,
    globs_overlap,
    load_matrix,
)
from tools.orchestrator.supervisor.graph import GraphSupervisor
from tools.orchestrator.supervisor.state import RunState

ROADMAP = """\
# ROADMAP

### Epic 1: Done Epic

**Depends on:** —

- [x] finished story
"""

TRACK = """\
# Track T — Test (Epics 900–901)

### Epic 900: Docs Epic A

**Size/Mode:** S / code
**Goal:** test.
**Depends on:** Core 1

- [ ] write docs/a.md describing A

### Epic 901: Docs Epic B

**Size/Mode:** S / code
**Goal:** test.
**Depends on:** 1

- [ ] write notes/b.md describing B
"""

MATRIX = {
    "version": 1,
    "generated": "2026-07-19",
    "track_scopes": {"core": ["Source/**"], "T": []},
    "epics": {
        "1": {"track": "core", "mode": "code", "status": "done", "depends_on": []},
        "900": {"track": "T", "mode": "code", "status": "open",
                "depends_on": ["1"], "scope": ["docs/**"]},
        "901": {"track": "T", "mode": "code", "status": "open",
                "depends_on": ["1"], "scope": ["notes/**"]},
    },
    "groups": [
        {"id": "GA", "label": "A", "epics": ["900"], "serialize_within": True},
        {"id": "GB", "label": "B", "epics": ["901"], "serialize_within": True},
    ],
    "conflicts": [],
}


def make_repo(path: Path, matrix=None) -> None:
    def git(*args):
        subprocess.run(["git", *args], cwd=str(path), check=True,
                       capture_output=True)

    git("init", "-q", "-b", "main")
    git("config", "user.email", "test@test.local")
    git("config", "user.name", "test")
    (path / "ROADMAP.md").write_text(ROADMAP, encoding="utf-8")
    (path / "roadmap").mkdir()
    (path / "roadmap" / "test-track.md").write_text(TRACK, encoding="utf-8")
    matrix_text = ("# PARALLEL\n\n```json parallel-matrix\n"
                   + json.dumps(matrix or MATRIX, indent=2) + "\n```\n")
    (path / "roadmap" / "PARALLEL.md").write_text(matrix_text, encoding="utf-8")
    git("add", "-A")
    git("commit", "-q", "-m", "init")


class BoardTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        make_repo(self.root)

    def tearDown(self):
        self._tmp.cleanup()

    def test_crawl_statuses_and_deps(self):
        board = crawl(self.root)
        self.assertEqual(board["1"].status, "done")
        self.assertEqual(board["900"].status, "open")
        self.assertEqual(board["900"].depends_on, ["1"])
        self.assertEqual(board["901"].open_story_indices(), [1])

    def test_check_parallel_clean(self):
        self.assertEqual(check_parallel(self.root), [])

    def test_check_parallel_flags_drift(self):
        stale = json.loads(json.dumps(MATRIX))
        stale["epics"]["1"]["status"] = "open"          # wrong status
        stale["epics"]["999"] = {"track": "T", "status": "open",
                                 "depends_on": ["888"]}  # ghost epic + dep
        del stale["epics"]["901"]                        # missing epic
        make_repo_root = self.root / "roadmap" / "PARALLEL.md"
        make_repo_root.write_text(
            "# PARALLEL\n\n```json parallel-matrix\n"
            + json.dumps(stale) + "\n```\n", encoding="utf-8")
        problems = "\n".join(check_parallel(self.root))
        self.assertIn("matrix says 'open', roadmap says 'done'", problems)
        self.assertIn("999 not found in roadmap", problems)
        self.assertIn("unknown dependency '888'", problems)
        self.assertIn("901 missing from matrix", problems)

    def test_load_matrix(self):
        self.assertEqual(load_matrix(self.root)["version"], 1)

    def test_globs_overlap(self):
        self.assertTrue(globs_overlap(["tools/**"], ["tools/orchestrator/**"]))
        self.assertFalse(globs_overlap(["docs/**"], ["notes/**"]))


class RunStateTests(unittest.TestCase):
    def test_new_save_load_roundtrip(self):
        with tempfile.TemporaryDirectory() as tmp:
            runs = Path(tmp) / "runs"
            state = RunState.new(runs, mode="graph", parallel_matrix_sha="abc")
            state.set_status("900.1", "assigned", group="GA")
            state.set_status("900.1", "coding", attempts=1)
            state.escalate("901.1", "test escalation")

            loaded = RunState.latest(runs)
            self.assertEqual(loaded.data["run_id"], state.data["run_id"])
            self.assertEqual(loaded.data["stories"]["900.1"]["status"], "coding")
            self.assertEqual(loaded.data["stories"]["901.1"]["status"],
                             "escalated")
            self.assertEqual(len(loaded.data["escalations"]), 1)
            self.assertIn("coding=1", loaded.summary())

    def test_bad_status_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            state = RunState.new(Path(tmp), mode="graph")
            with self.assertRaises(ValueError):
                state.set_status("x", "vibing")


class ScriptedDocWorker:
    """Writes the story's target file then finishes."""

    label = "fake:doc-worker"

    def __init__(self):
        self._step = 0

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self._step += 1
        task = messages[1].content
        target = "docs/a.md" if "docs/a.md" in task else "notes/b.md"
        if self._step == 1:
            return ChatResponse(text="", usage=Usage(5, 5), tool_calls=[
                ToolCall(id="1", name="write_file",
                         arguments={"path": target, "content": "content\n"})])
        return ChatResponse(text="", usage=Usage(5, 5), tool_calls=[
            ToolCall(id="2", name="finish", arguments={"summary": "done"})])


class ScriptedSupervisorModel:
    label = "fake:supervisor"

    def __init__(self):
        self.consulted = 0

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.consulted += 1
        payload = json.loads(messages[1].content)
        picks = [c["story"] for c in payload["candidates"]]
        return ChatResponse(
            text=json.dumps({"assign": picks, "reasoning": "all disjoint"}),
            usage=Usage(1, 1))


class GraphSmokeTests(unittest.TestCase):
    def test_two_worker_dry_run_over_disjoint_stories(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            make_repo(repo)
            supervisor_model = ScriptedSupervisorModel()

            supervisor = GraphSupervisor(
                repo,
                worker_client_factory=ScriptedDocWorker,
                supervisor_client=supervisor_model,
                max_workers=2, dry_run=True)
            state = supervisor.run()

            self.assertEqual(supervisor_model.consulted, 1)
            stories = state.data["stories"]
            self.assertEqual(sorted(stories), ["900.1", "901.1"])
            for entry in stories.values():
                self.assertEqual(entry["status"], "review")
            self.assertEqual(state.data["escalations"], [])
            self.assertIn("dry run", supervisor.stop_reason)
            # run state persisted under eval/runs/
            self.assertTrue(list((repo / "eval" / "runs").glob("*.json")))

    def test_dirty_repo_hard_stops(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            make_repo(repo)
            (repo / "dirty.txt").write_text("x", encoding="utf-8")
            supervisor = GraphSupervisor(
                repo, worker_client_factory=ScriptedDocWorker,
                max_workers=1, dry_run=True)
            state = supervisor.run()
            self.assertEqual(state.data["stories"], {})
            self.assertIn("dirty", supervisor.stop_reason)

    def test_failed_worker_retries_then_escalates(self):
        class FailingWorker:
            label = "fake:failing"

            def chat(self, messages, tools=None, temperature=0.2,
                     max_tokens=8192):
                return ChatResponse(text="I give up", usage=Usage(1, 1))

        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            single = json.loads(json.dumps(MATRIX))
            single["groups"] = [{"id": "GA", "label": "A", "epics": ["900"],
                                 "serialize_within": True}]
            make_repo(repo, matrix=single)
            supervisor = GraphSupervisor(
                repo, worker_client_factory=FailingWorker,
                max_workers=1, dry_run=True)
            # keep the fake cheap: 2 iterations per attempt
            original = supervisor._attempt
            supervisor._attempt = lambda c, findings="": __import__(
                "tools.orchestrator.worker.run", fromlist=["run_story"]
            ).run_story(repo, c.story_id, FailingWorker(), dry_run=True,
                        max_iterations=2, extra_context=findings)
            state = supervisor.run()
            entry = state.data["stories"]["900.1"]
            self.assertEqual(entry["status"], "escalated")
            self.assertEqual(entry["attempts"], 2)
            self.assertEqual(len(state.data["escalations"]), 1)


if __name__ == "__main__":
    unittest.main()
