# The world's street trees, made from nothing but this script: Blender (headless) draws each species'
# leaf, renders twigs of them into an atlas, makes the bark, grows every tree and writes raw meshes and
# textures, which trees.mjs packs into RawAssets/world/trees/trees.glb.
# Usage: blender --background --factory-startup --python tools/assets/world/trees/trees.py -- <out-dir> [stage...]
#        stages: leaves twigs bark trees (all when none is given)
import bpy, math, os, sys, json, random
import numpy as np
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else "trees-build")
STAGES = set(ARGS[1:]) or {"leaves", "twigs", "bark", "trees"}
RAW = os.path.join(OUT, "raw")
os.makedirs(RAW, exist_ok=True)

LEAF_PX = 512
CELL_PX = 1024

# A leaf: its half outline (x >= 0) from the tip to the petiole's notch, in a square where the leaf runs
# up y 0..1 (third value 1 keeps a point sharp: a lobe's tip), its teeth (depth, spacing), how much
# narrower its left half is, its colours (top, underside, veins: sRGB), and its veins.
SPECIES = {
    "plane": {
        "half": [(0, .97, 1), (.08, .83, 0), (.15, .69, 0), (.30, .77, 0), (.48, .84, 1), (.45, .64, 0),
                 (.40, .53, 0), (.50, .40, 1), (.44, .27, 0), (.30, .17, 0), (.14, .16, 0), (0, .19, 1)],
        "teeth": (.008, .08), "asym": .95,
        "top": (76, 102, 44), "under": (128, 146, 94), "vein": (124, 138, 70),
        "primary": [(0, .97), (.45, .80), (.47, .36)], "per": 5,
    },
    "linden": {
        "half": [(0, .98, 1), (.07, .90, 0), (.17, .80, 0), (.29, .66, 0), (.38, .52, 0), (.42, .39, 0),
                 (.41, .27, 0), (.35, .16, 0), (.25, .08, 0), (.13, .05, 0), (.05, .08, 0), (0, .13, 1)],
        "teeth": (.006, .035), "asym": .9,
        "top": (60, 88, 34), "under": (118, 138, 86), "vein": (104, 124, 58),
        "primary": [(0, .98), (.30, .40)], "per": 6,
    },
}


def chaikin(pts, n):
    for _ in range(n):
        out = []
        for i, (p, s) in enumerate(pts):
            q = pts[(i + 1) % len(pts)][0]
            if s:
                out.append((p, 1))
            out.append((p * .75 + q * .25, 0))
            out.append((p * .25 + q * .75, 0))
        pts = out
    return pts


def outline(sp):
    half = [(np.array([x, y]), s) for x, y, s in sp["half"]]
    left = [(np.array([-x * sp["asym"], y]), s) for (x, y), s in [(p, s) for p, s in half[1:-1]][::-1]]
    pts = chaikin(half + left, 4)
    poly = np.array([p for p, _ in pts])
    seg = np.linalg.norm(np.roll(poly, -1, 0) - poly, axis=1)
    s = np.concatenate([[0], np.cumsum(seg)])
    t = np.linspace(0, s[-1], 900, endpoint=False)
    res = np.stack([np.interp(t, s, np.append(poly[:, 0], poly[0, 0])), np.interp(t, s, np.append(poly[:, 1], poly[0, 1]))], 1)
    d = np.roll(res, -1, 0) - np.roll(res, 1, 0)
    nrm = np.stack([d[:, 1], -d[:, 0]], 1)
    nrm /= np.linalg.norm(nrm, axis=1, keepdims=True) + 1e-9
    area = np.sum(res[:, 0] * np.roll(res[:, 1], -1) - np.roll(res[:, 0], -1) * res[:, 1])
    if area < 0:
        nrm = -nrm
    depth, gap = sp["teeth"]
    f = (t / gap) % 1
    tooth = np.power(1 - np.abs(2 * f - 1), 1.6) - .4
    fade = np.clip((res[:, 1] - .2) / .15, 0, 1) * np.clip((.985 - res[:, 1]) / .05, 0, 1)
    return res + nrm * (depth * tooth * fade)[:, None]


def grid(n):
    ys, xs = np.mgrid[0:n, 0:n]
    return (xs + .5) / n - .5, (ys + .5) / n


def fill(poly, n):
    X, Y = grid(n)
    inside = np.zeros((n, n), bool)
    x0, y0 = poly[:, 0], poly[:, 1]
    x1, y1 = np.roll(x0, -1), np.roll(y0, -1)
    for a, b, c, d in zip(x0, y0, x1, y1):
        lo, hi = min(b, d), max(b, d)
        r0, r1 = max(0, int(lo * n - 1)), min(n, int(hi * n + 2))
        if r1 <= r0 or hi == lo:
            continue
        Ys, Xs = Y[r0:r1], X[r0:r1]
        cond = (b > Ys) != (d > Ys)
        xint = (c - a) * (Ys - b) / (d - b) + a
        inside[r0:r1] ^= cond & (Xs < xint)
    return inside


def blur(a, r, passes=3):
    for _ in range(passes):
        for ax in (0, 1):
            c = np.cumsum(np.pad(a, [(r + 1, r) if i == ax else (0, 0) for i in range(2)], mode="edge"), axis=ax)
            a = (np.take(c, range(2 * r + 1, c.shape[ax]), axis=ax) - np.take(c, range(0, c.shape[ax] - 2 * r - 1), axis=ax)) / (2 * r + 1)
    return a


def noise(n, cells, rng, wrap=False):
    g = rng.random((cells + 1, cells + 1))
    if wrap:
        g[-1, :] = g[0, :]
        g[:, -1] = g[:, 0]
    t = (np.arange(n) + .5) / n * cells
    i = np.minimum(t.astype(int), cells - 1)
    f = t - i
    f = f * f * (3 - 2 * f)
    a = g[i][:, i] * (1 - f)[None, :] + g[i][:, i + 1] * f[None, :]
    b = g[i + 1][:, i] * (1 - f)[None, :] + g[i + 1][:, i + 1] * f[None, :]
    return a * (1 - f)[:, None] + b * f[:, None]


def fbm(n, cells, rng, octaves=4, wrap=False):
    out, amp, tot = np.zeros((n, n)), 1.0, 0.0
    for o in range(octaves):
        out += noise(n, cells * 2 ** o, rng, wrap) * amp
        tot += amp
        amp *= .5
    return out / tot


