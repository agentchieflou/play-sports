#!/usr/bin/env python
"""The content pipeline's bookkeeping, without Unreal (Epic 146, Specs/ADR_Content_Pipeline.md).

The pipeline builds the project's binary content (levels, imported meshes) with Python steps that
run inside the editor, headlessly, on the CI runner (run_in_editor.py). This module is the part
that needs no editor: which steps exist, what each one reads and writes, and whether the
committed content still matches its sources. Plain Python 3, standard library only, so the
editor's interpreter imports it too.

  pipeline.json        The steps: each one's script, the files it reads (Inputs) and the assets
                       it owns (Outputs). The helper library (Library) counts as every step's
                       source.
  content.lock.json    Written by the pipeline, never by hand: for each step, the digest of every
                       source it was last built from and of every output it wrote.

A digest is the SHA-256 of a file's bytes, which for a file in Git LFS is its LFS object id, so a
pointer file (an LFS checkout skipped) digests the same as the real file. Text is digested with
CRLF read as LF, so a Windows checkout and a Linux one agree.

Commands (run from the repo root):

  check [--report-only]          The fast drift check: every step's sources unchanged since its
                                 content was generated, and every output present with the bytes
                                 the pipeline wrote. Exit 1 on drift unless --report-only.
  summarize REPORT [--summary F] Print an editor run's report (run_in_editor.py) and append it
                                 to F (the job summary). Exit 1 if the run failed, or if a check
                                 run found content to change.
  stage OUT_DIR                  Copy what an editor run changed under Content/, and the lock,
                                 into OUT_DIR (the generated-content artifact), with changed.txt
                                 and deleted.txt listing them. Uses git status.
  apply ARTIFACT_DIR             Copy a downloaded generated-content artifact into the repo and
                                 delete what it lists as deleted. Then commit: .gitattributes
                                 routes the assets through Git LFS.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

PIPELINE_FILE = "tools/content_pipeline/pipeline.json"
LOCK_FILE = "tools/content_pipeline/content.lock.json"
LFS_POINTER_PREFIX = b"version https://git-lfs.github.com/spec/v1"
LOCK_COMMENT = ("Written by the content pipeline (tools/content_pipeline, Specs/ADR_Content_Pipeline.md). "
                "Never edit it by hand: run the Content workflow and commit its generated-content artifact.")


def repo_root():
    """The repository root: two levels above this file."""
    return Path(__file__).resolve().parent.parent.parent


def load_pipeline(repo):
    with open(Path(repo) / PIPELINE_FILE, encoding="utf-8") as handle:
        return json.load(handle)


def load_lock(repo):
    """The lock, or an empty one when it doesn't exist yet."""
    path = Path(repo) / LOCK_FILE
    if not path.is_file():
        return {"Comment": LOCK_COMMENT, "Steps": {}}
    with open(path, encoding="utf-8") as handle:
        lock = json.load(handle)
    lock.setdefault("Steps", {})
    return lock


def write_lock(repo, lock):
    """Writes the lock with sorted keys and LF line endings; True if its content changed."""
    lock = dict(lock)
    lock["Comment"] = LOCK_COMMENT
    text = json.dumps(lock, indent=4, sort_keys=True) + "\n"
    path = Path(repo) / LOCK_FILE
    if path.is_file() and path.read_bytes().replace(b"\r\n", b"\n") == text.encode("utf-8"):
        return False
    path.write_bytes(text.encode("utf-8"))
    return True


def find_step(config, step_id):
    for step in config.get("Steps", []):
        if step.get("Id") == step_id:
            return step
    return None


def lfs_pointer_oid(data):
    """The object id an LFS pointer file names, or None when data isn't a pointer."""
    if len(data) > 1024 or not data.startswith(LFS_POINTER_PREFIX):
        return None
    for line in data.decode("utf-8", errors="replace").splitlines():
        if line.startswith("oid sha256:"):
            return line[len("oid sha256:"):].strip()
    return None


def file_digest(path):
    """SHA-256 of a file: an LFS pointer's oid, text with CRLF read as LF, binary as it is."""
    data = Path(path).read_bytes()
    oid = lfs_pointer_oid(data)
    if oid:
        return oid
    if b"\0" not in data[:8000]:
        data = data.replace(b"\r\n", b"\n")
    return hashlib.sha256(data).hexdigest()


