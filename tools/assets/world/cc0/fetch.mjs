// Downloads Poly Haven assets at 1k through its public API and checks every file's md5 against the
// one the API publishes. Usage:
//   node cc0/fetch.mjs textures <dir> <id>...   -> <dir>/<id>_{Diffuse,arm,nor_gl}.jpg
//   node cc0/fetch.mjs hdris    <dir> <id>...   -> <dir>/<id>_1k.hdr
//   node cc0/fetch.mjs models   <dir> <id>...   -> <dir>/<id>/<id>.gltf and everything it includes
import fs from "fs";
import path from "path";
import crypto from "crypto";

const [kind, dir, ...ids] = process.argv.slice(2);
if (!["textures", "hdris", "models"].includes(kind) || !dir || !ids.length) {
  console.error("usage: node cc0/fetch.mjs textures|hdris|models <dir> <id>...");
  process.exit(2);
}

async function get(url, md5, out) {
  const body = Buffer.from(await (await fetch(url)).arrayBuffer());
  if (md5 && crypto.createHash("md5").update(body).digest("hex") !== md5) throw new Error(`md5 mismatch: ${out}`);
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, body);
  return body.length;
}

for (const id of ids) {
  const files = await (await fetch("https://api.polyhaven.com/files/" + id)).json();
  let bytes = 0;
  if (kind === "textures") {
    for (const map of ["Diffuse", "arm", "nor_gl"]) {
      const f = files[map]["1k"].jpg;
      bytes += await get(f.url, f.md5, path.join(dir, `${id}_${map}.jpg`));
    }
  } else if (kind === "hdris") {
    const f = files.hdri["1k"].hdr;
    bytes += await get(f.url, f.md5, path.join(dir, `${id}_1k.hdr`));
  } else {
    const g = files.gltf["1k"].gltf;
    bytes += await get(g.url, g.md5, path.join(dir, id, `${id}.gltf`));
    for (const [rel, f] of Object.entries(g.include || {})) bytes += await get(f.url, f.md5, path.join(dir, id, rel));
  }
  console.log(id, bytes);
}
