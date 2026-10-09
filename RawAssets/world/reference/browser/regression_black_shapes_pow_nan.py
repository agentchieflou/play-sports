"""2026-10-05, reported by the operator (Windows, Chrome, /world on a desktop GPU): black shapes over the plaza.

Symptom: third person, the character on the plaza. "Two enormous near-black, flat-shaded shapes" filled
the left and right of the screen, meeting in a V, with a narrow wedge of the city between them and a
triangle of paving round the character's legs; the agents' name tags ("usage_tool · idle", "DPM · idle")
floated in the black. "They follow us wherever we rotate/walk."

Reproduced headless only through Mesa's llvmpipe (ANGLE on OpenGL, under Xvfb) with the page on its
desktop path (`quality=high`): the middle half of the frame read 100% black; hiding `street-cones`
alone brought it back to the night scene (about 20% dark pixels). SwiftShader, which CI draws in,
never showed it.

Cause: the light cone under each street lamp (`street.js`, `materials`) faded by
`pow( vF, 1.5 )`, `vF` the cone's facing. It is near zero along the cone's silhouette, and with
multisampling a fragment there is shaded at the pixel's centre, outside the triangle, where the
interpolated `vF` dips below zero. GLSL leaves `pow` of a negative base undefined; on a GPU it is NaN
(Direct3D's `pow` is `exp2(y * log2(x))`), and SwiftShader happened to return a number. The NaN was
added into the HDR target, the bloom's mip chain spread each one into blocks a sixty-fourth of the
screen wide and more, and the grade drew them black, wherever a lamp's cone was in view.

Fix: the cone clamps both of its fades to 0..1 before `pow`. This test holds every GLSL `pow` in the
world's scripts to a base that cannot go below zero, since the frame's bloom turns one NaN pixel into
a black screen.

Issue: https://github.com/agentchieflou/this-next-please/issues/626 (the world; the cones came with #632)
"""
from __future__ import annotations

import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WORLD = os.path.join(ROOT, "agentdata", "fleet", "static", "world")

#: A base that is non-negative by construction: clamped, floored at zero, or an absolute value.
GUARDED = re.compile(r"^(max|clamp|abs)\s*\(|^1\.0\s*-\s*clamp\s*\(")

#: Bases that are non-negative for a reason a reader can check one line away, by file.
KNOWN = {
    ("bots.js", "1.0 - edge"): "edge = abs(fract(x) - 0.5) * 2.0 is in 0..1",
    ("render.js", "ao"): "ao = clamp(..., 0.0, 1.0) just before",
    ("render.js", "c"): "srgb() is only given the graded colour after clamp(col, 0.0, 1.0)",
}


def _bases(src):
    """The first argument of every GLSL `pow(` in a script (`Math.pow` is JavaScript, not GLSL)."""
    out = []
    for m in re.finditer(r"(?<![\w.])pow\s*\(", src):
        depth, i = 0, m.end()
        while i < len(src):
            ch = src[i]
            if ch in "([":
                depth += 1
            elif ch in ")]":
                if depth == 0:
                    break
                depth -= 1
            elif ch == "," and depth == 0:
                break
            i += 1
        out.append(src[m.end():i].strip())
    return out


def test_the_lamp_cone_clamps_its_fades_before_pow():
    src = open(os.path.join(WORLD, "street.js"), encoding="utf-8").read()
    cone = src.split("var cone = new T.ShaderMaterial(", 1)[1].split("});", 1)[0]
    bases = _bases(cone)
    assert bases == ["clamp( vH, 0.0, 1.0 )", "clamp( vF, 0.0, 1.0 )"], bases


def test_no_glsl_pow_in_the_world_takes_a_base_that_can_go_negative():
    found, loose = 0, []
    for path in sorted(glob.glob(os.path.join(WORLD, "*.js"))):
        name = os.path.basename(path)
        for base in _bases(open(path, encoding="utf-8").read()):
            found += 1
            if not GUARDED.match(base) and (name, base) not in KNOWN:
                loose.append(f"{name}: pow({base}, ...)")
    assert found >= 8, f"only {found} GLSL pow calls found: has the parser stopped seeing them?"
    assert not loose, ("pow() of a negative base is NaN on a GPU, and the bloom spreads one NaN pixel "
                       "into a black block (see this file's docstring); clamp or max the base: " + "; ".join(loose))


def test_the_parser_sees_the_unguarded_form_this_fixed():
    assert _bases("float a = pow( vH, 1.6 ) * pow( vF, 1.5 ) * Math.pow(2, 3);") == ["vH", "vF"]
    assert not GUARDED.match("vF") and GUARDED.match("clamp( vF, 0.0, 1.0 )")
