# The world's cars, made from nothing but this script: Blender (headless) lofts each body from its
# profiles, lays the glass, lamps, grille, plates and seams on it, builds the wheels, and writes raw meshes
# that cars.mjs packs into RawAssets/world/cars/cars.glb.
# Usage: blender --background --factory-startup --python tools/assets/world/cars/cars.py -- <out-dir> [stage...]
#        stages: cars (the default), preview
import bpy, math, os, sys, json
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else "cars-build")
STAGES = set(ARGS[1:]) or {"cars"}
RAW = os.path.join(OUT, "raw")
os.makedirs(RAW, exist_ok=True)

# A car, in metres, front at +x, left at +y, up +z. `u` runs along it, 0 at the nose and 1 at the tail.
# Profiles are (u, value) keys: the centreline's top, the beltline (where the doors end and the windows
# begin), the half width of the top (roof, bonnet and boot), the underside. The glass, lamps and grille
# are outlines seen from the side (x, z), above (x, y) or the ends (y, z).
CARS = {
    "sedan": dict(
        L=4.66, W=.905, R=.318, tw=.215, track=.765, axles=(.2, .785), arch=.375, rb=.07, crown=.025, nose=.05, tail=.05,
        top=[(0, .66), (.012, .72), (.045, .775), (.17, .825), (.27, .87), (.295, .9), (.455, 1.405), (.49, 1.44), (.62, 1.452),
             (.7, 1.43), (.735, 1.385), (.855, 1.035), (.9, 1.012), (.96, .995), (.985, .945), (1, .8)],
        belt=[(0, .64), (.045, .74), (.3, .885), (.55, .928), (.85, .975), (.96, .97), (.99, .9), (1, .8)],
        roofw=[(0, .68), (.05, .82), (.27, .83), (.31, .8), (.455, .705), (.62, .695), (.735, .705), (.86, .8), (.96, .8), (1, .7)],
        bottom=[(0, .3), (.025, .23), (.07, .19), (.93, .19), (.975, .26), (1, .36)],
        ws=(.295, .455), rw=(.735, .855), bpil=.565, cpil=.845,
        ends=dict(head=((.46, .575), (.8, .59), (.76, .655), (.48, .65)), grille=((-.4, .5), (.4, .5), (.38, .62), (-.38, .62)),
                  intake=((-.62, .29), (.62, .29), (.58, .38), (-.58, .38)), plate_f=((-.26, .395), (.26, .395), (.26, .49), (-.26, .49)),
                  tail=((.46, .84), (.8, .848), (.79, .935), (.48, .93)), plate_r=((-.26, .58), (.26, .58), (.26, .68), (-.26, .68)),
                  diffuser=((-.7, .26), (.7, .26), (.68, .38), (-.68, .38))),
    ),
    "hatch": dict(
        L=4.25, W=.89, R=.31, tw=.205, track=.755, axles=(.195, .8), arch=.365, rb=.07, crown=.025, nose=.05, tail=.04,
        top=[(0, .66), (.012, .72), (.05, .78), (.19, .83), (.29, .9), (.315, .93), (.48, 1.43), (.52, 1.465), (.7, 1.475),
             (.8, 1.455), (.835, 1.41), (.93, 1.03), (.965, 1.0), (.99, .93), (1, .82)],
        belt=[(0, .64), (.05, .74), (.315, .89), (.6, .935), (.85, .985), (.93, .99), (.99, .93), (1, .82)],
        roofw=[(0, .67), (.05, .8), (.29, .81), (.33, .78), (.48, .7), (.7, .69), (.84, .71), (.93, .78), (1, .7)],
        bottom=[(0, .3), (.025, .23), (.07, .19), (.93, .2), (.975, .27), (1, .38)],
        ws=(.315, .48), rw=(.84, .925), bpil=.6, cpil=.81,
        ends=dict(head=((.45, .58), (.78, .595), (.74, .66), (.47, .655)), grille=((-.38, .5), (.38, .5), (.36, .62), (-.36, .62)),
                  intake=((-.6, .29), (.6, .29), (.56, .38), (-.56, .38)), plate_f=((-.26, .395), (.26, .395), (.26, .49), (-.26, .49)),
                  tail=((.52, .9), (.8, .91), (.78, 1.0), (.54, .99)), plate_r=((-.26, .6), (.26, .6), (.26, .7), (-.26, .7)),
                  diffuser=((-.68, .28), (.68, .28), (.66, .4), (-.66, .4))),
    ),
    "suv": dict(
        L=4.7, W=.94, R=.355, tw=.235, track=.79, axles=(.195, .79), arch=.41, rb=.08, crown=.02, nose=.05, tail=.04, clad=True,
        top=[(0, .8), (.012, .87), (.05, .935), (.2, 1.0), (.28, 1.04), (.3, 1.075), (.44, 1.6), (.48, 1.655), (.7, 1.68),
             (.86, 1.665), (.88, 1.635), (.96, 1.17), (.985, 1.07), (1, .94)],
        belt=[(0, .78), (.05, .9), (.3, 1.06), (.6, 1.1), (.88, 1.13), (.96, 1.12), (.99, 1.02), (1, .94)],
        roofw=[(0, .76), (.05, .86), (.28, .87), (.32, .84), (.44, .76), (.7, .75), (.88, .78), (.96, .85), (1, .78)],
        bottom=[(0, .38), (.025, .3), (.07, .25), (.93, .25), (.975, .32), (1, .44)],
        ws=(.3, .44), rw=(.88, .96), bpil=.58, cpil=.86,
        ends=dict(head=((.44, .69), (.76, .71), (.73, .78), (.46, .77)), grille=((-.4, .58), (.4, .58), (.38, .74), (-.38, .74)),
                  intake=((-.65, .38), (.65, .38), (.6, .52), (-.6, .52)), plate_f=((-.26, .53), (.26, .53), (.26, .62), (-.26, .62)),
                  tail=((.52, 1.0), (.78, 1.01), (.77, 1.11), (.54, 1.1)), plate_r=((-.26, .74), (.26, .74), (.26, .84), (-.26, .84)),
                  diffuser=((-.7, .34), (.7, .34), (.68, .5), (-.68, .5))),
    ),
    "van": dict(
        L=4.95, W=.96, R=.34, tw=.215, track=.81, axles=(.16, .8), arch=.39, rb=.08, crown=.02, nose=.05, tail=.03,
        top=[(0, .82), (.012, .9), (.05, .97), (.13, 1.04), (.155, 1.08), (.31, 1.88), (.35, 1.95), (.9, 1.975), (.975, 1.96), (1, 1.9)],
        belt=[(0, .8), (.05, .94), (.155, 1.07), (.5, 1.12), (.97, 1.14), (1, 1.12)],
        roofw=[(0, .78), (.05, .88), (.14, .9), (.18, .86), (.31, .83), (.9, .84), (1, .86)],
        bottom=[(0, .36), (.02, .3), (.06, .26), (.95, .27), (1, .32)],
        ws=(.155, .31), rw=None, bpil=.42, cpil=.6,
        ends=dict(head=((.46, .73), (.77, .75), (.74, .83), (.48, .82)), grille=((-.4, .6), (.4, .6), (.38, .77), (-.38, .77)),
                  intake=((-.66, .4), (.66, .4), (.62, .54), (-.62, .54)), plate_f=((-.26, .56), (.26, .56), (.26, .65), (-.26, .65)),
                  tail=((.68, .72), (.8, .72), (.8, 1.2), (.68, 1.2)), plate_r=((-.26, .56), (.26, .56), (.26, .66), (-.26, .66)),
                  diffuser=((-.72, .34), (.72, .34), (.7, .46), (-.7, .46)), rear_glass=((-.66, 1.32), (.66, 1.32), (.62, 1.78), (-.62, 1.78)),
                  door=((-.004, .5), (.004, .5), (.004, 1.84), (-.004, 1.84))),
    ),
}