def vein_field(lines, n):
    X, Y = grid(n)
    v = np.zeros((n, n))
    for pts, w0, w1 in lines:
        pts = np.asarray(pts)
        m = len(pts) - 1
        for k in range(m):
            (ax, ay), (bx, by) = pts[k], pts[k + 1]
            w = w0 + (w1 - w0) * (k + .5) / m
            pad = w * 3
            c0, c1 = max(0, int((min(ax, bx) + .5 - pad) * n)), min(n, int((max(ax, bx) + .5 + pad) * n) + 1)
            r0, r1 = max(0, int((min(ay, by) - pad) * n)), min(n, int((max(ay, by) + pad) * n) + 1)
            if c1 <= c0 or r1 <= r0:
                continue
            px, py = X[r0:r1, c0:c1] - ax, Y[r0:r1, c0:c1] - ay
            dx, dy = bx - ax, by - ay
            h = np.clip((px * dx + py * dy) / (dx * dx + dy * dy + 1e-12), 0, 1)
            d = np.hypot(px - dx * h, py - dy * h)
            v[r0:r1, c0:c1] = np.maximum(v[r0:r1, c0:c1], np.exp(-(d / w) ** 2 * 2))
    return v


def curve(a, b, bend, k=12):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = b - a
    side = np.array([-d[1], d[0]])
    return [a + d * t + side * bend * math.sin(math.pi * t) for t in np.linspace(0, 1, k)]


def to_margin(mask, p, d, n):
    p, d = np.asarray(p, float), np.asarray(d, float) / np.linalg.norm(d)
    t = 0.0
    while t < 1.2:
        q = p + d * (t + .01)
        c, r = int((q[0] + .5) * n), int(q[1] * n)
        if not (0 <= c < n and 0 <= r < n) or not mask[r, c]:
            break
        t += .01
    return p + d * t * .9


def veins(sp, mask, n, rng):
    base = np.array([0, sp["half"][-1][1]])
    lines = []
    tips = []
    for x, y in sp["primary"]:
        for sx in ([1] if x == 0 else [1, -sp["asym"]]):
            tips.append(np.array([x * sx, y]))
    for tip in tips:
        main = curve(base, tip, (.05 if tip[0] > 0 else -.05) if tip[0] != 0 else rng.uniform(-.01, .01))
        w0 = .011 if tip[0] == 0 else .008
        lines.append((main, w0, .0025))
        for j in range(sp["per"]):
            t = .22 + .7 * (j + rng.uniform(-.15, .15)) / sp["per"]
            k = int(t * (len(main) - 1))
            p, q = main[k], main[min(k + 1, len(main) - 1)]
            ax = (q - p) / (np.linalg.norm(q - p) + 1e-9)
            for side in (-1, 1):
                if tip[0] != 0 and side == (1 if tip[0] < 0 else -1):
                    continue
                if tip[0] == 0 and len(tips) > 3 and t < .45:
                    continue
                ang = math.radians(rng.uniform(38, 52)) * side
                d = np.array([ax[0] * math.cos(ang) - ax[1] * math.sin(ang), ax[0] * math.sin(ang) + ax[1] * math.cos(ang)])
                end = to_margin(mask, p, d, n)
                if np.linalg.norm(end - p) < .03:
                    continue
                lines.append((curve(p, end, -.08 * side, 8), .0045, .0015))
    return vein_field(lines, n)


def leaf(name, sp, seed):
    rng = np.random.default_rng(seed)
    n = LEAF_PX
    poly = outline(sp)
    hi = fill(poly, n * 2)
    alpha = hi.reshape(n, 2, n, 2).mean(axis=(1, 3))
    mask = alpha > .5
    v = veins(sp, mask, n, rng)
    reticulum = blur(fbm(n, 40, rng, 2), 1, 1)
    net = np.clip(1 - np.abs(reticulum - .5) * 22, 0, 1) * .1
    edge = blur(alpha, 3, 2)
    base_glow = np.exp(-(((grid(n)[1]) - sp["half"][-1][1]) / .25) ** 2)
    mott = fbm(n, 6, rng, 3)
    out = {}
    for side in ("top", "under"):
        col = np.array(sp[side], float) / 255
        c = np.ones((n, n, 3)) * col
        c *= (0.92 + .16 * mott)[..., None]
        c += np.array([.06, .04, -.01])[None, None, :] * (fbm(n, 3, rng, 2) - .4)[..., None]
        c *= (1 + .08 * base_glow)[..., None]
        vein = np.array(sp["vein"], float) / 255 * (1.12 if side == "under" else 1)
        c = c * (1 - v[..., None] * .6) + vein * v[..., None] * .6
        c *= (1 - net * (0.5 if side == "top" else .2))[..., None]
        c *= (.82 + .18 * np.clip((edge - .35) / .5, 0, 1))[..., None]
        c[~mask] = col
        out[side] = np.clip(c, 0, 1)
    height = -v + .35 * blur(1 - v, 6) - .4 * (1 - np.clip(edge / .6, 0, 1)) - net * .3
    return out["top"], out["under"], alpha, height


def image(name, rgb, alpha=None, data=False):
    n = rgb.shape[0]
    img = bpy.data.images.get(name) or bpy.data.images.new(name, rgb.shape[1], n, alpha=True, float_buffer=data)
    if data:
        img.colorspace_settings.name = "Non-Color"
    a = np.ones(rgb.shape[:2]) if alpha is None else alpha
    if rgb.ndim == 2:
        rgb = np.repeat(rgb[..., None], 3, 2)
    img.pixels.foreach_set(np.concatenate([rgb, a[..., None]], 2).astype(np.float32).ravel())
    img.update()
    return img


def pixels(path):
    img = bpy.data.images.load(path, check_existing=False)
    img.colorspace_settings.name = "Non-Color"
    a = np.empty(img.size[0] * img.size[1] * 4, np.float32)
    img.pixels.foreach_get(a)
    out = a.reshape(img.size[1], img.size[0], 4).copy()
    bpy.data.images.remove(img)
    return out


def save(img, path):
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()


def stage_leaves():
    for i, (name, sp) in enumerate(SPECIES.items()):
        top, under, alpha, height = leaf(name, sp, 11 + i)
        save(image(f"leaf_{name}_top", top, alpha), os.path.join(RAW, f"leaf_{name}_top.png"))
        save(image(f"leaf_{name}_under", under, alpha), os.path.join(RAW, f"leaf_{name}_under.png"))
        h = (height - height.min()) / (height.max() - height.min() + 1e-9)
        save(image(f"leaf_{name}_height", h, None, True), os.path.join(RAW, f"leaf_{name}_height.png"))
        print("leaf", name, "fill", round(float(alpha.mean()), 3))


def tube(pts, radii, sides, vscale=1.0, urep=1):
    pts = [Vector(p) for p in pts]
    m = len(pts)
    P, N, UV, I = [], [], [], []
    nrm = (pts[1] - pts[0]).normalized().orthogonal().normalized()
    length = 0.0
    for i in range(m):
        t = (pts[min(i + 1, m - 1)] - pts[max(i - 1, 0)]).normalized()
        nrm = (nrm - t * nrm.dot(t)).normalized()
        bi = t.cross(nrm)
        if i:
            length += (pts[i] - pts[i - 1]).length
        for j in range(sides + 1):
            a = 2 * math.pi * j / sides
            d = nrm * math.cos(a) + bi * math.sin(a)
            P.append(pts[i] + d * radii[i])
            N.append(d)
            UV.append((urep * j / sides, length / vscale))
    for i in range(m - 1):
        for j in range(sides):
            a = i * (sides + 1) + j
            b = a + sides + 1
            I += [a, a + 1, b + 1, a, b + 1, b]
    return P, N, UV, I


