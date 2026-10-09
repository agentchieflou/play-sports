// The street trees (trees.py's raw meshes and textures) to static/world/trees/trees.glb: a mesh a tree and
// level of detail, each with a bark and a leaves primitive, turned from Blender's Z up to glTF's Y up;
// WebP textures (the leaves' atlas at 1024 px,
// bark at 512 px); separate (not interleaved) vertex attributes, normals and colours as normalised bytes,
// which world/assets.js reads directly.
// Usage: node trees/trees.mjs <raw-dir> <out-dir>
import fs from "fs";
import path from "path";
import { Document, NodeIO, VertexLayout } from "@gltf-transform/core";
import { ALL_EXTENSIONS, EXTTextureWebP } from "@gltf-transform/extensions";
import sharp from "sharp";

const [raw, out] = process.argv.slice(2);
if (!raw || !out) {
  console.error("usage: node trees/trees.mjs <raw-dir> <out-dir>");
  process.exit(2);
}
const meta = JSON.parse(fs.readFileSync(path.join(raw, "trees.json"), "utf8"));
const bin = fs.readFileSync(path.join(raw, "trees.bin"));
const doc = new Document();
doc.createExtension(EXTTextureWebP).setRequired(true);
const buffer = doc.createBuffer();
const tex = async (name, size) => doc.createTexture(name).setMimeType("image/webp").setImage(
  await sharp(path.join(raw, `${name}.png`)).resize(size, size, { kernel: "lanczos3" }).webp({ quality: 84, alphaQuality: 92, effort: 6 }).toBuffer());
const mats = {
  leaves: doc.createMaterial("leaves").setBaseColorTexture(await tex("leaves_color", 1024)).setNormalTexture(await tex("leaves_normal", 1024))
    .setMetallicRoughnessTexture(await tex("leaves_orm", 512)).setMetallicFactor(0).setAlphaMode("MASK").setAlphaCutoff(0.4).setDoubleSided(true),
};
for (const sp of ["plane", "linden"]) {
  mats[`bark_${sp}`] = doc.createMaterial(`bark_${sp}`).setBaseColorTexture(await tex(`bark_${sp}_color`, 512))
    .setNormalTexture(await tex(`bark_${sp}_normal`, 512)).setMetallicRoughnessTexture(await tex(`bark_${sp}_orm`, 256)).setMetallicFactor(0);
}
const view = (off, n, K) => new K(bin.buffer.slice(bin.byteOffset + off, bin.byteOffset + off + n * K.BYTES_PER_ELEMENT));
const yUp = (a) => {
  for (let i = 0; i < a.length; i += 3) [a[i + 1], a[i + 2]] = [a[i + 2], -a[i + 1]];
  return a;
};
const acc = (arr, type, norm) => doc.createAccessor().setArray(arr).setType(type).setNormalized(!!norm).setBuffer(buffer);
const scene = doc.createScene();
let tris = 0;
for (const m of meta.meshes) {
  const mesh = doc.createMesh(m.name).setExtras({ species: m.species, lod: m.lod });
  for (const p of m.parts) {
    const nor = yUp(view(p.normal, p.count * 3, Float32Array));
    const idx = view(p.index, p.tris * 3, Uint32Array);
    mesh.addPrimitive(doc.createPrimitive()
      .setAttribute("POSITION", acc(yUp(view(p.position, p.count * 3, Float32Array)), "VEC3"))
      .setAttribute("NORMAL", acc(Int8Array.from(nor, (v) => Math.round(Math.max(-1, Math.min(1, v)) * 127)), "VEC3", true))
      .setAttribute("TEXCOORD_0", acc(view(p.uv, p.count * 2, Float32Array), "VEC2"))
      .setAttribute("COLOR_0", acc(view(p.color, p.count * 3, Uint8Array), "VEC3", true))
      .setIndices(acc(p.count < 65536 ? Uint16Array.from(idx) : idx, "SCALAR"))
      .setMaterial(mats[p.material]));
    tris += p.tris;
  }
  scene.addChild(doc.createNode(m.name).setMesh(mesh));
}
fs.mkdirSync(out, { recursive: true });
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).setVertexLayout(VertexLayout.SEPARATE);
const to = path.join(out, "trees.glb");
await io.write(to, doc);
console.log("trees.glb", meta.meshes.length, "meshes", tris, "tris", fs.statSync(to).size, "bytes");