def expand_paths(repo, relative_paths):
    """Repo-relative POSIX paths of the files named, a directory standing for every file in it."""
    repo = Path(repo)
    files = set()
    for relative in relative_paths:
        path = repo / relative
        if path.is_dir():
            for child in path.rglob("*"):
                if child.is_file() and "__pycache__" not in child.parts:
                    files.add(child.relative_to(repo).as_posix())
        else:
            files.add(Path(relative).as_posix())
    return sorted(files)


def step_sources(config, step):
    """Everything a step's content depends on: the helper library, its script and its inputs."""
    return list(config.get("Library", [])) + [step["Script"]] + list(step.get("Inputs", []))


def source_digests(repo, config, step):
    """{path: digest} over a step's sources; a missing source digests as "missing"."""
    digests = {}
    for relative in expand_paths(repo, step_sources(config, step)):
        path = Path(repo) / relative
        digests[relative] = file_digest(path) if path.is_file() else "missing"
    return digests


def output_digests(repo, step):
    """{path: digest} over a step's outputs; a missing output digests as "missing"."""
    digests = {}
    for relative in step.get("Outputs", []):
        path = Path(repo) / relative
        digests[relative] = file_digest(path) if path.is_file() else "missing"
    return digests


def lock_entry(repo, config, step):
    """What the lock records for a step just built: its sources' and its outputs' digests."""
    return {"Sources": source_digests(repo, config, step), "Outputs": output_digests(repo, step)}


def check(repo):
    """The fast drift check: a list of problems, empty when every step's content is current."""
    repo = Path(repo)
    config = load_pipeline(repo)
    lock = load_lock(repo)
    problems = []
    for step in config.get("Steps", []):
        step_id = step["Id"]
        recorded = lock["Steps"].get(step_id)
        if recorded is None:
            problems.append(f"{step_id}: never generated (no entry in {LOCK_FILE})")
            continue
        current = source_digests(repo, config, step)
        before = recorded.get("Sources", {})
        changed = sorted(path for path in set(current) | set(before) if current.get(path) != before.get(path))
        if changed:
            problems.append(f"{step_id}: sources changed since its content was generated: {', '.join(changed)}")
        outputs = output_digests(repo, step)
        for path, digest in sorted(outputs.items()):
            wrote = recorded.get("Outputs", {}).get(path)
            if digest == "missing":
                problems.append(f"{step_id}: output {path} is missing")
            elif wrote != digest:
                problems.append(f"{step_id}: output {path} is not what the pipeline wrote "
                                "(edited by hand, or committed without its lock)")
    for step_id in sorted(set(lock["Steps"]) - {step["Id"] for step in config.get("Steps", [])}):
        problems.append(f"{step_id}: in the lock but no longer a step in {PIPELINE_FILE}")
    return problems


def summarize(report_path, summary_path=None):
    """Prints an editor run's report; returns the exit code (0 when it passed)."""
    report_path = Path(report_path)
    if not report_path.is_file():
        lines = ["## Content pipeline", "", f"No report at `{report_path}`: the editor never ran the pipeline "
                 "(the editor log in the content-logs artifact says why)."]
        code = 1
    else:
        with open(report_path, encoding="utf-8-sig") as handle:
            report = json.load(handle)
        mode = report.get("Mode", "?")
        lines = ["## Content pipeline", "", f"Mode: **{mode}**", ""]
        code = 0 if report.get("Succeeded") else 1
        for step in report.get("Steps", []):
            changes = step.get("Changes", [])
            if step.get("Error"):
                state = "failed"
            elif not changes:
                state = "up to date"
            elif mode == "check":
                state = f"drifted ({len(changes)} difference(s))"
                code = 1
            else:
                state = f"built ({len(changes)} change(s))"
            lines.append(f"- `{step.get('Id')}`: {state}")
            lines.extend(f"  - {change}" for change in changes)
            if step.get("Error"):
                lines.append("")
                lines.append("```")
                lines.extend(step["Error"].rstrip().splitlines())
                lines.append("```")
    text = "\n".join(lines) + "\n"
    print(text)
    if summary_path:
        with open(summary_path, "a", encoding="utf-8") as handle:
            handle.write(text)
    return code


