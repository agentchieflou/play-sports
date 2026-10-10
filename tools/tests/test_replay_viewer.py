"""The Film Room replay viewer (tools/replay_viewer/, lane S5): its loader's field contract
against the C++ replay format, a fixture shaped like UPSReplayFormat::SerializeToJson output, the
synthetic sample, the staging script, and the page's own JS loader when Node is installed.

No engine and no network. The C++ side is read from the headers: every field the viewer reads
must be a UPROPERTY of its struct, and every key in the fixture and the sample must be one, spelled
as FJsonObjectConverter writes it (first letter lower-cased, 'ID' as 'Id').
"""

import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.replay_viewer import stage

REPO = Path(__file__).resolve().parent.parent.parent
VIEWER = REPO / "tools" / "replay_viewer"
PUBLIC = REPO / "Source" / "PlaySports" / "Public"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "replay_viewer_recording.json"
SCHEMA = json.loads((VIEWER / "replay_schema.json").read_text(encoding="utf-8"))

NUMBER_TYPES = {"int32", "int64", "uint8", "int16", "uint16", "uint32", "float", "double"}
STRING_TYPES = {"FString", "FName", "FText", "FDateTime"}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def body_after(text, start):
    """The text inside the braces that open at or after start."""
    open_at = text.index("{", start)
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_at + 1:i]
    raise ValueError("unbalanced braces")


def read_cpp():
    """Every USTRUCT's UPROPERTYs ({name: (type, transient)}) and every UENUM's values, from
    Source/PlaySports/Public."""
    structs, enums = {}, {}
    prop = re.compile(r"UPROPERTY\(([^)]*)\)\s*([A-Za-z_][\w:<>, *]*?)\s+(\w+)\s*(?:=[^;]*)?;", re.S)
    for header in sorted(PUBLIC.glob("*.h")):
        text = strip_comments(header.read_text(encoding="utf-8", errors="replace"))
        for m in re.finditer(r"USTRUCT\([^)]*\)\s*struct\s+(?:\w+_API\s+)?(F\w+)", text):
            body = body_after(text, m.end())
            structs[m.group(1)] = {p.group(3): (p.group(2).strip(), "Transient" in p.group(1)) for p in prop.finditer(body)}
        for m in re.finditer(r"UENUM\([^)]*\)\s*enum\s+class\s+(E\w+)\s*(?::\s*\w+)?", text):
            body = re.sub(r"UMETA\([^)]*\)", "", body_after(text, m.end()))
            enums[m.group(1)] = [v.split("=")[0].strip() for v in body.split(",") if v.strip()]
    return structs, enums


STRUCTS, ENUMS = read_cpp()


def standardize(name):
    """FJsonObjectConverter::StandardizeCase."""
    return (name[0].lower() + name[1:]).replace("ID", "Id")


