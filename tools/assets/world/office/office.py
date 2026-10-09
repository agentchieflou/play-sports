# The office's furniture, made from nothing but this script: Blender (headless) builds a workstation (a bench
# desk, a monitor whose display is its own part, keyboard, mouse, an office chair) and a planter, and writes
# raw meshes that office.mjs packs into RawAssets/world/office/office.glb.
# Usage: blender --background --factory-startup --python tools/assets/world/office/office.py -- <out-dir> [stage...]
#        stages: office (the default), preview
import bpy, bmesh, math, os, sys, json
import numpy as np
from mathutils import Vector, Matrix

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = os.path.abspath(ARGS[0] if ARGS else "office-build")
STAGES = set(ARGS[1:]) or {"office"}
RAW = os.path.join(OUT, "raw")
os.makedirs(RAW, exist_ok=True)

# Metres, Z up. A workstation stands round its desk's middle: the person sits on its -y side facing +y, the
# monitor at the back; colours are sRGB.
DESK = dict(w=1.5, d=.76, h=.74, top=.026, leg=.04)
OAK = (212, 178, 136)
WHITE = (226, 228, 230)
GRAPHITE = (52, 55, 60)
BLACK = (18, 19, 21)
ALU = (170, 174, 178)
FABRIC = (44, 47, 54)
MESH = (30, 32, 36)
CERAMIC = (232, 230, 224)
SOIL = (52, 40, 30)


def lin(c):
    return [(v / 255) for v in c]


class Part:
    def __init__(s):
        s.P, s.N, s.C, s.UV, s.I = [], [], [], [], []
        s.seen = {}

    def add(s, bm, color, uv=None, hard=35.0, matrix=None):
        if matrix is not None:
            bm.transform(matrix)
        bmesh.ops.triangulate(bm, faces=list(bm.faces))
        bm.normal_update()
        cos_hard = math.cos(math.radians(hard))
        for f in bm.faces:
            fn = f.normal
            if fn.length < 1e-9:
                continue
            for loop in f.loops:
                v = loop.vert
                n = Vector()
                for g in v.link_faces:
                    if g.normal.dot(fn) >= cos_hard:
                        n += g.normal * g.calc_area()
                n = n.normalized() if n.length > 1e-12 else fn
                c = lin(color) if not callable(color) else color(v.co)
                t = uv(v.co, fn) if uv else (0.0, 0.0)
                key = tuple(round(x, 5) for x in (*v.co, *n, *c, *t))
                if key not in s.seen:
                    s.seen[key] = len(s.P)
                    s.P.append(v.co.copy())
                    s.N.append(n)
                    s.C.append(c)
                    s.UV.append(t)
                s.I.append(s.seen[key])
        bm.free()


def canonical(p):
    # Blender's bevel orders what it makes differently from run to run: sort the vertices and triangles
    # so the same shapes always write the same bytes.
    key = lambda i: tuple(round(x, 5) for x in (*p.P[i], *p.N[i], *p.C[i], *p.UV[i]))
    order = sorted(range(len(p.P)), key=key)
    at = {old: new for new, old in enumerate(order)}
    tris = []
    for k in range(0, len(p.I), 3):
        t = tuple(at[i] for i in p.I[k:k + 3])
        tris.append(min(t, t[1:] + t[:1], t[2:] + t[:2]))
    p.P, p.N, p.C, p.UV = [p.P[i] for i in order], [p.N[i] for i in order], [p.C[i] for i in order], [p.UV[i] for i in order]
    p.I = [i for t in sorted(tris) for i in t]
    return p


def cube(size, at=(0, 0, 0), bevel=0.0, segs=2, rot=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
    if bevel:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, segments=segs, affect="EDGES", profile=.5)
    m = Matrix.Translation(Vector(at))
    if rot is not None:
        m = m @ rot
    bm.transform(m)
    return bm


def cylinder(r1, r2, h, at=(0, 0, 0), segs=16, rot=None, cap=True):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segs, radius1=r1, radius2=r2, depth=h)
    m = Matrix.Translation(Vector(at))
    if rot is not None:
        m = m @ rot
    bm.transform(m)
    return bm


def tube(points, r, segs=8):
    bm = bmesh.new()
    rings = []
    for i, p in enumerate(points):
        p = Vector(p)
        t = (Vector(points[min(i + 1, len(points) - 1)]) - Vector(points[max(i - 1, 0)])).normalized()
        a = t.orthogonal().normalized()
        b = t.cross(a)
        rings.append([bm.verts.new(p + (a * math.cos(2 * math.pi * k / segs) + b * math.sin(2 * math.pi * k / segs)) * r) for k in range(segs)])
    for i in range(len(rings) - 1):
        for k in range(segs):
            bm.faces.new((rings[i][k], rings[i][(k + 1) % segs], rings[i + 1][(k + 1) % segs], rings[i + 1][k]))
    for ring, flip in ((rings[0], True), (rings[-1], False)):
        bm.faces.new(ring[::-1] if flip else ring)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return bm


