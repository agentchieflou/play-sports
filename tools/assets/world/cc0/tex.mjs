// Poly Haven texture maps (from fetch.mjs) to the WebP files static/world/cc0/ holds: colour and
// normal maps at 1024 px, the ambient-occlusion/roughness/metalness map at 512 px.
// Usage: node cc0/tex.mjs <src-dir> <out-dir> <id>...
import sharp from "sharp";
import fs from "fs";
import path from "path";

const [src, out, ...ids] = process.argv.slice(2);
if (!src || !out || !ids.length) {
  console.error("usage: node cc0/tex.mjs <src-dir> <out-dir> <id>...");
  process.exit(2);
}
fs.mkdirSync(out, { recursive: true });
for (const id of ids) {
  const at = (name) => path.join(src, `${id}_${name}.jpg`), to = (name) => path.join(out, `${id}_${name}.webp`);
  await sharp(at("Diffuse")).resize(1024, 1024).webp({ quality: 76, effort: 6 }).toFile(to("diff"));
  await sharp(at("nor_gl")).resize(1024, 1024).webp({ quality: 78, effort: 6 }).toFile(to("nor"));
  await sharp(at("arm")).resize(512, 512, { kernel: "cubic" }).webp({ quality: 82, effort: 6 }).toFile(to("arm"));
  const sizes = ["diff", "nor", "arm"].map((k) => fs.statSync(to(k)).size);
  console.log(id, sizes.join(" "), sizes.reduce((a, b) => a + b));
}
