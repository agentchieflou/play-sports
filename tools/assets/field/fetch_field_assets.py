#!/usr/bin/env python
"""Fetches and prepares the field's CC0 source art: RawAssets/field/ (lane V2, Epic 146.3/46).

Nothing here runs in CI or at runtime. The content pipeline's field_look step
(tools/content_pipeline/steps/field_look.py) imports what this writes into /Game/Field.

  turf/  ambientCG "Grass 005" (CC0 1.0) at 2K: a clean, short lawn, the closest CC0 match to a
         manicured stadium field. The colour and DirectX normal maps are kept byte for byte; the
         ambient occlusion and roughness maps are packed into one linear map (R occlusion,
         G roughness, B 0: no metal). turf.json records the colour map's mean colour, which the
         turf material divides out so Data/field_markings.json's FieldColor is the field's
         average colour. T_Turf_Noise.png is generated here from a fixed seed (no source image):
         R a smooth macro variation (a few large blotches per tile), G a fine breakup with an
         even histogram (the paint's coverage), B a medium variation; every channel tiles.
  sky/   Two Poly Haven pure skies (CC0 1.0) at 1k, as published: a partly cloudy midday sky and a
         clear night sky. The sky light uses them as its cubemap on tiers without a real-time sky
         capture (Data/stadium_lighting.json).

Every download is checked against a pinned SHA-256 (ambientCG) or the md5 Poly Haven's API
publishes, so a changed upstream file fails loudly instead of changing the field.

Needs Python 3.9+ and Pillow. Run from the repository root:

    python tools/assets/field/fetch_field_assets.py [--cache DIR]

The downloads are cached in DIR (default: a folder in the system temp directory), outside the
repository. A second run rewrites identical files.
"""

import argparse
import hashlib
import io
import json
import os
import random
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
OUT = REPO / "RawAssets" / "field"

TURF_ZIP = {
    "url": "https://ambientcg.com/get?file=Grass005_2K-JPG.zip",
    "file": "Grass005_2K-JPG.zip",
}
TURF_ZIP_SHA256 = "89183d8dceabc3c23a26978f426e1662143dd2f84f1dc3586a1592d101e5234b"

SKIES = [
    {
        "name": "day",
        "asset": "kloofendal_48d_partly_cloudy_puresky",
        "url": "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloofendal_48d_partly_cloudy_puresky_1k.hdr",
        "md5": "c69498687e876bf68d2a8b8a62234eb9",
    },
    {
        "name": "night",
        "asset": "kloppenheim_02_puresky",
        "url": "https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/1k/kloppenheim_02_puresky_1k.hdr",
        "md5": "283ef04d37153527e5407df494eaf339",
    },
]

NOISE_SIZE = 1024
NOISE_SEED = 146


def download(url, target):
    if target.is_file():
        return target.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": "play-sports-asset-fetch/1.0"})
    with urllib.request.urlopen(request) as response:
        data = response.read()
    target.write_bytes(data)
    return data


def srgb_to_linear(c):
    c = c / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def mean_linear_color(image):
    """The mean colour of an sRGB image, in linear terms (what the material sees after sampling)."""
    from PIL import Image  # noqa: F401  (imported for the caller's error message when missing)
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


def tileable_noise(size, cells, rng):
    """Value noise on a periodic grid of cells x cells, smoothly interpolated: tiles seamlessly."""
    grid = [[rng.random() for _ in range(cells)] for _ in range(cells)]
    out = [0.0] * (size * size)
    step = size / cells
    for y in range(size):
        gy = y / step
        y0 = int(gy) % cells
        y1 = (y0 + 1) % cells
        ty = gy - int(gy)
        ty = ty * ty * (3 - 2 * ty)
        for x in range(size):
            gx = x / step
            x0 = int(gx) % cells
            x1 = (x0 + 1) % cells
            tx = gx - int(gx)
            tx = tx * tx * (3 - 2 * tx)
            a = grid[y0][x0] + (grid[y0][x1] - grid[y0][x0]) * tx
            b = grid[y1][x0] + (grid[y1][x1] - grid[y1][x0]) * tx
            out[y * size + x] = a + (b - a) * ty
    return out