def workstation():
    D = DESK
    wood, fit, screen = Part(), Part(), Part()
    w2, d2, top = D["w"] / 2, D["d"] / 2, D["h"]
    oak = lambda co: [c * (.92 if co.z < top - .004 else 1.0) for c in lin(OAK)]
    wood.add(cube((D["w"], D["d"], D["top"]), (0, 0, top - D["top"] / 2), bevel=.005, segs=2), oak, uv=lambda co, n: (co.x * .62, co.y * .13 + .4))
    for sx in (-1, 1):
        x = sx * (w2 - .09)
        leg = D["leg"]
        for y in (-d2 + .07, d2 - .07):
            fit.add(cube((leg, leg, top - D["top"]), (x, y, (top - D["top"]) / 2), bevel=.006), WHITE)
        fit.add(cube((leg, D["d"] - .12, leg), (x, 0, top - D["top"] - leg / 2), bevel=.006), WHITE)
        fit.add(cube((.06, D["d"] - .08, .02), (x, 0, .01), bevel=.006), WHITE)
    fit.add(cube((D["w"] - .2, .03, .12), (0, d2 - .08, top - D["top"] - .08), bevel=.004), WHITE)
    sy, sz, sw, sh = .2, top + .32, .62, .36
    fit.add(cube((sw + .018, .022, sh + .018), (0, sy, sz), bevel=.006, segs=2), BLACK)
    fit.add(cube((sw * .7, .03, sh * .62), (0, sy + .024, sz + .01), bevel=.012, segs=2), GRAPHITE)
    fit.add(cube((.05, .014, .3), (0, sy + .05, top + .16), rot=Matrix.Rotation(math.radians(-8), 4, "X"), bevel=.004), ALU)
    fit.add(cube((.24, .19, .01), (0, sy + .02, top + .006), bevel=.004), ALU)
    quad = bmesh.new()
    vs = [quad.verts.new((x, sy - .0115, z)) for x, z in ((-sw / 2, sz - sh / 2), (sw / 2, sz - sh / 2), (sw / 2, sz + sh / 2), (-sw / 2, sz + sh / 2))]
    quad.faces.new(vs)
    quad.normal_update()
    quad.faces.ensure_lookup_table()
    if quad.faces[0].normal.y > 0:
        bmesh.ops.reverse_faces(quad, faces=list(quad.faces))
    screen.add(quad, (255, 255, 255), uv=lambda co, n: ((co.x + sw / 2) / sw, (co.z - (sz - sh / 2)) / sh))
    fit.add(cube((.44, .135, .016), (0, -.13, top + .008), bevel=.004), GRAPHITE)
    fit.add(cube((.42, .115, .006), (0, -.13, top + .018), bevel=.002), BLACK)
    fit.add(cube((.065, .11, .03), (.32, -.12, top + .015), bevel=.014, segs=3), GRAPHITE)
    fit.add(cylinder(.042, .038, .1, (-.52, .05, top + .05), segs=14), (236, 236, 232))
    chair(fit)
    return {"wood": wood, "fittings": fit, "screen": screen}


def chair(fit):
    cy, seat = -.64, .47
    for k in range(5):
        a = 2 * math.pi * k / 5 + math.pi / 10
        tip = Vector((math.cos(a) * .32, cy + math.sin(a) * .32, .07))
        fit.add(tube([(0, cy, .1), (tip.x * .5, cy + (tip.y - cy) * .5, .085), tuple(tip)], .02, 6), BLACK)
        caster = bmesh.new()
        bmesh.ops.create_uvsphere(caster, u_segments=8, v_segments=5, radius=.03)
        caster.transform(Matrix.Translation(tip - Vector((0, 0, .04))))
        fit.add(caster, BLACK)
    fit.add(cylinder(.035, .035, .1, (0, cy, .12), segs=12), BLACK)
    fit.add(cylinder(.024, .024, .26, (0, cy, .3), segs=12), ALU)
    fit.add(cube((.5, .48, .085), (0, cy + .02, seat), bevel=.035, segs=3), FABRIC)
    lean = Matrix.Rotation(math.radians(-12), 4, "X")
    fit.add(cube((.46, .045, .5), (0, cy - .27, seat + .34), rot=lean, bevel=.03, segs=3), MESH)
    fit.add(cube((.05, .05, .32), (0, cy - .26, seat + .1), rot=lean, bevel=.01), BLACK)
    for sx in (-1, 1):
        fit.add(cube((.04, .04, .2), (sx * .24, cy + .02, seat + .1), bevel=.008), BLACK)
        fit.add(cube((.07, .24, .03), (sx * .24, cy + .04, seat + .21), bevel=.012, segs=2), BLACK)


def planter():
    fit = Part()
    fit.add(cylinder(.42, .34, .62, (0, 0, .31), segs=24), CERAMIC)
    fit.add(cylinder(.39, .39, .02, (0, 0, .6), segs=24), SOIL)
    return {"fittings": fit}


