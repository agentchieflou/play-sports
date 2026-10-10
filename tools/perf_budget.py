#!/usr/bin/env python
"""Performance budgets: the CI check and the trend history (Epic 114).

The profiling harness (UPSPerfHarness: PlaySports.Perf.StandardPlayProfile in CI,
`PS.Perf.RunHarness` on a device) and `PS.Perf.Capture` write Saved/Profiling/<Scenario>_<Tier>.json:
each system's game-thread time per frame (mean, median, 95th percentile, slowest) against its
platform tier's budget (Data/platform_tiers.json), and the work counted (bus events, AI decisions,
field scans). Unreal writes the keys camelCase; this reads them in any case.

  check [PATH] [--history FILE] [--record] [--summary FILE]
      PATH is a report or a directory of them (default Saved/Profiling). For each report:
        - a measured system whose 95th percentile is over its budget is a warning; over the
          budget times HardFailMultiplier (Data/perf_harness.json) the check fails;
        - with --history, a system whose 95th percentile is RegressionTolerance over the median
          of the last TrendWindow recorded runs of the same scenario and tier, and at least
          MinRegressionMs over it, is a regression warning;
        - --record appends the run to the history (CI records pushes to main only);
        - --summary appends a markdown table (GitHub's $GITHUB_STEP_SUMMARY).
      No report is not a failure: the automation step says why the tests didn't run.
  trend --history FILE [--scenario NAME] [--tier ID]
      Prints the recorded runs' 95th percentiles, oldest first.

Exit 0 when nothing failed, 1 otherwise. Run from the repo root.
"""

import argparse
import json
import os
import statistics
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HARNESS_TUNING = REPO / "Data" / "perf_harness.json"
DEFAULT_REPORTS = REPO / "Saved" / "Profiling"
DEFAULT_TUNING = {"HardFailMultiplier": 3.0, "RegressionTolerance": 0.25, "MinRegressionMs": 0.05, "TrendWindow": 10}


def field(obj, name, default=None):
    """obj[name] whatever the key's case (FJsonObjectConverter lower-cases the first letter)."""
    if not isinstance(obj, dict):
        return default
    wanted = name.lower()
    for key, value in obj.items():
        if key.lower() == wanted:
            return value
    return default


def system_name(value):
    """'EPSPerfSystem::AI' or 'AI' -> 'AI'."""
    return str(value).split("::")[-1]


def load_tuning(path=HARNESS_TUNING):
    tuning = dict(DEFAULT_TUNING)
    try:
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return tuning
    for key in tuning:
        value = field(payload, key)
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            tuning[key] = value
    return tuning


def load_report(path):
    try:
        return json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as exc:
        print(f"perf_budget: can't read {path}: {exc}")
        return None


def report_paths(path):
    path = Path(path)
    if path.is_file():
        return [path]
    if path.is_dir():
        return sorted(path.glob("*.json"))
    return []


def measured_systems(report):
    """{system: result} for the systems the capture measured."""
    systems = {}
    for result in field(report, "Systems", []) or []:
        if field(result, "bMeasured", False):
            systems[system_name(field(result, "System"))] = result
    return systems


def load_history(path):
    runs = []
    if not path or not Path(path).is_file():
        return runs
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            runs.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return runs


def history_entry(report, sha=None):
    return {
        "scenario": field(report, "Scenario", ""),
        "tier": field(report, "TierId", ""),
        "platform": field(report, "Platform", ""),
        "date": field(report, "Date", ""),
        "sha": sha or os.environ.get("GITHUB_SHA", ""),
        "frames": field(report, "Frames", 0),
        "bus_events": field(report, "BusEvents", 0),
        "ai_decisions": field(report, "AIDecisions", 0),
        "field_scans": field(report, "FieldScans", 0),
        "p95": {name: field(result, "P95Ms", 0.0) for name, result in measured_systems(report).items()},
    }


def trend_medians(history, scenario, tier, window):
    """Median 95th percentile per system over the last `window` runs of scenario on tier."""
    runs = [run for run in history if run.get("scenario") == scenario and run.get("tier") == tier][-window:]
    per_system = {}
    for run in runs:
        for name, p95 in (run.get("p95") or {}).items():
            per_system.setdefault(name, []).append(p95)
    return {name: statistics.median(values) for name, values in per_system.items()}, len(runs)


