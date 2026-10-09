"""Write every character spec the stand-in needs into raw/*.json, then make each one."""
import json, os, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(here, "raw"), exist_ok=True)

BASE = {"gender": 0.5, "age": 0.5, "muscle": 0.5, "weight": 0.5, "height": 0.6, "proportions": 0.6}
HERO = {"rig": "game_engine", "eyes": "low-poly", "eyeColour": "brown", "eyebrows": "eyebrow001",
        "eyelashes": "eyelashes01", "skin": "young_caucasian_male",
        "clothes": [{"name": "male_casualsuit01", "role": "outfit"}, {"name": "shoes06", "role": "shoes"},
                    {"name": "grinsegold_beard_sigmund_wip", "role": "beard", "keepBody": True},
                    {"name": "rehmanpolanski_moustache_viking", "role": "beard", "keepBody": True},
                    {"name": "toigo_round_glasses_leopard", "role": "glasses", "keepBody": True},
                    {"name": "frankyaye_glasses_library_male", "role": "glasses", "keepBody": True}],
        "hair": ["short02", "afro01", "long01", "ponytail01", "short01", "rehmanpolanski_hair_bun_brown"]}
VARIANTS = {
    "base": {},
    "angular": {"gender": 1.0},
    "curved": {"gender": 0.0},
    "older": {"age": 0.875},
    "slim": {"weight": 0.0, "muscle": 0.35},
    "broad": {"weight": 1.0, "muscle": 0.9},
}
CROWD = {
    "c1": ({"gender": 1.0, "weight": 0.68, "muscle": 0.62}, "male_casualsuit05", "shoes03", "short01"),
    "c2": ({"gender": 0.0, "age": 0.45}, "female_elegantsuit01", "shoes04", "bob01"),
    "c3": ({"gender": 0.8, "age": 0.85, "weight": 0.55}, "male_casualsuit03", "shoes01", "short02"),
    "c4": ({"gender": 0.1, "age": 0.42, "weight": 0.72}, "female_casualsuit01", "shoes05", "ponytail01"),
    "c5": ({"gender": 0.6, "age": 0.38, "weight": 0.3}, "male_casualsuit06", "shoes06", "afro01"),
}

jobs = []
for name, delta in VARIANTS.items():
    spec = dict(HERO, macros=dict(BASE, **delta))
    jobs.append(("hero_" + name, spec))
for name, (delta, outfit, shoes, hair) in CROWD.items():
    spec = {"rig": "game_engine", "eyes": "low-poly", "eyeColour": "brown", "eyebrows": "eyebrow001",
            "skin": "young_caucasian_male", "macros": dict(BASE, **delta),
            "clothes": [{"name": outfit, "role": "outfit"}, {"name": shoes, "role": "shoes"}], "hair": [hair]}
    jobs.append(("crowd_" + name, spec))

only = sys.argv[1:]
for name, spec in jobs:
    if only and name not in only:
        continue
    path = os.path.join(here, "raw", name + ".spec.json")
    json.dump(spec, open(path, "w"), indent=1)
    out = os.path.join(here, "raw", name + ".glb")
    r = subprocess.run([os.path.join(here, "bvenv", "bin", "python"), os.path.join(here, "mh_make.py"), path, out],
                       capture_output=True, text=True, timeout=1200)
    last = [l for l in r.stdout.splitlines() if l.startswith("WROTE")]
    print(name, r.returncode, last[-1] if last else r.stdout[-800:] + r.stderr[-800:], flush=True)
