// people.mjs: turn a character export (a MetaHuman from Unreal Engine 5.6+, or the MakeHuman stand-in)
// into the world's web assets: `<id>.glb` for the player's character and `<id>_crowd.glb` for the
// pedestrians. See README.md beside this file for the steps and the config.
//
//   node pipeline/people.mjs config.json
//
// What it does, in order, for every character:
//   1. reads each source (glTF or GLB; several files merge: a MetaHuman's body, face and hair cards),
//      skinning every vertex to the rest pose in world space, so any exporter's conventions (where the
//      mesh node sits, what the inverse bind matrices assume) come out the same;
//   2. turns the whole character to the world's frame: metres, Y up, facing -Z, feet on y = 0, pelvis
//      over the origin (units and facing are found from the skeleton, or given in the config);
//   3. keeps only the Unreal Engine body bones the world animates (pelvis, spine_01..spine_05, neck_01,
//      neck_02, head, clavicle/upperarm/lowerarm/hand, thigh/calf/foot/ball, and the fingers for the hero)
//      and hands every other bone's weights (face, twist, corrective, metacarpal bones) to its nearest
//      kept ancestor, four influences a vertex at most;
//   4. sorts every primitive into a role by its material and mesh names (skin, eyes, brows, lashes, hair,
//      top, bottom, shoes, other, or drop), and splits a one-piece outfit into top and bottom by the
//      bones its pieces move with;
//   5. adds a morph target per variant source (the same character with another body: the stand-in's
//      angular, curved, older, slim and broad builds) and the matching joint offsets;
//   6. decimates each role to its triangle budget (meshoptimizer), quantises (KHR_mesh_quantization)
//      and re-encodes textures as WebP; the tintable roles' colour maps become detail maps (their mean
//      colour normalised to 0.5) so the world can dye them any skin tone, hair or cloth colour;
//   7. for the crowd: one atlas for every character, one primitive per character and level of detail,
//      and a `_ROLE` attribute so one draw tints skin, top, bottom and hair per pedestrian.
import fs from "fs";
import path from "path";
import { Document, NodeIO } from "@gltf-transform/core";
import { KHRMeshQuantization, EXTTextureWebP } from "@gltf-transform/extensions";
import { MeshoptSimplifier } from "meshoptimizer";
import sharp from "sharp";
import crypto from "crypto";
import { readSource } from "./read.mjs";
import * as M from "./m4.mjs";

await MeshoptSimplifier.ready;

export const UE_BODY = ["pelvis", "spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02", "head",
  "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
  "thigh_l", "calf_l", "foot_l", "ball_l", "thigh_r", "calf_r", "foot_r", "ball_r"];
export const FINGERS = ["l", "r"].flatMap(s => ["thumb", "index", "middle", "ring", "pinky"].flatMap(f => [1, 2, 3].map(k => `${f}_0${k}_${s}`)));
const BOTTOM = /^(pelvis|thigh|calf|foot|ball)/;
const ROLE_RULES = [
  [/occlusion|tearline|tear_line|eyeshell|eye_shell|eyeedge|cartilage|teeth|tongue|saliva|mouth/i, "drop"],
  [/lash/i, "lashes"], [/brow/i, "brows"], [/beard|moustache|mustache|stubble|goatee|facial_?hair/i, "beard"],
  [/hair|cards|groom|helmet|fuzz/i, "hair"], [/eye|iris|cornea|sclera/i, "eyes"],
  [/glasses|spectacle/i, "glasses"],
  [/shoe|boot|sneaker|trainer|feet_?wear/i, "shoes"],
  [/top|shirt|jacket|hoodie|coat|blouse|sweater|vest/i, "top"],
  [/bottom|pants|trouser|jean|skirt|short|legging/i, "bottom"],
  [/outfit|suit|cloth|garment|dress|overall/i, "outfit"],
  [/skin|body|face|head|base|torso|arms|legs/i, "skin"]
];
const TINT = { skin: "chroma", top: "grey", bottom: "grey", hair: "grey", brows: "grey", lashes: "grey", beard: "grey" };
const ALPHA = { hair: 0.5, brows: 0.35, lashes: 0.35, beard: 0.4, glasses: 0.5 };
const OPTIONAL = ["hair", "beard", "glasses"];
const ROUGH = { skin: 0.45, hair: 0.55, brows: 0.8, lashes: 0.8, beard: 0.7, eyes: 0.08, top: 0.82, bottom: 0.86, shoes: 0.55, glasses: 0.25, other: 0.7 };
const ROLE_ID = { skin: 0, top: 1, bottom: 2, hair: 3, shoes: 4, eyes: 5, other: 5, brows: 3, lashes: 3, beard: 3, glasses: 5 };

