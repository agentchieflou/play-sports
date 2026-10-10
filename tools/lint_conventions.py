#!/usr/bin/env python
"""Convention linter for play-sports (Epic 113).

Enforces the conventions documented in AGENTS.md over Source/ and Plugins/:
  - Headers live in Public/, implementations in Private/ (per-module split).
  - Headers use #pragma once.
  - No tab indentation (4-space indent standard).
  - Allman braces: no opening brace at the end of a control-flow/function line.
  - UE type prefixes: UCLASS->U, USTRUCT->F, UENUM->E, and AActor-derived
    UCLASS names start with A; PlaySports gameplay types carry the PS infix.
  - Platform independence (Epic 152): gameplay code (Source/PlaySports, apart
    from the platform layer and the tests) never references a platform
    implementation (PSPlatformBackend*) and never forks on #if PLATFORM_*.
    It reaches the platform through UPSPlatformServices; a platform
    difference lives in data or in a backend.

Two tiers:
  errors   - structural violations (placement, pragma once, type prefixes);
             always fail the run.
  warnings - style drift (tab indentation, Allman braces); reported but
             non-fatal unless --strict is passed, so style never blocks a
             merge without an explicit decision to gate on it.

Exit 0 when no errors (warnings allowed), exit 1 on errors (or, with
--strict, on warnings too). Run from the repo root:
  python tools/lint_conventions.py [--strict]
"""

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SCAN_ROOTS = [REPO / "Source", REPO / "Plugins"]

findings = []
warnings = []


def finding(path, line_no, message):
    findings.append(f"{path.relative_to(REPO)}:{line_no}: {message}")


def warn(path, line_no, message):
    warnings.append(f"{path.relative_to(REPO)}:{line_no}: {message}")


def cpp_files():
    for root in SCAN_ROOTS:
        if root.is_dir():
            for path in sorted(root.rglob("*")):
                if path.suffix in (".h", ".cpp") and "Intermediate" not in path.parts:
                    yield path


# Opening brace at end of a code line (Allman violation), with pragmatic
# exclusions: initializer lists/aggregates, lambdas, empty braces, macros.
ALLMAN_RE = re.compile(r"^\s*[^\s/].*\)\s*(const)?\s*(override)?\s*\{\s*$")
ALLMAN_EXCLUDE = re.compile(r"=\s*\{|\{\s*\}|\[\S*\]\s*\(|^\s*#|UPROPERTY|UFUNCTION|UCLASS|USTRUCT|UENUM|GENERATED_BODY")

# Epic 152: the platform layer is UPSPlatformServices and its backends; only it, and the tests
# that exercise it, may name a backend.
PLATFORM_LAYER_PREFIXES = ("PSPlatformServices", "PSPlatformBackend")
PLATFORM_BACKEND_RE = re.compile(r"PSPlatformBackend\w*")
PLATFORM_IF_RE = re.compile(r"^\s*#\s*(?:if|elif|ifdef|ifndef)\b.*\bPLATFORM_[A-Z0-9_]+")


