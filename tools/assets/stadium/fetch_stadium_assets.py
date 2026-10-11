#!/usr/bin/env python
"""Fetches the stadium's CC0 source art: RawAssets/stadium/ (lane V3, Epics 48 and 52).

Nothing here runs in CI or at runtime. The content pipeline's stadium_look step
(tools/content_pipeline/steps/stadium_look.py) imports what this writes into /Game/Stadium.

  concrete/  Poly Haven "Concrete Floor Worn 001" (CC0 1.0) at 2K, as published: the colour map,
             the DirectX normal map and the packed occlusion/roughness/metalness map (R, G, B).
             It covers a 3 m square, so the bowl's concrete tiles every 300 cm of world space.
             concrete.json records the colour map's mean linear colour, which the material divides
             out so Data/stadium_set.json's colours are each piece's average colour.

Every download is checked against the md5 Poly Haven's API publishes for it, so a changed
upstream file fails loudly instead of changing the stadium.

Needs Python 3.9+ and Pillow. Run from the repository root:

    python tools/assets/stadium/fetch_stadium_assets.py [--cache DIR]

The downloads are cached in DIR (default: a folder in the system temp directory), outside the
repository. A second run rewrites identical files.
"""

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
OUT = REPO / "RawAssets" / "stadium"

# Each map of the concrete: what Poly Haven calls it, the file it becomes, its pinned md5.
CONCRETE_ASSET = "concrete_floor_worn_001"
CONCRETE_SIZE_CM = 300.0
CONCRETE = [
    ("diff", "T_Concrete_Color.jpg", "6a3645f19f2ac4b7f60f43419c5d09ec"),
    ("nor_dx", "T_Concrete_Normal.jpg", "082dbaf998c74dfb8e1d801e912524d0"),
    ("arm", "T_Concrete_ARM.jpg", "ed519bf68bcf8581447a9d3fbc1e8c45"),
]
URL = "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/{asset}/{asset}_{map}_2k.jpg"


def download(url, target, md5):
    """The file at url, from the cache when it's there; its md5 must be the pinned one."""
    if not target.is_file():
        request = urllib.request.Request(url, headers={"User-Agent": "play-sports-asset-fetch/1.0"})
        with urllib.request.urlopen(request) as response:
            target.write_bytes(response.read())
    data = target.read_bytes()
    digest = hashlib.md5(data).hexdigest()
    if digest != md5:
        target.unlink()
        raise SystemExit(f"{url}: md5 {digest} is not the pinned {md5}; Poly Haven changed the file.")
    return data


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def mean_linear_color(path):
    """The mean colour of an sRGB image, in linear terms (what the material sees after sampling)."""
    from PIL import Image
    with Image.open(path) as image:
        small = image.convert("RGB").resize((256, 256))
    lut = [srgb_to_linear(i) for i in range(256)]
    totals = [0.0, 0.0, 0.0]
    data = small.tobytes()
    for i in range(0, len(data), 3):
        totals[0] += lut[data[i]]
        totals[1] += lut[data[i + 1]]
        totals[2] += lut[data[i + 2]]
    count = len(data) // 3
    return [round(t / count, 5) for t in totals]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cache", default=str(Path(tempfile.gettempdir()) / "play-sports-stadium-assets"))
    args = parser.parse_args(argv)
    cache = Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    concrete = OUT / "concrete"
    concrete.mkdir(parents=True, exist_ok=True)

    for map_name, file_name, md5 in CONCRETE:
        url = URL.format(asset=CONCRETE_ASSET, map=map_name)
        cached = cache / Path(url).name
        download(url, cached, md5)
        shutil.copyfile(cached, concrete / file_name)
        print(f"concrete/{file_name} <- {url}")

    manifest = {
        "Asset": CONCRETE_ASSET,
        "TileSizeCm": CONCRETE_SIZE_CM,
        "MeanLinearColor": mean_linear_color(concrete / "T_Concrete_Color.jpg"),
    }
    (concrete / "concrete.json").write_text(json.dumps(manifest, indent=4) + "\n", encoding="utf-8")
    print(f"concrete/concrete.json: mean linear colour {manifest['MeanLinearColor']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