def blade(L, notch, gx=8, gy=10, fold=.18, droop=.22, twist=0.0):
    P, UV, I = [], [], []
    for iy in range(gy + 1):
        for ix in range(gx + 1):
            u, v = ix / gx, iy / gy
            x, y = (u - .5) * L, (v - notch) * L
            z = fold * abs(x) - droop * max(v - notch, 0) ** 2 * L
            c, s = math.cos(twist * v), math.sin(twist * v)
            P.append(Vector((x * c - z * s, y, x * s + z * c)))
            UV.append((u, v))
    for iy in range(gy):
        for ix in range(gx):
            a = iy * (gx + 1) + ix
            b = a + gx + 1
            I += [a, a + 1, b + 1, a, b + 1, b]
    return P, UV, I


def obj(name, P, UV, I, mat, N=None):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(p) for p in P], [], [tuple(I[k:k + 3]) for k in range(0, len(I), 3)])
    uv = me.uv_layers.new(name="UVMap")
    flat = np.array([UV[i] for i in I], np.float32).ravel()
    uv.data.foreach_set("uv", flat)
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    return ob


def node(nt, kind, **inputs):
    n = nt.nodes.new(kind)
    for k, v in inputs.items():
        n.inputs[k].default_value = v
    return n


def shader(name, mode, color=None, tex=None):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    L = nt.links.new
    geo = nt.nodes.new("ShaderNodeNewGeometry")
    alpha = None
    if tex:
        top, under, height = (nt.nodes.new("ShaderNodeTexImage") for _ in range(3))
        top.image, under.image, height.image = tex
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        L(geo.outputs["Backfacing"], mix.inputs[0])
        L(top.outputs["Color"], mix.inputs[6])
        L(under.outputs["Color"], mix.inputs[7])
        info = nt.nodes.new("ShaderNodeObjectInfo")
        shade = nt.nodes.new("ShaderNodeMath")
        shade.operation = "MULTIPLY_ADD"
        L(info.outputs["Random"], shade.inputs[0])
        shade.inputs[1].default_value, shade.inputs[2].default_value = .34, .83
        dim = nt.nodes.new("ShaderNodeMix")
        dim.data_type, dim.blend_type = "RGBA", "MULTIPLY"
        dim.inputs[0].default_value = 1
        L(mix.outputs[2], dim.inputs[6])
        L(shade.outputs[0], dim.inputs[7])
        hash2 = nt.nodes.new("ShaderNodeMath")
        hash2.operation = "FRACT"
        times = nt.nodes.new("ShaderNodeMath")
        times.operation = "MULTIPLY"
        times.inputs[1].default_value = 7.31
        L(info.outputs["Random"], times.inputs[0])
        L(times.outputs[0], hash2.inputs[0])
        yellow = nt.nodes.new("ShaderNodeMath")
        yellow.operation = "GREATER_THAN"
        yellow.inputs[1].default_value = .9
        L(hash2.outputs[0], yellow.inputs[0])
        warm = nt.nodes.new("ShaderNodeMix")
        warm.data_type, warm.blend_type = "RGBA", "MULTIPLY"
        L(yellow.outputs[0], warm.inputs[0])
        L(dim.outputs[2], warm.inputs[6])
        warm.inputs[7].default_value = (1.3, 1.12, .55, 1)
        col = warm.outputs[2]
        alpha = top.outputs["Alpha"]
        bump = node(nt, "ShaderNodeBump", Strength=.55, Distance=.003)
        L(height.outputs["Color"], bump.inputs["Height"])
        nrm = bump.outputs["Normal"]
    else:
        rgb = nt.nodes.new("ShaderNodeRGB")
        rgb.outputs[0].default_value = (*color, 1)
        col = rgb.outputs[0]
        nrm = geo.outputs["Normal"]
    if mode == "ao":
        surf = node(nt, "ShaderNodeBsdfDiffuse", Color=(1, 1, 1, 1))
    else:
        surf = nt.nodes.new("ShaderNodeEmission")
        surf.inputs["Strength"].default_value = 1
        if mode == "albedo":
            L(col, surf.inputs["Color"])
        else:
            enc = nt.nodes.new("ShaderNodeVectorMath")
            enc.operation = "MULTIPLY_ADD"
            L(nrm, enc.inputs[0])
            enc.inputs[1].default_value = (.5, .5, .5)
            enc.inputs[2].default_value = (.5, .5, .5)
            L(enc.outputs[0], surf.inputs["Color"])
    if alpha is None:
        L(surf.outputs[0], out.inputs["Surface"])
    else:
        clear = nt.nodes.new("ShaderNodeBsdfTransparent")
        mixs = nt.nodes.new("ShaderNodeMixShader")
        L(alpha, mixs.inputs[0])
        L(clear.outputs[0], mixs.inputs[1])
        L(surf.outputs[0], mixs.inputs[2])
        L(mixs.outputs[0], out.inputs["Surface"])
    return m


def srgb(c):
    return tuple((v / 255) ** 2.2 for v in c)


# A card of foliage: a short branchlet with twigs fanned off it (how many, how many leaves each), drawn on a
# square card (metres); each leaf's length, its stalk, the angle a stalk leaves its twig at, the stems'
# radius (base, tip) and colour.
TWIG = {
    "plane": {"size": 1.3, "twigs": 6, "per": (7, 9), "leaf": .21, "petiole": (.05, .08), "angle": (50, 72),
              "stem": (.008, .0018), "stemcol": (98, 84, 60)},
    "linden": {"size": .9, "twigs": 8, "per": (12, 15), "leaf": .092, "petiole": (.02, .034), "angle": (48, 68),
               "stem": (.006, .0012), "stemcol": (108, 72, 50)},
}


def along(pts, t):
    f = t * (len(pts) - 1)
    k = min(int(f), len(pts) - 2)
    p = pts[k].lerp(pts[k + 1], f - k)
    return p, (pts[k + 1] - pts[k]).normalized()


def turn(v, a):
    c, s = math.cos(a), math.sin(a)
    return Vector((v.x * c - v.y * s, v.x * s + v.y * c, v.z))


