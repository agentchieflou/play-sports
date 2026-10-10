#!/usr/bin/env python
"""Summarizes the live-play demos a CI run recorded (Saved/PlayDemos/index.json).

PlaySports.Demo.LivePlays runs Data/play_demos.json's plays end to end under the game mode and
writes one replay-format JSON per play plus index.json (UPSPlayDemoRunner::WriteDemos). This prints
one line per play (what was called, what happened, what ended it, the sanity checks' findings) and,
with --summary, appends the same as a Markdown table to a file (CI passes $GITHUB_STEP_SUMMARY).
Informational: it always exits 0, and says so when there is no index.

  python tools/play_demos.py summary [Saved/PlayDemos] [--summary FILE]
"""

import argparse
import json
import sys
from pathlib import Path


def load_index(directory):
    """index.json's plays, or None when there is none (the test didn't run or crashed)."""
    path = Path(directory) / "index.json"
    try:
        index = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    plays = index.get("plays") if isinstance(index, dict) else None
    return plays if isinstance(plays, list) else None


def describe(play):
    """One play as a table row's cells."""
    problems = play.get("problems") or []
    return [
        str(play.get("demoId", "")),
        str(play.get("title", "")),
        f"{play.get('outcome', '')} ({play.get('result', '')}, {play.get('yardsGained', 0)} yd)",
        str(play.get("endedBy", "")),
        f"{play.get('playSeconds', 0.0):.2f} s",
        f"{play.get('frameCount', 0)} @ {play.get('frameRateHz', 0):.0f} Hz",
        "OK" if not problems else "; ".join(str(p) for p in problems),
    ]


HEADER = ["Demo", "What happened", "Outcome", "Ended by", "Snap to whistle", "Frames", "Checks"]


def render_markdown(plays):
    lines = ["### Live-play demos (`play-demos` artifact)", "", "| " + " | ".join(HEADER) + " |",
             "|" + "---|" * len(HEADER)]
    for play in plays:
        cells = [cell.replace("|", "/") for cell in describe(play)]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    summary = sub.add_parser("summary", help="print the recorded plays")
    summary.add_argument("directory", nargs="?", default="Saved/PlayDemos")
    summary.add_argument("--summary", help="append a Markdown table to this file")
    args = parser.parse_args(argv)

    plays = load_index(args.directory)
    if plays is None:
        print(f"play_demos: no index.json in {args.directory} - PlaySports.Demo.LivePlays wrote no demos")
        return 0
    for play in plays:
        print("play_demos: " + " | ".join(describe(play)))
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as out:
            out.write(render_markdown(plays))
    return 0


if __name__ == "__main__":
    sys.exit(main())
