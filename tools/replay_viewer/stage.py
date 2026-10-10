#!/usr/bin/env python
"""Stages the Film Room replay viewer as one folder, ready to publish or serve (lane S5).

  python tools/replay_viewer/stage.py --recordings <folder> --out <folder> [--print-files]
      <folder> holds index.json and the replay files it lists: lane S4's play-demos artifact
      (gh run download <run> -R agentchieflou/play-sports -n play-demos -D <folder>).
      Every listed recording is checked against tools/replay_viewer/replay_schema.json first;
      one that the viewer can't play stops the staging. A SYNTHETIC_* file is refused here:
      the synthetic sample is never staged as the game's recordings.
  python tools/replay_viewer/stage.py --synthetic --out <folder>
      A preview with the synthetic test sample only (shown under a "not the game" banner).
  --standalone
      index.html gets its own document skeleton (doctype, charset, phone viewport), for serving
      the folder anywhere but the Artifact host, which adds that skeleton itself when it
      publishes (so the default leaves it out).

The staged folder:
  index.html app.js loader.js gait.js field.js replay_schema.json   the viewer
  data/field_dimensions.json data/sample_teams.json                 from Data/
  assets/standin.glb assets/LICENSE                                 RawAssets/world/people/ (CC0)
  recordings/index.json recordings/<play>.json ...                  the recordings, as listed
  sample/SYNTHETIC_*                                                with --synthetic only
--print-files prints the published-path -> source map for the Artifact tool's `files`.

Exit 0 when staged, 1 otherwise. Run from the repo root.
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
VIEWER = REPO / "tools" / "replay_viewer"
SCHEMA_PATH = VIEWER / "replay_schema.json"
VIEWER_FILES = ["index.html", "app.js", "loader.js", "gait.js", "field.js", "replay_schema.json"]
DATA_FILES = {"data/field_dimensions.json": REPO / "Data" / "field_dimensions.json",
              "data/sample_teams.json": REPO / "Data" / "sample_teams.json"}
ASSET_FILES = {"assets/standin.glb": REPO / "RawAssets" / "world" / "people" / "standin.glb",
               "assets/LICENSE": REPO / "RawAssets" / "world" / "people" / "LICENSE"}
# The Artifact limits (16 MB a text file, 15 MB a binary one, 255 files a publish).
MAX_TEXT_BYTES = 16 * 1024 * 1024
MAX_BINARY_BYTES = 15 * 1024 * 1024
MAX_FILES = 255


def load_schema(path=SCHEMA_PATH):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def lower_keys(pairs):
    """object_pairs_hook: every key lower-cased, as the page's loader reads them."""
    return {key.lower(): value for key, value in pairs}


def parse(text):
    if text.startswith("﻿"):
        text = text[1:]
    return json.loads(text, object_pairs_hook=lower_keys)


def field(obj, *names):
    """obj[name] for the first of names present (any case); None when none is."""
    if not isinstance(obj, dict):
        return None
    for name in names:
        if isinstance(name, (list, tuple)):
            found = field(obj, *name)
            if found is not None:
                return found
            continue
        value = obj.get(str(name).lower())
        if value is not None:
            return value
    return None


def enum_name(value):
    return "" if value is None else str(value).split("::")[-1]


def is_vector(value):
    if isinstance(value, list):
        return len(value) >= 3 and all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in value[:3])
    if isinstance(value, dict):
        return all(isinstance(value.get(k), (int, float)) and not isinstance(value.get(k), bool) for k in "xyz")
    if isinstance(value, str):
        return "X=" in value and "Y=" in value and "Z=" in value
    return False


def type_matches(kind, value):
    if kind in ("int", "number"):
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if kind == "bool":
        return isinstance(value, bool)
    if kind == "string" or kind.startswith("enum:"):
        return isinstance(value, str)
    if kind == "vector":
        return is_vector(value)
    if kind.startswith("struct:"):
        return isinstance(value, dict)
    if kind.startswith("array:"):
        return isinstance(value, list)
    return True


