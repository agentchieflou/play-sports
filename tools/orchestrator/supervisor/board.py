"""Roadmap crawler and parallel-matrix drift check (Epic 138).

`crawl(repo_root)` reads ROADMAP.md and roadmap/*.md into epics, each with its
checkbox stories, size/mode and the IDs on its "Depends on" line.
`load_matrix(repo_root)` reads the fenced `json parallel-matrix` block in
roadmap/PARALLEL.md. `check_parallel` compares the two and lists the drift a
person has to fix in PARALLEL.md. It is CI-runnable: stdlib only, no network,
exit status from the CLI.

This supersedes worker/stories.py's lookup for multi-story work. Single runs
keep using that one.
"""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from ..worker.stories import roadmap_files

EPIC_HEADER = re.compile(r"^### Epic ([A-Z]?[0-9]+):\s*(.+?)\s*$", re.M)
SECTION_END = re.compile(r"^(##|###) ", re.M)
CHECKBOX = re.compile(r"^- \[( |x)\]\s*(.+?)\s*$", re.M)
DEPENDS_LINE = re.compile(r"^\*\*Depends on:\*\*\s*(.*?)\s*$", re.M)
SIZE_MODE_LINE = re.compile(r"^\*\*Size/Mode:\*\*\s*([A-Z]+)\s*/\s*([a-z]+)", re.M)
DEP_ITEM = re.compile(
    r"^(?:Epics?\s+|Core\s+)?(C?\d+)(?:\s*[–-]\s*(\d+))?(?:'s\b.*)?$")
PSEUDO_ID = re.compile(r"^[A-Z]?\d+-ff-[A-Z]$")
MATRIX_BLOCK = re.compile(r"```json parallel-matrix\s*\n(.*?)\n```", re.S)


@dataclass
class Story:
    epic: str
    index: int  # 1-based position in the epic's checkbox list
    text: str
    done: bool

    @property
    def story_id(self) -> str:
        return f"{self.epic}.{self.index}"


@dataclass
class Epic:
    id: str
    title: str
    file: str  # repo-relative
    depends_on: list[str] = field(default_factory=list)
    stories: list[Story] = field(default_factory=list)
    size: str = ""
    mode: str = ""

    @property
    def status(self) -> str:
        """done (every box checked), partial (some) or open (none / no boxes)."""
        checked = sum(1 for story in self.stories if story.done)
        if self.stories and checked == len(self.stories):
            return "done"
        return "partial" if checked else "open"

    def open_stories(self) -> list[Story]:
        return [story for story in self.stories if not story.done]


@dataclass
class Board:
    epics: dict[str, Epic]

    def epic(self, epic_id: str) -> Epic | None:
        return self.epics.get(epic_id)

    def story(self, story_id: str) -> Story | None:
        epic_id, _, index_text = story_id.partition(".")
        epic = self.epics.get(epic_id)
        if not epic or not index_text.isdigit():
            return None
        index = int(index_text)
        return epic.stories[index - 1] if 1 <= index <= len(epic.stories) else None


def parse_depends(text: str) -> list[str]:
    """Epic IDs named on a "Depends on" line. Parenthetical remarks are
    context, not dependencies; "121-122" is a range; prose such as "starts
    alongside Phase 1" names nothing."""
    without_remarks = re.sub(r"\([^)]*\)", "", text)
    ids: list[str] = []
    for part in re.split(r",|;|\band\b", without_remarks):
        match = DEP_ITEM.match(part.strip().strip("`*.").strip())
        if not match:
            continue
        first, last = match.group(1), match.group(2)
        if last and first.isdigit():
            ids.extend(str(n) for n in range(int(first), int(last) + 1))
        else:
            ids.append(first)
    seen: set[str] = set()
    return [i for i in ids if not (i in seen or seen.add(i))]


