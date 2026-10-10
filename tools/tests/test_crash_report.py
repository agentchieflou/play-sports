"""Epic 117: crash report parsing, scrubbing, signatures and issue routing (no network)."""

import tempfile
import unittest
from pathlib import Path

from tools import crash_report

CONTEXT = """<?xml version="1.0" encoding="UTF-8"?>
<FGenericCrashContext>
    <RuntimeProperties>
        <CrashVersion>3</CrashVersion>
        <CrashGUID>UECC-Windows-0123456789ABCDEF</CrashGUID>
        <IsEnsure>false</IsEnsure>
        <IsAssert>true</IsAssert>
        <CrashType>Assert</CrashType>
        <ErrorMessage>Assertion failed: Index != INDEX_NONE [File:C:\\Users\\alice\\play-sports\\Source\\PlaySports\\Private\\PSRoster.cpp] [Line: 88]</ErrorMessage>
        <GameName>UE-play-sports</GameName>
        <BuildConfiguration>Development</BuildConfiguration>
        <PlatformName>WindowsEditor</PlatformName>
        <EngineVersion>5.8.0-0+++UE5+Release-5.8</EngineVersion>
        <UserName>alice</UserName>
        <LoginId>8d2f0c1e4b</LoginId>
        <EpicAccountId>acct-1234</EpicAccountId>
        <MachineId>MACHINE-5678</MachineId>
        <CommandLine>play-sports.uproject -secret-token=abc</CommandLine>
        <BaseDir>C:/Users/alice/UE_5.8/Engine/Binaries/Win64/</BaseDir>
        <CallStack>KERNELBASE!UnknownFunction []
UnrealEditor_Core!FDebug::CheckVerifyFailedImpl2() [D:\\build\\Engine\\Source\\Runtime\\Core\\Private\\Misc\\AssertionMacros.cpp:775]
UnrealEditor_PlaySports!UPSRoster::GetStarter() [C:\\Users\\alice\\play-sports\\Source\\PlaySports\\Private\\PSRoster.cpp:88]
UnrealEditor_PlaySports!APSGameMode::SpawnTeams() [C:\\Users\\alice\\play-sports\\Source\\PlaySports\\Private\\PSGameMode.cpp:212]
UnrealEditor_Engine!AActor::DispatchBeginPlay() [D:\\build\\Engine\\Source\\Runtime\\Engine\\Private\\Actor.cpp:4210]
UnrealEditor_Engine!UWorld::BeginPlay() [D:\\build\\Engine\\Source\\Runtime\\Engine\\Private\\World.cpp:5300]
kernel32!UnknownFunction []</CallStack>
    </RuntimeProperties>
    <GameData>
        <PS.Mode>PlayNow</PS.Mode>
        <PS.Plays>12</PS.Plays>
        <PS.RecentEvents>[10.0s] Snap: YardLine=20, Down=1, Distance=10
[12.5s] Tackle: Tackler=LB_1, Carrier=RB_2, YardsGained=4</PS.RecentEvents>
        <OtherPlugin.Key>not ours</OtherPlugin.Key>
    </GameData>
</FGenericCrashContext>
"""

UNSYMBOLIZED = CONTEXT.replace(
    "UnrealEditor_PlaySports!UPSRoster::GetStarter() [C:\\Users\\alice\\play-sports\\Source\\PlaySports\\Private\\PSRoster.cpp:88]",
    "UnrealEditor_PlaySports!UnknownFunction []").replace(
    "UnrealEditor_PlaySports!APSGameMode::SpawnTeams() [C:\\Users\\alice\\play-sports\\Source\\PlaySports\\Private\\PSGameMode.cpp:212]",
    "UnrealEditor_PlaySports!UnknownFunction []").replace(
    "UnrealEditor_Engine!AActor::DispatchBeginPlay() [D:\\build\\Engine\\Source\\Runtime\\Engine\\Private\\Actor.cpp:4210]",
    "UnrealEditor_Engine!UnknownFunction []").replace(
    "UnrealEditor_Engine!UWorld::BeginPlay() [D:\\build\\Engine\\Source\\Runtime\\Engine\\Private\\World.cpp:5300]",
    "UnrealEditor_Engine!UnknownFunction []")


class ParseTests(unittest.TestCase):
    def setUp(self):
        self.crash = crash_report.parse_crash_context(CONTEXT)

    def test_keeps_only_allowlisted_fields(self):
        fields = self.crash["fields"]
        self.assertEqual(fields["CrashType"], "Assert")
        self.assertEqual(fields["BuildConfiguration"], "Development")
        for private in ("UserName", "LoginId", "EpicAccountId", "MachineId", "CommandLine", "BaseDir"):
            self.assertNotIn(private, fields)
        rendered = crash_report.render_summary(self.crash, crash_report.signature(self.crash))
        for secret in ("alice", "8d2f0c1e4b", "acct-1234", "MACHINE-5678", "secret-token"):
            self.assertNotIn(secret, rendered)

    def test_scrubs_home_paths(self):
        self.assertIn("C:\\Users\\<user>\\play-sports", self.crash["fields"]["ErrorMessage"])
        self.assertTrue(any("C:\\Users\\<user>\\" in frame for frame in self.crash["frames"]))
        self.assertEqual(crash_report.scrub("/home/bob/x and /Users/carol/y"), "/home/<user>/x and /Users/<user>/y")

    def test_reads_only_the_games_keys(self):
        self.assertEqual(self.crash["game_data"]["PS.Mode"], "PlayNow")
        self.assertEqual(self.crash["game_data"]["PS.Plays"], "12")
        self.assertIn("Tackle: Tackler=LB_1", self.crash["game_data"]["PS.RecentEvents"])
        self.assertNotIn("OtherPlugin.Key", self.crash["game_data"])

    def test_rejects_what_is_not_a_crash_context(self):
        with self.assertRaises(ValueError):
            crash_report.parse_crash_context("<Other/>")
        with self.assertRaises(ValueError):
            crash_report.parse_crash_context("not xml")