def twig(name, sp, tw, seed, mats):
    rng = random.Random(seed)
    S = tw["size"]
    base = Vector((0, -S / 2 + .006, 0))
    top = Vector((rng.uniform(-.04, .04) * S, -S / 2 + S * .4, .004))
    main = [base.lerp(top, t) for t in np.linspace(0, 1, 8)]
    stems = [(main, (tw["stem"][0], tw["stem"][0] * .6))]
    spots = []
    n = tw["twigs"]
    for k in range(n):
        ang = math.radians(-60 + 120 * (k + .5) / n + rng.uniform(-8, 8))
        p, d = along(main, 1.0 if abs(ang) < .35 else rng.uniform(.35, .95))
        dirn = turn(Vector((0, 1, 0)), -ang)
        ln = S * rng.uniform(.44, .56) * (1 - .22 * abs(ang))
        bow, sgn = rng.uniform(.015, .04) * S, rng.choice((-1, 1))
        pts = [p + dirn * ln * u + turn(dirn, math.pi / 2) * math.sin(u * math.pi) * bow * sgn + Vector((0, 0, .003 + .005 * u + .004 * k))
               for u in np.linspace(0, 1, 10)]
        stems.append((pts, (tw["stem"][0] * .5, tw["stem"][1])))
        K = rng.randint(*tw["per"])
        for j in range(K):
            t = .14 + .8 * j / (K - 1) + rng.uniform(-.03, .03)
            spots.append((pts, t, 1 if (j + k) % 2 else -1, 1 - .3 * t, .004 * k + .002 * j))
        spots.append((pts, 1.0, 0, .7, .004 * k + .002 * K))
    L, notch = tw["leaf"], SPECIES[sp]["half"][-1][1]
    for k, (path, t, side, scale, z) in enumerate(spots):
        p, d = along(path, min(t, 1.0))
        if side:
            pd = turn(d, math.radians(rng.uniform(*tw["angle"])) * side)
            pl = rng.uniform(*tw["petiole"])
            pe = p + pd * pl + Vector((0, 0, .002))
            stems.append(([p, p.lerp(pe, .5) + Vector((0, 0, .002)), pe], (tw["stem"][1] * 1.1, tw["stem"][1] * .8)))
            ld = turn(pd, -side * math.radians(rng.uniform(12, 32)))
        else:
            pe, ld = p, d
        size = L * scale * rng.uniform(.85, 1.1)
        roll, pitch = math.radians(rng.uniform(-28, 28)), math.radians(rng.uniform(-14, 18))
        for _ in range(8):
            P, UV, I = blade(size, notch, fold=rng.uniform(.1, .25), droop=rng.uniform(.12, .3), twist=rng.uniform(-.25, .25))
            M = (Matrix.Translation(pe + Vector((0, 0, z + rng.uniform(0, .004)))) @ Matrix.Rotation(math.atan2(-ld.x, ld.y), 4, "Z")
                 @ Matrix.Rotation(pitch, 4, "X") @ Matrix.Rotation(roll, 4, "Y"))
            P = [M @ q for q in P]
            if max(max(abs(q.x), abs(q.y)) for q in P) < S / 2 * .97:
                break
            size *= .9
        obj(f"{name}_leaf{k}", P, UV, I, mats["leaf"])
    for k, (path, (r0, r1)) in enumerate(stems):
        m = len(path)
        P, N, UV, I = tube(path, [r0 + (r1 - r0) * i / (m - 1) for i in range(m)], 6)
        obj(f"{name}_stem{k}", P, UV, I, mats["stem"])


def scene_reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.render.film_transparent = True
    sc.render.resolution_percentage = 100
    sc.cycles.use_denoising = False
    sc.cycles.transparent_max_bounces = 64
    sc.view_settings.look = "None"
    world = bpy.data.worlds.new("w")
    sc.world = world
    world.use_nodes = True
    return sc


def render(sc, path, mode, samples):
    sc.cycles.samples = samples
    sc.cycles.max_bounces = 0
    sc.view_settings.view_transform = "Standard" if mode == "albedo" else "Raw"
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGBA"
    sc.render.image_settings.color_depth = "8" if mode == "albedo" else "16"
    bg = sc.world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (1, 1, 1, 1)
    bg.inputs["Strength"].default_value = 1.0 if mode == "ao" else 0.0
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)


def lin(c):
    return np.where(c <= .04045, c / 12.92, ((c + .055) / 1.055) ** 2.4)


def enc(c):
    return np.where(c <= .0031308, c * 12.92, 1.055 * np.power(np.maximum(c, 0), 1 / 2.4) - .055)


