#!/usr/bin/env python
"""Crash report triage for play-sports (Epic 117).

Reads the crash folders Unreal writes (one per crash, holding CrashContext.runtime-xml, a
minidump and a log) and turns each into a short, scrubbed summary: crash type, error, the
symbolized call stack, the engine and build, and the game's own PS.* keys (mode, plays,
recent telemetry events; see Source/PlaySports/Public/PSCrashContext.h). Two commands:

  summarize [DIR ...]   Print a summary of every crash under DIR (default Saved/Crashes).
                        CI runs this after the automation tests; it always exits 0.
  file [DIR ...]        Route each unreported crash to a GitHub issue through `gh api`:
                        one issue per crash signature, labelled `crash`. A crash seen again
                        comments on its issue, reopening it if it was closed. A crash folder
                        is marked reported so it is filed once. --dry-run prints instead.

Data minimization (Specs/Privacy_Telemetry.md): only allowlisted fields leave the crash
folder. User, login, machine and account IDs, the command line and install paths are never
read into a summary, user home paths are scrubbed from what is, and minidumps and logs are
never uploaded.

Run from the repo root:  python tools/crash_report.py summarize
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_CRASH_DIR = REPO / "Saved" / "Crashes"
DEFAULT_GITHUB_REPO = "agentchieflou/play-sports"
CONTEXT_FILE = "CrashContext.runtime-xml"
REPORTED_MARKER = ".ps-crash-reported"
CRASH_LABEL = "crash"

# RuntimeProperties copied into a summary. Everything else in the context (UserName, LoginId,
# EpicAccountId, MachineId, CommandLine, BaseDir, RootDir, UserDescription, ...) is left out.
ALLOWED_FIELDS = (
    "CrashType", "ErrorMessage", "EngineVersion", "BuildVersion", "BuildConfiguration",
    "PlatformName", "PlatformFullName", "EngineMode", "GameName", "SecondsSinceStart",
    "IsEnsure", "IsAssert", "IsStall",
)
GAME_DATA_PREFIX = "PS."
MAX_STACK_FRAMES = 40
SIGNATURE_FRAMES = 5

# Frames that are the crash machinery, not the crash: skipped when building a signature.
NOISE_FRAME = re.compile(
    r"KERNELBASE|ntdll|kernel32|ucrtbase|VCRUNTIME|"
    r"FDebug::|AssertFailed|CheckVerifyFailed|DispatchCheckVerify|FWindowsErrorOutputDevice|"
    r"FOutputDevice|ReportAssert|ReportCrash|ReportEnsure|RaiseException|"
    r"FGenericPlatformMisc::|FWindowsPlatformMisc::RaiseException|UE::Assert",
    re.IGNORECASE,
)
UNSYMBOLIZED_FRAME = re.compile(r"UnknownFunction|^0x[0-9a-fA-F]+\s*$|!0x[0-9a-fA-F]+")
HOME_PATHS = (
    (re.compile(r"([A-Za-z]:[\\/]+Users[\\/]+)[^\\/\s\]\[]+", re.IGNORECASE), r"\1<user>"),
    (re.compile(r"(/home/)[^/\s\]\[]+"), r"\1<user>"),
    (re.compile(r"(/Users/)[^/\s\]\[]+"), r"\1<user>"),
)


def scrub(text):
    """Text with user home directories replaced by <user>."""
    for pattern, replacement in HOME_PATHS:
        text = pattern.sub(replacement, text)
    return text


def _text(element):
    return (element.text or "").strip() if element is not None else ""


def parse_crash_context(xml_text):
    """The allowlisted fields, the PS.* game data and the call stack of one crash context.

    Returns a dict: {"fields": {name: value}, "game_data": {key: value}, "frames": [str]}.
    Raises ValueError when the text is not a crash context.
    """
    try:
        root = ET.fromstring(xml_text)
    except ET.ParseError as exc:
        raise ValueError(f"not XML: {exc}") from exc
    runtime = root.find("RuntimeProperties")
    if runtime is None:
        raise ValueError("no RuntimeProperties section")

    fields = {}
    for name in ALLOWED_FIELDS:
        value = _text(runtime.find(name))
        if value:
            fields[name] = scrub(value)

    game_data = {}
    section = root.find("GameData")
    for child in list(section) if section is not None else []:
        if child.tag.startswith(GAME_DATA_PREFIX):
            game_data[child.tag] = scrub(_text(child))

    frames = [scrub(line.strip()) for line in _text(runtime.find("CallStack")).splitlines() if line.strip()]
    return {"fields": fields, "game_data": game_data, "frames": frames}


def frame_function(frame):
    """A frame reduced to what identifies it across builds and machines: the function, without
    the address, module, source path or line."""
    frame = re.sub(r"\s*\[[^\]]*\]\s*$", "", frame)          # [D:\path\File.cpp:42]
    frame = re.sub(r"^0x[0-9a-fA-F]+\s+", "", frame)          # leading address
    frame = re.sub(r"\s*\+\s*0x[0-9a-fA-F]+$", "", frame)     # + 0x1a offset
    if "!" in frame:
        frame = frame.split("!", 1)[1]
    return frame.strip()


def is_symbolized(frames):
    """True when the stack names functions: a pdb-less build gives addresses only."""
    meaningful = [f for f in frames if not NOISE_FRAME.search(f)]
    if not meaningful:
        return False
    named = [f for f in meaningful if not UNSYMBOLIZED_FRAME.search(f)]
    return len(named) * 2 >= len(meaningful)


def signature(crash):
    """A short, stable ID for "the same crash": the crash type and the first meaningful
    symbolized frames. Unsymbolized stacks fall back to the error message with numbers and
    addresses removed."""
    crash_type = crash["fields"].get("CrashType", "Crash")
    frames = [frame_function(f) for f in crash["frames"] if not NOISE_FRAME.search(f) and not UNSYMBOLIZED_FRAME.search(f)]
    if frames:
        basis = "\n".join(frames[:SIGNATURE_FRAMES])
    else:
        message = crash["fields"].get("ErrorMessage", "")
        basis = re.sub(r"0x[0-9a-fA-F]+|\d+", "#", message)
    return hashlib.sha1(f"{crash_type}\n{basis}".encode("utf-8")).hexdigest()[:10]


def issue_title(crash, sig):
    message = crash["fields"].get("ErrorMessage", "").splitlines()
    headline = message[0].strip() if message else ""
    if not headline:
        frames = [frame_function(f) for f in crash["frames"] if not NOISE_FRAME.search(f)]
        headline = frames[0] if frames else crash["fields"].get("CrashType", "Crash")
    if len(headline) > 90:
        headline = headline[:87] + "..."
    return f"Crash: {headline} [crash-{sig}]"


def render_summary(crash, sig, folder_name=""):
    """Markdown for one crash: the issue body, and what `summarize` prints."""
    fields = crash["fields"]
    lines = []
    if folder_name:
        lines.append(f"Crash folder: `{folder_name}`")
    lines.append(f"Signature: `crash-{sig}`")
    lines.append("")
    lines.append("| Field | Value |")
    lines.append("| --- | --- |")
    for name in ALLOWED_FIELDS:
        if name in fields and name != "ErrorMessage":
            lines.append(f"| {name} | {fields[name].replace('|', '/')} |")
    lines.append("")
    lines.append("**Error**")
    lines.append("```")
    lines.append(fields.get("ErrorMessage", "(none)"))
    lines.append("```")
    if crash["game_data"]:
        lines.append("**Game state** (PS.* crash context keys)")
        lines.append("```")
        for key, value in sorted(crash["game_data"].items()):
            if "\n" in value:
                lines.append(f"{key}:")
                lines.extend(f"  {line}" for line in value.splitlines())
            else:
                lines.append(f"{key}: {value}")
        lines.append("```")
    frames = crash["frames"]
    lines.append(f"**Call stack** ({len(frames)} frame(s){', first ' + str(MAX_STACK_FRAMES) if len(frames) > MAX_STACK_FRAMES else ''})")
    if frames and not is_symbolized(frames):
        lines.append("")
        lines.append("> Unsymbolized: this build shipped without its .pdb files. Rebuild the same commit with "
                     "debug files (Config/DefaultGame.ini, bIncludeDebugFiles) to read this stack.")
    lines.append("```")
    lines.extend(frames[:MAX_STACK_FRAMES] or ["(no call stack)"])
    lines.append("```")
    return "\n".join(lines)


def find_crash_folders(roots):
    """Every folder under roots holding a crash context, oldest first."""
    found = []
    for root in roots:
        root = Path(root)
        if not root.is_dir():
            continue
        for context in root.rglob(CONTEXT_FILE):
            found.append(context.parent)
    return sorted(set(found), key=lambda folder: (folder.stat().st_mtime, str(folder)))


def load_crash(folder):
    raw = (Path(folder) / CONTEXT_FILE).read_bytes()
    # Unreal writes UTF-8 (older builds UTF-16); let the XML declaration decide.
    return parse_crash_context(raw)


class GitHubIssues:
    """Crash issues through the REST API (`gh api`); runner is swapped out in tests."""

    def __init__(self, repo, runner=None):
        self.repo = repo
        self.runner = runner or self._run_gh
        self._known = None

    @staticmethod
    def _run_gh(args, payload=None):
        command = ["gh", "api"] + args
        if payload is not None:
            command += ["--input", "-"]
        result = subprocess.run(command, input=json.dumps(payload) if payload is not None else None,
                                capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            raise RuntimeError(f"gh api {' '.join(args)} failed: {result.stderr.strip()}")
        return json.loads(result.stdout) if result.stdout.strip() else None

    def crash_issues(self):
        """{signature: issue} for every issue labelled crash, open or closed."""
        if self._known is None:
            self._known = {}
            for page in range(1, 51):
                issues = self.runner(["-X", "GET", f"repos/{self.repo}/issues?labels={CRASH_LABEL}&state=all&per_page=100&page={page}"]) or []
                for issue in issues:
                    match = re.search(r"\[crash-([0-9a-f]{10})\]", issue.get("title", ""))
                    if match and "pull_request" not in issue:
                        self._known.setdefault(match.group(1), issue)
                if len(issues) < 100:
                    break
        return self._known

    def ensure_label(self):
        try:
            self.runner([f"repos/{self.repo}/labels"], {"name": CRASH_LABEL, "color": "b60205",
                                                         "description": "Crash report routed by tools/crash_report.py"})
        except RuntimeError:
            pass  # it exists (422), or we may not create labels: issue creation still works

    def route(self, crash, sig, folder_name):
        """Files the crash; returns (action, issue number): created, commented or reopened."""
        body = render_summary(crash, sig, folder_name)
        existing = self.crash_issues().get(sig)
        if existing is None:
            self.ensure_label()
            issue = self.runner([f"repos/{self.repo}/issues"],
                                {"title": issue_title(crash, sig), "body": body, "labels": [CRASH_LABEL]})
            self._known[sig] = issue
            return "created", issue["number"]
        number = existing["number"]
        action = "commented"
        if existing.get("state") == "closed":
            self.runner(["-X", "PATCH", f"repos/{self.repo}/issues/{number}"], {"state": "open"})
            existing["state"] = "open"
            action = "reopened"
            body = "Seen again after this issue was closed: a regression.\n\n" + body
        else:
            body = "Seen again.\n\n" + body
        self.runner([f"repos/{self.repo}/issues/{number}/comments"], {"body": body})
        return action, number


def cmd_summarize(roots):
    folders = find_crash_folders(roots)
    if not folders:
        print("crash_report: no crash reports found under " + ", ".join(str(r) for r in roots))
        return 0
    print(f"crash_report: {len(folders)} crash report(s)")
    for folder in folders:
        print("")
        print("=" * 78)
        try:
            crash = load_crash(folder)
        except ValueError as exc:
            print(f"{folder.name}: unreadable crash context ({exc})")
            continue
        print(render_summary(crash, signature(crash), folder.name))
    return 0


def cmd_file(roots, repo, dry_run, runner=None):
    folders = [f for f in find_crash_folders(roots) if not (f / REPORTED_MARKER).exists()]
    if not folders:
        print("crash_report: nothing new to file")
        return 0
    issues = GitHubIssues(repo, runner)
    failures = 0
    for folder in folders:
        try:
            crash = load_crash(folder)
        except ValueError as exc:
            print(f"{folder.name}: skipped, unreadable crash context ({exc})")
            failures += 1
            continue
        sig = signature(crash)
        if dry_run:
            print(f"--- would file {issue_title(crash, sig)}")
            print(render_summary(crash, sig, folder.name))
            continue
        try:
            action, number = issues.route(crash, sig, folder.name)
        except RuntimeError as exc:
            print(f"{folder.name}: could not file ({exc})")
            failures += 1
            continue
        (folder / REPORTED_MARKER).write_text(f"{repo}#{number} {action}\n", encoding="utf-8")
        print(f"{folder.name}: {action} {repo}#{number} (crash-{sig})")
    return 1 if failures else 0


def main(argv=None):
    # A Windows console may not encode what a crash message holds; never fail on printing it.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    summarize = sub.add_parser("summarize", help="print a scrubbed summary of each crash")
    summarize.add_argument("dirs", nargs="*", default=[str(DEFAULT_CRASH_DIR)])
    file_cmd = sub.add_parser("file", help="route each unreported crash to a GitHub issue")
    file_cmd.add_argument("dirs", nargs="*", default=[str(DEFAULT_CRASH_DIR)])
    file_cmd.add_argument("--repo", default=DEFAULT_GITHUB_REPO)
    file_cmd.add_argument("--dry-run", action="store_true")
    args = parser.parse_args(argv)
    if args.command == "summarize":
        return cmd_summarize(args.dirs)
    return cmd_file(args.dirs, args.repo, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