const lin = c => (c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
const srgb = c => (c <= 0.0031308 ? c * 12.92 : 1.055 * Math.pow(c, 1 / 2.4) - 0.055);

function roleOf(p, rules) {
  for (const [re, role] of rules) if (re.test(p.mat.name)) return role;
  for (const [re, role] of rules) if (re.test(p.node) || re.test(p.mesh)) return role;
  return "other";
}

// The rotation that stands a source up: its "up" is the way from the pelvis to the head (else the neck,
// else the upper spine), whichever axis and sign that is. Extents were a guess that failed on a crowd
// character whose bind pose lay along another axis: it walked bent double. Without those joints, a
// source is Z-up when it is more than twice as tall in z as in y; `cfg.up` ("y" or "z") overrides both.
function upright(src, cfg, ext) {
  if (cfg.up) return cfg.up === "z" ? M.rotX(-Math.PI / 2) : M.ident();
  const P = n => src.joints.get(n) && M.translation(src.joints.get(n).world);
  const a = P("pelvis"), b = P("head") || P("neck_01") || P("spine_05");
  if (!a || !b) return ext[2] > 2 * ext[1] ? M.rotX(-Math.PI / 2) : M.ident();
  const d = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
  const c = [0, 1, 2].reduce((m, i) => (Math.abs(d[i]) > Math.abs(d[m]) ? i : m), 0), s = Math.sign(d[c]);
  if (c === 1) return s > 0 ? M.ident() : M.rotX(Math.PI);
  return c === 2 ? M.rotX(-s * Math.PI / 2) : M.rotZ(s * Math.PI / 2);
}

// The transform from a source's world to the web's: metres, Y up, facing -Z, feet on 0, pelvis at x = z = 0.
function frame(src, cfg) {
  const ext = [0, 1, 2].map(c => {
    let lo = Infinity, hi = -Infinity;
    for (const p of src.prims) for (let i = c; i < p.pos.length; i += 3) { lo = Math.min(lo, p.pos[i]); hi = Math.max(hi, p.pos[i]); }
    return hi - lo;
  });
  let F = upright(src, cfg, ext);
  const J = n => src.joints.get(n) && M.point(F, ...M.translation(src.joints.get(n).world));
  let ys = [Infinity, -Infinity];
  for (const p of src.prims) for (let i = 1; i < p.pos.length; i += 3) {
    const y = M.point(F, p.pos[i - 1], p.pos[i], p.pos[i + 1])[1];
    if (y < ys[0]) ys[0] = y;
    if (y > ys[1]) ys[1] = y;
  }
  const k = cfg.scale || (ys[1] - ys[0] > 20 ? 0.01 : 1);
  let f = null;
  if (J("foot_l") && J("ball_l")) {
    const a = J("foot_l"), b = J("ball_l"), c = J("foot_r") || a, d = J("ball_r") || b;
    f = [(b[0] - a[0]) + (d[0] - c[0]), (b[2] - a[2]) + (d[2] - c[2])];
  }
  if (!f) f = { "+z": [0, 1], "-z": [0, -1], "+x": [1, 0], "-x": [-1, 0] }[cfg.facing || "-z"];
  F = M.mul(M.mul(M.rotY(Math.atan2(-f[0], f[1]) + Math.PI), M.scale(k)), F);
  let minY = Infinity;
  for (const p of src.prims) for (let i = 0; i < p.pos.length; i += 3) minY = Math.min(minY, M.point(F, p.pos[i], p.pos[i + 1], p.pos[i + 2])[1]);
  const pel = src.joints.get("pelvis") ? M.point(F, ...M.translation(src.joints.get("pelvis").world)) : [0, 0, 0];
  return M.mul(M.translate(-pel[0], -minY, -pel[2]), F);
}

function applyFrame(src, F) {
  for (const p of src.prims) {
    for (let i = 0; i < p.pos.length; i += 3) {
      const q = M.point(F, p.pos[i], p.pos[i + 1], p.pos[i + 2]);
      p.pos[i] = q[0]; p.pos[i + 1] = q[1]; p.pos[i + 2] = q[2];
      const d = M.dir(F, p.nor[i], p.nor[i + 1], p.nor[i + 2]), l = Math.hypot(...d) || 1;
      p.nor[i] = d[0] / l; p.nor[i + 1] = d[1] / l; p.nor[i + 2] = d[2] / l;
    }
  }
  for (const j of src.joints.values()) j.world = M.mul(F, j.world);
}

// Keep the named bones that exist; every other bone's weight goes to its nearest kept ancestor.
function collapse(src, keep) {
  const names = keep.filter(n => src.joints.has(n));
  const kept = new Set(names);
  const up = n => { let c = n; while (c && !kept.has(c)) c = src.joints.get(c) ? src.joints.get(c).parent : null; return c || names[0]; };
  const index = new Map(names.map((n, i) => [n, i]));
  for (const p of src.prims) {
    const n = p.pos.length / 3, J = new Uint8Array(n * 4), W = new Float32Array(n * 4);
    for (let i = 0; i < n; i++) {
      const acc = new Map();
      for (const [j, w] of p.inf[i]) { const k = index.get(up(j)); acc.set(k, (acc.get(k) || 0) + w); }
      const top = [...acc.entries()].sort((a, b) => b[1] - a[1]).slice(0, 4), s = top.reduce((a, b) => a + b[1], 0) || 1;
      top.forEach(([j, w], k) => { J[i * 4 + k] = j; W[i * 4 + k] = w / s; });
    }
    p.J = J; p.W = W;
  }
  const joints = names.map(n => {
    const w = src.joints.get(n).world;
    let par = src.joints.get(n).parent;
    while (par && !kept.has(par)) par = src.joints.get(par) ? src.joints.get(par).parent : null;
    return { name: n, parent: par ? index.get(par) : -1, t: M.translation(w), q: M.rotation(w) };
  });
  return joints;
}

async function load(files, cfg, F) {
  const parts = [];
  for (const f of files) parts.push(await readSource(path.resolve(cfg.base, f), { rename: cfg.rename, attach: cfg.attach, pose: cfg.pose }));
  const src = { joints: new Map(), prims: [] };
  for (const s of parts) {
    for (const [n, j] of s.joints) if (!src.joints.has(n)) src.joints.set(n, j);
    src.prims.push(...s.prims);
  }
  const frameF = F || frame(src, cfg);
  applyFrame(src, frameF);
  for (const p of src.prims) tileUV(p);
  return { src, F: frameF };
}

// A primitive whose UVs all lie in one UDIM tile other than the first (a MetaHuman body's are in 1002, u 1..2)
// moved into 0..1. Its maps are one image, so this is the same picture; but the page clamps people's textures
// to the edge, which would draw the whole body from one column of each map. UVs spanning several tiles (a
// repeating pattern) are left as they are.
function tileUV(p) {
  if (!p.uv || !p.uv.length) return;
  for (const c of [0, 1]) {
    let lo = Infinity, hi = -Infinity;
    for (let i = c; i < p.uv.length; i += 2) { lo = Math.min(lo, p.uv[i]); hi = Math.max(hi, p.uv[i]); }
    const k = Math.floor(lo);
    if (k !== 0 && Math.floor(hi - 1e-6) === k) for (let i = c; i < p.uv.length; i += 2) p.uv[i] -= k;
  }
}

// Split a primitive into connected pieces, and give each piece to the role most of its weight is in.
function split(p) {
  const n = p.pos.length / 3, parent = Int32Array.from({ length: n }, (_, i) => i);
  const find = a => { while (parent[a] !== a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
  for (let t = 0; t < p.idx.length; t += 3) {
    const a = find(p.idx[t]), b = find(p.idx[t + 1]), c = find(p.idx[t + 2]);
    parent[b] = a; parent[find(c)] = a;
  }
  const score = new Map();
  for (let i = 0; i < n; i++) {
    let bot = 0;
    for (const [j, w] of p.inf[i]) if (BOTTOM.test(j)) bot += w;
    const r = find(i), s = score.get(r) || [0, 0];
    s[0] += bot; s[1] += 1 - bot;
    score.set(r, s);
  }
  const out = { top: [], bottom: [] };
  for (let t = 0; t < p.idx.length; t += 3) {
    const s = score.get(find(p.idx[t]));
    out[s[0] > s[1] ? "bottom" : "top"].push(p.idx[t], p.idx[t + 1], p.idx[t + 2]);
  }
  return Object.entries(out).filter(([, ix]) => ix.length).map(([role, ix]) => compact(Object.assign({}, p, { role }), Uint32Array.from(ix)));
}

// Glasses' lenses: flat, filled pieces (a disc fills ~78% of its box, a rim far less). The web draws
// no glass, and an opaque lens reads as sunglasses, so the lenses go and the frames stay.
function lensless(p) {
  const n = p.pos.length / 3, parent = Int32Array.from({ length: n }, (_, i) => i);
  const find = a => { while (parent[a] !== a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
  for (let t = 0; t < p.idx.length; t += 3) { const a = find(p.idx[t]); parent[find(p.idx[t + 1])] = a; parent[find(p.idx[t + 2])] = a; }
  const parts = new Map();
  for (let t = 0; t < p.idx.length; t += 3) {
    const r = find(p.idx[t]);
    if (!parts.has(r)) parts.set(r, { area: 0, lo: [1e9, 1e9, 1e9], hi: [-1e9, -1e9, -1e9] });
    const c = parts.get(r), P = k => [p.pos[p.idx[t + k] * 3], p.pos[p.idx[t + k] * 3 + 1], p.pos[p.idx[t + k] * 3 + 2]];
    const a = P(0), b = P(1), d = P(2), u = b.map((x, i) => x - a[i]), w = d.map((x, i) => x - a[i]);
    c.area += Math.hypot(u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]) / 2;
    for (const q of [a, b, d]) for (let i = 0; i < 3; i++) { c.lo[i] = Math.min(c.lo[i], q[i]); c.hi[i] = Math.max(c.hi[i], q[i]); }
  }
  const lens = new Set();
  for (const [r, c] of parts) {
    const e = c.hi.map((x, i) => x - c.lo[i]).sort((x, y) => x - y);
    if (e[0] < 0.1 * e[2] && e[1] > 0.01 && c.area > 0.55 * e[1] * e[2]) lens.add(r);
  }
  const keep = [];
  for (let t = 0; t < p.idx.length; t += 3) if (!lens.has(find(p.idx[t]))) keep.push(p.idx[t], p.idx[t + 1], p.idx[t + 2]);
  return lens.size ? compact(p, Uint32Array.from(keep)) : p;
}

function compact(p, idx) {
  const map = new Int32Array(p.pos.length / 3).fill(-1);
  let n = 0;
  for (const v of idx) if (map[v] < 0) map[v] = n++;
  const pick = (a, k) => { const o = new a.constructor(n * k); for (let i = 0; i < map.length; i++) if (map[i] >= 0) for (let j = 0; j < k; j++) o[map[i] * k + j] = a[i * k + j]; return o; };
  const q = Object.assign({}, p, {
    pos: pick(p.pos, 3), nor: pick(p.nor, 3), uv: pick(p.uv, 2), J: pick(p.J, 4), W: pick(p.W, 4),
    idx: Uint32Array.from(idx, v => map[v]), targets: {}, inf: p.inf ? p.inf.filter((_, i) => map[i] >= 0) : null
  });
  if (p.inf) { const inf = new Array(n); p.inf.forEach((x, i) => { if (map[i] >= 0) inf[map[i]] = x; }); q.inf = inf; }
  for (const [k, d] of Object.entries(p.targets || {})) q.targets[k] = pick(d, 3);
  if (p.roleAttr) q.roleAttr = pick(p.roleAttr, 1);
  return q;
}

// A shorter version of a hair style: every vertex outside the scalp pulled towards it, by `d.shrink`.
function shrink(p, prims, d) {
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (const q of prims.filter(x => x.role === "skin")) {
    for (let i = 0; i < q.pos.length / 3; i++) {
      if (!q.inf[i].some(([j, w]) => j === "head" && w > 0.5)) continue;
      for (let c = 0; c < 3; c++) { lo[c] = Math.min(lo[c], q.pos[i * 3 + c]); hi[c] = Math.max(hi[c], q.pos[i * 3 + c]); }
    }
  }
  const C = lo.map((v, c) => (v + hi[c]) / 2), E = lo.map((v, c) => (hi[c] - v) / 2 * (d.scalp || 0.92));
  const f = (x, y, z) => {
    const v = [x - C[0], y - C[1], z - C[2]], n = Math.hypot(v[0] / E[0], v[1] / E[1], v[2] / E[2]);
    if (n <= 1) return [x, y, z];
    const k = (1 + (n - 1) * d.shrink) / n;
    return [C[0] + v[0] * k, C[1] + v[1] * k, C[2] + v[2] * k];
  };
  const out = Object.assign({}, p, { node: "hair." + d.style, pos: new Float32Array(p.pos.length), targets: {} });
  for (let i = 0; i < p.pos.length; i += 3) out.pos.set(f(p.pos[i], p.pos[i + 1], p.pos[i + 2]), i);
  for (const [name, delta] of Object.entries(p.targets)) {
    const t = new Float32Array(delta.length);
    for (let i = 0; i < delta.length; i += 3) {
      const q = f(p.pos[i] + delta[i], p.pos[i + 1] + delta[i + 1], p.pos[i + 2] + delta[i + 2]);
      t[i] = q[0] - out.pos[i]; t[i + 1] = q[1] - out.pos[i + 1]; t[i + 2] = q[2] - out.pos[i + 2];
    }
    out.targets[name] = t;
  }
  return out;
}

function decimate(p, tris, error) {
  const now = p.idx.length / 3;
  if (!tris || tris >= now) return p;
  const flags = p.role === "hair" || p.role === "beard" ? [] : [];
  const [idx] = MeshoptSimplifier.simplify(p.idx, p.pos, 3, Math.floor(tris) * 3, error || 0.02, flags);
  return compact(p, idx);
}

function fill(mask, W, H, uv, idx, value) {
  for (let t = 0; t < idx.length; t += 3) {
    const a = idx[t], b = idx[t + 1], c = idx[t + 2];
    const x0 = uv[a * 2] * W, y0 = uv[a * 2 + 1] * H, x1 = uv[b * 2] * W, y1 = uv[b * 2 + 1] * H, x2 = uv[c * 2] * W, y2 = uv[c * 2 + 1] * H;
    const minx = Math.max(0, Math.floor(Math.min(x0, x1, x2))), maxx = Math.min(W - 1, Math.ceil(Math.max(x0, x1, x2)));
    const miny = Math.max(0, Math.floor(Math.min(y0, y1, y2))), maxy = Math.min(H - 1, Math.ceil(Math.max(y0, y1, y2)));
    const d = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (Math.abs(d) < 1e-9) continue;
    for (let y = miny; y <= maxy; y++) for (let x = minx; x <= maxx; x++) {
      const px = x + 0.5, py = y + 0.5;
      const w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / d, w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / d;
      if (w0 >= -0.02 && w1 >= -0.02 && 1 - w0 - w1 >= -0.02) mask[y * W + x] = value;
    }
  }
}

// One colour map, resized, with each tintable primitive's region turned into a detail map whose mean is 0.5.
// Roles in `kept` keep their authored colour (a realistic export's skin, cloth and hair), untinted.
async function colourMap(img, users, size, kept = []) {
  const tints = p => TINT[p.role] && !kept.includes(p.role);
  const { data, info } = await sharp(img.data).resize(size, size, { fit: "fill" }).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
  const W = info.width, H = info.height, region = new Int16Array(W * H).fill(-1);
  users.forEach((p, k) => { if (tints(p)) fill(region, W, H, p.uv, p.idx, k); });
  const sums = users.map(() => [0, 0, 0, 0, 0]);
  for (let i = 0; i < W * H; i++) {
    const k = region[i];
    if (k < 0 || data[i * 4 + 3] < 128) continue;
    const r = lin(data[i * 4] / 255), g = lin(data[i * 4 + 1] / 255), b = lin(data[i * 4 + 2] / 255);
    const s = sums[k];
    s[0] += r; s[1] += g; s[2] += b; s[3] += 0.2126 * r + 0.7152 * g + 0.0722 * b; s[4]++;
  }
  const tones = users.map((p, k) => {
    const s = sums[k];
    return s[4] ? [s[0] / s[4], s[1] / s[4], s[2] / s[4], s[3] / s[4]] : null;
  });
  const out = Buffer.from(data);
  for (let i = 0; i < W * H; i++) {
    const k = region[i];
    if (k < 0 || !tones[k]) continue;
    const mode = TINT[users[k].role], t = tones[k];
    const r = lin(data[i * 4] / 255), g = lin(data[i * 4 + 1] / 255), b = lin(data[i * 4 + 2] / 255);
    let o;
    if (mode === "chroma") o = [0.5 * r / t[0], 0.5 * g / t[1], 0.5 * b / t[2]];
    else { const l = 0.5 * (0.2126 * r + 0.7152 * g + 0.0722 * b) / t[3]; o = [l, l, l]; }
    for (let c = 0; c < 3; c++) out[i * 4 + c] = Math.round(255 * srgb(Math.min(1, Math.max(0, o[c]))));
  }
  const tone = users.map((p, k) => (tints(p) && tones[k] ? [0.5, 0.5, 0.5] : tones[k] ? tones[k].slice(0, 3) : [0.5, 0.5, 0.5]));
  return { raw: out, W, H, tone };
}

// One packed map for the page's roughnessMap and aoMap (glTF's layout): R occlusion, G roughness, B metalness.
// Occlusion from the source's occlusion map (e.g. one baked in Blender), roughness from its metallic-roughness
// map's G channel (scaled by its factor), else the role's constant. People are dielectric, whatever an
// export's greyscale "metallic-roughness" map says, so the material's metallicFactor is 0 and B is free:
// it carries `thin` (a greyscale image, 1 where light shows through, e.g. ears) for the page's skin, else 0.
// Null when there is none of the three.
async function ormMap(mat, role, size, thin) {
  if (!mat.orm && !mat.occlusion && !thin) return null;
  const chan = async (img, c) => (await sharp(img.data).resize(size, size, { fit: "fill" }).ensureAlpha().raw().toBuffer())
    .filter((_, i) => i % 4 === c);
  const ao = mat.occlusion ? await chan(mat.occlusion, 0) : null;
  const rough = mat.orm ? await chan(mat.orm, 1) : null;
  const through = thin ? await chan(thin, 0) : null;
  const k = mat.orm ? (mat.rough === undefined ? 1 : mat.rough) : 1, flat = Math.round(255 * (ROUGH[role] || 0.7));
  const out = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) {
    out[i * 4] = ao ? ao[i] : 255;
    out[i * 4 + 1] = rough ? Math.min(255, Math.round(rough[i] * k)) : flat;
    out[i * 4 + 2] = through ? through[i] : 0;
    out[i * 4 + 3] = 255;
  }
  return { img: await sharp(out, { raw: { width: size, height: size, channels: 4 } }).webp({ quality: 90, effort: 6 }).toBuffer(), ao: !!ao, thin: !!through };
}

async function webp(raw, W, H, alpha, q) {
  return sharp(raw, { raw: { width: W, height: H, channels: 4 } }).webp({ quality: q || 82, alphaQuality: alpha ? 90 : 100, effort: 6 }).toBuffer();
}

function palms(src) {
  const out = {}, P = n => src.joints.get(n) && M.translation(src.joints.get(n).world);
  for (const s of ["l", "r"]) {
    const h = P("hand_" + s), i = P("index_01_" + s), k = P("pinky_01_" + s), m = P("middle_01_" + s), t = P("thumb_02_" + s);
    if (!h || !i || !k || !m) continue;
    const f = m.map((v, c) => v - h[c]), q = k.map((v, c) => v - i[c]);
    let n = [f[1] * q[2] - f[2] * q[1], f[2] * q[0] - f[0] * q[2], f[0] * q[1] - f[1] * q[0]];
    const l = Math.hypot(...n) || 1;
    n = n.map(v => v / l);
    if (t && (t[0] - h[0]) * n[0] + (t[1] - h[1]) * n[1] + (t[2] - h[2]) * n[2] < 0) n = n.map(v => -v);
    out["palm" + s.toUpperCase()] = n.map(v => +v.toFixed(4));
  }
  return out;
}

function anchors(prims, joints, src) {
  const head = joints.findIndex(j => j.name === "head");
  const out = src ? palms(src) : {};
  const eyes = prims.filter(p => p.role === "eyes");
  for (const [side, sgn] of [["eyeL", -1], ["eyeR", 1]]) {
    let s = [0, 0, 0, 0];
    for (const p of eyes) for (let i = 0; i < p.pos.length; i += 3) if (Math.sign(p.pos[i]) === sgn) { s[0] += p.pos[i]; s[1] += p.pos[i + 1]; s[2] += p.pos[i + 2]; s[3]++; }
    if (s[3]) out[side] = [s[0] / s[3], s[1] / s[3], s[2] / s[3]].map(v => +v.toFixed(4));
  }
  let top = -Infinity, front = Infinity, back = -Infinity, side = 0, chin = Infinity;
  const head_ = [];
  for (const p of prims.filter(q => q.role === "skin")) {
    for (let i = 0; i < p.pos.length / 3; i++) {
      let hw = 0;
      for (let k = 0; k < 4; k++) if (p.J[i * 4 + k] === head) hw += p.W[i * 4 + k];
      if (hw < 0.5) continue;
      const x = p.pos[i * 3], y = p.pos[i * 3 + 1], z = p.pos[i * 3 + 2];
      head_.push([x, y, z]);
      top = Math.max(top, y); front = Math.min(front, z); back = Math.max(back, z); side = Math.max(side, Math.abs(x));
    }
  }
  const eyeY = out.eyeL ? (out.eyeL[1] + (out.eyeR || out.eyeL)[1]) / 2 : top - 0.12;
  for (const [x, y, z] of head_) if (z < front + 0.045 && y < eyeY && Math.abs(x) < 0.03) chin = Math.min(chin, y);
  if (chin === Infinity) chin = top - 0.24;
  if (top > -Infinity) Object.assign(out, { top: +top.toFixed(4), front: +front.toFixed(4), back: +back.toFixed(4), side: +side.toFixed(4), chin: +chin.toFixed(4) });
  return out;
}

function quantUV(uv) {
  for (const v of uv) if (v < 0 || v > 1) return null;
  return Uint16Array.from(uv, v => Math.round(v * 65535));
}

function weights8(W) {
  const out = new Uint8Array(W.length);
  for (let i = 0; i < W.length; i += 4) {
    let s = 0, big = 0;
    for (let k = 0; k < 4; k++) { out[i + k] = Math.round(W[i + k] * 255); s += out[i + k]; if (out[i + k] > out[i + big]) big = k; }
    out[i + big] += 255 - s;
  }
  return out;
}

function skeleton(doc, scene, joints, extras) {
  const nodes = joints.map(j => doc.createNode(j.name));
  const world = joints.map(j => M.fromTRS(j.t, j.q));
  joints.forEach((j, i) => {
    const local = j.parent >= 0 ? M.mul(M.invert(world[j.parent]), world[i]) : world[i];
    nodes[i].setTranslation(M.translation(local).map(v => +v.toFixed(6))).setRotation(M.rotation(local));
    if (j.parent >= 0) nodes[j.parent].addChild(nodes[i]); else scene.addChild(nodes[i]);
  });
  const ibm = new Float32Array(joints.length * 16);
  world.forEach((w, i) => ibm.set(M.invert(w), i * 16));
  const skin = doc.createSkin("skeleton").setInverseBindMatrices(doc.createAccessor().setType("MAT4").setArray(ibm)).setSkeleton(nodes[0]);
  nodes.forEach(n => skin.addJoint(n));
  if (extras) skin.setExtras(extras);
  return skin;
}

function primitive(doc, p, mat, targetNames) {
  const uv16 = quantUV(p.uv), w8 = weights8(p.W);
  const prim = doc.createPrimitive()
    .setAttribute("POSITION", doc.createAccessor().setType("VEC3").setArray(Float32Array.from(p.pos)))
    .setAttribute("NORMAL", doc.createAccessor().setType("VEC3").setArray(Int8Array.from(p.nor, v => Math.round(Math.max(-1, Math.min(1, v)) * 127))).setNormalized(true))
    .setAttribute("TEXCOORD_0", uv16 ? doc.createAccessor().setType("VEC2").setArray(uv16).setNormalized(true) : doc.createAccessor().setType("VEC2").setArray(Float32Array.from(p.uv)))
    .setAttribute("JOINTS_0", doc.createAccessor().setType("VEC4").setArray(Uint8Array.from(p.J, (j, i) => (w8[i] ? j : 0))))
    .setAttribute("WEIGHTS_0", doc.createAccessor().setType("VEC4").setArray(w8).setNormalized(true))
    .setIndices(doc.createAccessor().setType("SCALAR").setArray(p.pos.length / 3 < 65536 ? Uint16Array.from(p.idx) : Uint32Array.from(p.idx)))
    .setMaterial(mat);
  if (p.roleAttr) prim.setAttribute("_ROLE", doc.createAccessor().setType("SCALAR").setArray(Uint8Array.from(p.roleAttr)));
  for (const name of targetNames || []) {
    const d = p.targets[name] || new Float32Array(p.pos.length);
    const t = doc.createPrimitiveTarget(name).setAttribute("POSITION", doc.createAccessor().setType("VEC3").setArray(Int16Array.from(d, v => Math.round(Math.max(-1, Math.min(1, v)) * 32767))).setNormalized(true));
    prim.addTarget(t);
  }
  return prim;
}

function newDoc() {
  const doc = new Document();
  doc.createBuffer();
  doc.createExtension(KHRMeshQuantization).setRequired(true);
  doc.createExtension(EXTTextureWebP).setRequired(true);
  return doc;
}

function material(doc, name, role, tone, maps, extras, rough) {
  const m = doc.createMaterial(name).setRoughnessFactor((rough || {})[role] || ROUGH[role] || 0.7).setMetallicFactor(0).setBaseColorFactor([1, 1, 1, 1]);
  const tex = (img, suffix) => (img.getImage ? img : doc.createTexture(name + suffix).setImage(img).setMimeType("image/webp"));
  if (maps.base) m.setBaseColorTexture(tex(maps.base, "_c"));
  if (maps.normal) m.setNormalTexture(tex(maps.normal, "_n"));
  if (maps.orm) {
    m.setMetallicRoughnessTexture(tex(maps.orm.tex, "_orm")).setRoughnessFactor(1);
    if (maps.orm.ao) m.setOcclusionTexture(tex(maps.orm.tex, "_orm"));
  }
  if (ALPHA[role]) m.setAlphaMode("MASK").setAlphaCutoff(ALPHA[role]).setDoubleSided(true);
  m.setExtras(Object.assign({ role, tone: tone.map(v => +v.toFixed(4)) }, extras || {}));
  return m;
}

const io = new NodeIO().registerExtensions([KHRMeshQuantization, EXTTextureWebP]);

function tris(ps) { return ps.reduce((a, p) => a + p.idx.length / 3, 0); }

async function hero(o, cfg) {
  const rules = (cfg.roles || []).map(([re, r]) => [new RegExp(re, "i"), r]).concat(ROLE_RULES);
  const keep = UE_BODY.concat(o.fingers === false ? [] : FINGERS);
  const { src, F } = await load(o.sources, cfg);
  const joints = collapse(src, keep);
  let prims = src.prims.map(p => Object.assign(p, { role: (o.rolesByNode && o.rolesByNode[p.node]) || roleOf(p, rules) }))
    .filter(p => p.role !== "drop" && !(o.dropNodes || []).includes(p.node));
  const jointT = {}, targetNames = Object.keys(o.variants || {});
  for (const name of targetNames) {
    const v = await load(o.variants[name], cfg, F);
    const vj = collapse(v.src, keep);
    jointT[name] = joints.map((j, i) => [0, 1, 2].map(c => +(vj[i].t[c] - j.t[c]).toFixed(5)));
    for (const p of prims) {
      const q = v.src.prims.find(x => x.node === p.node && x.mat.name === p.mat.name);
      if (!q || q.pos.length !== p.pos.length) throw new Error(`variant ${name}: ${p.node} does not match (${q ? q.pos.length / 3 : "none"} vs ${p.pos.length / 3})`);
      p.targets[name] = Float32Array.from(p.pos, (x, i) => q.pos[i] - x);
    }
  }
  for (const d of o.derive || []) {
    const from = prims.find(p => p.node === "hair." + d.from);
    if (from) prims.push(shrink(from, prims, d));
  }
  prims = prims.flatMap(p => (p.role === "outfit" ? split(p) : [p])).map(p => (p.role === "glasses" ? lensless(p) : p));
  const before = tris(prims);
  prims = prims.map(p => decimate(p, (o.tris || {})[p.role === "top" || p.role === "bottom" ? "outfit" : p.role] * (p.role === "top" || p.role === "bottom" ? p.idx.length / 3 / tris(prims.filter(q => q.role === "top" || q.role === "bottom")) : 1), o.error));
  const doc = newDoc(), scene = doc.createScene("character");
  const base = anchors(prims, joints, src), anchorT = {};
  for (const name of targetNames) {
    const moved = anchors(prims.map(p => Object.assign({}, p, { pos: Float32Array.from(p.pos, (x, i) => x + p.targets[name][i]) })), joints);
    anchorT[name] = {};
    for (const [k, v] of Object.entries(moved)) {
      anchorT[name][k] = Array.isArray(v) ? v.map((x, i) => +(x - base[k][i]).toFixed(4)) : +(v - base[k]).toFixed(4);
    }
  }
  const skin = skeleton(doc, scene, joints, { jointTargets: jointT, anchors: base, anchorTargets: anchorT });
  const mesh = doc.createMesh("character").setExtras({ targetNames });
  const byImage = new Map();
  for (const p of prims) {
    const key = p.mat.base ? crypto.createHash("md5").update(p.mat.base.data).digest("hex") : null;
    if (!byImage.has(key)) byImage.set(key, []);
    byImage.get(key).push(p);
  }
  const report = {};
  for (const [img, users] of byImage) {
    const size = Math.max(...users.map(p => (o.texture || {})[p.role] || 512));
    const cm = img ? await colourMap(users[0].mat.base, users, size, o.keep || []) : null;
    const baseImg = cm ? await webp(cm.raw, cm.W, cm.H, users.some(p => ALPHA[p.role]), o.quality) : null;
    const base = baseImg ? doc.createTexture(users[0].role + "_c").setImage(baseImg).setMimeType("image/webp") : null;
    let normal = null;
    if (users[0].mat.normal && (o.normals || []).includes(users[0].role === "top" || users[0].role === "bottom" ? "outfit" : users[0].role)) {
      normal = doc.createTexture(users[0].role + "_n").setMimeType("image/webp")
        .setImage(await sharp(users[0].mat.normal.data).resize(Math.min(size, o.normalSize || 512), Math.min(size, o.normalSize || 512), { fit: "fill" }).webp({ quality: 88 }).toBuffer());
    }
    const ormRole = users[0].role === "top" || users[0].role === "bottom" ? "outfit" : users[0].role;
    const thinFile = (o.thin || {})[users[0].mat.name];
    const thin = thinFile ? { data: fs.readFileSync(path.resolve(cfg.base, thinFile)) } : null;
    const packed = (o.orm || []).includes(ormRole) ? await ormMap(users[0].mat, users[0].role, Math.min(size, o.normalSize || 512), thin) : null;
    const orm = packed ? { tex: doc.createTexture(users[0].role + "_orm").setImage(packed.img).setMimeType("image/webp"), ao: packed.ao } : null;
    users.forEach((p, k) => {
      const style = OPTIONAL.includes(p.role) ? p.node.replace(/^[a-z]+\./, "") : undefined;
      const extras = Object.assign(style ? { style } : {}, (o.keep || []).includes(p.role) ? { tint: false } : {}, packed && packed.thin ? { thin: true } : {});
      const mat = material(doc, p.role + (style ? "_" + style : ""), p.role, cm ? cm.tone[k] : [0.5, 0.5, 0.5], { base, normal, orm }, Object.keys(extras).length ? extras : null, o.rough);
      mesh.addPrimitive(primitive(doc, p, mat, targetNames));
      report[p.role + (style ? ":" + style : "")] = p.idx.length / 3;
    });
  }
  scene.addChild(doc.createNode("character").setMesh(mesh).setSkin(skin));
  const out = path.resolve(cfg.base, cfg.out, o.id + ".glb");
  await io.write(out, doc);
  console.log(o.id, "joints", joints.length, "tris", before, "->", tris(prims), "bytes", fs.statSync(out).size, JSON.stringify(report));
}

async function crowd(o, cfg) {
  const rules = (cfg.roles || []).map(([re, r]) => [new RegExp(re, "i"), r]).concat(ROLE_RULES);
  const chars = [];
  for (const c of o.characters) {
    const { src } = await load(c.sources, cfg);
    const joints = collapse(src, UE_BODY);
    let prims = src.prims.map(p => Object.assign(p, { role: roleOf(p, rules) })).filter(p => p.role !== "drop" && !(o.skip || ["brows", "lashes"]).includes(p.role) && !(c.dropNodes || []).some(n => n === p.node || n === p.mesh));
    prims = prims.flatMap(p => (p.role === "outfit" ? split(p) : [p]));
    chars.push({ name: c.name, joints, prims, palm: palms(src) });
  }
  const images = new Map();
  for (const ch of chars) for (const p of ch.prims) {
    const key = p.mat.base ? crypto.createHash("md5").update(p.mat.base.data).digest("hex") : "none";
    if (!images.has(key)) images.set(key, { img: p.mat.base, users: [] });
    images.get(key).users.push(p);
  }
  const tiles = [...images.values()].filter(t => t.img).map(t => ({ ...t, size: Math.max(...t.users.map(p => (o.tile || {})[p.role] || 256)) }));
  tiles.sort((a, b) => b.size - a.size);
  const AW = o.atlas || 2048;
  let x = 0, y = 0, row = 0;
  for (const t of tiles) {
    if (x + t.size > AW) { x = 0; y += row; row = 0; }
    t.x = x; t.y = y; x += t.size; row = Math.max(row, t.size);
  }
  const AH = Math.pow(2, Math.ceil(Math.log2(y + row)));
  const atlas = Buffer.alloc(AW * AH * 4, 255);
  for (const t of tiles) {
    const cm = await colourMap(t.img, t.users, t.size);
    for (let r = 0; r < t.size; r++) cm.raw.copy(atlas, ((t.y + r) * AW + t.x) * 4, r * t.size * 4, (r + 1) * t.size * 4);
    const pad = 2 / AW;
    for (const p of t.users) {
      const u0 = t.x / AW, v0 = t.y / AH, su = t.size / AW, sv = t.size / AH;
      for (let i = 0; i < p.uv.length; i += 2) {
        p.uv[i] = u0 + Math.min(1 - pad, Math.max(pad, p.uv[i])) * su;
        p.uv[i + 1] = v0 + Math.min(1 - pad, Math.max(pad, p.uv[i + 1])) * sv;
      }
    }
  }
  const doc = newDoc(), scene = doc.createScene("crowd");
  const tex = await webp(atlas, AW, AH, true, o.quality || 80);
  const mat = material(doc, "crowd", "other", [0.5, 0.5, 0.5], { base: tex }, { atlas: [AW, AH] });
  mat.setAlphaMode("MASK").setAlphaCutoff(0.5).setDoubleSided(false);
  const report = [];
  for (const ch of chars) {
    const skin = skeleton(doc, scene, ch.joints, { anchors: Object.assign(anchors(ch.prims, ch.joints), ch.palm) });
    (o.lods || [{}]).forEach((lod, li) => {
      const parts = ch.prims.filter(p => !(lod.skip || []).includes(p.role)).map(p => decimate(p, (lod.tris || {})[p.role === "top" || p.role === "bottom" ? "outfit" : p.role] *
        (p.role === "top" || p.role === "bottom" ? p.idx.length / 3 / tris(ch.prims.filter(q => q.role === "top" || q.role === "bottom")) : 1), lod.error || o.error));
      const n = parts.reduce((a, p) => a + p.pos.length / 3, 0);
      const merged = { pos: new Float32Array(n * 3), nor: new Float32Array(n * 3), uv: new Float32Array(n * 2), J: new Uint8Array(n * 4), W: new Float32Array(n * 4), roleAttr: new Uint8Array(n), idx: [], targets: {} };
      let at = 0;
      for (const p of parts) {
        const m = p.pos.length / 3;
        merged.pos.set(p.pos, at * 3); merged.nor.set(p.nor, at * 3); merged.uv.set(p.uv, at * 2); merged.J.set(p.J, at * 4); merged.W.set(p.W, at * 4);
        merged.roleAttr.fill(ROLE_ID[p.role] !== undefined ? ROLE_ID[p.role] : 5, at, at + m);
        for (const v of p.idx) merged.idx.push(v + at);
        at += m;
      }
      merged.idx = Uint32Array.from(merged.idx);
      const mesh = doc.createMesh(ch.name + "_lod" + li).setExtras({ lod: li, character: ch.name });
      mesh.addPrimitive(primitive(doc, merged, mat, []));
      scene.addChild(doc.createNode(ch.name + "_lod" + li).setMesh(mesh).setSkin(skin));
      report.push(ch.name + ":" + li + "=" + merged.idx.length / 3);
    });
  }
  const out = path.resolve(cfg.base, cfg.out, o.id + ".glb");
  await io.write(out, doc);
  console.log(o.id, "atlas", AW + "x" + AH, "bytes", fs.statSync(out).size, report.join(" "));
}

const cfgPath = process.argv[2];
const cfg = JSON.parse(fs.readFileSync(cfgPath, "utf8"));
cfg.base = cfg.base || path.dirname(path.resolve(cfgPath));
fs.mkdirSync(path.resolve(cfg.base, cfg.out), { recursive: true });
for (const o of cfg.outputs) {
  if (process.argv[3] && o.id !== process.argv[3]) continue;
  if (o.kind === "crowd") await crowd(o, cfg); else await hero(o, cfg);
}