class ShapeChecker:
    """Checks a JSON document is what FJsonObjectConverter writes for a C++ struct: each key is
    one of the struct's (non-transient) UPROPERTYs, spelled as the converter spells it, holding a
    value of that property's type. Keys may be missing (the viewer tolerates it); extra or
    misspelled ones are reported."""

    def __init__(self):
        self.problems = []

    def check(self, value, cpp_type, path):
        cpp_type = cpp_type.replace("const ", "").strip()
        if cpp_type.startswith("TArray<"):
            if not isinstance(value, list):
                self.problems.append(f"{path}: expected an array")
                return
            inner = cpp_type[len("TArray<"):-1].strip()
            for i, item in enumerate(value):
                self.check(item, inner, f"{path}[{i}]")
            return
        if cpp_type.startswith("TEnumAsByte<"):
            cpp_type = cpp_type[len("TEnumAsByte<"):-1].strip()
        if cpp_type == "bool":
            ok = isinstance(value, bool)
        elif cpp_type in NUMBER_TYPES:
            ok = isinstance(value, (int, float)) and not isinstance(value, bool)
        elif cpp_type in STRING_TYPES:
            ok = isinstance(value, str)
        elif cpp_type == "FVector":
            ok = isinstance(value, dict) and set(value) == {"x", "y", "z"} and all(isinstance(value[k], (int, float)) for k in "xyz")
        elif cpp_type in ENUMS:
            ok = isinstance(value, str) and value.split("::")[-1] in ENUMS[cpp_type]
        elif cpp_type in STRUCTS:
            if not isinstance(value, dict):
                self.problems.append(f"{path}: expected an object for {cpp_type}")
                return
            self.struct(value, cpp_type, path)
            return
        else:
            return
        if not ok:
            self.problems.append(f"{path}: {value!r} is not a {cpp_type}")

    def struct(self, obj, struct_name, path):
        props = STRUCTS[struct_name]
        by_key = {standardize(name): (name, t, transient) for name, (t, transient) in props.items()}
        for key, value in obj.items():
            if key not in by_key:
                self.problems.append(f"{path}.{key}: not a UPROPERTY of {struct_name} as FJsonObjectConverter spells it")
                continue
            name, cpp_type, transient = by_key[key]
            if transient:
                self.problems.append(f"{path}.{key}: {struct_name}::{name} is Transient; SerializeToJson leaves it out")
                continue
            self.check(value, cpp_type, f"{path}.{key}")

    def recording(self, doc, path="recording"):
        self.struct(doc, "FPSReplayRecording", path)
        for i, event in enumerate(doc.get("events", [])):
            kind = event.get("eventType")
            spec = SCHEMA["payloads"].get(kind)
            if spec and event.get("payloadJson"):
                self.struct(json.loads(event["payloadJson"]), spec["struct"], f"{path}.events[{i}].payloadJson")
        return self.problems


def node():
    return shutil.which("node")


class ContractTests(unittest.TestCase):
    def test_every_field_the_viewer_reads_is_a_cpp_uproperty(self):
        for struct_name, spec in SCHEMA["structs"].items():
            self.assertIn(struct_name, STRUCTS, f"{struct_name} not found in {spec['header']}")
            self.assertTrue((REPO / spec["header"]).is_file(), spec["header"])
            for name in spec["fields"]:
                self.assertIn(name, STRUCTS[struct_name], f"{struct_name}::{name} is read by the viewer but is not a UPROPERTY")
        for event_type, spec in SCHEMA["payloads"].items():
            if event_type.startswith("_"):
                continue
            self.assertIn(event_type, ENUMS["EPSTelemetryEventType"], event_type)
            for name in spec["fields"]:
                self.assertIn(name, STRUCTS[spec["struct"]], f"{spec['struct']}::{name}")

    def test_enum_values_the_viewer_knows_exist(self):
        for enum_name, spec in SCHEMA["enums"].items():
            for value in spec["values"]:
                self.assertIn(value, ENUMS[enum_name], f"{enum_name}::{value}")

    def test_known_format_version_is_the_cpp_one(self):
        header = (PUBLIC / "PSReplayFormat.h").read_text(encoding="utf-8")
        version = int(re.search(r"CurrentFormatVersion\s*=\s*(\d+)", header).group(1))
        self.assertEqual(SCHEMA["knownFormatVersion"], version)

    def test_units_match_the_engine(self):
        pawn = (REPO / "Source" / "PlaySports" / "Private" / "PSPlayerPawn.cpp").read_text(encoding="utf-8")
        radius, half = re.search(r"InitCapsuleSize\(\s*([\d.]+)f?\s*,\s*([\d.]+)f?\s*\)", pawn).groups()
        self.assertEqual(SCHEMA["units"]["capsuleRadiusCm"], float(radius))
        self.assertEqual(SCHEMA["units"]["capsuleHalfHeightCm"], float(half))
        ball = (REPO / "Source" / "PlaySports" / "Private" / "PSBall.cpp").read_text(encoding="utf-8")
        self.assertEqual(SCHEMA["units"]["ballRadiusCm"], float(re.search(r"InitSphereRadius\(\s*([\d.]+)f?\s*\)", ball).group(1)))

    def test_field_defaults_equal_the_data_file(self):
        data = json.loads((REPO / "Data" / "field_dimensions.json").read_text(encoding="utf-8"))
        defaults = {k: v for k, v in SCHEMA["fieldDefaults"].items() if k != "about"}
        self.assertEqual(defaults, data)