def changed_paths(repo):
    """(changed, deleted): repo-relative paths under Content/, and the lock, that differ from HEAD."""
    output = subprocess.run(
        ["git", "status", "--porcelain=v1", "-z", "--untracked-files=all", "--", "Content", LOCK_FILE],
        cwd=repo, check=True, capture_output=True).stdout.decode("utf-8")
    changed, deleted = [], []
    entries = output.split("\0")
    index = 0
    while index < len(entries):
        entry = entries[index]
        index += 1
        if len(entry) < 4:
            continue
        status, path = entry[:2], entry[3:]
        if status[0] in "RC":
            index += 1  # a rename's source path follows; the destination is what changed
        if "D" in status:
            deleted.append(path)
        else:
            changed.append(path)
    return sorted(changed), sorted(deleted)


def stage(repo, out_dir):
    """Copies what an editor run changed into out_dir; returns (changed, deleted)."""
    repo, out_dir = Path(repo), Path(out_dir)
    changed, deleted = changed_paths(repo)
    if not changed and not deleted:
        return changed, deleted
    out_dir.mkdir(parents=True, exist_ok=True)
    for relative in changed:
        target = out_dir / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo / relative, target)
    (out_dir / "changed.txt").write_text("".join(f"{path}\n" for path in changed), encoding="utf-8")
    (out_dir / "deleted.txt").write_text("".join(f"{path}\n" for path in deleted), encoding="utf-8")
    return changed, deleted


def apply(repo, artifact_dir):
    """Copies a generated-content artifact into the repo; returns (changed, deleted)."""
    repo, artifact_dir = Path(repo), Path(artifact_dir)

    def listed(name):
        path = artifact_dir / name
        return [line.strip() for line in path.read_text(encoding="utf-8").splitlines() if line.strip()] if path.is_file() else []

    changed, deleted = listed("changed.txt"), listed("deleted.txt")
    for relative in changed + deleted:
        if relative != LOCK_FILE and not relative.startswith("Content/"):
            raise ValueError(f"the artifact names {relative}, outside Content/ and the lock")
    for relative in changed:
        target = repo / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(artifact_dir / relative, target)
    for relative in deleted:
        (repo / relative).unlink(missing_ok=True)
    return changed, deleted


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    check_parser = commands.add_parser("check")
    check_parser.add_argument("--report-only", action="store_true")
    summarize_parser = commands.add_parser("summarize")
    summarize_parser.add_argument("report")
    summarize_parser.add_argument("--summary")
    stage_parser = commands.add_parser("stage")
    stage_parser.add_argument("out_dir")
    apply_parser = commands.add_parser("apply")
    apply_parser.add_argument("artifact_dir")
    args = parser.parse_args(argv)
    repo = repo_root()

    if args.command == "check":
        problems = check(repo)
        if not problems:
            print("Content is current: every step's sources and outputs match content.lock.json.")
            return 0
        print("Content drift:" if not args.report_only else "Content drift (reported, not failing):")
        for problem in problems:
            print(f"  - {problem}")
        print("Run the Content workflow (it runs on any pull request that changes the pipeline or "
              "Content/) and commit its generated-content artifact: python tools/content_pipeline/pipeline.py apply <dir>.")
        return 0 if args.report_only else 1
    if args.command == "summarize":
        return summarize(args.report, args.summary or os.environ.get("GITHUB_STEP_SUMMARY"))
    if args.command == "stage":
        changed, deleted = stage(repo, args.out_dir)
        if not changed and not deleted:
            print("The pipeline changed nothing: the branch's content is current.")
        for path in changed:
            print(f"changed: {path}")
        for path in deleted:
            print(f"deleted: {path}")
        return 0
    if args.command == "apply":
        changed, deleted = apply(repo, args.artifact_dir)
        for path in changed:
            print(f"copied: {path}")
        for path in deleted:
            print(f"deleted: {path}")
        print("Now commit them (git add Content tools/content_pipeline/content.lock.json); "
              ".gitattributes routes the assets through Git LFS. Push the LFS objects first if "
              "your clone has no pre-push hook: git lfs push origin <branch>.")
        return 0
    return 2


if __name__ == "__main__":
    sys.exit(main())
