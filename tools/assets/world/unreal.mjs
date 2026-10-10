// The --unreal output (Epic 142.2): RawAssets/world's browser-sized files made engine-ready, so the
// content pipeline (Epic 146.5) can import them through Interchange with no hand step.
//
// Interchange decodes neither WebP nor quantised geometry (RawAssets/world/README.md, "Importing
// into Unreal"). For every file under <world-dir>'s import folders:
//   - .glb: textures re-encoded from WebP to PNG (EXT_texture_webp dropped), positions, normals and
//     UVs dequantised to floats (KHR_mesh_quantization dropped), meshopt-compressed buffers
//     decoded; nothing else changes (nodes, skins, morph targets, materials, names);
//   - .webp: written as .png;
//   - .hdr and LICENSE: copied as they are.
// Each folder's LICENSE goes with it, as RawAssets/world/README.md asks.
//
// Usage (from tools/assets/world, after npm ci): node unreal.mjs <world-dir> <out-dir>
// The Content workflow runs it on the runner into Saved/WorldKit; tools/content_pipeline's
// world_kit step imports from there. With the versions pinned in package.json the output is the
// same on every run.
import fs from "fs";
import path from "path";
import { NodeIO } from "@gltf-transform/core";
import { ALL_EXTENSIONS } from "@gltf-transform/extensions";
import { dequantize, textureCompress } from "@gltf-transform/functions";
import { MeshoptDecoder } from "meshoptimizer";
import sharp from "sharp";

// The folders Unreal imports; reference/ is the three.js reference implementation, not assets.
export const FOLDERS = ["cars", "cc0", "office", "people", "trees"];
const DROPPED = ["EXT_texture_webp", "KHR_mesh_quantization", "EXT_meshopt_compression"];

const [world, out] = process.argv.slice(2);
if (!world || !out) {
  console.error("usage: node unreal.mjs <world-dir> <out-dir>");
  process.exit(2);
}
await MeshoptDecoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ "meshopt.decoder": MeshoptDecoder });

for (const folder of FOLDERS) {
  const from = path.join(world, folder);
  const to = path.join(out, folder);
  fs.mkdirSync(to, { recursive: true });
  for (const name of fs.readdirSync(from).sort()) {
    const source = path.join(from, name);
    const ext = path.extname(name).toLowerCase();
    if (ext === ".glb") {
      const doc = await io.read(source);
      await doc.transform(dequantize(), textureCompress({ encoder: sharp, targetFormat: "png" }));
      for (const extension of doc.getRoot().listExtensionsUsed()) {
        if (DROPPED.includes(extension.extensionName)) extension.dispose();
      }
      const left = doc.getRoot().listTextures().filter((t) => t.getMimeType() !== "image/png");
      if (left.length) throw new Error(`${source}: ${left.length} texture(s) still not PNG`);
      await io.write(path.join(to, name), doc);
    } else if (ext === ".webp") {
      await sharp(source).png().toFile(path.join(to, `${path.basename(name, ext)}.png`));
    } else if (ext === ".hdr" || name === "LICENSE") {
      fs.copyFileSync(source, path.join(to, name));
    } else {
      continue;
    }
    console.log(`${folder}/${name}`);
  }
}