def validate_recording(doc, schema):
    """(errors, warnings) for a parsed recording: the same rules as loader.js's
    validateRecording (arrays checked on their first, middle and last element)."""
    errors, warnings = [], []
    structs = schema["structs"]

    def check(obj, struct_name, path):
        spec = structs.get(struct_name)
        if not spec:
            return
        for name, f in spec["fields"].items():
            value = field(obj, name)
            at = f"{path}.{name}"
            if value is None:
                if f.get("required"):
                    errors.append(f"{at} is missing.")
                continue
            kind = f["type"]
            if not type_matches(kind, value):
                (errors if f.get("required") else warnings).append(f"{at} is not a {kind}.")
                continue
            if "min" in f and value < f["min"]:
                errors.append(f"{at} is {value}, below {f['min']}.")
            if kind.startswith("struct:"):
                check(value, kind[7:], at)
            elif kind.startswith("array:"):
                if f.get("nonEmpty") and not value:
                    errors.append(f"{at} is empty.")
                for i in sorted({0, len(value) // 2, len(value) - 1}):
                    if 0 <= i < len(value):
                        check(value[i], kind[6:], f"{at}[{i}]")

    if not isinstance(doc, dict):
        return ["The file is not a JSON object."], warnings
    check(doc, "FPSReplayRecording", "recording")
    version = field(field(doc, "Header"), "FormatVersion")
    if isinstance(version, (int, float)) and version > schema["knownFormatVersion"]:
        warnings.append(f"Format version {version} is newer than {schema['knownFormatVersion']}, the newest the viewer knows; fields it doesn't know are ignored.")
    return errors, warnings


def summarize(doc):
    """What the viewer will find in a valid recording: frames, players, events by type."""
    frames = field(doc, "Frames") or []
    players = set()
    for frame in frames:
        for pawn in field(frame, "Pawns") or []:
            players.add(str(field(pawn, "PlayerId")))
    events = {}
    for event in field(doc, "Events") or []:
        kind = enum_name(field(event, "EventType"))
        events[kind] = events.get(kind, 0) + 1
    times = [field(f, "Time") for f in frames if isinstance(field(f, "Time"), (int, float))]
    return {"frames": len(frames), "players": len(players), "events": events,
            "duration": round(max(times) - min(times), 3) if times else 0.0}


def parse_index(doc, schema):
    """The index's entries as {file, ...}: the same rules as loader.js's parseIndex."""
    spec = schema["index"]
    entries = doc if isinstance(doc, list) else field(doc, *spec["listKeys"])
    if not isinstance(entries, list):
        entries = next((v for v in (doc or {}).values() if isinstance(v, list)
                        and any(field(e, *spec["fields"]["file"]) is not None for e in v if isinstance(e, dict))), [])
    out = []
    for entry in entries:
        if isinstance(entry, str):
            out.append({"file": entry})
            continue
        row = {key: field(entry, *aliases) for key, aliases in spec["fields"].items()}
        if row.get("file"):
            out.append(row)
    return out


def is_synthetic_name(name, schema):
    return Path(name).name.startswith(schema["synthetic"]["filePrefix"])


class StageError(Exception):
    pass


SKELETON_HEAD = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
                 '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
                 '<style>:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}'
                 '[hidden]{display:none!important}</style>\n</head>\n<body>\n')
SKELETON_TAIL = "\n</body>\n</html>\n"


def stage(out, recordings=None, synthetic=False, schema=None, log=print, standalone=False):
    """Builds the folder at out; returns {published path: source path}. Raises StageError."""
    schema = schema or load_schema()
    out = Path(out)
    if not recordings and not synthetic:
        raise StageError("Give --recordings <folder> (the play-demos files) or --synthetic.")
    files = {name: VIEWER / name for name in VIEWER_FILES}
    files.update(DATA_FILES)
    files.update(ASSET_FILES)

    if recordings:
        folder = Path(recordings)
        index_path = folder / "index.json"
        if not index_path.is_file():
            raise StageError(f"{index_path} is missing: the recordings folder needs the index.json beside the plays.")
        entries = parse_index(parse(index_path.read_text(encoding="utf-8")), schema)
        if not entries:
            raise StageError(f"{index_path} lists no plays.")
        files["recordings/index.json"] = index_path
        for entry in entries:
            name = str(entry["file"])
            if is_synthetic_name(name, schema) or entry.get("synthetic") is True:
                raise StageError(f"{name} is synthetic test data; it is never staged as the game's recordings.")
            path = folder / name
            if not path.is_file():
                raise StageError(f"{path} is listed in index.json but missing.")
            doc = parse(path.read_text(encoding="utf-8-sig"))
            errors, warnings = validate_recording(doc, schema)
            if errors:
                raise StageError(f"{name} can't be played: " + " ".join(errors[:4]))
            build = str(field(field(doc, "Header"), "GameBuildVersion") or "")
            if schema["synthetic"]["buildMarker"] in build.upper():
                raise StageError(f"{name} says it is synthetic ({build}); it is never staged as the game's recordings.")
            for warning in warnings:
                log(f"stage: {name}: {warning}")
            s = summarize(doc)
            log(f"stage: {name}: {s['frames']} frames, {s['players']} players, {s['duration']} s, events {s['events']}")
            files["recordings/" + name.replace("\\", "/")] = path
    if synthetic:
        for path in sorted((VIEWER / "sample").glob(schema["synthetic"]["filePrefix"] + "*.json")):
            files["sample/" + path.name] = path

    if len(files) > MAX_FILES:
        raise StageError(f"{len(files)} files is more than one publish takes ({MAX_FILES}).")
    for published, source in files.items():
        if not Path(source).is_file():
            raise StageError(f"{source} is missing.")
        size = Path(source).stat().st_size
        limit = MAX_BINARY_BYTES if published.endswith(".glb") else MAX_TEXT_BYTES
        if size > limit:
            raise StageError(f"{published} is {size} bytes, over the {limit}-byte limit for one file.")

    if out.exists():
        shutil.rmtree(out)
    for published, source in files.items():
        target = out / published
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    if standalone:
        page = out / "index.html"
        page.write_text(SKELETON_HEAD + page.read_text(encoding="utf-8") + SKELETON_TAIL, encoding="utf-8")
    log(f"stage: {len(files)} files in {out}")
    return files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--recordings", help="folder with index.json and the replay files (play-demos)")
    parser.add_argument("--synthetic", action="store_true", help="include the synthetic test sample")
    parser.add_argument("--out", required=True, help="the folder to build (replaced)")
    parser.add_argument("--standalone", action="store_true", help="give index.html its own document skeleton (not for the Artifact host)")
    parser.add_argument("--print-files", action="store_true", help="print the published path -> source map as JSON")
    args = parser.parse_args(argv)
    try:
        files = stage(args.out, args.recordings, args.synthetic, standalone=args.standalone)
    except StageError as exc:
        print(f"stage: {exc}")
        return 1
    if args.print_files:
        out = Path(args.out).resolve()
        print(json.dumps({k: str(out / k) for k in files if k != "index.html"}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