class SignatureTests(unittest.TestCase):
    def test_signature_skips_crash_machinery_and_ignores_paths(self):
        crash = crash_report.parse_crash_context(CONTEXT)
        moved = crash_report.parse_crash_context(CONTEXT.replace("PSRoster.cpp:88]", "PSRoster.cpp:91]").replace("alice", "bob"))
        self.assertEqual(crash_report.signature(crash), crash_report.signature(moved))
        self.assertEqual(crash_report.frame_function(crash["frames"][2]), "UPSRoster::GetStarter()")

    def test_different_top_frame_is_a_different_crash(self):
        crash = crash_report.parse_crash_context(CONTEXT)
        other = crash_report.parse_crash_context(CONTEXT.replace("UPSRoster::GetStarter", "UPSRoster::GetBackup"))
        self.assertNotEqual(crash_report.signature(crash), crash_report.signature(other))

    def test_unsymbolized_stack_is_flagged_and_falls_back_to_the_message(self):
        crash = crash_report.parse_crash_context(UNSYMBOLIZED)
        self.assertTrue(crash_report.is_symbolized(crash_report.parse_crash_context(CONTEXT)["frames"]))
        self.assertFalse(crash_report.is_symbolized(crash["frames"]))
        self.assertIn("Unsymbolized", crash_report.render_summary(crash, crash_report.signature(crash)))
        renumbered = crash_report.parse_crash_context(UNSYMBOLIZED.replace("[Line: 88]", "[Line: 90]"))
        self.assertEqual(crash_report.signature(crash), crash_report.signature(renumbered))

    def test_title_carries_the_signature(self):
        crash = crash_report.parse_crash_context(CONTEXT)
        sig = crash_report.signature(crash)
        title = crash_report.issue_title(crash, sig)
        self.assertTrue(title.startswith("Crash: Assertion failed"))
        self.assertTrue(title.endswith(f"[crash-{sig}]"))
        self.assertLessEqual(len(title), 120)


class FakeGitHub:
    """Records gh api calls and answers them from a small in-memory issue list."""

    def __init__(self, issues=None):
        self.issues = list(issues or [])
        self.calls = []

    def __call__(self, args, payload=None):
        self.calls.append((args, payload))
        path = [a for a in args if a.startswith("repos/")][0]
        if "-X" in args and args[args.index("-X") + 1] == "GET":
            return self.issues if "page=1" in path else []
        if path.endswith("/labels"):
            raise RuntimeError("422 already exists")
        if path.endswith("/issues"):
            issue = {"number": 100 + len(self.issues), "title": payload["title"], "state": "open"}
            self.issues.append(issue)
            return issue
        return {}


class RoutingTests(unittest.TestCase):
    def make_crash_folder(self, root, name, text=CONTEXT):
        folder = Path(root) / name
        folder.mkdir(parents=True)
        (folder / crash_report.CONTEXT_FILE).write_text(text, encoding="utf-8")
        return folder

    def test_new_crash_creates_one_issue_and_marks_the_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = self.make_crash_folder(tmp, "UECC-1")
            self.make_crash_folder(tmp, "UECC-2")
            fake = FakeGitHub()
            self.assertEqual(crash_report.cmd_file([tmp], "owner/repo", False, runner=fake), 0)
            created = [c for c in fake.calls if c[0] == ["repos/owner/repo/issues"]]
            comments = [c for c in fake.calls if c[0][-1].endswith("/comments")]
            self.assertEqual(len(created), 1, "the same crash twice is one issue")
            self.assertEqual(len(comments), 1, "and a comment for the second sighting")
            self.assertEqual(created[0][1]["labels"], ["crash"])
            self.assertNotIn("alice", created[0][1]["body"])
            self.assertTrue((first / crash_report.REPORTED_MARKER).exists())

            fake.calls.clear()
            crash_report.cmd_file([tmp], "owner/repo", False, runner=fake)
            self.assertEqual(fake.calls, [], "reported folders are not filed again")

    def test_closed_issue_is_reopened_as_a_regression(self):
        crash = crash_report.parse_crash_context(CONTEXT)
        sig = crash_report.signature(crash)
        fake = FakeGitHub([{"number": 7, "title": f"Crash: old [crash-{sig}]", "state": "closed"}])
        action, number = crash_report.GitHubIssues("owner/repo", fake).route(crash, sig, "UECC-3")
        self.assertEqual((action, number), ("reopened", 7))
        self.assertIn((["-X", "PATCH", "repos/owner/repo/issues/7"], {"state": "open"}), fake.calls)
        comment = [c for c in fake.calls if c[0] == ["repos/owner/repo/issues/7/comments"]][0]
        self.assertIn("regression", comment[1]["body"])

    def test_summarize_with_no_crashes_succeeds(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(crash_report.cmd_summarize([tmp, str(Path(tmp) / "missing")]), 0)


if __name__ == "__main__":
    unittest.main()
