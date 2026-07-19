"""System-prompt rendering for worker runs (Epic 136).

The `.agents/skills/` prose remains the behavioral source of truth: the
worker's system prompt is rendered FROM the matching coder skill's SKILL.md,
plus the story assignment and the harness's own tool rules.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

SPECIALIZATIONS = ("gameplay-cpp-story", "data-content-author",
                   "ai-behavior-specialist")
DEFAULT_SPECIALIZATION = "gameplay-cpp-story"

HARNESS_RULES = """\
You are a coder agent completing exactly ONE roadmap story in this repository.

Rules of engagement:
- Work only through the provided tools. You have no shell and no git; the
  harness commits and opens the PR for you when you call `finish`.
- Stay inside the story's scope. Do not refactor unrelated code, do not tick
  roadmap checkboxes yourself, and do not start other stories.
- Explore before editing: `list_dir`/`grep`/`read_file` the relevant files
  first. Match the conventions you see (4-space indent, Allman braces, PS*
  prefixes for UE types).
- Verify before finishing: use `run_check` (lint_conventions, validate_data,
  unittest) where applicable and fix what it reports.
- When the story is complete and verified, call `finish` with an honest
  summary: what changed, what you verified locally, and what CI still needs
  to prove (you cannot compile UE C++ locally - never claim you built it).
"""


@dataclass(frozen=True)
class StoryAssignment:
    """One story handed to one worker - the unit of work (one story = one run)."""

    epic: str            # e.g. "12" or "C3"
    story_index: int     # 1-based index within the epic's checkbox list
    story_text: str
    epic_title: str = ""
    track_file: str = ""
    branch: str = ""
    specialization: str = DEFAULT_SPECIALIZATION

    @property
    def story_id(self) -> str:
        return f"{self.epic}.{self.story_index}"


def load_skill_prose(repo_root: Path, specialization: str) -> str:
    """Read the coder skill's SKILL.md (frontmatter stripped). Missing skill
    file degrades to empty prose rather than failing the run."""
    skill_path = repo_root / ".agents" / "skills" / specialization / "SKILL.md"
    if not skill_path.is_file():
        return ""
    text = skill_path.read_text(encoding="utf-8", errors="replace")
    match = re.match(r"\s*---\n.*?\n---\n", text, re.S)
    return text[match.end():].strip() if match else text.strip()


def build_system_prompt(repo_root: Path, assignment: StoryAssignment) -> str:
    sections = [HARNESS_RULES]
    skill_prose = load_skill_prose(repo_root, assignment.specialization)
    if skill_prose:
        sections.append(
            f"## Your specialization skill ({assignment.specialization})\n\n"
            + skill_prose
        )
    return "\n\n".join(sections)


def build_task_prompt(assignment: StoryAssignment) -> str:
    lines = [
        "## Story assignment",
        f"- Story: {assignment.story_id}"
        + (f" (Epic {assignment.epic}: {assignment.epic_title})"
           if assignment.epic_title else ""),
        f"- Roadmap file: {assignment.track_file}" if assignment.track_file else "",
        f"- Branch (harness-managed): {assignment.branch}" if assignment.branch else "",
        "",
        "Story text:",
        assignment.story_text.strip(),
        "",
        "Complete this story now. Explore first, then implement, then verify, "
        "then call finish.",
    ]
    return "\n".join(line for line in lines if line is not None)
