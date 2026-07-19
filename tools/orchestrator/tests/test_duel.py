"""Epic 137: score_lib local-diff path, judge parsing, winner decision, and a
full dry-run duel in a temp git repo with scripted workers. No network."""

import importlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.orchestrator.duel import (
    Contestant,
    decide_winner,
    parse_judge_verdict,
    run_duel,
)
from tools.orchestrator.models.base import ChatResponse, ToolCall, Usage

score_lib = importlib.import_module("tools.score_lib")

ROADMAP_FIXTURE = """\
### Epic 900: Fixture Epic

**Goal:** Exists for tests.

- [ ] Open story: write the fixture output file
"""


def make_git_repo(path: Path) -> None:
    def git(*args):
        subprocess.run(["git", *args], cwd=str(path), check=True,
                       capture_output=True)

    git("init", "-q", "-b", "main")
    git("config", "user.email", "test@test.local")
    git("config", "user.name", "test")
    (path / "ROADMAP.md").write_text(ROADMAP_FIXTURE, encoding="utf-8")
    git("add", "-A")
    git("commit", "-q", "-m", "init")


class ScoreLibTests(unittest.TestCase):
    def test_local_diff_scores_without_ci(self):
        scores, composite = score_lib.score_local_diff(
            ["roadmap/x.md", "Source/PlaySports/Private/Tests/FooTest.cpp"],
            commits=1)
        self.assertIsNone(scores["ci_green"])
        self.assertTrue(scores["roadmap_tick"])
        self.assertEqual(scores["local_max"], 60)
        self.assertLessEqual(composite, 60)

    def test_manual_recall(self):
        scores, composite = score_lib.score_manual_recall("2/3")
        self.assertEqual(composite, 67)
        self.assertAlmostEqual(scores["recall"], 2 / 3)

    def test_pr_facts_composite_matches_original_rubric(self):
        scores, composite = score_lib.score_pr_facts(
            ["roadmap/x.md"], commits=1, ci_green=True)
        # 40 CI + 20 scope + 10 tests(not required) + 10 tick + 10 efficiency
        # + lint/data depends on the real repo tools, which pass here
        self.assertTrue(scores["ci_green"])
        self.assertGreaterEqual(composite, 90)


class JudgeParsingTests(unittest.TestCase):
    def test_parses_clean_json(self):
        winner, reasoning = parse_judge_verdict(
            '{"winner": "b", "reasoning": "tighter diff"}')
        self.assertEqual(winner, "b")
        self.assertEqual(reasoning, "tighter diff")

    def test_parses_json_wrapped_in_prose(self):
        winner, _ = parse_judge_verdict(
            'Sure! Here is my verdict:\n{"winner": "a", "reasoning": "x"}\n')
        self.assertEqual(winner, "a")

    def test_garbage_defaults_to_tie(self):
        winner, reasoning = parse_judge_verdict("no json here")
        self.assertEqual(winner, "tie")
        self.assertIn("unparseable", reasoning)


class DecideWinnerTests(unittest.TestCase):
    def contestant(self, key, status="finished", composite=50, cost_rank=0):
        c = Contestant(key=key, client=None, cost_rank=cost_rank)
        c.status = status
        c.local_composite = composite
        return c

    def test_only_finisher_wins(self):
        a = self.contestant("a", status="budget_exhausted")
        b = self.contestant("b")
        self.assertEqual(decide_winner(a, b, "a")[0], "b")

    def test_neither_finished(self):
        a = self.contestant("a", status="provider_error")
        b = self.contestant("b", status="budget_exhausted")
        self.assertEqual(decide_winner(a, b, "tie")[0], "none")

    def test_objective_margin_beats_judge(self):
        a = self.contestant("a", composite=55)
        b = self.contestant("b", composite=30)
        winner, basis = decide_winner(a, b, "b")
        self.assertEqual(winner, "a")
        self.assertEqual(basis, "objective score margin")

    def test_judge_decides_inside_margin(self):
        a = self.contestant("a", composite=50)
        b = self.contestant("b", composite=45)
        self.assertEqual(decide_winner(a, b, "b")[0], "b")

    def test_tie_goes_to_cheaper(self):
        a = self.contestant("a", composite=50, cost_rank=0)
        b = self.contestant("b", composite=50, cost_rank=1)
        winner, basis = decide_winner(a, b, "tie")
        self.assertEqual(winner, "a")
        self.assertEqual(basis, "tie -> cheaper model")


class ScriptedWorker:
    """Writes one file then finishes; content differs per worker."""

    def __init__(self, label, content):
        self.label = label
        self.content = content
        self._step = 0

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self._step += 1
        if self._step == 1:
            return ChatResponse(text="", usage=Usage(5, 5), tool_calls=[
                ToolCall(id="1", name="write_file",
                         arguments={"path": "roadmap/output.md",
                                    "content": self.content})])
        return ChatResponse(text="", usage=Usage(5, 5), tool_calls=[
            ToolCall(id="2", name="finish",
                     arguments={"summary": f"{self.label} done"})])


class ScriptedJudge:
    label = "fake:judge"

    def __init__(self, verdict='{"winner": "b", "reasoning": "better"}'):
        self.verdict = verdict
        self.prompts = []

    def chat(self, messages, tools=None, temperature=0.2, max_tokens=8192):
        self.prompts.append(messages)
        return ChatResponse(text=self.verdict, usage=Usage(1, 1))


class DryRunDuelTests(unittest.TestCase):
    def test_full_dry_run_duel(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "repo"
            repo.mkdir()
            make_git_repo(repo)
            judge = ScriptedJudge()

            # scoring shells out to tools/lint_conventions.py etc. in the repo
            # root; stub them so the temp repo passes hygiene checks
            (repo / "tools").mkdir()
            for stub in ("lint_conventions.py", "validate_data.py"):
                (repo / "tools" / stub).write_text("import sys; sys.exit(0)\n",
                                                   encoding="utf-8")

            result = run_duel(
                repo, "900.1",
                worker_clients=[ScriptedWorker("fake:a", "version a\n"),
                                ScriptedWorker("fake:b", "version b\n")],
                judge_client=judge, dry_run=True, max_iterations=5)

            self.assertEqual(result.winner, "b")  # judge said b, scores tied
            record = json.loads(Path(result.record_path).read_text(
                encoding="utf-8"))
            self.assertEqual(record["story"], "900.1")
            self.assertEqual(len(record["contestants"]), 2)
            self.assertEqual(record["contestants"][1]["status"], "finished")
            self.assertGreater(record["contestants"][0]["local_composite"], 0)
            self.assertEqual(record["pr"], "")  # dry run: no PR
            self.assertEqual(len(judge.prompts), 1)
            # both duel worktrees cleaned up
            self.assertFalse((repo / ".worktrees").exists()
                             and any((repo / ".worktrees").iterdir()))


if __name__ == "__main__":
    unittest.main()