def strip_comments(text):
    """text with its // and /* */ comments blanked out, line breaks kept so line numbers hold.
    String literals stay: a backend's path in a string is still a reference to it."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == "'" and i + 2 < n and (text[i + 2] == "'" or (text[i + 1] == "\\" and i + 3 < n and text[i + 3] == "'")):
            j = i + (3 if text[i + 2] == "'" else 4)
            out.append(text[i:j])
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j == -1 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j == -1 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def is_gameplay_code(rel_path):
    """Whether rel_path (relative to the repo) is gameplay code the platform rule covers."""
    parts = Path(rel_path).parts
    if len(parts) < 2 or parts[0] != "Source" or parts[1] != "PlaySports" or "Tests" in parts:
        return False
    return not Path(rel_path).name.startswith(PLATFORM_LAYER_PREFIXES)


def platform_problems(rel_path, text):
    """[(line, message)]: where gameplay code at rel_path names a platform implementation or
    forks on the platform. Empty for the platform layer, tests and non-gameplay code."""
    if not is_gameplay_code(rel_path):
        return []
    found = []
    for number, line in enumerate(strip_comments(text).splitlines(), start=1):
        match = PLATFORM_BACKEND_RE.search(line)
        if match:
            found.append((number, f"gameplay code names the platform implementation '{match.group(0)}' - "
                                  "reach the platform through UPSPlatformServices (Epic 152)"))
        if PLATFORM_IF_RE.match(line):
            found.append((number, "#if PLATFORM_* in gameplay code - a platform difference lives in data "
                                  "or behind UPSPlatformServices (Epic 152)"))
    return found


UCLASS_RE = re.compile(r"^\s*(?:class|struct)\s+\w+_API\s+(\w+)|^\s*(?:class|struct)\s+(\w+)\s*[:{]")


def check_file(path):
    rel_parts = path.parts
    text = path.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines()

    # Placement: inside a module that has a Public/ or Private/ dir, headers
    # belong under Public/ and .cpp under Private/. (Target.cs files and
    # module roots without the split are exempt.)
    if "Public" in rel_parts and path.suffix == ".cpp":
        finding(path, 1, "implementation file under Public/ - move to Private/")
    if "Private" in rel_parts and path.suffix == ".h":
        # Private headers are legal in UE but not this repo's convention yet.
        finding(path, 1, "header under Private/ - this repo keeps headers in Public/")

    if path.suffix == ".h" and "#pragma once" not in text:
        finding(path, 1, "header missing #pragma once")

    for line_no, message in platform_problems(path.relative_to(REPO), text):
        finding(path, line_no, message)

    pending_macro = None  # UCLASS/USTRUCT/UENUM seen, awaiting the declaration
    tab_lines = 0
    for i, line in enumerate(lines, start=1):
        indent = line[: len(line) - len(line.lstrip())]
        if line.strip() and "\t" in indent:
            tab_lines += 1

        if ALLMAN_RE.match(line) and not ALLMAN_EXCLUDE.search(line):
            warn(path, i, "opening brace on statement line - use Allman style")

        macro_match = re.match(r"\s*(UCLASS|USTRUCT|UENUM)\s*\(", line)
        if macro_match:
            pending_macro = macro_match.group(1)
            continue

        if pending_macro:
            decl = re.match(
                r"\s*(?:class|struct|enum\s+class)\s+(?:\w+_API\s+)?(\w+)", line
            )
            if decl:
                name = decl.group(1)
                expected = {"UCLASS": ("U", "A"), "USTRUCT": ("F",), "UENUM": ("E",)}[pending_macro]
                if not name.startswith(expected):
                    finding(
                        path, i,
                        f"{pending_macro} type '{name}' should start with "
                        f"{' or '.join(expected)}",
                    )
                # Classes carry the PS infix (UPS*/APS*); structs and enums may
                # be generic (FSeasonWeek, EPlayerRole are established usage).
                if "PlaySports" in rel_parts and pending_macro == "UCLASS" and not re.match(r"^[UA]PS", name):
                    finding(path, i, f"PlaySports gameplay class '{name}' missing PS prefix")
                pending_macro = None
            elif line.strip() and not line.strip().startswith("//"):
                pending_macro = None

    if tab_lines:
        warn(path, 1, f"tab indentation on {tab_lines} line(s) - AGENTS.md says 4 spaces")


def main():
    strict = "--strict" in sys.argv
    count = 0
    for path in cpp_files():
        count += 1
        check_file(path)
    for w in warnings:
        print("  warning: " + w)
    if findings:
        print(f"lint_conventions: {len(findings)} error(s) across {count} files:")
        for f in findings:
            print("  error: " + f)
        return 1
    if strict and warnings:
        print(f"lint_conventions: FAIL (strict) - {len(warnings)} warning(s), 0 errors ({count} files)")
        return 1
    print(f"lint_conventions: OK ({count} files, {len(warnings)} style warning(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
