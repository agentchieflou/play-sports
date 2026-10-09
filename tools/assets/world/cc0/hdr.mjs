// Poly Haven skies (1k Radiance .hdr, from fetch.mjs) to the files static/world/cc0/ holds: averaged
// down to 512 x 256 and written as uncompressed RGBE, which world/assets.js reads without decoding the
// run-length encoding. Prints each sky's peak and mean luminance and the round trip's worst error.
// Usage: node cc0/hdr.mjs <src-dir> <out-dir> <id>...   -> <out-dir>/<id>.hdr
import fs from "fs";
import path from "path";

/** Reads a Radiance file, run-length encoded or flat, as linear RGB floats. */
export function read(file) {
  const b = fs.readFileSync(file);
  let i = 0, line = "", m = null;
  while (!m) {
    const c = b[i++];
    if (c !== 10) { line += String.fromCharCode(c); continue; }
    m = line.match(/^-Y (\d+) \+X (\d+)$/);
    line = "";
  }
  const H = +m[1], W = +m[2], out = new Float32Array(W * H * 3), scan = new Uint8Array(W * 4);
  for (let y = 0; y < H; y++) {
    if (b[i] === 2 && b[i + 1] === 2 && !(b[i + 2] & 0x80)) {
      i += 4;
      for (let ch = 0; ch < 4; ch++) {
        let x = 0;
        while (x < W) {
          let n = b[i++];
          if (n > 128) { n -= 128; const v = b[i++]; while (n--) scan[(x++) * 4 + ch] = v; }
          else { while (n--) scan[(x++) * 4 + ch] = b[i++]; }
        }
      }
    } else {
      for (let x = 0; x < W * 4; x++) scan[x] = b[i++];
    }
    for (let x = 0; x < W; x++) {
      const e = scan[x * 4 + 3], f = e ? Math.pow(2, e - 136) : 0;
      for (let c = 0; c < 3; c++) out[(y * W + x) * 3 + c] = e ? (scan[x * 4 + c] + 0.5) * f : 0;
    }
  }
  return { W, H, data: out };
}

/** Box-averages by an integer factor. */
export function down(img, k) {
  const W = img.W / k, H = img.H / k, out = new Float32Array(W * H * 3);
  for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) for (let c = 0; c < 3; c++) {
    let s = 0;
    for (let j = 0; j < k; j++) for (let i = 0; i < k; i++) s += img.data[((y * k + j) * img.W + x * k + i) * 3 + c];
    out[(y * W + x) * 3 + c] = s / (k * k);
  }
  return { W, H, data: out };
}

/** Writes flat (not run-length encoded) RGBE: a short header, then four bytes a pixel. */
export function write(img, file, note) {
  const head = Buffer.from(`#?RADIANCE\n# ${note}\nFORMAT=32-bit_rle_rgbe\n\n-Y ${img.H} +X ${img.W}\n`, "latin1");
  const px = Buffer.alloc(img.W * img.H * 4);
  for (let p = 0; p < img.W * img.H; p++) {
    const r = img.data[p * 3], g = img.data[p * 3 + 1], bl = img.data[p * 3 + 2], v = Math.max(r, g, bl);
    if (v < 1e-32) continue;
    let s = 256 / Math.pow(2, Math.ceil(Math.log2(v) + 1e-9));
    if (v * s >= 256) s /= 2;
    const E = Math.round(Math.log2(256 / s)), sc = 256 / Math.pow(2, E);
    px[p * 4] = Math.min(255, Math.floor(r * sc));
    px[p * 4 + 1] = Math.min(255, Math.floor(g * sc));
    px[p * 4 + 2] = Math.min(255, Math.floor(bl * sc));
    px[p * 4 + 3] = E + 128;
  }
  fs.writeFileSync(file, Buffer.concat([head, px]));
}

const [src, out, ...ids] = process.argv.slice(2);
if (import.meta.url === `file://${process.argv[1]}`) {
  if (!src || !out || !ids.length) {
    console.error("usage: node cc0/hdr.mjs <src-dir> <out-dir> <id>...");
    process.exit(2);
  }
  fs.mkdirSync(out, { recursive: true });
  for (const id of ids) {
    const img = read(path.join(src, `${id}_1k.hdr`));
    let peak = 0, sum = 0;
    for (let p = 0; p < img.W * img.H; p++) {
      const l = 0.2126 * img.data[p * 3] + 0.7152 * img.data[p * 3 + 1] + 0.0722 * img.data[p * 3 + 2];
      peak = Math.max(peak, l);
      sum += l;
    }
    const small = down(img, 2), to = path.join(out, `${id}.hdr`);
    write(small, to, `${id}, Poly Haven, CC0`);
    console.log(id, "peak", peak.toFixed(2), "mean", (sum / img.W / img.H).toFixed(4), "bytes", fs.statSync(to).size);
  }
}