def stage_office():
    meshes, blob = [], bytearray()

    def put(arr, kind):
        a = np.ascontiguousarray(arr, dtype=kind)
        off = len(blob)
        blob.extend(a.tobytes())
        while len(blob) % 4:
            blob.append(0)
        return off

    for name, parts in (("workstation", workstation()), ("planter", planter())):
        out = []
        for mat, p in parts.items():
            p = canonical(p)
            out.append({"material": mat, "count": len(p.P), "tris": len(p.I) // 3,
                        "position": put([tuple(v) for v in p.P], np.float32), "normal": put([tuple(n) for n in p.N], np.float32),
                        "uv": put(p.UV, np.float32), "color": put(np.round(np.clip(np.array(p.C), 0, 1) * 255), np.uint8),
                        "index": put(p.I, np.uint32)})
        meshes.append({"name": name, "parts": out})
        print(name, {o["material"]: o["tris"] for o in out})
    open(os.path.join(RAW, "office.bin"), "wb").write(blob)
    json.dump({"meshes": meshes}, open(os.path.join(RAW, "office.json"), "w"), indent=1)


def stage_preview():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 48
    sc.cycles.use_denoising = True
    sc.render.resolution_x, sc.render.resolution_y = 1280, 800
    sc.view_settings.view_transform = "AgX"
    meta = json.load(open(os.path.join(RAW, "office.json")))
    blob = open(os.path.join(RAW, "office.bin"), "rb").read()

    def mat(name, rough, metal, emit=0.0, checker=False):
        m = bpy.data.materials.new(name)
        if m.node_tree is None:
            m.use_nodes = True
        nt = m.node_tree
        bs = nt.nodes["Principled BSDF"]
        vc = nt.nodes.new("ShaderNodeVertexColor")
        vc.layer_name = "Col"
        src = vc.outputs["Color"]
        if checker:
            ck = nt.nodes.new("ShaderNodeTexChecker")
            ck.inputs["Scale"].default_value = 8
            uvn = nt.nodes.new("ShaderNodeUVMap")
            nt.links.new(uvn.outputs["UV"], ck.inputs["Vector"])
            src = ck.outputs["Color"]
        nt.links.new(src, bs.inputs["Base Color"])
        bs.inputs["Roughness"].default_value = rough
        bs.inputs["Metallic"].default_value = metal
        if emit:
            nt.links.new(src, bs.inputs["Emission Color"])
            bs.inputs["Emission Strength"].default_value = emit
        return m

    mats = {"wood": mat("wood", .45, 0), "fittings": mat("fit", .45, .15), "screen": mat("screen", .2, 0, 1.5, True)}
    for k, m in enumerate(meta["meshes"]):
        for part in m["parts"]:
            n, t = part["count"], part["tris"]
            P = np.frombuffer(blob, np.float32, n * 3, part["position"]).reshape(n, 3)
            N = np.frombuffer(blob, np.float32, n * 3, part["normal"]).reshape(n, 3)
            UV = np.frombuffer(blob, np.float32, n * 2, part["uv"]).reshape(n, 2)
            C = np.frombuffer(blob, np.uint8, n * 3, part["color"]).reshape(n, 3) / 255
            I = np.frombuffer(blob, np.uint32, t * 3, part["index"])
            me = bpy.data.meshes.new(m["name"] + part["material"])
            me.from_pydata(P.tolist(), [], I.reshape(-1, 3).tolist())
            me.uv_layers.new(name="UVMap").data.foreach_set("uv", UV[I].ravel())
            col = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
            col.data.foreach_set("color", np.concatenate([C ** 2.2, np.ones((n, 1))], 1).astype(np.float32).ravel())
            me.normals_split_custom_set_from_vertices(N.tolist())
            me.materials.append(mats[part["material"]])
            ob = bpy.data.objects.new(me.name, me)
            ob.location = (k * 1.9, 0, 0)
            sc.collection.objects.link(ob)
    ground = bpy.data.meshes.new("ground")
    ground.from_pydata([(-6, -6, 0), (8, -6, 0), (8, 8, 0), (-6, 8, 0)], [], [(0, 1, 2, 3)])
    gm = bpy.data.materials.new("ground")
    if gm.node_tree is None:
        gm.use_nodes = True
    gm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (.32, .31, .3, 1)
    ground.materials.append(gm)
    sc.collection.objects.link(bpy.data.objects.new("ground", ground))
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.lens = 35
    sc.collection.objects.link(cam)
    sc.camera = cam
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.5
    sun.rotation_euler = (math.radians(50), 0, math.radians(-30))
    sc.collection.objects.link(sun)
    world = bpy.data.worlds.new("w")
    sc.world = world
    if world.node_tree is None:
        world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (.6, .65, .72, 1)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = .6
    for i, (pos, look) in enumerate((((-1.6, -2.4, 1.6), (0.5, 0, .7)), ((0.2, 2.2, 1.5), (0.2, -.4, .7)))):
        cam.location = pos
        cam.rotation_euler = (Vector(look) - Vector(pos)).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(OUT, f"view{i}.png")
        bpy.ops.render.render(write_still=True)


if "office" in STAGES:
    stage_office()
if "preview" in STAGES:
    stage_preview()
