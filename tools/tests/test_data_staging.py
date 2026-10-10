"""The packaged build's data staging audit (Epic 145.2; no Unreal).

A packaged build stages the whole Data/ folder as loose files under its project directory
(Source/PlaySports/PlaySports.Build.cs), and the loaders read FPaths::ProjectDir() / "Data/...".
PSDataPaths.cpp lists every data file a loader reads by default; the packaged smoke test checks the
build carries each one. This test keeps that list complete: every data path the game's source
names must be listed, exist, and lie inside the staged folder.
"""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
SOURCE = REPO / "Source" / "PlaySports"
BUILD_CS = SOURCE / "PlaySports.Build.cs"
REGISTRY = SOURCE / "Private" / "PSDataPaths.cpp"

# A data path literal: "Data/x.json", or "../Data/x.csv" (string tables resolve from Content/).
LITERAL_RE = re.compile(r'"(?:\.\./)?(Data/[A-Za-z0-9_/.-]+\.(?:json|csv))"')
# Built from parts: TEXT("Data") / TEXT("x.json"), or the play-call subsystem's DataPath(TEXT("x.json")).
JOINED_RE = re.compile(r'TEXT\("Data"\)\s*/\s*TEXT\("([A-Za-z0-9_/.-]+\.(?:json|csv))"\)')
DATA_PATH_RE = re.compile(r'DataPath\(TEXT\("([A-Za-z0-9_/.-]+\.(?:json|csv))"\)\)')

# Paths the source names that the game writes rather than reads from the project's Data/.
GENERATED_OUTPUTS = {
    # UPSLeagueGenerator writes a generated league's team file under its output root
    # (Saved/GeneratedLeague); the shipped league config names Data/sample_teams.json.
    "Data/league_teams.json",
}


def source_files():
    for path in sorted(SOURCE.rglob("*")):
        if path.suffix in (".h", ".cpp") and "Tests" not in path.relative_to(SOURCE).parts:
            yield path


def paths_named_in_source():
    """Each data path the game's source names, with the first file that names it."""
    found = {}
    for path in source_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        names = LITERAL_RE.findall(text)
        names += ["Data/" + name for name in JOINED_RE.findall(text)]
        names += ["Data/" + name for name in DATA_PATH_RE.findall(text)]
        for name in names:
            found.setdefault(name, path.relative_to(REPO).as_posix())
    return found


def registry_paths():
    text = REGISTRY.read_text(encoding="utf-8")
    block = re.search(r"DefaultDataFiles\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not block:
        raise AssertionError(f"{REGISTRY.relative_to(REPO)} has no DefaultDataFiles[] list")
    return re.findall(r'TEXT\("([^"]+)"\)', block.group(1))


class DataStagingTest(unittest.TestCase):
    def test_the_build_stages_the_data_folder(self):
        build = BUILD_CS.read_text(encoding="utf-8")
        self.assertIn('RuntimeDependencies.Add("$(ProjectDir)/Data/...", StagedFileType.NonUFS);', build,
                      "PlaySports.Build.cs no longer stages Data/: the loaders' paths won't resolve in a packaged build")

    def test_registry_files_exist_inside_the_staged_folder(self):
        listed = registry_paths()
        self.assertGreaterEqual(len(listed), 80)
        self.assertEqual(len(listed), len(set(listed)), "PSDataPaths.cpp lists a file twice")
        for name in listed:
            with self.subTest(name=name):
                self.assertTrue(name.startswith("Data/") and ".." not in name.split("/"),
                                f"{name} is outside the staged Data/ folder")
                self.assertTrue((REPO / name).is_file(), f"{name} is listed in PSDataPaths.cpp but doesn't exist")

    def test_every_data_path_in_the_source_is_listed(self):
        listed = set(registry_paths())
        unlisted = {name: where for name, where in paths_named_in_source().items()
                    if name not in listed and name not in GENERATED_OUTPUTS}
        self.assertEqual(unlisted, {}, "these data files are read by the source but missing from "
                         "PSDataPaths.cpp's DefaultDataFiles (add them, so the packaged smoke test checks "
                         "the build carries them): " + ", ".join(f"{n} ({w})" for n, w in sorted(unlisted.items())))

    def test_the_scan_sees_each_way_a_path_is_written(self):
        sample = '''
            FString A = FPaths::ProjectDir() / TEXT("Data/formations.json");
            LOCTABLE_FROMFILE_GAME("PSUI", "PSUI", "../Data/ui_text.csv");
            FString B = FPaths::ProjectDir() / TEXT("Data") / TEXT("session_telemetry.json");
            return PSPlayCallPrivate::DataPath(TEXT("play_call.json"));
            UE_LOG(LogTemp, Warning, TEXT("'%s' is not in Data/ui_text.csv."), *Key);
        '''
        self.assertEqual(LITERAL_RE.findall(sample), ["Data/formations.json", "Data/ui_text.csv"])
        self.assertEqual(JOINED_RE.findall(sample), ["session_telemetry.json"])
        self.assertEqual(DATA_PATH_RE.findall(sample), ["play_call.json"])


if __name__ == "__main__":
    unittest.main()
