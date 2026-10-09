// Poly Haven models (1k glTF, from fetch.mjs) to the .glb files static/world/cc0/ holds. For each:
// keep one variant's nodes (and materials), bake the node transforms, drop attributes the world does
// not read, join, weld, stand it on y = 0 over the origin, decimate with meshoptimizer to `tris`,
// make it opaque, put its textures in the file as 512 px WebP, and write separate (not interleaved)
// vertex attributes, which world/assets.js reads directly.
// Usage: node cc0/props.mjs <models-dir> <out-dir> [id...]   (all of SPECS when no id is given)
import fs from "fs";
import path from "path";
import { NodeIO, VertexLayout } from "@gltf-transform/core";
import { ALL_EXTENSIONS } from "@gltf-transform/extensions";
import { prune, dedup, weld, simplify, join, center, textureCompress, unpartition, clearNodeTransform, getBounds } from "@gltf-transform/functions";
import { MeshoptSimplifier } from "meshoptimizer";
import sharp from "sharp";

export const SPECS = {
  fire_hydrant: { keep: ["fire_hydrant", "fire_hydrant_cap_01", "fire_hydrant_cap_02", "fire_hydrant_cap_03"], tris: 1500 },
  metal_trash_can: { keep: ["metal_trash_can", "metal_trash_can_lid", "metal_trash_can_handle_left", "metal_trash_can_handle_right"], tris: 1200 },
  utility_box_01: { keep: ["utility_box_01_box"], tris: 900 },
  utility_box_02: { keep: ["utility_box_02"], tris: 1100 },
  concrete_road_barrier: { keep: ["concrete_road_barrier"], tris: 1200 },
  exterior_aircon_unit: { keep: ["exterior_aircon_unit"], mats: ["exterior_aircon_unit_01"], tris: 1000 },
  trashbag: { keep: ["trashbag"], tris: 800 },
  cardboard_box_01: { keep: ["cardboard_box_01"], tris: 600 },
  covered_car: { keep: ["covered_car", "covered_car_wheel_01", "covered_car_wheel_02", "covered_car_wheel_03", "covered_car_wheel_04"], tris: 3200 },
};

const [models, out, ...only] = process.argv.slice(2);
if (!models || !out) {
  console.error("usage: node cc0/props.mjs <models-dir> <out-dir> [id...]");
  process.exit(2);
}
await MeshoptSimplifier.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).setVertexLayout(VertexLayout.SEPARATE);
const tris = (root) => root.listMeshes().flatMap((m) => m.listPrimitives()).reduce((a, p) => a + p.getIndices().getCount() / 3, 0);
fs.mkdirSync(out, { recursive: true });
for (const [id, s] of Object.entries(SPECS)) {
  if (only.length && !only.includes(id)) continue;
  const doc = await io.read(path.join(models, id, `${id}.gltf`));
  const root = doc.getRoot();
  for (const n of root.listNodes()) if (n.getMesh() && !s.keep.includes(n.getName())) n.dispose();
  if (s.mats) for (const m of root.listMeshes()) for (const p of m.listPrimitives()) if (!s.mats.includes(p.getMaterial().getName())) p.dispose();
  await doc.transform(prune());
  for (const n of root.listNodes()) clearNodeTransform(n);
  for (const m of root.listMeshes()) for (const p of m.listPrimitives()) for (const sem of p.listSemantics()) {
    if (!["POSITION", "NORMAL", "TEXCOORD_0"].includes(sem)) p.setAttribute(sem, null);
  }
  const before = tris(root);
  await doc.transform(dedup(), join({ keepNamed: false }), weld(), center({ pivot: "below" }));
  await doc.transform(simplify({ simplifier: MeshoptSimplifier, ratio: Math.min(1, s.tris / tris(root)), error: 0.02, lockBorder: false }), prune());
  for (const m of root.listMaterials()) m.setAlphaMode("OPAQUE");
  await doc.transform(textureCompress({ encoder: sharp, targetFormat: "webp", resize: [512, 512], quality: 80 }), prune(), unpartition());
  const b = getBounds(root.listScenes()[0]), to = path.join(out, `${id}.glb`);
  await io.write(to, doc);
  console.log(id, "tris", before, "->", tris(root), "bytes", fs.statSync(to).size,
              "bounds", b.min.map((v) => v.toFixed(2)).join(","), b.max.map((v) => v.toFixed(2)).join(","));
}
