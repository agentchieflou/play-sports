"""Isolated git-worktree lifecycle for worker runs (Epic 136).

Each run gets `.worktrees/<branch-slug>` off the base branch. The harness
commits/pushes; the model only sees the jailed file tools.
"""

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

GIT_TIMEOUT_S = 120


class GitError(RuntimeError):
    pass


def run_git(args: list[str], cwd: Path, check: bool = True) -> str:
    result = subprocess.run(
        ["git", *args], cwd=str(cwd), capture_output=True, text=True,
        timeout=GIT_TIMEOUT_S,
    )
    if check and result.returncode != 0:
        raise GitError(
            f"git {' '.join(args)} failed ({result.returncode}): "
            f"{result.stderr.strip() or result.stdout.strip()}"
        )
    return result.stdout


class Workspace:
    """One worker's isolated worktree.

    branch follows the git-steward convention: epic-<N>/<short-kebab-slug>
    (optionally suffixed by the duel runner to keep contestants apart).
    """

    def __init__(self, repo_root: Path, branch: str, base: str = "main"):
        self.repo_root = Path(repo_root)
        self.branch = branch
        self.base = base
        slug = branch.replace("/", "-")
        self.path = self.repo_root / ".worktrees" / slug

    def create(self) -> Path:
        if self.path.exists():
            raise GitError(f"worktree path already exists: {self.path}")
        self.path.parent.mkdir(parents=True, exist_ok=True)
        run_git(["worktree", "add", str(self.path), "-b", self.branch, self.base],
                cwd=self.repo_root)
        return self.path

    def commit_all(self, message: str) -> bool:
        """Stage and commit everything in the worktree. Returns False when
        there was nothing to commit."""
        run_git(["add", "-A"], cwd=self.path)
        status = run_git(["status", "--porcelain"], cwd=self.path)
        if not status.strip():
            return False
        run_git(["commit", "-m", message], cwd=self.path)
        return True

    def diff_against_base(self) -> str:
        """Committed diff against the merge-base with the base branch."""
        return run_git(["diff", f"{self.base}...HEAD"], cwd=self.path)

    def changed_files(self) -> list[str]:
        output = run_git(["diff", "--name-only", f"{self.base}...HEAD"],
                         cwd=self.path)
        return [line for line in output.splitlines() if line.strip()]

    def push(self) -> None:
        run_git(["push", "-u", "origin", self.branch], cwd=self.path)

    def remove(self, delete_branch: bool = False) -> None:
        if self.path.exists():
            try:
                run_git(["worktree", "remove", "--force", str(self.path)],
                        cwd=self.repo_root)
            except GitError:
                shutil.rmtree(self.path, ignore_errors=True)
                run_git(["worktree", "prune"], cwd=self.repo_root, check=False)
        if delete_branch:
            run_git(["branch", "-D", self.branch], cwd=self.repo_root,
                    check=False)
