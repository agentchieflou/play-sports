"""Epic 136: jailed tools, worktree lifecycle, story lookup, and the harness
loop driven by a scripted fake model. No network; git runs in temp repos."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.orchestrator.models.base import ChatResponse, ToolCall, Usage
from tools.orchestrator.worker.harness import WorkerHarness
from tools.orchestrator.worker.prompts import StoryAssignment, build_system_prompt
from tools.orchestrator.worker.stories import default_branch_name, find_story
from tools.orchestrator.worker.tools import MAX_WRITES_PER_RUN, WorkerTools
from tools.orchestrator.worker.workspace import GitError, Workspace


def make_git_repo(path: Path) -> None:
    def git(*args):
        subprocess.run(["git", *args], cwd=str(path), check=True,
                       capture_output=True)

    git("init", "-q", "-b", "main")
    git("config", "user.email", "test@test.local")
    git("config", "user.name", "test")
    (path / "README.md").write_text("hello\n", encoding="utf-8")
    git("add", "-A")
    git("commit", "-q", "-m", "init")


class WorkerToolsTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "src").mkdir()
        (self.root / "src" / "a.txt").write_text("alpha\nneedle here\n",
                                                 encoding="utf-8")
        self.tools = WorkerTools(self.root)

    def tearDown(self):
        self._tmp.cleanup()

    def test_read_write_roundtrip(self):
        self.tools.write_file("src/b.txt", "beta\n")
        self.assertEqual(self.tools.read_file("src/b.txt"), "beta\n")

    def test_path_escape_rejected(self):
        result = self.tools.dispatch(
            ToolCall(id="1", name="read_file",
                     arguments={"path": "../../outside.txt"}))
        self.assertTrue(result.startswith("ERROR:"), result)

    def test_write_cap_enforced(self):
        self.tools.writes = MAX_WRITES_PER_RUN
        result = self.tools.dispatch(
            ToolCall(id="1", name="write_file",
                     arguments={"path": "x.txt", "content": "y"}))
        self.assertIn("write cap", result)

    def test_grep_and_list(self):
        self.assertIn("src/a.txt:2:needle here",
                      self.tools.grep("needle"))
        self.assertIn("src/", self.tools.list_dir(""))

    def test_unknown_check_rejected(self):
        result = self.tools.dispatch(
            ToolCall(id="1", name="run_check", arguments={"check": "rm-rf"}))
        self.assertTrue(result.startswith("ERROR:"), result)


class WorkspaceTests(unittest.TestCase):
    def test_lifecycle_create_commit_diff_remove(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            make_git_repo(repo)
            workspace = Workspace(repo, branch="epic-t/test-story")
            worktree = workspace.create()
            self.assertTrue((worktree / "README.md").is_file())

            (worktree / "new.txt").write_text("added\n", encoding="utf-8")
            self.assertTrue(workspace.commit_all("test commit"))
            self.assertIn("new.txt", workspace.changed_files())
            self.assertIn("+added", workspace.diff_against_base())
            self.assertFalse(workspace.commit_all("empty"))  # nothing left

            workspace.remove(delete_branch=True)
            self.assertFalse(worktree.exists())

    def test_duplicate_worktree_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            make_git_repo(repo)
            workspace = Workspace(repo, branch="epic-t/dup")
            workspace.create()
            with self.assertRaises(GitError):
                Workspace(repo, branch="epic-t/dup").create()
            workspace.remove(delete_branch=True)


ROADMAP_FIXTURE = """\
# Track T — Test (Epics 900–901)

### Epic 900: Fixture Epic

**Size/Mode:** S / code
**Goal:** Exists for tests.
**Depends on:** —

- [x] Already done story
- [ ] Open story: add the `PSWidget` thing
"""


class StoryLookupTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "roadmap").mkdir()
        (self.root / "ROADMAP.md").write_text("# ROADMAP\n", encoding="utf-8")
        (self.root / "roadmap" / "test-track.md").write_text(
            ROADMAP_FIXTURE, encoding="utf-8")

    def tearDown(self):
        self._tmp.cleanup()

    def test_finds_open_story(self):
        assignment = find_story(self.root, "900.2")
        self.assertEqual(assignment.epic_title, "Fixture Epic")
        self.assertIn("PSWidget", assignment.story_text)
        self.assertEqual(assignment.track_file, "roadmap/test-track.md")
        branch = default_branch_name(assignment)
        self.assertTrue(branch.startswith("epic-900/"))
        self.assertNotIn(" ", branch)

    def test_checked_story_refused(self):
        with self.assertRaises(ValueError):
            find_story(self.root, "900.1")

    def test_missing_epic(self):
        with self.assertRaises(ValueError):
            find_story(self.root, "999.1")

    def test_bad_id(self):
        with self.assertRaises(ValueError):
            find_story(self.root, "nonsense")


class ScriptedClient:
    """Fake model: emits a fixed sequence of responses."""

    label = "fake:scripted"

    def __init__(self, responses):
        self.responses = list(responses)
        self.seen_messages = []

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.seen_messages.append(list(messages))
        return self.responses.pop(0)


def tool_response(*calls):
    return ChatResponse(text="", tool_calls=list(calls), usage=Usage(10, 5))


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.tools = WorkerTools(self.root)
        self.assignment = StoryAssignment(
            epic="900", story_index=2, story_text="Open story",
            epic_title="Fixture Epic", specialization="data-content-author")

    def tearDown(self):
        self._tmp.cleanup()

    def make_harness(self, client, **kwargs):
        return WorkerHarness(client, self.tools, repo_root=self.root,
                             transcript_dir=self.root / "transcripts", **kwargs)

    def test_write_then_finish(self):
        client = ScriptedClient([
            tool_response(ToolCall(id="1", name="write_file",
                                   arguments={"path": "out.txt",
                                              "content": "done\n"})),
            tool_response(ToolCall(id="2", name="finish",
                                   arguments={"summary": "wrote out.txt"})),
        ])
        result = self.make_harness(client).run(self.assignment)
        self.assertEqual(result.status, "finished")
        self.assertEqual(result.summary, "wrote out.txt")
        self.assertEqual((self.root / "out.txt").read_text(encoding="utf-8"),
                         "done\n")
        self.assertTrue(Path(result.transcript_path).is_file())
        self.assertEqual(result.usage.prompt_tokens, 20)

    def test_text_only_response_gets_nudged(self):
        client = ScriptedClient([
            ChatResponse(text="thinking out loud", usage=Usage(1, 1)),
            tool_response(ToolCall(id="1", name="finish",
                                   arguments={"summary": "ok"})),
        ])
        result = self.make_harness(client).run(self.assignment)
        self.assertEqual(result.status, "finished")
        final_messages = client.seen_messages[-1]
        self.assertIn("tool calls only", final_messages[-1].content)

    def test_iteration_budget(self):
        endless = tool_response(ToolCall(id="1", name="list_dir",
                                         arguments={}))
        client = ScriptedClient([endless] * 3)
        result = self.make_harness(client, max_iterations=3).run(self.assignment)
        self.assertEqual(result.status, "budget_exhausted")
        self.assertEqual(result.iterations, 3)

    def test_system_prompt_includes_harness_rules(self):
        prompt = build_system_prompt(self.root, self.assignment)
        self.assertIn("exactly ONE roadmap story", prompt)


if __name__ == "__main__":
    unittest.main()
