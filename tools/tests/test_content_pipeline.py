"""Epic 146: the content pipeline's bookkeeping: digests, the lock, the drift check, staging (no Unreal)."""

import hashlib
import io
import json
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from tools.content_pipeline import pipeline

STEP = {"Id": "level", "Script": "tools/content_pipeline/steps/level.py", "Inputs": ["Data/level.json"],
        "Outputs": ["Content/Maps/Level.umap"]}
CONFIG = {"Library": ["tools/content_pipeline/ue_content.py"], "Steps": [STEP]}
UMAP_BYTES = b"\xc1\x83\x2a\x9e\0binary level"


def pointer_for(data):
    return (f"version https://git-lfs.github.com/spec/v1\noid sha256:{hashlib.sha256(data).hexdigest()}\n"
            f"size {len(data)}\n").encode("utf-8")


class FakeRepo:
    """A repo laid out like this one, with one step built and its lock written."""

    def __init__(self, root):
        self.root = Path(root)
        self.write(pipeline.PIPELINE_FILE, json.dumps(CONFIG))
        self.write("tools/content_pipeline/ue_content.py", "def helper():\n    pass\n")
        self.write(STEP["Script"], "def build(ctx):\n    pass\n")
        self.write("Data/level.json", '{"Size": 1}\n')
        self.write_bytes(STEP["Outputs"][0], UMAP_BYTES)
        lock = pipeline.load_lock(self.root)
        lock["Steps"]["level"] = pipeline.lock_entry(self.root, CONFIG, STEP)
        pipeline.write_lock(self.root, lock)

    def write(self, relative, text):
        self.write_bytes(relative, text.encode("utf-8"))

    def write_bytes(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


class DigestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def digest_of(self, data):
        path = self.root / "file"
        path.write_bytes(data)
        return pipeline.file_digest(path)

    def test_text_digests_the_same_with_either_line_ending(self):
        self.assertEqual(self.digest_of(b"a = 1\r\nb = 2\r\n"), self.digest_of(b"a = 1\nb = 2\n"))

    def test_binary_is_digested_as_it_is(self):
        data = b"\0\r\n\0"
        self.assertEqual(self.digest_of(data), hashlib.sha256(data).hexdigest())

    def test_an_lfs_pointer_digests_as_the_file_it_stands_for(self):
        self.assertEqual(self.digest_of(pointer_for(UMAP_BYTES)), self.digest_of(UMAP_BYTES))

    def test_text_that_merely_mentions_lfs_is_not_a_pointer(self):
        self.assertIsNone(pipeline.lfs_pointer_oid(b"version https://git-lfs.github.com/spec/v1\nno oid here\n"))


class DriftCheckTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = FakeRepo(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_content_just_built_is_current(self):
        self.assertEqual(pipeline.check(self.repo.root), [])

    def test_a_checkout_with_pointer_files_is_current(self):
        self.repo.write_bytes(STEP["Outputs"][0], pointer_for(UMAP_BYTES))
        self.assertEqual(pipeline.check(self.repo.root), [])

    def test_a_windows_checkout_of_the_sources_is_current(self):
        script = self.repo.root / STEP["Script"]
        script.write_bytes(script.read_bytes().replace(b"\n", b"\r\n"))
        self.assertEqual(pipeline.check(self.repo.root), [])

    def test_a_changed_input_is_drift_and_named(self):
        self.repo.write("Data/level.json", '{"Size": 2}\n')
        problems = pipeline.check(self.repo.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("sources changed", problems[0])
        self.assertIn("Data/level.json", problems[0])

    def test_a_changed_shared_helper_is_drift_for_every_step(self):
        self.repo.write("tools/content_pipeline/ue_content.py", "def helper():\n    return 1\n")
        problems = pipeline.check(self.repo.root)
        self.assertTrue(problems and "tools/content_pipeline/ue_content.py" in problems[0])

    def test_a_new_file_in_an_input_directory_is_drift(self):
        config = json.loads(json.dumps(CONFIG))
        config["Steps"][0]["Inputs"] = ["Data"]
        self.repo.write(pipeline.PIPELINE_FILE, json.dumps(config))
        lock = pipeline.load_lock(self.repo.root)
        lock["Steps"]["level"] = pipeline.lock_entry(self.repo.root, config, config["Steps"][0])
        pipeline.write_lock(self.repo.root, lock)
        self.assertEqual(pipeline.check(self.repo.root), [])
        self.repo.write("Data/extra.json", "{}\n")
        problems = pipeline.check(self.repo.root)
        self.assertTrue(problems and "Data/extra.json" in problems[0])

    def test_a_hand_edited_output_is_drift(self):
        self.repo.write_bytes(STEP["Outputs"][0], UMAP_BYTES + b"\0edited")
        problems = pipeline.check(self.repo.root)
        self.assertEqual(len(problems), 1)
        self.assertIn("not what the pipeline wrote", problems[0])

    def test_a_missing_output_is_drift(self):
        (self.repo.root / STEP["Outputs"][0]).unlink()
        self.assertIn("missing", " ".join(pipeline.check(self.repo.root)))

    def test_a_step_never_generated_is_drift(self):
        (self.repo.root / pipeline.LOCK_FILE).unlink()
        self.assertIn("never generated", " ".join(pipeline.check(self.repo.root)))

    def test_a_lock_entry_for_a_removed_step_is_drift(self):
        lock = pipeline.load_lock(self.repo.root)
        lock["Steps"]["gone"] = {"Sources": {}, "Outputs": {}}
        pipeline.write_lock(self.repo.root, lock)
        self.assertIn("gone: in the lock but no longer a step", " ".join(pipeline.check(self.repo.root)))

    def test_writing_an_unchanged_lock_changes_nothing(self):
        self.assertFalse(pipeline.write_lock(self.repo.root, pipeline.load_lock(self.repo.root)))

    def test_the_cli_fails_on_drift_unless_only_reporting(self):
        self.repo.write("Data/level.json", '{"Size": 3}\n')
        original = pipeline.repo_root
        pipeline.repo_root = lambda: self.repo.root
        try:
            with redirect_stdout(io.StringIO()):
                self.assertEqual(pipeline.main(["check"]), 1)
                self.assertEqual(pipeline.main(["check", "--report-only"]), 0)
        finally:
            pipeline.repo_root = original


class SummarizeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def summarize(self, report):
        path = self.root / "report.json"
        if report is not None:
            path.write_text(json.dumps(report), encoding="utf-8")
        summary = self.root / "summary.md"
        with redirect_stdout(io.StringIO()):
            code = pipeline.summarize(path, summary)
        return code, summary.read_text(encoding="utf-8")

    def test_a_build_that_changed_content_passes(self):
        code, text = self.summarize({"Mode": "build", "Succeeded": True,
                                     "Steps": [{"Id": "level", "Changes": ["add Sun"], "Error": None}]})
        self.assertEqual(code, 0)
        self.assertIn("built (1 change(s))", text)

    def test_a_check_that_would_change_content_fails(self):
        code, text = self.summarize({"Mode": "check", "Succeeded": True,
                                     "Steps": [{"Id": "level", "Changes": ["add Sun"], "Error": None}]})
        self.assertEqual(code, 1)
        self.assertIn("drifted", text)

    def test_a_failed_step_fails_and_shows_its_traceback(self):
        code, text = self.summarize({"Mode": "build", "Succeeded": False,
                                     "Steps": [{"Id": "level", "Changes": [], "Error": "Traceback: boom"}]})
        self.assertEqual(code, 1)
        self.assertIn("Traceback: boom", text)

    def test_no_report_fails(self):
        code, text = self.summarize(None)
        self.assertEqual(code, 1)
        self.assertIn("No report", text)


class StageAndApplyTests(unittest.TestCase):
    """stage reads git status; apply puts an artifact into another checkout."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = FakeRepo(Path(self.tmp.name) / "repo")
        self.git("init", "-q")
        self.git("add", "-A")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.com", "commit", "-q", "-m", "base")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        subprocess.run(["git", *args], cwd=self.repo.root, check=True, capture_output=True)

    def test_nothing_changed_stages_nothing(self):
        out = Path(self.tmp.name) / "out"
        self.assertEqual(pipeline.stage(self.repo.root, out), ([], []))
        self.assertFalse(out.exists())

    def test_changes_under_content_and_the_lock_round_trip(self):
        self.repo.write_bytes("Content/Maps/Level.umap", UMAP_BYTES + b"\0v2")
        self.repo.write_bytes("Content/Maps/New.umap", b"\0new")
        lock = pipeline.load_lock(self.repo.root)
        lock["Steps"]["level"] = pipeline.lock_entry(self.repo.root, CONFIG, STEP)
        pipeline.write_lock(self.repo.root, lock)
        self.repo.write("Data/level.json", '{"Size": 9}\n')  # outside Content/: never staged

        out = Path(self.tmp.name) / "out"
        changed, deleted = pipeline.stage(self.repo.root, out)
        self.assertEqual(changed, ["Content/Maps/Level.umap", "Content/Maps/New.umap", pipeline.LOCK_FILE])
        self.assertEqual(deleted, [])

        other = FakeRepo(Path(self.tmp.name) / "other")
        self.assertEqual(pipeline.apply(other.root, out), (changed, []))
        self.assertEqual((other.root / "Content/Maps/New.umap").read_bytes(), b"\0new")
        self.assertEqual((other.root / "Content/Maps/Level.umap").read_bytes(), UMAP_BYTES + b"\0v2")

    def test_a_deleted_asset_is_deleted_on_apply(self):
        (self.repo.root / "Content/Maps/Level.umap").unlink()
        out = Path(self.tmp.name) / "out"
        self.assertEqual(pipeline.stage(self.repo.root, out), ([], ["Content/Maps/Level.umap"]))
        other = FakeRepo(Path(self.tmp.name) / "other")
        pipeline.apply(other.root, out)
        self.assertFalse((other.root / "Content/Maps/Level.umap").exists())

    def test_apply_refuses_paths_outside_content(self):
        out = Path(self.tmp.name) / "out"
        out.mkdir()
        (out / "changed.txt").write_text("Source/Evil.cpp\n", encoding="utf-8")
        with self.assertRaises(ValueError):
            pipeline.apply(self.repo.root, out)


class RepoPipelineTests(unittest.TestCase):
    """The repo's own pipeline.json is sound (whether its content is current is the drift check's job)."""

    def test_every_step_is_well_formed(self):
        repo = pipeline.repo_root()
        config = pipeline.load_pipeline(repo)
        ids = [step["Id"] for step in config["Steps"]]
        self.assertEqual(len(ids), len(set(ids)), "step ids must be unique")
        for relative in config.get("Library", []):
            self.assertTrue((repo / relative).is_file(), relative)
        owned = []
        for step in config["Steps"]:
            self.assertTrue((repo / step["Script"]).is_file(), step["Script"])
            for relative in step.get("Inputs", []):
                self.assertTrue((repo / relative).exists(), f"{step['Id']} reads {relative}, which doesn't exist")
            for relative in step["Outputs"]:
                self.assertTrue(relative.startswith("Content/") and relative.endswith((".umap", ".uasset")),
                                f"{step['Id']} output {relative} must be an asset under Content/")
                owned.append(relative)
        self.assertEqual(len(owned), len(set(owned)), "an asset has one writer: no two steps own it")


if __name__ == "__main__":
    unittest.main()