def dilate(rgb, alpha, steps=24):
    rgb, have = rgb.copy(), alpha > .02
    for _ in range(steps):
        acc, cnt = np.zeros_like(rgb), np.zeros(have.shape)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            h = np.roll(have, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * h[..., None]
            cnt += h
        grow = ~have & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        have = have | grow
    rgb[~have] = rgb[have].mean(axis=0)
    return rgb


# The atlas: a 2 x 2 grid of twig cells, a row a species (plane below, linden above), two twigs each.
CELLS = [("plane", 0), ("plane", 1), ("linden", 0), ("linden", 1)]


def stage_twigs():
    n = CELL_PX
    atlas = {"color": np.zeros((2 * n, 2 * n, 4)), "normal": np.zeros((2 * n, 2 * n, 3)), "orm": np.zeros((2 * n, 2 * n, 3))}
    for ci, (sp, var) in enumerate(CELLS):
        sc = scene_reset()
        sc.render.resolution_x = sc.render.resolution_y = n
        tex = [bpy.data.images.load(os.path.join(RAW, f"leaf_{sp}_{k}.png")) for k in ("top", "under", "height")]
        tex[2].colorspace_settings.name = "Non-Color"
        tw = TWIG[sp]
        cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = tw["size"]
        cam.location = (0, 0, 3)
        sc.collection.objects.link(cam)
        sc.camera = cam
        layers = {}
        for mode, samples in (("albedo", 24), ("normal", 24), ("ao", 384)):
            mats = {"leaf": shader(f"leaf_{mode}", mode, tex=tex), "stem": shader(f"stem_{mode}", mode, color=srgb(tw["stemcol"]))}
            if mode == "albedo":
                twig(f"{sp}{var}", sp, tw, 100 + ci * 7, mats)
            else:
                for ob in sc.objects:
                    if ob.type == "MESH":
                        ob.data.materials[0] = mats["leaf" if "_leaf" in ob.name else "stem"]
            path = os.path.join(RAW, f"twig_{sp}{var}_{mode}.png")
            render(sc, path, mode, samples)
            layers[mode] = pixels(path)
        a = layers["albedo"][..., 3]
        ao = layers["ao"][..., 0]
        col = enc(lin(layers["albedo"][..., :3]) * (1 - .55 * (1 - ao))[..., None])
        nrm = layers["normal"][..., :3]
        nrm[a < .02] = (.5, .5, 1)
        oy, ox = (ci // 2) * n, (ci % 2) * n
        atlas["color"][oy:oy + n, ox:ox + n] = np.concatenate([dilate(col, a), a[..., None]], 2)
        atlas["normal"][oy:oy + n, ox:ox + n] = dilate(nrm, a)
        rough = np.where(layers["albedo"][..., 1] > layers["albedo"][..., 0] * 1.05, .58, .82)
        atlas["orm"][oy:oy + n, ox:ox + n] = np.stack([ao, rough, np.zeros_like(ao)], 2)
        print("twig", sp, var, "cover", round(float(a.mean()), 3))
    save(image("atlas_color", atlas["color"][..., :3], atlas["color"][..., 3]), os.path.join(RAW, "leaves_color.png"))
    save(image("atlas_normal", atlas["normal"], None, True), os.path.join(RAW, "leaves_normal.png"))
    save(image("atlas_orm", atlas["orm"], None, True), os.path.join(RAW, "leaves_orm.png"))


def pnoise(n, cx, cy, rng):
    g = rng.random((cy, cx))
    s = lambda f: f * f * (3 - 2 * f)
    ty, tx = (np.arange(n) + .5) / n * cy, (np.arange(n) + .5) / n * cx
    iy, ix = ty.astype(int), tx.astype(int)
    fy, fx = s(ty - iy), s(tx - ix)
    a, b = g[iy % cy][:, ix % cx], g[iy % cy][:, (ix + 1) % cx]
    c, d = g[(iy + 1) % cy][:, ix % cx], g[(iy + 1) % cy][:, (ix + 1) % cx]
    return (a * (1 - fx) + b * fx) * (1 - fy)[:, None] + (c * (1 - fx) + d * fx) * fy[:, None]


def pfbm(n, cx, cy, rng, octaves=4):
    out, amp, tot = np.zeros((n, n)), 1.0, 0.0
    for o in range(octaves):
        out += pnoise(n, cx * 2 ** o, cy * 2 ** o, rng) * amp
        tot += amp
        amp *= .5
    return out / tot


def cells(n, k, rng, stretch, warp=None):
    seeds = rng.random((k, 2))
    v, u = (np.mgrid[0:n, 0:n] + .5) / n
    if warp is not None:
        u, v = u + warp[0], v + warp[1]
    f1, f2, idx = np.full((n, n), 9.0), np.full((n, n), 9.0), np.zeros((n, n), int)
    for i, (sx, sy) in enumerate(seeds):
        dx = np.abs(u - sx) % 1
        dy = np.abs(v - sy) % 1
        d = np.hypot(np.minimum(dx, 1 - dx), np.minimum(dy, 1 - dy) * stretch)
        closer = d < f1
        f2 = np.where(closer, f1, np.minimum(f2, d))
        idx = np.where(closer, i, idx)
        f1 = np.where(closer, d, f1)
    return f1, f2, idx


# London plane bark: old bark (grey, olive, brown) flaking off in patches of every size, the new bark
# under it cream to yellow-green, the edges between them sharp but ragged, never outlined.
def bark_plane(n, rng):
    warp = ((pfbm(n, 6, 6, rng, 4) - .5) * .09, (pfbm(n, 6, 6, rng, 4) - .5) * .09)
    under = np.array([196, 188, 146]) / 255 * (.9 + .2 * pfbm(n, 10, 10, rng, 4))[..., None]
    under = under * (1 - .25 * np.clip((pfbm(n, 5, 5, rng, 3) - .45) * 3, 0, 1))[..., None] + \
        np.array([150, 160, 112]) / 255 * (.25 * np.clip((pfbm(n, 5, 5, rng, 3) - .45) * 3, 0, 1))[..., None]
    old = np.array([(138, 136, 108), (112, 108, 86), (126, 130, 100), (98, 94, 78), (150, 146, 116)], float) / 255
    col, height = under.copy(), np.zeros((n, n))
    for k, keep in ((36, .6), (90, .45)):
        f1, f2, idx = cells(n, k, rng, .8, warp)
        pick = rng.integers(0, len(old), k)
        on = (rng.random(k) < keep)[idx]
        ragged = (f2 - f1) - .006 - .02 * pfbm(n, 24, 24, rng, 3)
        mask = np.clip(ragged / .004, 0, 1) * on
        tone = old[pick[idx]] * (.85 + .3 * pfbm(n, 14, 14, rng, 3))[..., None]
        col = col * (1 - mask[..., None]) + tone * mask[..., None]
        height = np.maximum(height, mask * (.6 + .4 * np.clip(ragged * 20, 0, 1)))
    height = height + .15 * pfbm(n, 32, 32, rng, 3)
    return col, height


def bark_linden(n, rng):
    f = pfbm(n, 7, 1, rng, 4)
    ridge = 1 - np.abs(2 * f - 1)
    plates = pfbm(n, 16, 3, rng, 3)
    height = ridge ** 1.6 * .72 + plates * .28
    dark, light = np.array([66, 61, 54]) / 255, np.array([126, 120, 108]) / 255
    col = dark + (light - dark) * np.clip(height * 1.15, 0, 1)[..., None]
    col *= (.92 + .16 * pfbm(n, 4, 4, rng, 3))[..., None]
    lichen = np.clip((pfbm(n, 6, 6, rng, 4) - .62) * 6, 0, 1) * .4
    col = col * (1 - lichen[..., None]) + np.array([128, 136, 108]) / 255 * lichen[..., None]
    return col, height


def normals(height, k):
    dx = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * k
    dy = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * k
    n = np.stack([-dx, -dy, np.ones_like(dx)], 2)
    return n / np.linalg.norm(n, axis=2, keepdims=True) * .5 + .5


BARK_PX = 512


def stage_bark():
    for i, (name, make, k) in enumerate((("plane", bark_plane, 5.0), ("linden", bark_linden, 9.0))):
        rng = np.random.default_rng(31 + i)
        col, h = make(BARK_PX, rng)
        h = (h - h.min()) / (h.max() - h.min())
        orm = np.stack([.55 + .45 * h, .97 - .12 * h, np.zeros_like(h)], 2)
        save(image(f"bark_{name}_color", np.clip(col, 0, 1)), os.path.join(RAW, f"bark_{name}_color.png"))
        save(image(f"bark_{name}_normal", normals(h, k), None, True), os.path.join(RAW, f"bark_{name}_normal.png"))
        save(image(f"bark_{name}_orm", orm, None, True), os.path.join(RAW, f"bark_{name}_orm.png"))
        print("bark", name)


# A tree: its clear trunk under the crown, the crown (width, height), the trunk's radius, how many scaffold
# limbs leave it and at what angle from the vertical, how far a twig card reaches, how many cards, and the
# share of them the far level of detail keeps.
TREES = {
    "plane": {"clear": 3.0, "crown": (5.6, 5.6), "r": .16, "limbs": (4, 6), "rise": (40, 60), "cards": 210},
    "linden": {"clear": 2.7, "crown": (4.8, 5.6), "r": .13, "limbs": (5, 7), "rise": (30, 48), "cards": 230},
}
VARIANTS = [("plane", 0, 1.0), ("plane", 1, 1.0), ("linden", 0, 1.0), ("linden", 1, 1.0), ("linden", 2, .68)]
FAR_SHARE, FAR_GROW = .46, 1.5
UP = Vector((0, 0, 1))


def unit(rng):
    while True:
        v = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1)))
        if .01 < v.length <= 1:
            return v.normalized()


