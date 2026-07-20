"""Roadmap crawler + PARALLEL.md drift check (Epic 138).

Parses ROADMAP.md and every roadmap/*.md track file into an epic/story board
with checkbox state and the Depends-on graph, and validates the machine
payload in roadmap/PARALLEL.md against that crawl (`check-parallel`).
Supersedes worker/stories.py's minimal lookup for graph runs.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

EPIC_HEADER = re.compile(r"^### Epic ([A-Z0-9]+):\s*(.+?)\s*$", re.M)
CHECKBOX = re.compile(r"^- \[( |x)\]\s*(.+?)\s*$", re.M)
DEPENDS_LINE = re.compile(r"^\*\*Depends on:\*\*\s*(.+?)\s*$", re.M)
DEP_TOKEN = re.compile(r"(?:Core\s+)?\b(C?\d+)\b")
MATRIX_BLOCK = re.compile(r"```json parallel-matrix\n(.*?)```", re.S)
PSEUDO_ID = re.compile(r"^[A-Z0-9]+-ff-[A-Z]$")  # e.g. C3-ff-A fast-follows


@dataclass
class Epic:
    epic_id: str
    title: str
    file: str
    depends_on: list[str] = field(default_factory=list)
    stories: list[tuple[str, bool]] = field(default_factory=list)  # (text, checked)

    @property
    def status(self) -> str:
        if not self.stories:
            return "open"
        checked = sum(1 for _, done in self.stories if done)
        if checked == len(self.stories):
            return "done"
        return "partial" if checked else "open"

    def open_story_indices(self) -> list[int]:
        return [i for i, (_, done) in enumerate(self.stories, 1) if not done]


def crawl(repo_root: Path) -> dict[str, Epic]:
    """Parse every epic section across ROADMAP.md + roadmap/*.md."""
    board: dict[str, Epic] = {}
    files = [repo_root / "ROADMAP.md"]
    track_dir = repo_root / "roadmap"
    if track_dir.is_dir():
        files.extend(sorted(track_dir.glob("*.md")))
    for file_path in files:
        if not file_path.is_file() or file_path.name in ("PARALLEL.md",):
            continue
        text = file_path.read_text(encoding="utf-8", errors="replace")
        headers = list(EPIC_HEADER.finditer(text))
        for position, header in enumerate(headers):
            section_end = (headers[position + 1].start()
                           if position + 1 < len(headers) else len(text))
            section = text[header.start():section_end]
            depends: list[str] = []
            depends_match = DEPENDS_LINE.search(section)
            if depends_match and not depends_match.group(1).startswith("—"):
                # Only the dependency clause before any parenthetical prose
                clause = depends_match.group(1).split("(")[0]
                depends = DEP_TOKEN.findall(clause)
            epic = Epic(
                epic_id=header.group(1),
                title=header.group(2),
                file=file_path.relative_to(repo_root).as_posix(),
                depends_on=depends,
                stories=[(m.group(2), m.group(1) == "x")
                         for m in CHECKBOX.finditer(section)],
            )
            board[epic.epic_id] = epic
    return board


def load_matrix(repo_root: Path) -> dict:
    path = repo_root / "roadmap" / "PARALLEL.md"
    text = path.read_text(encoding="utf-8", errors="replace")
    match = MATRIX_BLOCK.search(text)
    if not match:
        raise ValueError("no ```json parallel-matrix``` block in roadmap/PARALLEL.md")
    return json.loads(match.group(1))


def check_parallel(repo_root: Path) -> list[str]:
    """Drift check: PARALLEL.md's payload vs a fresh roadmap crawl. Returns a
    list of problems (empty = clean). CI-runnable via the CLI."""
    problems: list[str] = []
    board = crawl(repo_root)
    try:
        matrix = load_matrix(repo_root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return [f"cannot load matrix: {error}"]

    epics = matrix.get("epics", {})
    for epic_id, entry in epics.items():
        for dep in entry.get("depends_on", []):
            if dep not in epics:
                problems.append(f"epic {epic_id}: unknown dependency {dep!r}")
        if PSEUDO_ID.match(epic_id):
            continue  # fast-follow pseudo-stories live only in the matrix
        if epic_id not in board:
            problems.append(f"matrix epic {epic_id} not found in roadmap")
            continue
        crawled = board[epic_id].status
        declared = entry.get("status")
        if declared != crawled:
            problems.append(
                f"epic {epic_id}: matrix says {declared!r}, roadmap says {crawled!r}")
    for epic_id in board:
        if epic_id not in epics:
            problems.append(f"roadmap epic {epic_id} missing from matrix")
    for group in matrix.get("groups", []):
        for epic_id in group.get("epics", []):
            if epic_id not in epics:
                problems.append(
                    f"group {group.get('id')}: unknown epic {epic_id!r}")
    return problems


# -- scope math (used by the graph loop) ------------------------------------

def epic_scope(matrix: dict, epic_id: str) -> list[str]:
    entry = matrix.get("epics", {}).get(epic_id, {})
    if "scope" in entry:
        return list(entry["scope"])
    track = entry.get("track", "")
    return list(matrix.get("track_scopes", {}).get(track, []))


def globs_overlap(scope_a: list[str], scope_b: list[str]) -> bool:
    """Approximate glob-set overlap: equal patterns, or one pattern matching
    the other's literal prefix. Conservative in the safe direction."""
    import fnmatch

    for a in scope_a:
        for b in scope_b:
            if a == b:
                return True
            a_prefix = a.split("*")[0]
            b_prefix = b.split("*")[0]
            if a_prefix and b_prefix and (
                    a_prefix.startswith(b_prefix) or b_prefix.startswith(a_prefix)):
                return True
            if fnmatch.fnmatch(a, b) or fnmatch.fnmatch(b, a):
                return True
    return False