def pchip(keys):
    xs = np.array([k[0] for k in keys], float)
    ys = np.array([k[1] for k in keys], float)
    h = np.diff(xs)
    d = np.diff(ys) / h
    m = np.zeros_like(ys)
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, len(xs) - 1):
        if d[i - 1] * d[i] <= 0:
            continue
        w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
        m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])

    def f(u):
        u = min(max(u, xs[0]), xs[-1])
        i = max(0, min(int(np.searchsorted(xs, u)) - 1, len(xs) - 2))
        t = (u - xs[i]) / h[i]
        return ((2 * t ** 3 - 3 * t ** 2 + 1) * ys[i] + (t ** 3 - 2 * t ** 2 + t) * h[i] * m[i]
                + (-2 * t ** 3 + 3 * t ** 2) * ys[i + 1] + (t ** 3 - t ** 2) * h[i] * m[i + 1])
    return f


class Car:
    def __init__(s, spec):
        s.c = spec
        s.top, s.belt, s.roofw, s.bottom = (pchip(spec[k]) for k in ("top", "belt", "roofw", "bottom"))
        s.L, s.W = spec["L"], spec["W"]

    def x(s, u):
        return s.L / 2 - u * s.L

    def plan(s, u):
        f = 1.0
        if u < .065:
            f -= .13 * (1 - u / .065) ** 2
        if u > .945:
            f -= .12 * (1 - (1 - u) / .055) ** 2
        return s.W * f

    # The half section at u: sixteen points from the underside's middle round the side to the top's
    # middle (y >= 0), the same sixteen at every u so the body is one grid.
    def half(s, u):
        c = s.c
        w = s.plan(u)
        zb = s.bottom(u)
        for ua in c["axles"]:
            dx = abs(u - ua) * s.L
            if dx < c["arch"]:
                zb = max(zb, c["R"] + math.sqrt(c["arch"] ** 2 - dx * dx))
        zs = s.belt(u)
        zt = max(s.top(u), zs + .015)
        wt = min(s.roofw(u), w - .03)
        g = zt - zs
        rt = min(.085, max(.012, g * .35))
        rb = c["rb"]
        side = max(zs - zb - rb, .01)
        bulge = .018
        lip = min(.025, g * .3)
        p = [(0, zb), (.5 * (w - rb), zb), (w - rb, zb), (w - rb * .29, zb + rb * .29), (w, zb + rb),
             (w + bulge * .7, zb + rb + side * .33), (w + bulge, zb + rb + side * .66), (w + bulge * .55, zs - .035 * min(1, side / .1)),
             (w + bulge * .15, zs), (w - .015, zs + lip), None, (wt + min(.03, g * .1), zt - rt),
             (wt - rt * .29, zt - rt * .29), (wt - rt, zt), (.45 * (wt - rt), zt + c["crown"] * .8), (0, zt + c["crown"])]
        p[10] = ((p[9][0] + p[11][0]) / 2 + (.012 if g > .2 else 0), (p[9][1] + p[11][1]) / 2)
        return p

    def ring(s, u, lod=0):
        h = s.half(u)
        if lod:
            h = [h[i] for i in (0, 2, 4, 6, 8, 9, 11, 13, 15)]
        return [(y, z) for y, z in h] + [(-y, z) for y, z in h[-2:0:-1]]