class Crown:
    def __init__(s, sp, scale, rng):
        W, H = sp["crown"]
        s.c = Vector((0, 0, (sp["clear"] + H / 2) * scale))
        s.r = Vector((W / 2, W / 2, H / 2)) * scale
        s.lobes = [(unit(rng), rng.uniform(.12, .28)) for _ in range(9)]

    def level(s, p):
        q = p - s.c
        e = Vector((q.x / s.r.x, q.y / s.r.y, q.z / s.r.z))
        l = e.length
        if l < 1e-6:
            return 0.0
        d = e / l
        return l / (.94 + sum(a * max(0.0, d.dot(u)) ** 3 for u, a in s.lobes))

    def at(s, d, lvl):
        k = lvl * (.94 + sum(a * max(0.0, d.dot(u)) ** 3 for u, a in s.lobes))
        return s.c + Vector((s.r.x * d.x, s.r.y * d.y, s.r.z * d.z)) * k

    def out(s, p):
        q = p - s.c
        g = Vector((q.x / s.r.x ** 2, q.y / s.r.y ** 2, q.z / s.r.z ** 2))
        return g.normalized() if g.length > 1e-6 else UP.copy()

    def reach(s, p, d, cap=12.0):
        t = 0.0
        while t < cap and s.level(p + d * t) < 1.0:
            t += .08
        return t


class Branch:
    def __init__(s, pts, radii, depth):
        s.pts, s.radii, s.depth = pts, radii, depth
        s.cum = [0.0]
        for i in range(len(pts) - 1):
            s.cum.append(s.cum[-1] + (pts[i + 1] - pts[i]).length)
        s.length = s.cum[-1]

    def at(s, x):
        x = min(max(x, 0.0), s.length)
        for i in range(len(s.pts) - 1):
            if s.cum[i + 1] >= x or i == len(s.pts) - 2:
                seg = s.cum[i + 1] - s.cum[i]
                f = (x - s.cum[i]) / seg if seg > 1e-9 else 0.0
                d = (s.pts[i + 1] - s.pts[i]).normalized()
                return s.pts[i].lerp(s.pts[i + 1], f), d, s.radii[i] + (s.radii[i + 1] - s.radii[i]) * f


def grow(crown, p0, d0, length, r0, r1, depth, rng, step, bend, wiggle, slack=1.04):
    pts, d = [p0.copy()], d0.normalized()
    n = max(2, int(math.ceil(length / step)))
    for i in range(n):
        d = (d + UP * bend * step + unit(rng) * wiggle * step).normalized()
        q = pts[-1] + d * (length / n)
        if i > 0 and crown.level(q) > slack:
            break
        pts.append(q)
    if len(pts) < 2:
        pts.append(p0 + d0 * .1)
    m = len(pts)
    return Branch(pts, [r0 + (r1 - r0) * (i / (m - 1)) ** .8 for i in range(m)], depth)


def side_dir(d, rot, angle):
    a = d.orthogonal().normalized()
    a = Matrix.Rotation(rot, 3, d) @ a
    return (d * math.cos(angle) + a * math.sin(angle)).normalized()


def skeleton(sp, scale, rng):
    crown = Crown(sp, scale, rng)
    clear, H = sp["clear"] * scale, sp["crown"][1] * scale
    R = sp["r"] * scale ** 1.3
    top = clear + H * .58
    ph = rng.uniform(0, 6.3), rng.uniform(0, 6.3)
    lean = Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), 0)) * .015
    pts, radii = [], []
    m = int(top / .22) + 1
    for i in range(m):
        z = -.08 + i * (top + .08) / (m - 1)
        pts.append(Vector((math.sin(z * 1.4 + ph[0]) * .025 + lean.x * z, math.cos(z * 1.2 + ph[1]) * .025 + lean.y * z, z)))
        flare = 1 + .55 * math.exp(-max(z, 0) / .22)
        radii.append(R * (1.08 - .5 * max(z, 0) / top) * flare)
    trunk = Branch(pts, radii, 0)
    branches = [trunk]
    limbs = []
    n1 = rng.randint(*sp["limbs"])
    az0 = rng.uniform(0, 6.3)
    for k in range(n1):
        h = clear + (k + rng.uniform(0, .7)) / n1 * H * .4
        p, d, r = trunk.at(h + .08)
        az = az0 + k * 2.39996 + rng.uniform(-.35, .35)
        el = math.radians(rng.uniform(*sp["rise"]) + 12 * (1 - k / n1))
        dd = Vector((math.sin(el) * math.cos(az), math.sin(el) * math.sin(az), math.cos(el)))
        L = crown.reach(p, dd) * rng.uniform(.82, .98)
        limbs.append(grow(crown, p, dd, L, r * rng.uniform(.55, .72), .025 * scale, 1, rng, .32, .06, .12))
    p, d, r = trunk.at(trunk.length)
    lead_d = (UP + unit(rng) * .15).normalized()
    limbs.append(grow(crown, p, lead_d, crown.reach(p, lead_d) * .9, r, .025 * scale, 1, rng, .32, .02, .1))
    branches += limbs
    seconds = []
    for limb in limbs:
        x, rot = limb.length * rng.uniform(.22, .32), rng.uniform(0, 6.3)
        while x < limb.length * .96:
            p, d, r = limb.at(x)
            rot += 2.39996 + rng.uniform(-.3, .3)
            cd = (side_dir(d, rot, math.radians(rng.uniform(36, 56))) + UP * .18).normalized()
            L = min(crown.reach(p, cd) * rng.uniform(.8, 1.05), (limb.length - x) * .85 + .9)
            if L > .45:
                seconds.append(grow(crown, p, cd, L, max(r * rng.uniform(.42, .58), .012), .008 * scale, 2, rng, .3, .1, .3))
            x += rng.uniform(.4, .65) * scale ** .5
    branches += seconds
    thirds = []
    for sb in seconds:
        x, rot = sb.length * rng.uniform(.28, .4), rng.uniform(0, 6.3)
        while x < sb.length * .98:
            p, d, r = sb.at(x)
            rot += 2.39996 + rng.uniform(-.4, .4)
            cd = (side_dir(d, rot, math.radians(rng.uniform(38, 62))) + UP * .1).normalized()
            L = min(rng.uniform(.3, .75) * scale ** .5, crown.reach(p, cd) + .25)
            if L > .18:
                thirds.append(grow(crown, p, cd, L, max(r * .55, .005), .003, 3, rng, .25, .05, .4, 1.12))
            x += rng.uniform(.22, .36) * scale ** .5
    branches += thirds
    return crown, branches