def crawl(repo_root: Path) -> Board:
    epics: dict[str, Epic] = {}
    for path in roadmap_files(repo_root):
        text = path.read_text(encoding="utf-8", errors="replace")
        for header in EPIC_HEADER.finditer(text):
            body_start = header.end()
            end_match = SECTION_END.search(text, body_start)
            section = text[body_start:end_match.start() if end_match else len(text)]
            epic = Epic(id=header.group(1), title=header.group(2),
                        file=path.relative_to(repo_root).as_posix())
            depends = DEPENDS_LINE.search(section)
            if depends:
                epic.depends_on = parse_depends(depends.group(1))
            size_mode = SIZE_MODE_LINE.search(section)
            if size_mode:
                epic.size, epic.mode = size_mode.group(1), size_mode.group(2)
            for index, box in enumerate(CHECKBOX.finditer(section), start=1):
                epic.stories.append(Story(epic=epic.id, index=index,
                                          text=box.group(2), done=box.group(1) == "x"))
            epics[epic.id] = epic
    return Board(epics=epics)


@dataclass
class Matrix:
    data: dict
    sha256: str

    @property
    def epics(self) -> dict:
        return self.data.get("epics", {})

    @property
    def groups(self) -> list[dict]:
        return self.data.get("groups", [])

    @property
    def conflicts(self) -> list[dict]:
        return self.data.get("conflicts", [])

    def scope(self, epic_id: str) -> list[str]:
        """The epic's own scope, else its track's (rule 3)."""
        entry = self.epics.get(epic_id, {})
        if entry.get("scope"):
            return list(entry["scope"])
        return list(self.data.get("track_scopes", {}).get(entry.get("track", ""), []))

    def group_of(self, epic_id: str) -> dict | None:
        for group in self.groups:
            if epic_id in group.get("epics", []):
                return group
        return None


def load_matrix(repo_root: Path) -> Matrix:
    path = repo_root / "roadmap" / "PARALLEL.md"
    text = path.read_text(encoding="utf-8")
    match = MATRIX_BLOCK.search(text)
    if not match:
        raise ValueError(f"{path}: no ```json parallel-matrix block")
    payload = match.group(1)
    return Matrix(data=json.loads(payload),
                  sha256=hashlib.sha256(payload.encode("utf-8")).hexdigest())


def check_parallel(board: Board, matrix: Matrix) -> list[str]:
    """Drift between PARALLEL.md's matrix and the roadmap, one line each.

    - every crawled epic has a matrix entry, and every matrix entry is a
      crawled epic (or a documented pseudo-ID such as C3-ff-A);
    - the matrix status is the checkbox state;
    - every dependency the roadmap names is in the matrix's depends_on (the
      matrix may add curated ones, never drop one);
    - the matrix mode is the epic's Size/Mode mode, where the epic has one;
    - a partial epic's open_stories name only unchecked stories;
    - groups, conflicts and depends_on refer to known IDs, and every track
      named has a scope.
    """
    problems: list[str] = []
    matrix_epics = matrix.epics
    known = set(matrix_epics)

    for epic_id, epic in sorted(board.epics.items(), key=lambda kv: _sort_key(kv[0])):
        entry = matrix_epics.get(epic_id)
        if entry is None:
            problems.append(f"Epic {epic_id} ({epic.file}) has no entry in the matrix")
            continue
        if entry.get("status") != epic.status:
            problems.append(f"Epic {epic_id}: matrix status {entry.get('status')!r}, "
                            f"roadmap checkboxes say {epic.status!r}")
        missing = [dep for dep in epic.depends_on if dep not in entry.get("depends_on", [])]
        if missing:
            problems.append(f"Epic {epic_id}: roadmap depends on {', '.join(missing)}, "
                            "which the matrix's depends_on lacks")
        if epic.mode and entry.get("mode") and entry["mode"] != epic.mode:
            problems.append(f"Epic {epic_id}: matrix mode {entry['mode']!r}, "
                            f"roadmap says {epic.mode!r}")
        for listed in entry.get("open_stories", []):
            story = match_story_label(epic, listed)
            if story is None:
                problems.append(f"Epic {epic_id}: open_stories entry {listed!r} matches no story")
            elif story.done:
                problems.append(f"Epic {epic_id}: open_stories entry {listed!r} is checked "
                                f"({story.story_id}) - the story is done")

    for epic_id, entry in matrix_epics.items():
        if epic_id not in board.epics and not PSEUDO_ID.match(epic_id):
            problems.append(f"Matrix epic {epic_id} is not in the roadmap")
        for dep in entry.get("depends_on", []):
            if dep not in known:
                problems.append(f"Matrix epic {epic_id} depends on unknown {dep}")
        track = entry.get("track")
        if not entry.get("scope") and track not in matrix.data.get("track_scopes", {}):
            problems.append(f"Matrix epic {epic_id}: track {track!r} has no scope")

    for group in matrix.groups:
        for member in group.get("epics", []):
            if member not in known:
                problems.append(f"Group {group.get('id')}: unknown epic {member}")
    for conflict in matrix.conflicts:
        for member in conflict.get("epics", []):
            if member not in known:
                problems.append(f"Conflict entry names unknown epic {member}")
    return problems