class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.text = FIXTURE.read_text(encoding="utf-8")

    def test_fixture_is_shaped_like_the_cpp_output(self):
        problems = ShapeChecker().recording(json.loads(self.text))
        self.assertEqual(problems, [])

    def test_loader_contract_accepts_the_fixture(self):
        doc = stage.parse(self.text)
        errors, warnings = stage.validate_recording(doc, SCHEMA)
        self.assertEqual((errors, warnings), ([], []))
        summary = stage.summarize(doc)
        self.assertEqual(summary["frames"], 4)
        self.assertEqual(summary["players"], 2)
        self.assertEqual(summary["events"], {"Snap": 1, "Throw": 1, "Tackle": 1, "PhaseChange": 1, "PlayResult": 1})

    def test_keys_are_read_in_any_case(self):
        doc = stage.parse(self.text.replace('"formatVersion"', '"FormatVersion"').replace('"frames"', '"Frames"'))
        self.assertEqual(stage.validate_recording(doc, SCHEMA)[0], [])
        self.assertEqual(stage.enum_name("EPSTeamSide::Offense"), "Offense")

    def test_unplayable_recordings_are_refused(self):
        doc = stage.parse(self.text)
        doc["frames"] = []
        self.assertIn("recording.Frames is empty.", stage.validate_recording(doc, SCHEMA)[0])
        doc = stage.parse(self.text)
        doc["header"]["formatversion"] = 0
        self.assertTrue(any("FormatVersion" in e for e in stage.validate_recording(doc, SCHEMA)[0]))
        doc = stage.parse(self.text)
        del doc["frames"][0]["pawns"][0]["location"]
        self.assertTrue(any("Location is missing" in e for e in stage.validate_recording(doc, SCHEMA)[0]))
        self.assertEqual(stage.validate_recording([], SCHEMA)[0], ["The file is not a JSON object."])

    def test_a_newer_format_version_plays_with_a_warning(self):
        doc = stage.parse(self.text)
        doc["header"]["formatversion"] = SCHEMA["knownFormatVersion"] + 1
        errors, warnings = stage.validate_recording(doc, SCHEMA)
        self.assertEqual(errors, [])
        self.assertEqual(len(warnings), 1)


class SyntheticSampleTests(unittest.TestCase):
    def test_the_sample_is_labelled_synthetic_everywhere(self):
        sample = VIEWER / "sample"
        prefix = SCHEMA["synthetic"]["filePrefix"]
        files = sorted(p.name for p in sample.glob("*.json"))
        self.assertTrue(files)
        self.assertTrue(all(name.startswith(prefix) for name in files), files)
        index = stage.parse_index(stage.parse((sample / (prefix + "index.json")).read_text(encoding="utf-8")), SCHEMA)
        self.assertEqual(len(index), 2)
        for entry in index:
            self.assertIs(entry["synthetic"], True)
            doc = stage.parse((sample / entry["file"]).read_text(encoding="utf-8"))
            build = stage.field(stage.field(doc, "Header"), "GameBuildVersion")
            self.assertIn(SCHEMA["synthetic"]["buildMarker"], build.upper())

    def test_the_sample_is_shaped_like_the_cpp_output_and_plays(self):
        for path in sorted((VIEWER / "sample").glob("SYNTHETIC_*.json")):
            if path.name.endswith("index.json"):
                continue
            text = path.read_text(encoding="utf-8")
            self.assertEqual(ShapeChecker().recording(json.loads(text)), [], path.name)
            errors, _ = stage.validate_recording(stage.parse(text), SCHEMA)
            self.assertEqual(errors, [], path.name)
            self.assertEqual(stage.summarize(stage.parse(text))["players"], 22, path.name)


class StageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def quiet(self, *args, **kwargs):
        return stage.stage(*args, log=lambda *_: None, **kwargs)

    def recordings(self, names):
        folder = self.tmp / "play-demos"
        folder.mkdir()
        for name in names:
            shutil.copyfile(FIXTURE, folder / name)
        (folder / "index.json").write_text(json.dumps({"Plays": [{"File": n, "Result": "Tackle", "Yards": -1} for n in names]}), encoding="utf-8")
        return folder

    def test_stages_real_recordings_with_the_viewer_data_and_model(self):
        files = self.quiet(self.tmp / "out", recordings=self.recordings(["sack_seed7.json"]))
        out = self.tmp / "out"
        for published in ["index.html", "app.js", "loader.js", "gait.js", "field.js", "replay_schema.json",
                          "data/field_dimensions.json", "data/sample_teams.json", "assets/standin.glb", "assets/LICENSE",
                          "recordings/index.json", "recordings/sack_seed7.json"]:
            self.assertTrue((out / published).is_file(), published)
            self.assertIn(published, files)
        self.assertFalse((out / "sample").exists(), "the synthetic sample is not staged with real recordings")

    def test_refuses_synthetic_files_as_the_games_recordings(self):
        with self.assertRaises(stage.StageError):
            self.quiet(self.tmp / "out", recordings=self.recordings(["SYNTHETIC_run.json"]))

    def test_refuses_a_recording_the_viewer_cant_play(self):
        folder = self.recordings(["broken.json"])
        doc = json.loads((folder / "broken.json").read_text(encoding="utf-8"))
        doc["frames"] = []
        (folder / "broken.json").write_text(json.dumps(doc), encoding="utf-8")
        with self.assertRaises(stage.StageError):
            self.quiet(self.tmp / "out", recordings=folder)

    def test_synthetic_preview(self):
        self.quiet(self.tmp / "out", synthetic=True)
        self.assertTrue((self.tmp / "out" / "sample" / "SYNTHETIC_index.json").is_file())
        self.assertFalse((self.tmp / "out" / "recordings").exists())
        self.assertFalse((self.tmp / "out" / "index.html").read_text(encoding="utf-8").lower().startswith("<!doctype"))

    def test_standalone_page_has_its_own_skeleton(self):
        self.quiet(self.tmp / "out", synthetic=True, standalone=True)
        page = (self.tmp / "out" / "index.html").read_text(encoding="utf-8")
        self.assertTrue(page.startswith("<!doctype html>"))
        self.assertIn('name="viewport"', page)
        self.assertIn("<title>Play-Sports Film Room</title>", page)


class PageTests(unittest.TestCase):
    def test_libraries_are_pinned_to_one_exact_three_version_on_allowed_hosts(self):
        page = (VIEWER / "index.html").read_text(encoding="utf-8")
        imports = json.loads(re.search(r'<script type="importmap">(.*?)</script>', page, re.S).group(1))["imports"]
        versions = set()
        for url in imports.values():
            m = re.match(r"https://(cdnjs\.cloudflare\.com/ajax/libs/three\.js/|cdn\.jsdelivr\.net/npm/three@)(\d+\.\d+\.\d+)/", url)
            self.assertIsNotNone(m, url)
            versions.add(m.group(2))
        self.assertEqual(len(versions), 1, versions)
        self.assertNotIn("<!doctype", page.lower(), "the publish step adds the document skeleton")
        for script in ["app.js", "gait.js", "field.js", "loader.js"]:
            for spec in re.findall(r'^import .*? from "([^"]+)";', (VIEWER / script).read_text(encoding="utf-8"), re.M):
                self.assertTrue(spec == "three" or spec.startswith("three/addons/") or spec.startswith("./"), f"{script}: {spec}")

    def test_every_staged_viewer_file_exists(self):
        for name in stage.VIEWER_FILES:
            self.assertTrue((VIEWER / name).is_file(), name)


