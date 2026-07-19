"""Model-facing tools, jailed to one worktree (Epic 136).

Safety rails: every path resolves inside the worktree root; per-file size
caps; a per-run write cap; `run_check` executes only an allowlisted set of
repo checks. The model gets no shell and no git.
"""

from __future__ import annotations

import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path

from ..models.base import ToolCall, ToolSpec

MAX_READ_BYTES = 200_000
MAX_WRITE_BYTES = 200_000
MAX_WRITES_PER_RUN = 40
MAX_GREP_MATCHES = 100
MAX_LIST_ENTRIES = 200
CHECK_TIMEOUT_S = 600
OUTPUT_TRUNCATE = 20_000

# Allowlisted checks, run with the worktree as cwd.
CHECKS: dict[str, list[str]] = {
    "lint_conventions": [sys.executable, "tools/lint_conventions.py"],
    "validate_data": [sys.executable, "tools/validate_data.py"],
    "unittest": [sys.executable, "-m", "unittest", "discover",
                 "-s", "tools/orchestrator/tests"],
}

SKIP_DIRS = {".git", ".worktrees", "Saved", "Intermediate", "DerivedDataCache",
             "Binaries", "__pycache__"}

FINISH_TOOL = "finish"

TOOL_SPECS: list[ToolSpec] = [
    ToolSpec("read_file", "Read a UTF-8 text file (repo-relative path).",
             {"type": "object",
              "properties": {"path": {"type": "string"}},
              "required": ["path"]}),
    ToolSpec("write_file", "Create or overwrite a UTF-8 text file "
             "(repo-relative path). Parent dirs are created.",
             {"type": "object",
              "properties": {"path": {"type": "string"},
                             "content": {"type": "string"}},
              "required": ["path", "content"]}),
    ToolSpec("list_dir", "List a directory (repo-relative path; '' = root). "
             "Directories end with '/'.",
             {"type": "object",
              "properties": {"path": {"type": "string"}}}),
    ToolSpec("grep", "Regex-search file contents. Optional glob filters file "
             "paths (e.g. '*.cpp', 'Source/**'). Returns path:line:text.",
             {"type": "object",
              "properties": {"pattern": {"type": "string"},
                             "glob": {"type": "string"}},
              "required": ["pattern"]}),
    ToolSpec("run_check", "Run an allowlisted repo check: one of "
             "lint_conventions | validate_data | unittest.",
             {"type": "object",
              "properties": {"check": {"type": "string"}},
              "required": ["check"]}),
    ToolSpec(FINISH_TOOL, "Declare the story complete. Summarize what you "
             "changed and how you verified it.",
             {"type": "object",
              "properties": {"summary": {"type": "string"}},
              "required": ["summary"]}),
]


class ToolError(RuntimeError):
    """Recoverable tool failure - reported back to the model as the result."""


class WorkerTools:
    def __init__(self, root: Path):
        self.root = Path(root).resolve()
        self.writes = 0

    # -- path jail ----------------------------------------------------------

    def _resolve(self, raw: str) -> Path:
        candidate = (self.root / str(raw)).resolve()
        if candidate != self.root and self.root not in candidate.parents:
            raise ToolError(f"path escapes the worktree: {raw!r}")
        return candidate

    # -- tools --------------------------------------------------------------

    def read_file(self, path: str) -> str:
        target = self._resolve(path)
        if not target.is_file():
            raise ToolError(f"not a file: {path!r}")
        if target.stat().st_size > MAX_READ_BYTES:
            raise ToolError(f"file exceeds {MAX_READ_BYTES} bytes: {path!r}")
        return target.read_text(encoding="utf-8", errors="replace")

    def write_file(self, path: str, content: str) -> str:
        if self.writes >= MAX_WRITES_PER_RUN:
            raise ToolError(f"write cap reached ({MAX_WRITES_PER_RUN} files/run)")
        if len(content.encode("utf-8")) > MAX_WRITE_BYTES:
            raise ToolError(f"content exceeds {MAX_WRITE_BYTES} bytes")
        target = self._resolve(path)
        if ".git" in target.relative_to(self.root).parts:
            raise ToolError("writing into .git is not allowed")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8", newline="\n")
        self.writes += 1
        return f"wrote {path} ({len(content)} chars)"

    def list_dir(self, path: str = "") -> str:
        target = self._resolve(path or ".")
        if not target.is_dir():
            raise ToolError(f"not a directory: {path!r}")
        entries = []
        for child in sorted(target.iterdir()):
            if child.name in SKIP_DIRS:
                continue
            entries.append(child.name + ("/" if child.is_dir() else ""))
            if len(entries) >= MAX_LIST_ENTRIES:
                entries.append("... (truncated)")
                break
        return "\n".join(entries) or "(empty)"

    def grep(self, pattern: str, glob: str = "") -> str:
        try:
            regex = re.compile(pattern)
        except re.error as error:
            raise ToolError(f"bad regex: {error}") from error
        matches: list[str] = []
        for file_path in self._walk_files():
            relative = file_path.relative_to(self.root).as_posix()
            if glob and not (fnmatch.fnmatch(relative, glob)
                             or fnmatch.fnmatch(file_path.name, glob)):
                continue
            try:
                text = file_path.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for line_number, line in enumerate(text.splitlines(), 1):
                if regex.search(line):
                    matches.append(f"{relative}:{line_number}:{line.strip()[:200]}")
                    if len(matches) >= MAX_GREP_MATCHES:
                        matches.append("... (truncated)")
                        return "\n".join(matches)
        return "\n".join(matches) or "(no matches)"

    def _walk_files(self):
        stack = [self.root]
        while stack:
            directory = stack.pop()
            try:
                children = sorted(directory.iterdir())
            except OSError:
                continue
            for child in children:
                if child.is_dir():
                    if child.name not in SKIP_DIRS:
                        stack.append(child)
                elif child.stat().st_size <= MAX_READ_BYTES:
                    yield child

    def run_check(self, check: str) -> str:
        if check not in CHECKS:
            raise ToolError(f"unknown check {check!r}; allowed: {sorted(CHECKS)}")
        command = CHECKS[check]
        if check == "unittest" and not (self.root / "tools/orchestrator/tests").is_dir():
            raise ToolError("no tools/orchestrator/tests directory in this worktree")
        result = subprocess.run(command, cwd=str(self.root), capture_output=True,
                                text=True, timeout=CHECK_TIMEOUT_S)
        output = (result.stdout + result.stderr)[:OUTPUT_TRUNCATE]
        return f"exit={result.returncode}\n{output}"

    # -- dispatch -----------------------------------------------------------

    def dispatch(self, call: ToolCall) -> str:
        """Execute one tool call; errors come back as text so the model can
        correct itself instead of crashing the run."""
        handlers = {
            "read_file": lambda a: self.read_file(a["path"]),
            "write_file": lambda a: self.write_file(a["path"], a["content"]),
            "list_dir": lambda a: self.list_dir(a.get("path", "")),
            "grep": lambda a: self.grep(a["pattern"], a.get("glob", "")),
            "run_check": lambda a: self.run_check(a["check"]),
        }
        if call.name not in handlers:
            return f"ERROR: unknown tool {call.name!r}"
        try:
            return handlers[call.name](call.arguments)
        except ToolError as error:
            return f"ERROR: {error}"
        except (KeyError, TypeError) as error:
            return (f"ERROR: bad arguments for {call.name}: {error}; "
                    f"got {json.dumps(call.arguments)[:500]}")
        except subprocess.TimeoutExpired:
            return f"ERROR: {call.name} timed out"