def match_story_label(epic: Epic, label: str) -> Story | None:
    """The story an open_stories label such as "17.4 broken-play adaptation"
    means. Labels are matched by their words first (every word of four or more
    letters must appear in the story's text), because the matrix's numbers
    aren't always checkbox positions; open stories win over checked ones. A
    label whose words match nothing falls back to its number as a position."""
    number, _, description = label.partition(" ")
    words = [w for w in re.findall(r"[a-z]+", re.sub(r"\([^)]*\)", "", description).lower())
             if len(w) >= 4]
    matches = [story for story in epic.stories
               if words and all(word in story.text.lower() for word in words)]
    open_matches = [story for story in matches if not story.done]
    if open_matches or matches:
        return (open_matches or matches)[0]
    _, _, index_text = number.partition(".")
    if index_text.isdigit() and 1 <= int(index_text) <= len(epic.stories):
        return epic.stories[int(index_text) - 1]
    return None


def _sort_key(epic_id: str) -> tuple:
    digits = re.sub(r"\D", "", epic_id) or "0"
    return (epic_id[0].isalpha(), int(digits), epic_id)


# --- file-scope overlap -------------------------------------------------------------------

def _segment_overlap(a: str, b: str) -> bool:
    """Whether two single path-segment patterns (literals and '*') can match
    the same name."""
    if "*" not in a and "*" not in b:
        return a == b
    if "*" not in a or "*" not in b:
        literal, pattern = (a, b) if "*" not in a else (b, a)
        return re.fullmatch(re.escape(pattern).replace(r"\*", ".*"), literal) is not None
    a_head, a_tail = a.split("*", 1)[0], a.rsplit("*", 1)[1]
    b_head, b_tail = b.split("*", 1)[0], b.rsplit("*", 1)[1]
    heads_agree = a_head.startswith(b_head) or b_head.startswith(a_head)
    tails_agree = a_tail.endswith(b_tail) or b_tail.endswith(a_tail)
    return heads_agree and tails_agree


def _segments_overlap(a: list[str], b: list[str]) -> bool:
    if not a and not b:
        return True
    if a and a[0] == "**":
        return _segments_overlap(a[1:], b) or (bool(b) and _segments_overlap(a, b[1:]))
    if b and b[0] == "**":
        return _segments_overlap(a, b[1:]) or (bool(a) and _segments_overlap(a[1:], b))
    if not a or not b:
        return False
    return _segment_overlap(a[0], b[0]) and _segments_overlap(a[1:], b[1:])


def globs_overlap(a: str, b: str) -> bool:
    """Whether two repo globs ('**' for any depth, '*' within a segment) can
    match a common path. Conservative: a doubtful pair counts as overlapping."""
    return _segments_overlap([s for s in a.split("/") if s], [s for s in b.split("/") if s])


def scopes_overlap(a: list[str], b: list[str]) -> bool:
    if not a or not b:
        return True  # an unknown scope can't be shown disjoint
    return any(globs_overlap(x, y) for x in a for y in b)