@unittest.skipUnless(node(), "Node isn't installed: the page's JS loader is checked where it is")
class JsLoaderTests(unittest.TestCase):
    """The page's own loader.js, run by Node, on the same files."""

    def run_loader(self, *paths):
        result = subprocess.run([node(), str(VIEWER / "check_loader.mjs"), *map(str, paths)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_js_loader_reads_the_fixture(self):
        out = self.run_loader(FIXTURE)[str(FIXTURE)]
        self.assertNotIn("error", out)
        self.assertEqual((out["frames"], out["offense"], out["defense"]), (4, 1, 1))
        self.assertEqual(out["formatVersion"], 2)
        self.assertEqual(out["events"], {"Snap": 1, "Throw": 1, "Tackle": 1, "Whistle": 1, "PlayResult": 1})
        self.assertFalse(out["synthetic"])
        self.assertEqual(out["players"][0]["name"], "Test Quarterback")
        self.assertEqual(out["result"]["result"], "Tackle")
        self.assertTrue(out["result"]["sack"])
        self.assertEqual(out["carriers"], [-1, 0, 0, 0])
        self.assertEqual(out["contactFrames"], 1, "the defensive back reaches the quarterback in the last frame")
        labels = {e["type"]: e["label"] for e in out["timeline"]}
        self.assertEqual(labels["Tackle"], "Sack by Test Defensive Back")

    def test_js_and_python_agree_on_the_synthetic_sample(self):
        paths = [p for p in sorted((VIEWER / "sample").glob("SYNTHETIC_*.json")) if not p.name.endswith("index.json")]
        out = self.run_loader(*paths)
        for path in paths:
            js = out[str(path)]
            py = stage.summarize(stage.parse(path.read_text(encoding="utf-8")))
            self.assertNotIn("error", js)
            self.assertTrue(js["synthetic"])
            self.assertEqual((js["frames"], js["offense"] + js["defense"]), (py["frames"], py["players"]))
            self.assertEqual(js["duration"], py["duration"])

    def test_participants_teams_and_clock_offsets(self):
        """S4's additive Participants and Teams name the players and their kits; events stamped
        on another clock are moved onto the frames' by their keyframes."""
        doc = json.loads(FIXTURE.read_text(encoding="utf-8"))
        doc["participants"] = [
            {"playerId": "QB_01", "teamId": "Hawks", "teamSide": "Offense", "role": "Quarterback", "displayName": "R. Talon", "jerseyNumber": 7},
            {"playerId": "DB_01", "teamId": "Bears", "teamSide": "Defense", "role": "DefensiveBack", "displayName": "K. Maul", "jerseyNumber": 24},
        ]
        doc["teams"] = [{"teamId": "Hawks", "displayName": "Harbor Hawks", "abbreviation": "HAW", "primaryColor": "#0B5E8A"},
                        {"teamId": "Bears", "displayName": "Blackridge Bears", "abbreviation": "BER"}]
        for event in doc["events"]:
            event["timestampSeconds"] += 100.0  # the world's clock, not the sampler's
        path = Path(tempfile.mkdtemp()) / "participants.json"
        try:
            path.write_text(json.dumps(doc), encoding="utf-8")
            out = self.run_loader(path)[str(path)]
        finally:
            shutil.rmtree(path.parent, ignore_errors=True)
        players = {p["id"]: p for p in out["players"]}
        self.assertEqual((players["QB_01"]["name"], players["QB_01"]["jersey"], players["QB_01"]["teamId"]), ("R. Talon", "7", "Hawks"))
        self.assertEqual(players["DB_01"]["jersey"], "24")
        sides = {t["teamId"]: t["side"] for t in out["teams"]}
        self.assertEqual(sides, {"Hawks": "Offense", "Bears": "Defense"})
        self.assertEqual(out["teams"][0]["primaryColor"], "#0B5E8A")
        self.assertEqual(out["eventClock"], "shifted by keyframes")
        snap = next(e for e in out["timeline"] if e["type"] == "Snap")
        self.assertAlmostEqual(snap["t"], 0.0, places=3)

    def test_labels_fall_back_to_role_and_index(self):
        out = self.run_loader(VIEWER / "sample" / "SYNTHETIC_handoff_run.json")
        players = next(iter(out.values()))["players"]
        linemen = sorted(p["roleIndex"] for p in players if p["role"] == "OffensiveLineman")
        self.assertEqual(linemen, [1, 2, 3, 4, 5])
        self.assertTrue(all(p["jersey"] is None for p in players))
        quarterback = next(p for p in players if p["role"] == "Quarterback")
        self.assertEqual(quarterback["roleCount"], 1)


if __name__ == "__main__":
    unittest.main()
