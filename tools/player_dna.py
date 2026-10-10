#!/usr/bin/env python
"""Player DNA generator for play-sports (Epic 79.3).

Gives every rostered player a style profile (FPlayerAttributes::DNA, the axes in
Data/player_dna.json) that is plausible for his ratings and his own. Each axis the player's
role has is

    RatingLean * ((his HighAttribute - his LowAttribute) - the league's average of that for the role)
    + seeded variation (standard deviation Spread)

clamped to -1..1 and rounded to two places. The variation is seeded by his PlayerId and the
axis, so the same league always gets the same DNA, and two players rated alike still differ.
Track L's league generator (Epic 122) calls generate_profile for each player it makes.

  python tools/player_dna.py            report which players have no DNA
  python tools/player_dna.py --write    give DNA to every player who has none
  python tools/player_dna.py --write --force   regenerate everyone's (authored DNA is lost)

Rosters are every Data/ JSON file with a "Players" array; their layout is kept. Run from the
repo root.
"""

import argparse
import hashlib
import json
import random
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CATALOG = Path("Data") / "player_dna.json"


def load_catalog(repo=REPO):
    """Data/player_dna.json, or None when it is missing or broken (validate_data reports that)."""
    try:
        payload = json.loads((Path(repo) / CATALOG).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError, UnicodeDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def role_axes(catalog, role):
    """The catalog's axis definitions that apply to role, in catalog order."""
    return [a for a in catalog.get("Axes", []) if isinstance(a, dict) and role in (a.get("Roles") or [])]


def _rating(player, name):
    value = player.get(name)
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else 0.0


def _rating_gap(player, axis):
    generator = axis.get("Generator") or {}
    return _rating(player, generator.get("HighAttribute")) - _rating(player, generator.get("LowAttribute"))


def role_centers(players, catalog):
    """{(role, axis): the average rating gap of the role's players}: what counts as neutral."""
    gaps = {}
    for player in players:
        for axis in role_axes(catalog, player.get("Role")):
            gaps.setdefault((player.get("Role"), axis.get("Axis")), []).append(_rating_gap(player, axis))
    return {key: sum(values) / len(values) for key, values in gaps.items()}


def _rng(player_id, axis_name):
    digest = hashlib.sha256(f"{player_id}/{axis_name}".encode("utf-8")).digest()
    return random.Random(int.from_bytes(digest[:8], "big"))


def generate_profile(player, catalog, centers=None):
    """{axis: value} for every axis of the player's role; centers from role_centers (a role
    without one leans from 0)."""
    profile = {}
    for axis in role_axes(catalog, player.get("Role")):
        name = axis.get("Axis")
        generator = axis.get("Generator") or {}
        center = (centers or {}).get((player.get("Role"), name), 0.0)
        lean = float(generator.get("RatingLean", 0.0)) * (_rating_gap(player, axis) - center)
        spread = float(generator.get("Spread", 0.0))
        noise = _rng(player.get("PlayerId", ""), name).gauss(0.0, spread) if spread > 0 else 0.0
        value = round(max(-1.0, min(1.0, lean + noise)), 2)
        profile[name] = value if value != 0 else 0.0
    return profile


def fill(rosters, catalog, force=False):
    """Gives DNA to every player in rosters ({path: payload}) who has none (everyone, with force),
    centered on the whole league. Returns the PlayerIds given DNA."""
    players = [p for payload in rosters.values() for p in payload.get("Players", []) if isinstance(p, dict)]
    centers = role_centers(players, catalog)
    changed = []
    for player in players:
        if not role_axes(catalog, player.get("Role")) or ("DNA" in player and not force):
            continue
        player["DNA"] = generate_profile(player, catalog, centers)
        changed.append(player.get("PlayerId"))
    return changed


def _compact(value):
    if isinstance(value, dict):
        return "{ " + ", ".join(f"{json.dumps(k, ensure_ascii=False)}: {_compact(v)}" for k, v in value.items()) + " }"
    return json.dumps(value, ensure_ascii=False)


def render(payload, compact):
    """A roster as written: one player per line (compact), or indented two spaces."""
    if not compact:
        return json.dumps(payload, indent=2, ensure_ascii=False) + "\n"
    rows = ",\n".join(f"    {_compact(p)}" for p in payload["Players"])
    return "{\n  \"Players\": [\n" + rows + "\n  ]\n}\n"


def load_rosters(repo=REPO):
    """{path: (payload, compact)} for every Data/ JSON file with a "Players" array whose layout
    render() reproduces, plus [paths it doesn't] (left alone)."""
    rosters, skipped = {}, []
    for path in sorted((Path(repo) / "Data").rglob("*.json")):
        try:
            text = path.read_text(encoding="utf-8")
            payload = json.loads(text)
        except (OSError, json.JSONDecodeError, UnicodeDecodeError):
            continue
        if not isinstance(payload, dict) or not isinstance(payload.get("Players"), list):
            continue
        compact = '\n    { "PlayerId"' in text
        if render(payload, compact) != text.replace("\r\n", "\n"):
            skipped.append(path)
            continue
        rosters[path] = (payload, compact)
    return rosters, skipped


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--write", action="store_true", help="give DNA to every player who has none")
    parser.add_argument("--force", action="store_true", help="with --write, regenerate everyone's")
    args = parser.parse_args(argv)

    catalog = load_catalog()
    if catalog is None:
        print(f"player_dna: {CATALOG} is missing or broken")
        return 1
    rosters, skipped = load_rosters()
    for path in skipped:
        print(f"player_dna: {path.relative_to(REPO)} is laid out in a way this tool doesn't rewrite; left alone")
    payloads = {path: payload for path, (payload, _) in rosters.items()}
    if not args.write:
        missing = [p.get("PlayerId") for payload in payloads.values() for p in payload["Players"]
                   if isinstance(p, dict) and "DNA" not in p and role_axes(catalog, p.get("Role"))]
        print(f"player_dna: {len(missing)} player(s) without DNA" + (": " + ", ".join(map(str, missing)) if missing else ""))
        return 0
    changed = fill(payloads, catalog, force=args.force)
    for path, (payload, compact) in rosters.items():
        text = render(payload, compact)
        if text != path.read_text(encoding="utf-8").replace("\r\n", "\n"):
            with open(path, "w", encoding="utf-8", newline="\n") as out:
                out.write(text)
    print(f"player_dna: gave DNA to {len(changed)} player(s) across {len(rosters)} roster file(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
