"""Minimal roadmap story lookup for single runs (Epic 136).

Finds `### Epic <N>:` sections across ROADMAP.md and roadmap/*.md and indexes
their checkbox stories. Epic 138's supervisor/board.py supersedes this with a
full dependency-graph crawl; this stays deliberately tiny.
"""

from __future__ import annotations

import re
from pathlib import Path

from .prompts import StoryAssignment

EPIC_HEADER = re.compile(r"^### Epic ([A-Z0-9]+):\s*(.+?)\s*$", re.M)
CHECKBOX = re.compile(r"^- \[( |x)\]\s*(.+?)\s*$", re.M)


def roadmap_files(repo_root: Path) -> list[Path]:
    files = [repo_root / "ROADMAP.md"]
    track_dir = repo_root / "roadmap"
    if track_dir.is_dir():
        files.extend(sorted(track_dir.glob("*.md")))
    return [f for f in files if f.is_file()]


def find_story(repo_root: Path, story_id: str) -> StoryAssignment:
    """story_id is "<epic>.<index>", 1-based index into the epic's checkbox
    list (checked or not - the caller decides whether re-running a checked
    story is a mistake)."""
    epic, _, index_text = story_id.partition(".")
    if not epic or not index_text.isdigit():
        raise ValueError(f"story id must look like '12.5', got {story_id!r}")
    story_index = int(index_text)

    for file_path in roadmap_files(repo_root):
        text = file_path.read_text(encoding="utf-8", errors="replace")
        headers = list(EPIC_HEADER.finditer(text))
        for position, header in enumerate(headers):
            if header.group(1) != epic:
                continue
            section_end = (headers[position + 1].start()
                           if position + 1 < len(headers) else len(text))
            section = text[header.start():section_end]
            boxes = list(CHECKBOX.finditer(section))
            if not (1 <= story_index <= len(boxes)):
                raise ValueError(
                    f"Epic {epic} has {len(boxes)} stories; "
                    f"story index {story_index} is out of range")
            box = boxes[story_index - 1]
            if box.group(1) == "x":
                raise ValueError(
                    f"story {story_id} is already checked in "
                    f"{file_path.name}; refusing to re-run it")
            return StoryAssignment(
                epic=epic,
                story_index=story_index,
                story_text=box.group(2),
                epic_title=header.group(2),
                track_file=file_path.relative_to(repo_root).as_posix(),
            )
    raise ValueError(f"Epic {epic} not found in ROADMAP.md or roadmap/*.md")


def default_branch_name(assignment: StoryAssignment) -> str:
    """git-steward convention: epic-<N>/<short-kebab-slug>."""
    slug_source = re.sub(r"[`*\[\]().,:;'\"]+", "", assignment.story_text.lower())
    words = [w for w in re.split(r"[^a-z0-9]+", slug_source) if w][:5]
    slug = "-".join(words) or "story"
    return f"epic-{assignment.epic.lower()}/{slug}"