def stations(car, lod):
    c = car.c
    us = set(np.round(np.linspace(0, 1, 16 if lod == 0 else 9), 4))
    feats = [c["ws"][0], c["ws"][1]] + (list(c["rw"]) if c["rw"] else [])
    for ua in c["axles"]:
        a = c["arch"] / car.L
        feats += list(ua + a * np.array([-1, -.75, -.4, 0, .4, .75, 1] if lod == 0 else [-1, -.6, 0, .6, 1]))
    us |= set(np.round(feats, 4))
    if lod == 0:
        us |= set(np.round([c["bpil"], c["cpil"]], 4))
        us |= set(np.round(np.linspace(c["ws"][0] - .01, c["ws"][1] + .01, 5), 4))
        if c["rw"]:
            us |= set(np.round(np.linspace(c["rw"][0] - .01, c["rw"][1] + .01, 4), 4))
    out = []
    for u in sorted(us):
        if 0 <= u <= 1 and (not out or u - out[-1] > (.01 if lod == 0 else .03)):
            out.append(float(u))
    return out


def body(car, lod):
    c = car.c
    rings = []
    for u in stations(car, lod):
        x = car.x(u)
        rings.append([Vector((x, y, z)) for y, z in car.ring(u, lod)])
    n = len(rings[0])

    def cap(base, depth, sign, steps=4):
        cy, cz = 0.0, sum(p.z for p in base) / len(base)
        out = []
        for k in range(1, steps + 1):
            phi = k / steps * math.pi / 2
            sc = math.cos(phi)
            out.append([Vector((p.x + sign * depth * math.sin(phi), cy + (p.y - cy) * sc, cz + (p.z - cz) * sc)) for p in base])
        return out

    steps = 4 if lod == 0 else 2
    ends = len(rings)
    rings = cap(rings[0], c["nose"], 1, steps)[::-1] + rings + cap(rings[-1], c["tail"], -1, steps)
    P, I, C = [], [], []
    for r in rings:
        P += r
    dark = [.06, .06, .06]
    for k in range(len(rings) - 1):
        for j in range(n):
            a, b = k * n + j, k * n + (j + 1) % n
            d, e = a + n, b + n
            for t in ((a, d, e), (a, e, b)):
                if (P[t[1]] - P[t[0]]).cross(P[t[2]] - P[t[0]]).length > 1e-10:
                    I += t
    under = (0, 1, 2) if lod == 0 else (0, 1)
    for k, r in enumerate(rings):
        main = steps <= k < steps + ends
        for j, p in enumerate(r):
            C.append(dark if main and min(j, n - j) in under else [1, 1, 1])
    return P, I, C