def cards(sp, crown, branches, size, count, rng):
    anchors = []
    for b in branches:
        if b.depth < 2:
            continue
        x = b.length * .3
        while x <= b.length:
            p, d, r = b.at(x)
            anchors.append((p, d))
            x += .2
    kd = KDTree(len(anchors))
    for i, (p, d) in enumerate(anchors):
        kd.insert(p, i)
    kd.balance()
    out, cell, gap, tries = [], {}, size * .4, 0
    while len(out) < count and tries < count * 80:
        tries += 1
        d = unit(rng)
        if d.z < -.35 and rng.random() < .6:
            continue
        p = crown.at(d, rng.uniform(.6, 1.0) ** .55)
        key = tuple(int(math.floor(v / gap)) for v in p)
        near = [q for dx in (-1, 0, 1) for dy in (-1, 0, 1) for dz in (-1, 0, 1)
                for q in cell.get((key[0] + dx, key[1] + dy, key[2] + dz), ())]
        if any((q - p).length < gap for q in near):
            continue
        cell.setdefault(key, []).append(p)
        a, i, dist = kd.find(p)
        bd = anchors[i][1]
        o = crown.out(p)
        reach = p - a
        up = ((reach.normalized() if reach.length > 1e-3 else o) * .45 + o * .4 + bd * .25 + UP * .15).normalized()
        nrm = o - up * o.dot(up)
        if nrm.length < 1e-3:
            nrm = UP - up * UP.dot(up)
        nrm = Matrix.Rotation(math.radians(rng.uniform(-40, 40)), 3, up) @ nrm.normalized()
        sz = size * rng.uniform(.82, 1.12)
        out.append({"p": p - up * sz * .5, "up": up, "n": nrm.normalized(), "s": sz, "lvl": crown.level(p),
                    "cell": rng.randint(0, 1), "flip": rng.random() < .5})
    out.sort(key=lambda c: -(c["lvl"] + rng.uniform(0, .3)))
    return out


def card_geo(c, cell0, grow_by=1.0, fold=.2):
    s = c["s"] * grow_by
    up, n = c["up"], c["n"]
    side = up.cross(n).normalized()
    base = c["p"] - up * (s - c["s"]) * .25
    lift = n * fold * s * .5
    cx, cy = (cell0 + c["cell"]) % 2, (cell0 + c["cell"]) // 2
    u0, u1 = cx * .5, cx * .5 + .5
    if c["flip"]:
        u0, u1 = u1, u0
    vb, vt = 1 - cy * .5 - .002, 1 - cy * .5 - .5
    P, UV = [], []
    for row, (h, v) in enumerate(((0.0, vb), (s, vt))):
        for col, (w, u) in enumerate(((-.5, u0), (0.0, (u0 + u1) / 2), (.5, u1))):
            P.append(base + up * h + side * w * s + (lift if col != 1 else Vector()))
            UV.append((u, v))
    I = [0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4]
    return P, UV, I


def visibility(bvh, p, n, rng, rays=24, reach=6.0):
    seen = 0.0
    for _ in range(rays):
        d = unit(rng)
        if d.dot(n) < 0:
            d = -d
        t, at = 1.0, p + n * .05
        for _ in range(6):
            hit = bvh.ray_cast(at, d, reach)
            if hit[0] is None:
                break
            t *= .0 if hit[2] >= OPAQUE_FROM else .5
            if t < .02:
                break
            at = hit[0] + d * .02
        seen += t
    return seen / rays


OPAQUE_FROM = 1 << 30


