"""Epic 114: the performance budget check and its trend history (no engine, no network)."""

import io
import json
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from tools import perf_budget

TUNING = {"HardFailMultiplier": 3.0, "RegressionTolerance": 0.25, "MinRegressionMs": 0.05, "TrendWindow": 3}


def make_report(p95_by_system, scenario="StandardPlay", tier="DesktopHigh", budget=1.0):
    """A report as FJsonObjectConverter writes it: camelCase keys, enum names as strings."""
    systems = []
    for name in ("Simulation", "AI", "Telemetry", "Overlays", "UI", "Animation", "Crowd", "Audio"):
        p95 = p95_by_system.get(name)
        systems.append({
            "system": name,
            "bMeasured": p95 is not None,
            "budgetMs": budget,
            "meanMs": (p95 or 0.0) / 2,
            "p50Ms": (p95 or 0.0) / 2,
            "p95Ms": p95 or 0.0,
            "maxMs": (p95 or 0.0) * 1.5,
            "bOverBudget": bool(p95 and p95 > budget),
        })
    return {"scenario": scenario, "tierId": tier, "platform": "Windows", "date": "2026-10-10T00:00:00.000Z",
            "targetFrameRate": 60, "frameBudgetMs": 16.67, "frames": 330, "systems": systems,
            "busEvents": 20, "aIDecisions": 7000, "fieldScans": 300}


def quiet_check(*args, **kwargs):
    """perf_budget.check without its output: its ::warning:: and ::error:: lines would become
    GitHub annotations on the CI run that runs these tests."""
    with redirect_stdout(io.StringIO()):
        return perf_budget.check(*args, **kwargs)


class PerfBudgetTests(unittest.TestCase):
    def test_reads_unreal_keys_in_any_case(self):
        report = make_report({"AI": 0.4})
        self.assertEqual(perf_budget.field(report, "TierId"), "DesktopHigh")
        self.assertEqual(perf_budget.field(report, "AIDecisions"), 7000)
        self.assertEqual(list(perf_budget.measured_systems(report)), ["AI"])
        self.assertEqual(perf_budget.system_name("EPSPerfSystem::Overlays"), "Overlays")

    def test_within_budget_passes(self):
        failures, warnings, rows = perf_budget.evaluate(make_report({"AI": 0.5, "Telemetry": 0.1}), TUNING)
        self.assertEqual((failures, warnings), ([], []))
        self.assertEqual({row[0] for row in rows}, {"AI", "Telemetry"})
        self.assertTrue(all(row[-1] == "ok" for row in rows))

    def test_over_budget_warns_and_far_over_fails(self):
        failures, warnings, _ = perf_budget.evaluate(make_report({"AI": 1.5}), TUNING)
        self.assertEqual(failures, [])
        self.assertEqual(len(warnings), 1)
        self.assertIn("over its 1.00 ms budget", warnings[0])
        failures, _, rows = perf_budget.evaluate(make_report({"AI": 3.5}), TUNING)
        self.assertEqual(len(failures), 1)
        self.assertEqual(rows[0][-1], "FAIL")

    def test_unmeasured_systems_are_not_judged(self):
        failures, warnings, rows = perf_budget.evaluate(make_report({}), TUNING)
        self.assertEqual((failures, warnings, rows), ([], [], []))

    def test_regression_against_the_trend(self):
        history = [perf_budget.history_entry(make_report({"AI": 0.30}), sha="a"),
                   perf_budget.history_entry(make_report({"AI": 0.32}), sha="b"),
                   perf_budget.history_entry(make_report({"AI": 0.31}), sha="c"),
                   perf_budget.history_entry(make_report({"AI": 0.90}, tier="MobileLow"), sha="d")]
        _, warnings, rows = perf_budget.evaluate(make_report({"AI": 0.60}), TUNING, history)
        self.assertEqual(len(warnings), 1)
        self.assertIn("regressed from a median 0.310 ms over the last 3 runs", warnings[0])
        self.assertAlmostEqual(rows[0][5], 0.31)
        self.assertEqual(rows[0][-1], "regressed")
        # Within the tolerance, or a few microseconds over: noise, not a regression.
        _, warnings, _ = perf_budget.evaluate(make_report({"AI": 0.36}), TUNING, history)
        self.assertEqual(warnings, [])
        tiny = [perf_budget.history_entry(make_report({"AI": 0.010}), sha=str(n)) for n in range(3)]
        _, warnings, _ = perf_budget.evaluate(make_report({"AI": 0.030}), TUNING, tiny)
        self.assertEqual(warnings, [])

    def test_check_records_history_and_writes_the_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            reports = Path(tmp) / "Profiling"
            reports.mkdir()
            (reports / "StandardPlay_DesktopHigh.json").write_text(json.dumps(make_report({"AI": 0.4})), encoding="utf-8")
            history = Path(tmp) / "ci" / "perf_history.jsonl"
            summary = Path(tmp) / "summary.md"
            self.assertEqual(quiet_check(reports, history, record=True, summary_path=summary, tuning=TUNING), 0)
            self.assertEqual(quiet_check(reports, history, record=True, summary_path=summary, tuning=TUNING), 0)
            runs = perf_budget.load_history(history)
            self.assertEqual(len(runs), 2)
            self.assertEqual(runs[0]["p95"], {"AI": 0.4})
            self.assertIn("| AI | 1.00 |", summary.read_text(encoding="utf-8"))
            # Without --record the history is left alone.
            self.assertEqual(quiet_check(reports, history, record=False, tuning=TUNING), 0)
            self.assertEqual(len(perf_budget.load_history(history)), 2)

    def test_check_fails_far_over_budget_and_tolerates_no_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "Live_MobileLow.json"
            report.write_text(json.dumps(make_report({"Simulation": 5.0}, scenario="Live", tier="MobileLow")), encoding="utf-8")
            self.assertEqual(quiet_check(report, tuning=TUNING), 1)
            self.assertEqual(quiet_check(Path(tmp) / "missing", tuning=TUNING), 0)

    def test_harness_tuning_comes_from_the_data_file(self):
        tuning = perf_budget.load_tuning()
        data = json.loads(perf_budget.HARNESS_TUNING.read_text(encoding="utf-8"))
        for key in ("HardFailMultiplier", "RegressionTolerance", "MinRegressionMs", "TrendWindow"):
            self.assertEqual(tuning[key], data[key])


if __name__ == "__main__":
    unittest.main()
