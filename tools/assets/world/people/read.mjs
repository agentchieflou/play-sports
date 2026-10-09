// Read one source glTF/GLB into a plain intermediate form: every primitive's vertices skinned to the
// rest pose in world space, and every joint's world rest matrix by name.
import { NodeIO } from "@gltf-transform/core";
import { ALL_EXTENSIONS } from "@gltf-transform/extensions";
import * as M from "./m4.mjs";

const io = new NodeIO().registerExtensions(ALL_EXTENSIONS);

function floats(acc) {
  const n = acc.getCount(), k = acc.getElementSize(), out = new Float32Array(n * k), el = new Array(k);
  for (let i = 0; i < n; i++) { acc.getElement(i, el); for (let j = 0; j < k; j++) out[i * k + j] = el[j]; }
  return out;
}

function ints(acc) {
  const n = acc.getCount(), k = acc.getElementSize(), out = new Uint32Array(n * k), a = acc.getArray();
  for (let i = 0; i < n * k; i++) out[i] = a[i];
  return out;
}

// opts.rename: {source name: new name}; opts.attach: joint for meshes without a skin (default "head").
export async function readSource(path, opts = {}) {
  const doc = await io.read(path);
  const root = doc.getRoot();
  const rename = opts.rename || {};
  const nm = n => rename[n] !== undefined ? rename[n] : n;
  const joints = new Map();
  const jointNodes = new Set();
  for (const skin of root.listSkins()) for (const j of skin.listJoints()) jointNodes.add(j);
  // opts.pose "bind": the rest pose is the one the inverse bind matrices hold, not the nodes' own
  // transforms (an export saved mid-animation, e.g. sitting, would otherwise come out sitting).
  const bind = new Map();
  if (opts.pose === "bind") for (const skin of root.listSkins()) {
    const ibm = skin.getInverseBindMatrices();
    if (ibm) skin.listJoints().forEach((j, i) => {
      if (bind.has(j)) return;
      const inv = new Array(16);
      ibm.getElement(i, inv);
      bind.set(j, M.invert(Float64Array.from(inv)));
    });
  }
  const worldOf = j => bind.get(j) || Float64Array.from(j.getWorldMatrix());
  for (const node of jointNodes) {
    const name = nm(node.getName());
    let p = node.getParentNode();
    while (p && !jointNodes.has(p)) p = p.getParentNode();
    if (!joints.has(name)) joints.set(name, { name, world: worldOf(node), parent: p ? nm(p.getName()) : null });
  }
  const prims = [];
  for (const node of root.listNodes()) {
    const mesh = node.getMesh();
    if (!mesh) continue;
    const skin = node.getSkin();
    let jm = null, jn = null;
    if (skin) {
      const ibm = skin.getInverseBindMatrices();
      jn = skin.listJoints().map(j => nm(j.getName()));
      jm = skin.listJoints().map((j, i) => {
        const inv = new Array(16);
        if (ibm) ibm.getElement(i, inv); else M.ident().forEach((v, k) => { inv[k] = v; });
        return M.mul(worldOf(j), Float64Array.from(inv));
      });
    }
    const nodeWorld = Float64Array.from(node.getWorldMatrix());
    const targetNames = (mesh.getExtras() && mesh.getExtras().targetNames) || [];
    for (const prim of mesh.listPrimitives()) {
      if (prim.getMode() !== 4) continue;
      const pos = floats(prim.getAttribute("POSITION"));
      const n = pos.length / 3;
      const norA = prim.getAttribute("NORMAL"), uvA = prim.getAttribute("TEXCOORD_0");
      const nor = norA ? floats(norA) : new Float32Array(n * 3);
      const uv = uvA ? floats(uvA) : new Float32Array(n * 2);
      const idx = prim.getIndices() ? ints(prim.getIndices()) : Uint32Array.from({ length: n }, (_, i) => i);
      let J = new Uint16Array(n * 4), W = new Float32Array(n * 4);
      const outP = new Float32Array(n * 3), outN = new Float32Array(n * 3);
      if (skin) {
        const j0 = ints(prim.getAttribute("JOINTS_0")), w0 = floats(prim.getAttribute("WEIGHTS_0"));
        const j1A = prim.getAttribute("JOINTS_1"), w1A = prim.getAttribute("WEIGHTS_1");
        const j1 = j1A ? ints(j1A) : null, w1 = w1A ? floats(w1A) : null;
        const names = [];
        for (let i = 0; i < n; i++) {
          const inf = [];
          for (let k = 0; k < 4; k++) if (w0[i * 4 + k] > 0) inf.push([j0[i * 4 + k], w0[i * 4 + k]]);
          if (j1) for (let k = 0; k < 4; k++) if (w1[i * 4 + k] > 0) inf.push([j1[i * 4 + k], w1[i * 4 + k]]);
          let px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, ws = 0;
          for (const [j, w] of inf) {
            const p = M.point(jm[j], pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
            const d = M.dir(jm[j], nor[i * 3], nor[i * 3 + 1], nor[i * 3 + 2]);
            px += p[0] * w; py += p[1] * w; pz += p[2] * w; nx += d[0] * w; ny += d[1] * w; nz += d[2] * w; ws += w;
          }
          ws = ws || 1;
          outP.set([px / ws, py / ws, pz / ws], i * 3);
          const l = Math.hypot(nx, ny, nz) || 1;
          outN.set([nx / l, ny / l, nz / l], i * 3);
          names.push(inf.map(([j, w]) => [jn[j], w / ws]));
        }
        prims.push(make(node, mesh, prim, outP, outN, uv, idx, names, targetNames, jm, pos));
      } else {
        const names = [];
        for (let i = 0; i < n; i++) {
          const p = M.point(nodeWorld, pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
          const d = M.dir(nodeWorld, nor[i * 3], nor[i * 3 + 1], nor[i * 3 + 2]);
          outP.set(p, i * 3);
          const l = Math.hypot(d[0], d[1], d[2]) || 1;
          outN.set([d[0] / l, d[1] / l, d[2] / l], i * 3);
          names.push([[opts.attach || "head", 1]]);
        }
        prims.push(make(node, mesh, prim, outP, outN, uv, idx, names, targetNames, null, pos));
      }
    }
  }
  return { path, joints, prims };
}

function image(tex) {
  if (!tex) return null;
  return { data: Buffer.from(tex.getImage()), mime: tex.getMimeType(), name: tex.getName() || tex.getURI() };
}

function make(node, mesh, prim, pos, nor, uv, idx, inf, targetNames, jm, raw) {
  const mat = prim.getMaterial();
  const pbr = mat ? {
    name: mat.getName(), color: mat.getBaseColorFactor(), alpha: mat.getAlphaMode(),
    base: image(mat.getBaseColorTexture()), normal: image(mat.getNormalTexture()), orm: image(mat.getMetallicRoughnessTexture()),
    occlusion: image(mat.getOcclusionTexture()), rough: mat.getRoughnessFactor(), metal: mat.getMetallicFactor()
  } : { name: "", color: [1, 1, 1, 1], alpha: "OPAQUE" };
  return { node: node.getName(), mesh: mesh.getName(), mat: pbr, pos, nor, uv, idx, inf, targets: {} };
}
