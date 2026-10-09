// The cars (cars.py's raw meshes) to static/world/cars/cars.glb: a mesh a car and level of detail, the near
// one with a body, glass, trim and lamp primitive in that order, the far one with a body (glass and trim in
// it, by colour) and a lamp primitive, turned from Blender's Z up to glTF's Y up;
// separate (not interleaved) vertex attributes, normals and colours as normalised bytes, which
// world/assets.js reads directly. No textures: the page paints them (world/street.js's materials).
// Usage: node cars/cars.mjs <raw-dir> <out-dir>
import fs from "fs";
import path from "path";
import { Document, NodeIO, VertexLayout } from "@gltf-transform/core";

const [raw, out] = process.argv.slice(2);
if (!raw || !out) {
  console.error("usage: node cars/cars.mjs <raw-dir> <out-dir>");
  process.exit(2);
}
const meta = JSON.parse(fs.readFileSync(path.join(raw, "cars.json"), "utf8"));
const bin = fs.readFileSync(path.join(raw, "cars.bin"));
const doc = new Document();
const buffer = doc.createBuffer();
const mats = Object.fromEntries(["body", "glass", "trim", "lamp"].map((m) => [m, doc.createMaterial(m).setMetallicFactor(0)]));
const view = (off, n, K) => new K(bin.buffer.slice(bin.byteOffset + off, bin.byteOffset + off + n * K.BYTES_PER_ELEMENT));
const yUp = (a) => {
  for (let i = 0; i < a.length; i += 3) [a[i + 1], a[i + 2]] = [a[i + 2], -a[i + 1]];
  return a;
};
const acc = (arr, type, norm) => doc.createAccessor().setArray(arr).setType(type).setNormalized(!!norm).setBuffer(buffer);
const scene = doc.createScene();
let tris = 0;
for (const m of meta.meshes) {
  const mesh = doc.createMesh(m.name).setExtras({ kind: m.kind, lod: m.lod });
  for (const p of m.parts) {
    const nor = yUp(view(p.normal, p.count * 3, Float32Array));
    const idx = view(p.index, p.tris * 3, Uint32Array);
    mesh.addPrimitive(doc.createPrimitive()
      .setAttribute("POSITION", acc(yUp(view(p.position, p.count * 3, Float32Array)), "VEC3"))
      .setAttribute("NORMAL", acc(Int8Array.from(nor, (v) => Math.round(Math.max(-1, Math.min(1, v)) * 127)), "VEC3", true))
      .setAttribute("COLOR_0", acc(view(p.color, p.count * 3, Uint8Array), "VEC3", true))
      .setIndices(acc(p.count < 65536 ? Uint16Array.from(idx) : idx, "SCALAR"))
      .setMaterial(mats[p.material]));
    tris += p.tris;
  }
  scene.addChild(doc.createNode(m.name).setMesh(mesh));
}
fs.mkdirSync(out, { recursive: true });
const io = new NodeIO().setVertexLayout(VertexLayout.SEPARATE);
const to = path.join(out, "cars.glb");
await io.write(to, doc);
console.log("cars.glb", meta.meshes.length, "meshes", tris, "tris", fs.statSync(to).size, "bytes");