def stage_trees():
    global OPAQUE_FROM
    meshes, blob = [], bytearray()

    def put(arr, kind):
        a = np.ascontiguousarray(arr, dtype=kind)
        off = len(blob)
        blob.extend(a.tobytes())
        while len(blob) % 4:
            blob.append(0)
        return off

    for sp_name, var, scale in VARIANTS:
        sp = TREES[sp_name]
        rng = random.Random(f"{sp_name}{var}")
        crown, branches = skeleton(sp, scale, rng)
        twig = TWIG[sp_name]["size"] * (.92 if scale < 1 else 1)
        near_cards = cards(sp, crown, branches, twig, int(sp["cards"] * scale ** 1.6), rng)
        far_n = int(len(near_cards) * FAR_SHARE)
        cell0 = 0 if sp_name == "plane" else 2
        leafP, leafI = [], []
        for c in near_cards:
            P, UV, I = card_geo(c, cell0)
            leafI += [i + len(leafP) for i in I]
            leafP += P
        OPAQUE_FROM = len(leafI) // 3
        barkP, barkI = [], []
        for b in branches:
            if b.depth > 2:
                continue
            sides = (12, 8, 5, 3)[b.depth]
            P, N, UV, I = tube(b.pts, b.radii, sides)
            barkI += [i + len(barkP) for i in I]
            barkP += P
        bvh = BVHTree.FromPolygons([tuple(p) for p in leafP + barkP],
                                   [tuple(leafI[k:k + 3]) for k in range(0, len(leafI), 3)] +
                                   [tuple(i + len(leafP) for i in barkI[k:k + 3]) for k in range(0, len(barkI), 3)])
        vis_rng = random.Random(7)
        for c in near_cards:
            mid = c["p"] + c["up"] * c["s"] * .5
            c["vis"] = visibility(bvh, mid, crown.out(mid), vis_rng)
            c["tint"] = (1 + .07 * (vis_rng.random() - .5), 1 + .05 * (vis_rng.random() - .5), 1 + .1 * (vis_rng.random() - .5))
        for lod in (0, 1):
            keep = near_cards if lod == 0 else near_cards[:far_n]
            grow_by = 1.0 if lod == 0 else FAR_GROW
            P, N, UV, C, I = [], [], [], [], []
            for c in keep:
                p, uv, idx = card_geo(c, cell0, grow_by, .2 if lod == 0 else .0)
                if lod == 1:
                    p, uv, idx = [p[0], p[2], p[3], p[5]], [uv[0], uv[2], uv[3], uv[5]], [0, 1, 3, 0, 3, 2]
                k0 = len(P)
                for q in p:
                    o = crown.out(q)
                    N.append((o * .72 + c["n"] * .28).normalized())
                    shade = (.32 + .68 * c["vis"]) * (.86 + .14 * (o.z * .5 + .5))
                    C.append(tuple(min(1.0, shade * t) for t in c["tint"]))
                P += p
                UV += uv
                I += [i + k0 for i in idx]
            leaves = (P, N, UV, C, I)
            P, N, UV, C, I = [], [], [], [], []
            for b in branches:
                if b.depth > 2:
                    continue
                sides = ((12, 8, 5, 3) if lod == 0 else (7, 5, 3, 3))[b.depth]
                pts, radii = b.pts, b.radii
                if lod == 1 and len(pts) > 3:
                    pick = sorted(set(list(range(0, len(pts), 2)) + [len(pts) - 1]))
                    pts, radii = [pts[i] for i in pick], [radii[i] for i in pick]
                urep = max(1, round(2 * math.pi * radii[0] / .55))
                p, nn, uv, idx = tube(pts, radii, sides, .9, urep)
                uv = [(u, -v) for u, v in uv]
                ring = sides + 1
                k0 = len(P)
                for ri in range(len(pts)):
                    v = visibility(bvh, pts[ri] + UP * (radii[ri] + .05), UP, vis_rng, 8) if b.depth else 1.0
                    z = pts[ri].z
                    shade = (.42 + .58 * v) * (.72 + .28 * min(max(z, 0) / .35, 1.0))
                    C += [(shade, shade, shade)] * ring
                P += p
                N += nn
                UV += uv
                I += [i + k0 for i in idx]
            bark = (P, N, UV, C, I)
            parts = []
            for mat, (P, N, UV, C, I) in ((f"bark_{sp_name}", bark), ("leaves", leaves)):
                parts.append({
                    "material": mat, "count": len(P), "tris": len(I) // 3,
                    "position": put([tuple(p) for p in P], np.float32), "normal": put([tuple(n) for n in N], np.float32),
                    "uv": put(UV, np.float32), "color": put(np.round(np.array(C) * 255), np.uint8),
                    "index": put(I, np.uint32),
                })
            name = f"{sp_name}_{var}_lod{lod}"
            lo = np.min([tuple(p) for p in bark[0] + leaves[0]], axis=0)
            hi = np.max([tuple(p) for p in bark[0] + leaves[0]], axis=0)
            meshes.append({"name": name, "species": sp_name, "lod": lod, "scale": scale, "parts": parts,
                           "min": [round(float(v), 3) for v in lo], "max": [round(float(v), 3) for v in hi]})
            print(name, "bark tris", parts[0]["tris"], "leaf tris", parts[1]["tris"], "cards", len(keep),
                  "height", round(float(hi[2]), 2), "width", round(float(hi[0] - lo[0]), 2))
    with open(os.path.join(RAW, "trees.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(RAW, "trees.json"), "w") as f:
        json.dump({"meshes": meshes, "bytes": len(blob)}, f, indent=1)


def look(name, color, normal, orm, alpha):
    m = bpy.data.materials.new(name)
    if m.node_tree is None:
        m.use_nodes = True
    nt = m.node_tree
    L = nt.links.new
    bsdf = nt.nodes["Principled BSDF"]
    tc, tn, to = (nt.nodes.new("ShaderNodeTexImage") for _ in range(3))
    tc.image = bpy.data.images.load(color)
    tn.image, to.image = bpy.data.images.load(normal), bpy.data.images.load(orm)
    tn.image.colorspace_settings.name = to.image.colorspace_settings.name = "Non-Color"
    vc = nt.nodes.new("ShaderNodeVertexColor")
    vc.layer_name = "Col"
    mul = nt.nodes.new("ShaderNodeMix")
    mul.data_type, mul.blend_type = "RGBA", "MULTIPLY"
    mul.inputs[0].default_value = 1
    L(tc.outputs["Color"], mul.inputs[6])
    L(vc.outputs["Color"], mul.inputs[7])
    L(mul.outputs[2], bsdf.inputs["Base Color"])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    L(tn.outputs["Color"], nm.inputs["Color"])
    L(nm.outputs["Normal"], bsdf.inputs["Normal"])
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    L(to.outputs["Color"], sep.inputs["Color"])
    L(sep.outputs[1], bsdf.inputs["Roughness"])
    if alpha:
        L(tc.outputs["Alpha"], bsdf.inputs["Alpha"])
        bsdf.inputs["Transmission Weight"].default_value = .25
    return m


def stage_preview():
    sc = scene_reset()
    sc.render.film_transparent = False
    sc.render.resolution_x, sc.render.resolution_y = 1800, 760
    meta = json.load(open(os.path.join(RAW, "trees.json")))
    blob = open(os.path.join(RAW, "trees.bin"), "rb").read()
    mats = {"leaves": look("leaves", *(os.path.join(RAW, f"leaves_{k}.png") for k in ("color", "normal", "orm")), True)}
    for sp in TREES:
        mats[f"bark_{sp}"] = look(f"bark_{sp}", *(os.path.join(RAW, f"bark_{sp}_{k}.png") for k in ("color", "normal", "orm")), False)
    x = 0.0
    pick = os.environ.get("PREVIEW", "plane_0_lod0,linden_0_lod0,linden_2_lod0,plane_0_lod1").split(",")
    show = [m for name in pick for m in meta["meshes"] if m["name"] == name]
    for m in show:
        for part in m["parts"]:
            n, t = part["count"], part["tris"]
            P = np.frombuffer(blob, np.float32, n * 3, part["position"]).reshape(n, 3)
            N = np.frombuffer(blob, np.float32, n * 3, part["normal"]).reshape(n, 3)
            UV = np.frombuffer(blob, np.float32, n * 2, part["uv"]).reshape(n, 2).copy()
            C = np.frombuffer(blob, np.uint8, n * 3, part["color"]).reshape(n, 3) / 255
            I = np.frombuffer(blob, np.uint32, t * 3, part["index"])
            UV[:, 1] = 1 - UV[:, 1]
            me = bpy.data.meshes.new(m["name"] + part["material"])
            me.from_pydata((P + np.array([x, 0, 0])).tolist(), [], I.reshape(-1, 3).tolist())
            me.uv_layers.new(name="UVMap").data.foreach_set("uv", UV[I].ravel())
            col = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
            col.data.foreach_set("color", np.concatenate([C, np.ones((n, 1))], 1).astype(np.float32).ravel())
            me.normals_split_custom_set_from_vertices(N.tolist())
            me.materials.append(mats[part["material"]])
            ob = bpy.data.objects.new(me.name, me)
            sc.collection.objects.link(ob)
        x += 10.0
    ground = bpy.data.meshes.new("ground")
    ground.from_pydata([(-10, -10, 0), (x + 2, -10, 0), (x + 2, 12, 0), (-10, 12, 0)], [], [(0, 1, 2, 3)])
    gm = bpy.data.materials.new("ground")
    if gm.node_tree is None:
        gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (.12, .12, .12, 1)
    ground.materials.append(gm)
    sc.collection.objects.link(bpy.data.objects.new("ground", ground))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 34
    cam.location = ((x - 10) / 2, -33, 4.5)
    cam.rotation_euler = (math.radians(88), 0, 0)
    sc.collection.objects.link(cam)
    sc.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(30))
    sc.collection.objects.link(sun)
    bg = sc.world.node_tree.nodes["Background"]
    bg.inputs["Color"].default_value = (.55, .62, .72, 1)
    bg.inputs["Strength"].default_value = 1.0
    sc.view_settings.view_transform = "AgX"
    sc.cycles.samples = 48
    sc.cycles.max_bounces = 4
    sc.cycles.use_denoising = True
    sc.render.image_settings.file_format = "PNG"
    sc.render.image_settings.color_mode = "RGB"
    sc.render.image_settings.color_depth = "8"
    sc.render.filepath = os.path.join(OUT, "preview.png")
    bpy.ops.render.render(write_still=True)


if "leaves" in STAGES:
    stage_leaves()
if "twigs" in STAGES:
    stage_twigs()
if "bark" in STAGES:
    stage_bark()
if "trees" in STAGES:
    stage_trees()
if "preview" in STAGES:
    stage_preview()