def normals(P, I):
    N = [Vector() for _ in P]
    for k in range(0, len(I), 3):
        a, b, c = I[k], I[k + 1], I[k + 2]
        f = (P[b] - P[a]).cross(P[c] - P[a])
        for v in (a, b, c):
            N[v] += f
    return [n.normalized() if n.length > 1e-12 else Vector((0, 0, 1)) for n in N]


class Surface:
    def __init__(s, P, I):
        s.bvh = BVHTree.FromPolygons([tuple(p) for p in P], [tuple(I[k:k + 3]) for k in range(0, len(I), 3)])

    def hit(s, origin, d):
        h = s.bvh.ray_cast(Vector(origin), Vector(d), 10.0)
        return (h[0], h[1]) if h[0] is not None else (None, None)


def overlay(surf, corners, axis, sign, nu, nv, lift, color, grid=None):
    # A patch laid on the body: a quad given in the plane across `axis` (seen from +axis when sign is
    # +1), its grid points cast onto the body along -axis*sign, lifted off it along the normal.
    P, N, C, I = [], [], [], []
    (a0, b0), (a1, b1), (a2, b2), (a3, b3) = corners
    for j in range(nv + 1):
        t = j / nv
        for i in range(nu + 1):
            s = i / nu
            l0 = (a0 + (a1 - a0) * s, b0 + (b1 - b0) * s)
            l1 = (a3 + (a2 - a3) * s, b3 + (b2 - b3) * s)
            q = (l0[0] + (l1[0] - l0[0]) * t, l0[1] + (l1[1] - l0[1]) * t)
            o = [0.0, 0.0, 0.0]
            ax = [k for k in range(3) if k != axis]
            o[ax[0]], o[ax[1]] = q
            o[axis] = 4.0 * sign
            d = [0.0, 0.0, 0.0]
            d[axis] = -sign
            p, n = surf.hit(o, d)
            if p is None:
                return None
            if n.length < .5:
                n = -Vector(d)
            n = n.normalized()
            if n.dot(Vector(d)) > 0:
                n = -n
            P.append(p + n * lift)
            N.append(n)
            C.append(color)
    for j in range(nv):
        for i in range(nu):
            a = j * (nu + 1) + i
            b, c, d = a + 1, a + nu + 2, a + nu + 1
            I += [a, b, c, a, c, d]
    tri = [P[I[0]], P[I[1]], P[I[2]]]
    f = (tri[1] - tri[0]).cross(tri[2] - tri[0])
    if f.dot(N[I[0]]) < 0:
        I = [I[k + w] for k in range(0, len(I), 3) for w in (0, 2, 1)]
    return P, N, C, I


def face(out, pts, n, col):
    # One flat polygon (a fan) facing `n`.
    P, N, C, I = out
    k = len(P)
    P += pts
    N += [n] * len(pts)
    C += [col] * len(pts)
    f = (pts[1] - pts[0]).cross(pts[2] - pts[0])
    flip = f.dot(n) < 0
    for i in range(1, len(pts) - 1):
        I += [k, k + i + 1, k + i] if flip else [k, k + i, k + i + 1]