def evaluate(report, tuning, history=()):
    """(failures, warnings, rows) for one report. Each row: system, budget, p50, p95, max,
    trend median (None without history) and a status word."""
    failures, warnings, rows = [], [], []
    scenario, tier = field(report, "Scenario", ""), field(report, "TierId", "")
    medians, run_count = trend_medians(list(history), scenario, tier, int(tuning["TrendWindow"]))
    where = f"{scenario} on {tier}"
    for name, result in measured_systems(report).items():
        budget = float(field(result, "BudgetMs", 0.0) or 0.0)
        p95 = float(field(result, "P95Ms", 0.0) or 0.0)
        status = "ok"
        if budget > 0 and p95 > budget * float(tuning["HardFailMultiplier"]):
            failures.append(f"{where}: {name} p95 {p95:.3f} ms is over {tuning['HardFailMultiplier']}x its {budget:.2f} ms budget")
            status = "FAIL"
        elif budget > 0 and p95 > budget:
            warnings.append(f"{where}: {name} p95 {p95:.3f} ms is over its {budget:.2f} ms budget")
            status = "over budget"
        median = medians.get(name)
        if median is not None and p95 > median * (1 + float(tuning["RegressionTolerance"])) \
                and p95 - median >= float(tuning["MinRegressionMs"]):
            warnings.append(f"{where}: {name} p95 {p95:.3f} ms regressed from a median {median:.3f} ms over the last {run_count} runs")
            status = status if status != "ok" else "regressed"
        rows.append((name, budget, float(field(result, "P50Ms", 0.0) or 0.0), p95, float(field(result, "MaxMs", 0.0) or 0.0), median, status))
    return failures, warnings, rows


def format_table(report, rows):
    lines = [
        f"### {field(report, 'Scenario', '?')} on {field(report, 'TierId', '?')} ({field(report, 'Platform', '?')})",
        "",
        f"{field(report, 'Frames', 0)} frames at {field(report, 'TargetFrameRate', 0)} fps "
        f"({float(field(report, 'FrameBudgetMs', 0.0) or 0.0):.2f} ms a frame); "
        f"{field(report, 'BusEvents', 0)} bus events, {field(report, 'AIDecisions', 0)} AI decisions, "
        f"{field(report, 'FieldScans', 0)} field scans.",
        "",
        "| System | Budget ms | p50 ms | p95 ms | Max ms | Trend p95 ms | Status |",
        "|---|---|---|---|---|---|---|",
    ]
    for name, budget, p50, p95, slowest, median, status in rows:
        trend = "-" if median is None else f"{median:.3f}"
        lines.append(f"| {name} | {budget:.2f} | {p50:.3f} | {p95:.3f} | {slowest:.3f} | {trend} | {status} |")
    return "\n".join(lines) + "\n"


def check(path, history_path=None, record=False, summary_path=None, tuning=None):
    tuning = tuning or load_tuning()
    paths = report_paths(path)
    if not paths:
        print(f"perf_budget: no report under {path} - nothing to check")
        return 0
    history = load_history(history_path)
    failed = False
    for report_path in paths:
        report = load_report(report_path)
        if report is None:
            continue
        failures, warnings, rows = evaluate(report, tuning, history)
        table = format_table(report, rows)
        print(table)
        for warning in warnings:
            print(f"::warning::perf_budget: {warning}")
        for failure in failures:
            print(f"::error::perf_budget: {failure}")
        failed = failed or bool(failures)
        if summary_path:
            with open(summary_path, "a", encoding="utf-8") as summary:
                summary.write(table + "\n")
        if record and history_path:
            Path(history_path).parent.mkdir(parents=True, exist_ok=True)
            with open(history_path, "a", encoding="utf-8") as out:
                out.write(json.dumps(history_entry(report)) + "\n")
    return 1 if failed else 0


def trend(history_path, scenario=None, tier=None):
    runs = [run for run in load_history(history_path)
            if (scenario is None or run.get("scenario") == scenario) and (tier is None or run.get("tier") == tier)]
    if not runs:
        print(f"perf_budget: no recorded runs in {history_path}")
        return 0
    systems = sorted({name for run in runs for name in (run.get("p95") or {})})
    print("date | sha | scenario | tier | " + " | ".join(systems))
    for run in runs:
        values = " | ".join(f"{(run.get('p95') or {}).get(name, 0.0):.3f}" for name in systems)
        print(f"{run.get('date', '')} | {str(run.get('sha', ''))[:8]} | {run.get('scenario', '')} | {run.get('tier', '')} | {values}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check_cmd = sub.add_parser("check")
    check_cmd.add_argument("path", nargs="?", default=str(DEFAULT_REPORTS))
    check_cmd.add_argument("--history")
    check_cmd.add_argument("--record", action="store_true")
    check_cmd.add_argument("--summary")
    trend_cmd = sub.add_parser("trend")
    trend_cmd.add_argument("--history", required=True)
    trend_cmd.add_argument("--scenario")
    trend_cmd.add_argument("--tier")
    args = parser.parse_args(argv)
    if args.command == "check":
        return check(args.path, args.history, args.record, args.summary)
    return trend(args.history, args.scenario, args.tier)


if __name__ == "__main__":
    sys.exit(main())