def fractal(size, octaves, rng):
    total = [0.0] * (size * size)
    weight = 0.0
    amplitude = 1.0
    for cells in octaves:
        layer = tileable_noise(size, cells, rng)
        for i, v in enumerate(layer):
            total[i] += v * amplitude
        weight += amplitude
        amplitude *= 0.5
    return [v / weight for v in total]


def normalize(values):
    lo, hi = min(values), max(values)
    span = hi - lo or 1.0
    return [(v - lo) / span for v in values]


def equalize(values):
    """Rank-based: an even histogram, so a coverage threshold of c keeps about c of the pixels."""
    order = sorted(range(len(values)), key=values.__getitem__)
    out = [0.0] * len(values)
    scale = 1.0 / (len(values) - 1)
    for rank, index in enumerate(order):
        out[index] = rank * scale
    return out


def make_noise(path):
    from PIL import Image
    rng = random.Random(NOISE_SEED)
    macro = normalize(fractal(NOISE_SIZE, (4, 8, 16), rng))
    breakup = equalize(fractal(NOISE_SIZE, (128, 256), rng))
    medium = normalize(fractal(NOISE_SIZE, (16, 32, 64), rng))
    image = Image.new("RGB", (NOISE_SIZE, NOISE_SIZE))
    image.putdata([(int(r * 255 + 0.5), int(g * 255 + 0.5), int(b * 255 + 0.5))
                   for r, g, b in zip(macro, breakup, medium)])
    image.save(path, optimize=True)


def prepare_turf(cache):
    from PIL import Image
    data = download(TURF_ZIP["url"], cache / TURF_ZIP["file"])
    digest = hashlib.sha256(data).hexdigest()
    if digest != TURF_ZIP_SHA256:
        raise SystemExit(f"{TURF_ZIP['file']}: SHA-256 {digest} is not the pinned {TURF_ZIP_SHA256}")
    out = OUT / "turf"
    out.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = {Path(n).name: n for n in archive.namelist()}

        def member(suffix):
            for name, full in names.items():
                if name.endswith(suffix):
                    return archive.read(full)
            raise SystemExit(f"{TURF_ZIP['file']} has no *{suffix}")

        color = member("_Color.jpg")
        (out / "T_Turf_Color.jpg").write_bytes(color)
        (out / "T_Turf_Normal.jpg").write_bytes(member("_NormalDX.jpg"))
        occlusion = Image.open(io.BytesIO(member("_AmbientOcclusion.jpg"))).convert("L")
        roughness = Image.open(io.BytesIO(member("_Roughness.jpg"))).convert("L")
        black = Image.new("L", occlusion.size, 0)
        Image.merge("RGB", (occlusion, roughness.resize(occlusion.size), black)).save(out / "T_Turf_ORM.png", optimize=True)
        mean = mean_linear_color(Image.open(io.BytesIO(color)))
    make_noise(out / "T_Turf_Noise.png")
    manifest = {
        "Comment": "Written by tools/assets/field/fetch_field_assets.py. MeanLinearColor is T_Turf_Color.jpg's mean colour in linear terms; the turf material divides it out so FieldColor (Data/field_markings.json) is the field's average colour.",
        "Source": "ambientCG Grass005 2K-JPG",
        "SourceSha256": digest,
        "MeanLinearColor": mean,
        "NoiseSeed": NOISE_SEED,
    }
    (out / "turf.json").write_text(json.dumps(manifest, indent=4) + "\n", encoding="utf-8")
    return digest


def prepare_skies(cache):
    out = OUT / "sky"
    out.mkdir(parents=True, exist_ok=True)
    for sky in SKIES:
        data = download(sky["url"], cache / Path(sky["url"]).name)
        digest = hashlib.md5(data).hexdigest()
        if digest != sky["md5"]:
            raise SystemExit(f"{sky['url']}: md5 {digest} is not Poly Haven's {sky['md5']}")
        (out / f"HDR_Sky_{sky['name'].capitalize()}.hdr").write_bytes(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--cache", default=os.path.join(tempfile.gettempdir(), "ps-field-assets"))
    args = parser.parse_args(argv)
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Needs Pillow: pip install Pillow", file=sys.stderr)
        return 1
    cache = Path(args.cache)
    cache.mkdir(parents=True, exist_ok=True)
    digest = prepare_turf(cache)
    prepare_skies(cache)
    print(f"RawAssets/field written (turf zip SHA-256 {digest}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