def merge(parts):
    P, N, C, I = [], [], [], []
    for part in parts:
        if not part:
            continue
        p, n, c, i = part
        I += [k + len(P) for k in i]
        P += p
        N += n
        C += c
    return P, N, C, I


def wheel(c, x, y, lod):
    side = 1 if y > 0 else -1
    R, tw = c["R"], c["tw"]
    rim = R * .64
    seg = 14 if lod == 0 else 8
    prof = [(rim * 1.03, -tw / 2), (R - .028, -tw / 2), (R - .004, -tw / 2 + .028), (R, 0), (R - .004, tw / 2 - .028), (R - .028, tw / 2), (rim * 1.03, tw / 2)]
    if lod:
        prof = [(rim, -tw / 2), (R - .01, -tw / 2 + .01), (R - .01, tw / 2 - .01), (rim, tw / 2)]
    P, N, C, I = [], [], [], []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        for r, w in prof:
            P.append(Vector((x + r * math.cos(a), y + side * w, R + r * math.sin(a))))
            N.append(Vector((math.cos(a), 0, math.sin(a))))
            C.append([.045, .045, .05])
    m = len(prof)
    for i in range(seg):
        for k in range(m - 1):
            a, b = i * m + k, ((i + 1) % seg) * m + k
            I += [a, b + 1, b, a, a + 1, b + 1] if side > 0 else [a, b, b + 1, a, b + 1, a + 1]
    out = (P, N, C, I)
    yf = y + side * (tw / 2 - .006)
    n = Vector((0, side, 0))
    at = lambda r, a, dy=0.0: Vector((x + r * math.cos(a), yf - side * dy, R + r * math.sin(a)))
    if lod:
        face(out, [at(rim * 1.03, 2 * math.pi * i / 8) for i in range(8)], n, [.4, .41, .43])
        return out
    k = 16
    for i in range(k):
        a0, a1 = 2 * math.pi * i / k, 2 * math.pi * (i + 1) / k
        face(out, [at(rim * 1.03, a0), at(rim * 1.03, a1), at(rim * .86, a1, .01), at(rim * .86, a0, .01)], n, [.42, .43, .45])
        face(out, [at(rim * .87, a0, .045), at(rim * .87, a1, .045), at(0, 0, .045)], n, [.035, .035, .04])
    for i in range(5):
        a = 2 * math.pi * i / 5 + .3
        d = Vector((math.cos(a), 0, math.sin(a)))
        t = Vector((-math.sin(a), 0, math.cos(a)))
        c0 = Vector((x, yf - side * .006, R))
        face(out, [c0 + d * rim * .18 + t * .03, c0 + d * rim * .88 + t * .02, c0 + d * rim * .88 - t * .02, c0 + d * rim * .18 - t * .03], n, [.46, .47, .49])
    face(out, [at(rim * .2, 2 * math.pi * i / 8, -.002) for i in range(8)], n, [.4, .41, .43])
    return out


def build(name, spec, lod):
    car = Car(spec)
    c = spec
    fine = lod == 0
    bP, bI, bC = body(car, lod)
    bN = normals(bP, bI)
    surf = Surface(bP, bI)
    X = car.x
    glass, trim, lamp = [], [], []
    ws0, ws1, bp, cp = c["ws"][0], c["ws"][1], c["bpil"], c["cpil"]
    rw0 = c["rw"][0] if c["rw"] else None
    low = lambda u: car.half(u)[9][1] + .02
    high = lambda u: car.half(u)[11][1] - .025
    black = [.03, .03, .035]
    for sign in (1, -1):
        a0, a1 = ws0 + .05, ws1 + .012
        front = [(X(a0), low(a0)), (X(bp - .012), low(bp - .012)), (X(bp - .012), high(bp - .012)), (X(a1), high(a1))]
        c1 = rw0 - .012 if rw0 else cp
        rear = [(X(bp + .014), low(bp + .014)), (X(cp), low(cp)), (X(c1), high(c1)), (X(bp + .014), high(bp + .014))]
        for quad in (front, rear):
            glass.append(overlay(surf, quad, 1, sign, 6 if fine else 2, 3 if fine else 1, .012 if fine else .02, [1, 1, 1]))
        if not fine:
            continue
        pillar = [(X(bp - .017), low(bp) - .015), (X(bp + .019), low(bp) - .015), (X(bp + .019), high(bp) + .012), (X(bp - .017), high(bp) + .012)]
        trim.append(overlay(surf, pillar, 1, sign, 1, 3, .003, black))
        for us in (ws0 + .035, bp, cp + .004):
            if us > c["axles"][1] - .02:
                continue
            seam = [(X(us) - .0025, car.bottom(us) + .12), (X(us) + .0025, car.bottom(us) + .12), (X(us) + .0025, low(us) - .03), (X(us) - .0025, low(us) - .03)]
            trim.append(overlay(surf, seam, 1, sign, 1, 4, .002, [.1, .1, .1]))
        zs = car.bottom(.5) + .11
        sill = [(X(c["axles"][0] + .09), zs), (X(c["axles"][1] - .09), zs), (X(c["axles"][1] - .09), zs + .005), (X(c["axles"][0] + .09), zs + .005)]
        trim.append(overlay(surf, sill, 1, sign, 6, 1, .002, [.1, .1, .1]))
    ye = lambda u: car.half(u)[13][0] - .015
    wsq = [(X(ws0 + .008), -ye(ws0 + .008)), (X(ws0 + .008), ye(ws0 + .008)), (X(ws1 - .006), ye(ws1 - .006)), (X(ws1 - .006), -ye(ws1 - .006))]
    glass.append(overlay(surf, wsq, 2, 1, 6 if fine else 2, 6 if fine else 2, .016 if fine else .03, [1, 1, 1]))
    if c["rw"]:
        rw1 = c["rw"][1]
        rwq = [(X(rw1 - .006), -ye(rw1 - .006)), (X(rw1 - .006), ye(rw1 - .006)), (X(rw0 + .006), ye(rw0 + .006)), (X(rw0 + .006), -ye(rw0 + .006))]
        glass.append(overlay(surf, rwq, 2, 1, 6 if fine else 2, 4 if fine else 2, .016 if fine else .03, [1, 1, 1]))
    E = c["ends"]
    if "rear_glass" in E:
        glass.append(overlay(surf, E["rear_glass"], 0, -1, 4 if fine else 1, 3 if fine else 1, .012, [1, 1, 1]))
        if fine:
            trim.append(overlay(surf, E["door"], 0, -1, 1, 6, .006, [.1, .1, .1]))
    nose, tail = car.L / 2, -car.L / 2
    housing = [.13, .13, .14]

    def grow(q, d):
        cy, cz = sum(p[0] for p in q) / 4, sum(p[1] for p in q) / 4
        return [(y + (d if y > cy else -d), z + (d if z > cz else -d)) for y, z in q]

    def side_of(q):
        (y0, z0), (y1, z1), (y2, z2), (y3, z3) = q
        return [(.02, z1 + .008), (.16, z1 + .02), (.14, z2 - .004), (.02, z2)]

    g = (4, 2) if fine else (1, 1)
    for sign in (1, -1):
        m = lambda q: [(y * sign, z) for y, z in q]
        if fine:
            trim.append(overlay(surf, grow(m(E["head"]), .012), 0, 1, 4, 2, .004, housing))
            trim.append(overlay(surf, grow(m(E["tail"]), .01), 0, -1, 4, 2, .004, [.08, .02, .02]))
            lamp.append(overlay(surf, [(nose - dx, z) for dx, z in side_of(E["head"])], 1, sign, 3, 2, .008, [1, .97, .9]))
            lamp.append(overlay(surf, [(tail + dx, z) for dx, z in side_of(E["tail"])], 1, sign, 3, 2, .008, [1, .06, .03]))
        lamp.append(overlay(surf, m(E["head"]), 0, 1, *g, .008 if fine else .015, [1, .97, .9]))
        lamp.append(overlay(surf, m(E["tail"]), 0, -1, *g, .008 if fine else .015, [1, .06, .03]))
    trim.append(overlay(surf, E["grille"], 0, 1, *((6, 3) if fine else (1, 1)), .008 if fine else .02, black))
    trim.append(overlay(surf, E["intake"], 0, 1, *((6, 3) if fine else (1, 1)), .008 if fine else .02, black))
    trim.append(overlay(surf, E["diffuser"], 0, -1, *((6, 3) if fine else (1, 1)), .008 if fine else .02, black))
    if fine:
        trim.append(overlay(surf, E["plate_f"], 0, 1, 2, 1, .011, [.92, .92, .88]))
        trim.append(overlay(surf, E["plate_r"], 0, -1, 2, 1, .011, [.92, .92, .88]))
    if c.get("clad"):
        for sign in (1, -1):
            a0, a1 = c["axles"][0] + c["arch"] / car.L + .01, c["axles"][1] - c["arch"] / car.L - .01
            zb = car.bottom(.5) + c["rb"]
            band = [(X(a0), zb + .01), (X(a1), zb + .01), (X(a1), zb + .15), (X(a0), zb + .15)]
            trim.append(overlay(surf, band, 1, sign, 4 if fine else 1, 1, .006 if fine else .015, [.07, .07, .075]))
    for sign in (1, -1) if fine else ():
        um = ws0 + .05
        h = car.half(um)
        root = Vector((X(um) + .02, sign * (h[9][0] - .01), h[9][1] + .02))
        out = Vector((0, sign * .2, .015))
        shell = []
        for dx, dz in ((0, 0), (-.2, 0), (-.21, .12), (-.03, .13)):
            shell.append((root + Vector((dx, 0, dz)), root + out + Vector((dx * .9 - .01, 0, dz * .9 + .005))))
        body_part = (bP, bN, bC, bI)
        inner = [q[0] for q in shell]
        outer = [q[1] for q in shell]
        face(body_part, outer, Vector((0, sign, 0)), [1, 1, 1])
        for i in range(4):
            j = (i + 1) % 4
            nn = ((outer[i] + outer[j]) / 2 - (sum(outer, Vector()) / 4))
            nn.y = 0
            face(body_part, [inner[i], inner[j], outer[j], outer[i]], nn.normalized() if nn.length > 1e-6 else Vector((1, 0, 0)), [1, 1, 1] if i != 1 else [.05, .05, .05])
    wheels = []
    for ua in c["axles"]:
        for sign in (1, -1):
            wheels.append(wheel(c, X(ua), sign * c["track"], lod))
    missing = [(kind, i) for kind, lst in (("glass", glass), ("trim", trim), ("lamp", lamp)) for i, g in enumerate(lst) if g is None]
    if fine:
        parts = {"body": (bP, bN, bC, bI), "glass": merge(glass), "trim": merge(trim + wheels), "lamp": merge(lamp)}
    else:
        tinted = [(p, n, [[.03, .035, .04]] * len(p), i) for p, n, _, i in (g for g in glass if g)]
        parts = {"body": merge([(bP, bN, bC, bI)] + tinted + trim + wheels), "lamp": merge(lamp)}
    print(name, lod, {k: len(v[3]) // 3 for k, v in parts.items()}, "missing", missing)
    return parts


def stage_cars():
    meshes, blob = [], bytearray()

    def put(arr, kind):
        a = np.ascontiguousarray(arr, dtype=kind)
        off = len(blob)
        blob.extend(a.tobytes())
        while len(blob) % 4:
            blob.append(0)
        return off

    for name, spec in CARS.items():
        for lod in (0, 1):
            parts = build(name, spec, lod)
            out = []
            for part, (P, N, C, I) in parts.items():
                out.append({"material": part, "count": len(P), "tris": len(I) // 3,
                            "position": put([tuple(p) for p in P], np.float32), "normal": put([tuple(n) for n in N], np.float32),
                            "color": put(np.round(np.array(C) * 255), np.uint8), "index": put(I, np.uint32)})
            meshes.append({"name": f"{name}_lod{lod}", "kind": name, "lod": lod, "parts": out})
    open(os.path.join(RAW, "cars.bin"), "wb").write(blob)
    json.dump({"meshes": meshes}, open(os.path.join(RAW, "cars.json"), "w"), indent=1)


def stage_preview():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 48
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = 1600, 900
    sc.view_settings.view_transform = "AgX"
    meta = json.load(open(os.path.join(RAW, "cars.json")))
    blob = open(os.path.join(RAW, "cars.bin"), "rb").read()

    def mat(name, base, rough, metal, coat=0.0, emit=None):
        m = bpy.data.materials.new(name)
        if m.node_tree is None:
            m.use_nodes = True
        nt = m.node_tree
        bs = nt.nodes["Principled BSDF"]
        vc = nt.nodes.new("ShaderNodeVertexColor")
        vc.layer_name = "Col"
        mix = nt.nodes.new("ShaderNodeMix")
        mix.data_type, mix.blend_type = "RGBA", "MULTIPLY"
        mix.inputs[0].default_value = 1
        mix.inputs[6].default_value = base
        nt.links.new(vc.outputs["Color"], mix.inputs[7])
        nt.links.new(mix.outputs[2], bs.inputs["Base Color"])
        bs.inputs["Roughness"].default_value = rough
        bs.inputs["Metallic"].default_value = metal
        bs.inputs["Coat Weight"].default_value = coat
        if emit:
            nt.links.new(mix.outputs[2], bs.inputs["Emission Color"])
            bs.inputs["Emission Strength"].default_value = emit
        return m

    mats = {"body": mat("paint", (.55, .05, .04, 1), .32, .55, 1.0), "glass": mat("glass", (.02, .025, .03, 1), .04, .2),
            "trim": mat("trim", (1, 1, 1, 1), .55, .4), "lamp": mat("lamp", (1, 1, 1, 1), .2, 0, 0, 2.0)}
    only = os.environ.get("CAR", "sedan_lod0")
    for k, m in enumerate(meta["meshes"]):
        if m["name"] != only:
            continue
        for part in m["parts"]:
            n, t = part["count"], part["tris"]
            P = np.frombuffer(blob, np.float32, n * 3, part["position"]).reshape(n, 3)
            N = np.frombuffer(blob, np.float32, n * 3, part["normal"]).reshape(n, 3)
            C = np.frombuffer(blob, np.uint8, n * 3, part["color"]).reshape(n, 3) / 255
            I = np.frombuffer(blob, np.uint32, t * 3, part["index"])
            me = bpy.data.meshes.new(f"{m['name']}{part['material']}")
            me.from_pydata(P.tolist(), [], I.reshape(-1, 3).tolist())
            col = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
            col.data.foreach_set("color", np.concatenate([C, np.ones((n, 1))], 1).astype(np.float32).ravel())
            me.normals_split_custom_set_from_vertices(N.tolist())
            me.materials.append(mats[part["material"]])
            sc.collection.objects.link(bpy.data.objects.new(me.name, me))
        break
    ground = bpy.data.meshes.new("ground")
    ground.from_pydata([(-20, -20, 0), (30, -20, 0), (30, 30, 0), (-20, 30, 0)], [], [(0, 1, 2, 3)])
    gm = bpy.data.materials.new("ground")
    if gm.node_tree is None:
        gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (.05, .05, .05, 1)
    gm.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = .25
    ground.materials.append(gm)
    sc.collection.objects.link(bpy.data.objects.new("ground", ground))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 50
    sc.collection.objects.link(cam)
    sc.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.0
    sun.rotation_euler = (math.radians(40), 0, math.radians(-40))
    sc.collection.objects.link(sun)
    world = bpy.data.worlds.new("w")
    sc.world = world
    if world.node_tree is None:
        world.use_nodes = True
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    world.node_tree.links.new(sky.outputs[0], world.node_tree.nodes["Background"].inputs["Color"])
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = .4
    sc.render.resolution_x, sc.render.resolution_y = 960, 600
    for i, (az, el) in enumerate(((-38, 12), (-90, 5), (140, 14), (0, 8))):
        a, e = math.radians(az), math.radians(el)
        d = 7.2
        cam.location = (d * math.cos(e) * math.cos(a), d * math.cos(e) * math.sin(a), .75 + d * math.sin(e))
        look = Vector((0, 0, .75)) - Vector(cam.location)
        cam.rotation_euler = look.to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(OUT, f"view{i}.png")
        bpy.ops.render.render(write_still=True)



if "cars" in STAGES:
    stage_cars()
if "preview" in STAGES:
    stage_preview()
