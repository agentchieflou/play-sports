"use strict";

var WorldCity = (function () {
  var K = 3;
  var PITCH = 64;
  var AVE = { road: 8, walk: 5 };
  var ST = { road: 5.5, walk: 4 };
  var RING = 12;
  var LIMIT = 150;
  var FACADE = {
    brick: ["#ffffff", "#e8d8cc", "#d9c0b0", "#f2e6dc", "#c9b2a0"],
    stucco: ["#e9dcc4", "#d7c7a8", "#c8d2c9", "#e3c9b8", "#bfc9d4", "#e6e1d6", "#d8b9a0"],
    concrete: ["#d6d4d0", "#c4c2bd", "#e0ddd6", "#b8b6b2"],
    stone: ["#e8e0d2", "#d6cdbd", "#c9c0b0"],
    glass: ["#9aa6b0", "#7e8c96", "#a9b4b8"]
  };
  var FRAMES = ["#1b1e21", "#e9e6df", "#2e3a33", "#3a2a22", "#6b6f72"];
  var NEON = [[1, 0.15, 0.55], [0.1, 0.85, 1], [1, 0.35, 0.1], [0.55, 1, 0.3], [1, 0.85, 0.5], [0.6, 0.35, 1], [1, 0.1, 0.12]];
  var rnd = WorldKit.rnd;
  var M = { T: null, lib: null, mats: null, group: null, P: -1, lod: 1 };

  /** @param {number} k @returns {{road: number, walk: number}} */
  function half(k) { return k === 0 ? AVE : ST; }

  /** @param {number} es @returns {{p: Array<number>, n: Array<number>, u: Array<number>, c: Array<number>, e: Array<number>, es: number}} */
  function bag(es) { return { p: [], n: [], u: [], c: [], e: [], es: es || 0 }; }

  /** @param {Object} b @param {Array<Array<number>>} P @param {Array<number>} N @param {Array<Array<number>>} U @param {Array<number>} c @param {Array<Array<number>>} [E] */
  function quad(b, P, N, U, c, E) {
    var o = [0, 1, 2, 0, 2, 3];
    for (var i = 0; i < 6; i++) {
      var k = o[i];
      b.p.push(P[k][0], P[k][1], P[k][2]);
      b.n.push(N[0], N[1], N[2]);
      b.u.push(U[k][0], U[k][1]);
      b.c.push(c[0], c[1], c[2]);
      if (b.es) for (var j = 0; j < b.es; j++) b.e.push(E ? E[k][j] : 0);
    }
  }

  /** @param {Object} b @param {number} x0 @param {number} x1 @param {number} y0 @param {number} y1 @param {number} z0 @param {number} z1 @param {Array<number>} c @param {number} [skip] */
  function box(b, x0, x1, y0, y1, z0, z1, c, skip) {
    var s = skip || 0;
    if (!(s & 1)) quad(b, [[x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1]], [1, 0, 0], [[-z1, y0], [-z0, y0], [-z0, y1], [-z1, y1]], c);
    if (!(s & 2)) quad(b, [[x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0]], [-1, 0, 0], [[z0, y0], [z1, y0], [z1, y1], [z0, y1]], c);
    if (!(s & 4)) quad(b, [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]], [0, 0, 1], [[x0, y0], [x1, y0], [x1, y1], [x0, y1]], c);
    if (!(s & 8)) quad(b, [[x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0]], [0, 0, -1], [[-x1, y0], [-x0, y0], [-x0, y1], [-x1, y1]], c);
    if (!(s & 16)) quad(b, [[x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0]], [0, 1, 0], [[x0, -z1], [x1, -z1], [x1, -z0], [x0, -z0]], c);
    if (!(s & 32)) quad(b, [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]], [0, -1, 0], [[x0, z0], [x1, z0], [x1, z1], [x0, z1]], c);
  }

  /** @param {Object} F @param {number} a @param {number} y @param {number} d @returns {Array<number>} */
  function at(F, a, y, d) {
    return [F.ox + F.tx * a + F.nx * d, y, F.oz + F.tz * a + F.nz * d];
  }

  /** @param {Object} b @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 @param {number} y1 @param {number} d @param {Array<number>} c @param {Array<Array<number>>} [E] */
  function face(b, F, a0, a1, y0, y1, d, c, E) {
    quad(b, [at(F, a0, y0, d), at(F, a1, y0, d), at(F, a1, y1, d), at(F, a0, y1, d)], [F.nx, 0, F.nz],
      [[F.u0 + a0, y0], [F.u0 + a1, y0], [F.u0 + a1, y1], [F.u0 + a0, y1]], c, E);
  }

  /** @param {Object} b @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 @param {number} y1 @param {number} d0 @param {number} d1 @param {Array<number>} c @param {number} [flags] */
  function fbox(b, F, a0, a1, y0, y1, d0, d1, c, flags) {
    var p = at(F, a0, 0, d0), q = at(F, a1, 0, d1), f = flags || 0, skip = 0;
    if (f & 1) skip |= F.nz > 0 ? 8 : F.nz < 0 ? 4 : F.nx > 0 ? 2 : 1;
    if (f & 2) skip |= 48;
    if (f & 4) skip |= F.nz !== 0 ? 3 : 12;
    box(b, Math.min(p[0], q[0]), Math.max(p[0], q[0]), y0, y1, Math.min(p[2], q[2]), Math.max(p[2], q[2]), c, skip);
  }

  /** @param {Object} b @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 @param {number} y1 @param {number} r @param {Array<number>} c */
  function reveal(b, F, a0, a1, y0, y1, r, c) {
    var t = [F.tx, 0, F.tz];
    quad(b, [at(F, a0, y0, 0), at(F, a0, y0, -r), at(F, a0, y1, -r), at(F, a0, y1, 0)], t, [[0, y0], [r, y0], [r, y1], [0, y1]], c);
    quad(b, [at(F, a1, y0, -r), at(F, a1, y0, 0), at(F, a1, y1, 0), at(F, a1, y1, -r)], [-t[0], 0, -t[2]], [[0, y0], [r, y0], [r, y1], [0, y1]], c);
    quad(b, [at(F, a0, y1, 0), at(F, a0, y1, -r), at(F, a1, y1, -r), at(F, a1, y1, 0)], [0, -1, 0], [[a0, 0], [a0, r], [a1, r], [a1, 0]], c);
    quad(b, [at(F, a0, y0, 0), at(F, a1, y0, 0), at(F, a1, y0, -r), at(F, a0, y0, -r)], [0, 1, 0], [[a0, 0], [a1, 0], [a1, r], [a0, r]], c);
  }

  /** @param {string} hex @param {number} [k] @returns {Array<number>} */
  function col(hex, k) {
    var c = new M.T.Color(hex), m = k == null ? 1 : k;
    return [c.r * m, c.g * m, c.b * m];
  }

  /** @param {Object} B @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 @param {number} y1 @param {number} seed @param {number} kind @param {Array<number>} dim @param {Array<number>} frame @param {number} d */
  function glass(B, F, a0, a1, y0, y1, seed, kind, dim, frame, d) {
    var w = a1 - a0, h = y1 - y0, k = kind + Math.min(h, 9.9) * 0.01;
    var E = [[0, 0, seed, k, dim[0], dim[1], dim[2], dim[3]], [w, 0, seed, k, dim[0], dim[1], dim[2], dim[3]],
             [w, h, seed, k, dim[0], dim[1], dim[2], dim[3]], [0, h, seed, k, dim[0], dim[1], dim[2], dim[3]]];
    face(B.glass, F, a0, a1, y0, y1, d, frame, E);
  }

  /** @param {Object} B @param {Object} F @param {number} a0 @param {number} a1 @param {number} y @param {number} h @param {number} seed @param {number} type @param {Array<number>} rgb @param {number} d */
  function sign(B, F, a0, a1, y, h, seed, type, rgb, d) {
    var chars = Math.max(1, Math.round((a1 - a0) / (h * 0.75)));
    var E = [[0, 0, seed, type], [chars, 0, seed, type], [chars, 1, seed, type], [0, 1, seed, type]];
    face(B.signs, F, a0, a1, y, y + h, d, rgb, E);
  }

  /** @param {Object} B @param {Object} F @param {number} a @param {number} y @param {number} h @param {number} seed @param {Array<number>} rgb @param {Array<Object>} lights */
  function blade(B, F, a, y, h, seed, rgb, lights) {
    var w = 0.85, s = 0.08, d0 = 0.05;
    fbox(B.metal, F, a - s, a + s, y - 0.08, y + h + 0.08, d0, d0 + w, [0.08, 0.08, 0.09]);
    var chars = Math.max(2, Math.round(h / 0.55));
    var p = at(F, a + s + 0.005, 0, d0 + w), q = at(F, a - s - 0.005, 0, d0);
    var FA = { ox: p[0], oz: p[2], tx: -F.nx, tz: -F.nz, nx: F.tx, nz: F.tz, u0: 0 };
    var FB = { ox: q[0], oz: q[2], tx: F.nx, tz: F.nz, nx: -F.tx, nz: -F.tz, u0: 0 };
    [FA, FB].forEach(function (G) {
      quad(B.signs, [at(G, 0, y, 0), at(G, w, y, 0), at(G, w, y + h, 0), at(G, 0, y + h, 0)], [G.nx, 0, G.nz],
        [[0, 0], [0, 0], [0, 0], [0, 0]], rgb, [[0, 0, seed, 3], [1, 0, seed, 3], [1, chars, seed, 3], [0, chars, seed, 3]]);
    });
    var c = at(F, a, y + h / 2, 0.6);
    lights.push({ p: [c[0], c[1], c[2]], c: [rgb[0] * 9, rgb[1] * 9, rgb[2] * 9], r: 9, f: rnd(seed * 3.1) > 0.8 ? 1 : 0 });
  }

  /** @param {Object} B @param {Object} bd @param {Object} F @param {number} W @param {number} side @param {Array<Object>} lights */
  function facade(B, bd, F, W, side, lights) {
    var st = bd.style, wallBag = B[st === "glass" ? "stone" : st], c = bd.tint, frame = bd.frame, s = bd.seed + side * 0.173;
    var g = bd.ground, fh = bd.fh, n = bd.floors, top = g + n * fh, front = side === bd.front;
    var simple = side === (bd.front + 2) % 4 || bd.near > (M.lod ? 105 : 0);
    var stoneC = col("#d9d2c4", 0.95);
    var shade = function (/** @type {number} */ y) { var k = 0.78 + 0.22 * Math.min(1, y / 3.5); return [c[0] * k, c[1] * k, c[2] * k]; };
    if (bd.party[side]) {
      face(wallBag, F, 0, W, 0, top, 0, shade(top * 0.5));
      if (st !== "concrete" && st !== "glass") fbox(B.stone, F, -0.25, W + 0.25, top, top + 0.45, -0.02, 0.38, stoneC, 5);
      return;
    }
    if (st === "glass") {
      if (!(bd.shop && front)) face(wallBag, F, 0, W, 0, g, 0, shade(1));
      var cols = Math.max(1, Math.round(W / 3));
      glass(B, F, 0.35, W - 0.35, g, top, s, 2, [(W - 0.7) / cols, fh, 0, 0], col("#2a3036"), 0);
      fbox(B.metal, F, -0.05, 0.35, g, top + 0.6, -0.05, 0.25, [0.12, 0.13, 0.14]);
      fbox(B.metal, F, W - 0.35, W + 0.05, g, top + 0.6, -0.05, 0.25, [0.12, 0.13, 0.14]);
      shopfront(B, bd, F, W, s, front, lights);
      return;
    }
    var bayW = st === "concrete" ? 3.6 : st === "stone" ? 2.4 : 3.0;
    var margin = 0.9, nb = Math.max(1, Math.floor((W - margin * 2) / bayW)), bay = (W - margin * 2) / nb;
    var ratio = st === "concrete" ? 0.84 : st === "stone" ? 0.46 : 0.52, ww = bay * ratio;
    var wh = st === "concrete" ? fh * 0.52 : fh * 0.58, sill = st === "concrete" ? 1.0 : 0.85, r = st === "concrete" ? 0.12 : 0.22;
    shopfront(B, bd, F, W, s, front, lights);
    if (!M.lod) {
      glass(B, F, margin, W - margin, g, top, s, 3, [bay, fh, ww, wh], shade(top), 0.005);
      face(wallBag, F, 0, margin, g, top, 0, shade(g));
      face(wallBag, F, W - margin, W, g, top, 0, shade(g));
      if (st !== "concrete") fbox(B.stone, F, -0.25, W + 0.25, top, top + 0.45, -0.02, 0.38, stoneC, 5);
      return;
    }
    for (var f = 0; f < n; f++) {
      var y0 = g + f * fh, ys = y0 + sill, yh = ys + wh, y1 = y0 + fh;
      face(wallBag, F, 0, W, y0, ys, 0, shade(y0));
      face(wallBag, F, 0, W, yh, y1, 0, shade(yh));
      var a = 0;
      for (var i = 0; i < nb; i++) {
        var a0 = margin + i * bay + (bay - ww) / 2, a1 = a0 + ww;
        face(wallBag, F, a, a0, ys, yh, 0, shade(ys));
        if (simple) glass(B, F, a0, a1, ys, yh, s + f * 0.031 + i * 0.0137, 0, [ww, (bay - ww) / 2, sill, fh], frame, 0.01);
        else {
          reveal(wallBag, F, a0, a1, ys, yh, r, shade(ys).map(function (v) { return v * 0.85; }));
          glass(B, F, a0, a1, ys, yh, s + f * 0.031 + i * 0.0137, 0, [ww, (bay - ww) / 2, sill, fh], frame, -r);
        }
        if (simple) { a = a1; continue; }
        if (st === "brick" || st === "stone") {
          fbox(B.stone, F, a0 - 0.08, a1 + 0.08, ys - 0.1, ys, -0.02, 0.08, stoneC, 5);
          if (st === "brick") fbox(B.stone, F, a0 - 0.1, a1 + 0.1, yh, yh + 0.22, -0.02, 0.05, stoneC, 7);
        } else if (st === "stucco") {
          if (f > 0 && rnd(s * 31 + f * 7 + i) > 0.55 && bd.near < 90 && M.lod) balcony(B, F, a0 - 0.25, a1 + 0.25, y0);
          else fbox(B.stone, F, a0 - 0.06, a1 + 0.06, ys - 0.07, ys, -0.02, 0.06, stoneC, 5);
        }
        a = a1;
      }
      face(wallBag, F, a, W, ys, yh, 0, shade(ys));
      if (st === "concrete") fbox(B.concrete, F, -0.02, W + 0.02, y0 - 0.12, y0 + 0.08, -0.02, 0.12, shade(y0), 1);
      if (st === "stone" && f % 4 === 0) fbox(B.stone, F, -0.02, W + 0.02, y0 - 0.1, y0 + 0.12, -0.02, 0.1, stoneC, 1);
    }
    if (st !== "concrete") {
      fbox(B.stone, F, -0.25, W + 0.25, top, top + 0.45, -0.02, 0.38, stoneC, 1);
      if (!simple) fbox(B.stone, F, -0.15, W + 0.15, top - 0.25, top, -0.02, 0.18, stoneC, 5);
    }
    if (st === "brick" && front && bd.escape && bd.near < 100 && M.lod) escape(B, F, W, g, fh, n);
  }

  /** @param {Object} B @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 */
  function balcony(B, F, a0, a1, y0) {
    var iron = [0.07, 0.075, 0.08];
    fbox(B.stone, F, a0, a1, y0 - 0.12, y0 + 0.02, 0, 1.1, col("#cfc8bb"), 1);
    fbox(B.metal, F, a0, a1, y0 + 0.95, y0 + 1.0, 1.04, 1.1, iron, 1);
    fbox(B.metal, F, a0, a1, y0 + 0.12, y0 + 0.16, 1.04, 1.1, iron, 3);
    bars(B.metal, F, a0 + 0.05, a1 - 0.05, y0 + 0.02, y0 + 0.95, 1.07, 0.16, iron);
  }

  /** @param {Object} b @param {Object} F @param {number} a0 @param {number} a1 @param {number} y0 @param {number} y1 @param {number} d @param {number} step @param {Array<number>} c */
  function bars(b, F, a0, a1, y0, y1, d, step, c) {
    var B2 = { ox: F.ox, oz: F.oz, tx: -F.tx, tz: -F.tz, nx: -F.nx, nz: -F.nz, u0: 0 };
    for (var a = a0; a <= a1 + 1e-3; a += step) {
      face(b, F, a - 0.012, a + 0.012, y0, y1, d, c);
      face(b, B2, -a - 0.012, -a + 0.012, y0, y1, -d, c);
    }
  }

  /** @param {Object} B @param {Object} F @param {number} W @param {number} g @param {number} fh @param {number} n */
  function escape(B, F, W, g, fh, n) {
    var iron = [0.06, 0.065, 0.07], a0 = W * 0.3, a1 = Math.min(W - 0.6, a0 + 4.2), d = 1.2;
    for (var f = 1; f <= n; f++) {
      var y = g + (f - 1) * fh + 0.02;
      fbox(B.metal, F, a0, a1, y - 0.06, y, 0, d, iron, 1);
      fbox(B.metal, F, a0, a1, y + 0.92, y + 0.97, d - 0.04, d, iron, 1);
      fbox(B.metal, F, a0, a0 + 0.04, y, y + 0.97, 0, d, iron, 1);
      fbox(B.metal, F, a1 - 0.04, a1, y, y + 0.97, 0, d, iron, 1);
      bars(B.metal, F, a0 + 0.2, a1 - 0.1, y, y + 0.93, d - 0.02, 0.3, iron);
      if (f < n) {
        var steps = 9;
        for (var k = 0; k < steps; k++) {
          var t = k / steps, sa = a0 + 0.4 + t * (a1 - a0 - 1.2), sy = y + t * fh;
          fbox(B.metal, F, sa, sa + 0.32, sy + 0.1, sy + 0.14, 0.15, 0.85, iron, 7);
        }
      }
    }
  }

  /** @param {Object} B @param {Object} bd @param {Object} F @param {number} W @param {number} s @param {boolean} front @param {Array<Object>} lights */
  function shopfront(B, bd, F, W, s, front, lights) {
    var g = bd.ground, st = bd.style, wallBag = B[st === "glass" ? "stone" : st], c = bd.tint;
    var base = [c[0] * 0.8, c[1] * 0.8, c[2] * 0.8];
    if (!bd.shop || !front) {
      if (st !== "glass") face(wallBag, F, 0, W, 0, g, 0, base);
      return;
    }
    var bays = Math.max(1, Math.round(W / 5)), bw = W / bays, fascia = g - 0.95;
    face(wallBag, F, 0, W, fascia + 0.75, g, 0, base);
    face(B.stone, F, 0, W, 0, 0.45, 0.02, col("#5a5550"));
    for (var i = 0; i < bays; i++) {
      var a0 = i * bw + 0.25, a1 = (i + 1) * bw - 0.25, seed = s + i * 0.19;
      face(wallBag, F, i * bw, a0, 0.45, fascia + 0.75, 0, base);
      face(wallBag, F, a1, (i + 1) * bw, 0.45, fascia + 0.75, 0, base);
      reveal(B.metal, F, a0, a1, 0.45, fascia, 0.3, [0.1, 0.1, 0.11]);
      glass(B, F, a0, a1, 0.45, fascia, seed, 1, [a1 - a0, 0, 0.45, 3.65], [0.12, 0.12, 0.13], -0.3);
      var neon = NEON[Math.floor(rnd(seed * 7) * NEON.length)];
      var type = rnd(seed * 11) > 0.5 ? 0 : 1;
      sign(B, F, a0, a1, fascia + 0.05, 0.62, seed, type, neon, 0.09);
      fbox(B.metal, F, a0 - 0.05, a1 + 0.05, fascia, fascia + 0.75, 0, 0.08, [0.05, 0.05, 0.055], 1);
      if (rnd(seed * 5.3) > 0.55) awning(B, F, a0 - 0.1, a1 + 0.1, fascia - 0.05, seed);
      var p = at(F, (a0 + a1) / 2, 2.4, 1.4);
      lights.push({ p: p, c: [5 + neon[0] * 3, 4 + neon[1] * 3, 3 + neon[2] * 3], r: 8, f: 0 });
    }
    if (rnd(s * 17) > 0.35 && bd.floors > 2) {
      var neon2 = NEON[Math.floor(rnd(s * 23) * NEON.length)];
      blade(B, F, bays > 1 ? bw : W * 0.5, g + 0.6, Math.min(4.2, bd.fh * 1.6), s + 0.5, neon2, lights);
    }
  }

  /** @param {Object} B @param {Object} F @param {number} a0 @param {number} a1 @param {number} y @param {number} seed */
  function awning(B, F, a0, a1, y, seed) {
    var cols = [["#7a1f22", "#e8e0d0"], ["#1f4a3a", "#e8e0d0"], ["#1d2f5a", "#d9d2c4"], ["#3a3a3a", "#c8a050"]][Math.floor(rnd(seed * 3) * 4)];
    var d = 1.4, drop = 0.55, n = Math.max(2, Math.round((a1 - a0) / 0.35));
    for (var i = 0; i < n; i++) {
      var s0 = a0 + (a1 - a0) * i / n, s1 = a0 + (a1 - a0) * (i + 1) / n, cc = col(cols[i % 2]);
      var p0 = at(F, s0, y, 0), p1 = at(F, s1, y, 0), p2 = at(F, s1, y - drop, d), p3 = at(F, s0, y - drop, d);
      var nn = [F.nx * 0.37, 0.93, F.nz * 0.37];
      quad(B.cloth, [p3, p2, p1, p0], nn, [[s0, d], [s1, d], [s1, 0], [s0, 0]], cc);
      quad(B.cloth, [p0, p1, p2, p3], [-nn[0], -nn[1], -nn[2]], [[s0, 0], [s1, 0], [s1, d], [s0, d]], cc.map(function (v) { return v * 0.6; }));
      face(B.cloth, { ox: F.ox + F.nx * d, oz: F.oz + F.nz * d, tx: F.tx, tz: F.tz, nx: F.nx, nz: F.nz, u0: 0 }, s0, s1, y - drop - 0.25, y - drop, 0, cc);
    }
  }

  /** @param {Object} B @param {Object} bd */
  function roof(B, bd) {
    var x0 = bd.x0, x1 = bd.x1, z0 = bd.z0, z1 = bd.z1, top = bd.ground + bd.floors * bd.fh, s = bd.seed;
    var wall = B[bd.style === "glass" ? "stone" : bd.style], c = bd.tint, t = 0.3, hp = bd.style === "glass" ? 1.2 : 0.95;
    quad(B.roof, [[x0, top, z1], [x1, top, z1], [x1, top, z0], [x0, top, z0]], [0, 1, 0], [[x0, -z1], [x1, -z1], [x1, -z0], [x0, -z0]], [1, 1, 1]);
    box(wall, x0, x1, top, top + hp, z1 - t, z1, c, 32);
    box(wall, x0, x1, top, top + hp, z0, z0 + t, c, 32);
    box(wall, x0, x0 + t, top, top + hp, z0 + t, z1 - t, c, 32 | 4 | 8);
    box(wall, x1 - t, x1, top, top + hp, z0 + t, z1 - t, c, 32 | 4 | 8);
    var w = x1 - x0, d = z1 - z0, metal = [0.55, 0.56, 0.57];
    if (rnd(s * 3) > 0.3) box(wall, x0 + w * 0.6, x0 + w * 0.6 + 3, top, top + 2.8, z0 + d * 0.2, z0 + d * 0.2 + 3.2, c, 32);
    var units = 1 + Math.floor(rnd(s * 5) * 4);
    for (var i = 0; i < units; i++) {
      var ux = x0 + 1.5 + rnd(s * 7 + i) * (w - 4), uz = z0 + 1.5 + rnd(s * 11 + i) * (d - 4);
      box(B.metal, ux, ux + 1.6, top, top + 1.1, uz, uz + 1.2, metal, 32);
      box(B.metal, ux + 0.3, ux + 1.3, top + 1.1, top + 1.16, uz + 0.1, uz + 1.1, [0.15, 0.15, 0.16], 32);
    }
    if (bd.style === "brick" && rnd(s * 13) > 0.4) tank(B, x0 + w * 0.25, z0 + d * 0.65, top);
    if (rnd(s * 19) > 0.6) {
      var ax = x0 + w * 0.8, az = z0 + d * 0.3, ah = 3 + rnd(s * 29) * 5;
      box(B.metal, ax, ax + 0.08, top, top + ah, az, az + 0.08, [0.2, 0.2, 0.22], 32);
      box(B.metal, ax - 0.5, ax + 0.58, top + ah * 0.8, top + ah * 0.8 + 0.05, az, az + 0.08, [0.2, 0.2, 0.22], 32);
    }
  }

  /** @param {Object} B @param {number} x @param {number} z @param {number} y */
  function tank(B, x, z, y) {
    var T = M.T, wood = [0.42, 0.3, 0.22];
    for (var i = 0; i < 4; i++) {
      var lx = x + (i % 2 ? 1.1 : -1.1), lz = z + (i < 2 ? 1.1 : -1.1);
      box(B.metal, lx - 0.08, lx + 0.08, y, y + 2.6, lz - 0.08, lz + 0.08, [0.1, 0.1, 0.1], 32);
    }
    box(B.metal, x - 1.4, x + 1.4, y + 2.6, y + 2.75, z - 1.4, z + 1.4, [0.1, 0.1, 0.1]);
    var cyl = new T.CylinderGeometry(1.45, 1.45, 3.2, 14, 1, true).toNonIndexed(), cone = new T.ConeGeometry(1.6, 1.0, 14, 1, true).toNonIndexed();
    [[cyl, y + 4.35, wood], [cone, y + 6.45, [0.18, 0.15, 0.13]]].forEach(function (g) {
      var geo = g[0], pa = geo.attributes.position.array, na = geo.attributes.normal.array, ua = geo.attributes.uv.array;
      for (var k = 0; k < pa.length / 3; k++) {
        B.wood.p.push(pa[k * 3] + x, pa[k * 3 + 1] + g[1], pa[k * 3 + 2] + z);
        B.wood.n.push(na[k * 3], na[k * 3 + 1], na[k * 3 + 2]);
        B.wood.u.push(ua[k * 2] * 9, ua[k * 2 + 1] * 3.2);
        B.wood.c.push(g[2][0], g[2][1], g[2][2]);
      }
      geo.dispose();
    });
  }

  /** @param {Object} B @param {Object} bd @param {Array<Object>} lights */
  function building(B, bd, lights) {
    var faces = [
      { ox: bd.x0, oz: bd.z1, tx: 1, tz: 0, nx: 0, nz: 1, w: bd.x1 - bd.x0 },
      { ox: bd.x1, oz: bd.z1, tx: 0, tz: -1, nx: 1, nz: 0, w: bd.z1 - bd.z0 },
      { ox: bd.x1, oz: bd.z0, tx: -1, tz: 0, nx: 0, nz: -1, w: bd.x1 - bd.x0 },
      { ox: bd.x0, oz: bd.z0, tx: 0, tz: 1, nx: -1, nz: 0, w: bd.z1 - bd.z0 }
    ];
    var u0 = 0;
    faces.forEach(function (F, side) {
      F.u0 = u0;
      u0 += F.w;
      facade(B, bd, F, F.w, side, lights);
    });
    roof(B, bd);
    if (bd.board) billboard(B, bd, faces[bd.front], lights);
  }

  /** @param {Object} B @param {Object} bd @param {Object} F @param {Array<Object>} lights */
  function billboard(B, bd, F, lights) {
    var top = bd.ground + bd.floors * bd.fh + 1.2, W = Math.min(F.w - 2, 11), a0 = (F.w - W) / 2, h = W * 0.42;
    var iron = [0.12, 0.12, 0.13];
    [a0 + 0.8, a0 + W - 0.8].forEach(function (a) { fbox(B.metal, F, a - 0.1, a + 0.1, top - 1.2, top + 1.2, -2.0, -1.8, iron); });
    fbox(B.metal, F, a0, a0 + W, top + 1.2, top + 1.35, -2.1, -1.7, iron);
    fbox(B.metal, F, a0 - 0.1, a0 + W + 0.1, top + 1.3, top + 1.4 + h, -2.0, -1.85, iron);
    var neon = NEON[Math.floor(rnd(bd.seed * 41) * NEON.length)];
    var E = [[0, 0, bd.seed, 2], [W / h * 1.6, 0, bd.seed, 2], [W / h * 1.6, 1, bd.seed, 2], [0, 1, bd.seed, 2]];
    face(B.signs, F, a0, a0 + W, top + 1.4, top + 1.4 + h, -1.83, neon, E);
    var p = at(F, a0 + W / 2, top + 1.4 + h / 2, 3);
    lights.push({ p: p, c: [neon[0] * 30, neon[1] * 30, neon[2] * 30], r: 18, f: 0 });
  }

  /** @param {number} P @returns {Array<Object>} */
  function plan(P) {
    var out = [], clear = P + RING + 3, k = M.lod ? K : K - 1;
    for (var i = -k; i < k; i++) {
      for (var j = -k; j < k; j++) {
        var hx0 = half(i), hx1 = half(i + 1), hz0 = half(j), hz1 = half(j + 1);
        var bx0 = i * PITCH + hx0.road + hx0.walk, bx1 = (i + 1) * PITCH - hx1.road - hx1.walk;
        var bz0 = j * PITCH + hz0.road + hz0.walk, bz1 = (j + 1) * PITCH - hz1.road - hz1.walk;
        var seed = rnd(i * 31.7 + j * 7.3 + 100);
        lots(out, bx0, bx1, bz0, bz1, seed, clear);
      }
    }
    return out;
  }

  /** @param {Array<Object>} out @param {number} x0 @param {number} x1 @param {number} z0 @param {number} z1 @param {number} seed @param {number} clear */
  function lots(out, x0, x1, z0, z1, seed, clear) {
    var depth = Math.min(19, (Math.min(x1 - x0, z1 - z0)) / 2 - 1), s = seed * 1000;
    var add = function (/** @type {number} */ a0, /** @type {number} */ a1, /** @type {number} */ b0, /** @type {number} */ b1, /** @type {number} */ front, /** @type {number} */ k) {
      var nx = Math.max(a0, Math.min(0, a1)), nz = Math.max(b0, Math.min(0, b1));
      var near = Math.hypot(nx, nz);
      if (near < clear) return;
      var h = rnd(s + k * 3.1), far = Math.min(1, Math.max(0, (near - clear) / 120));
      var style = h < 0.3 ? "brick" : h < 0.52 ? "stucco" : h < 0.72 ? "concrete" : h < 0.86 ? "stone" : "glass";
      if (near < clear + 25 && style === "glass") style = "stone";
      var floors = style === "brick" ? 3 + Math.floor(rnd(s + k * 5.7) * 4) : style === "stucco" ? 3 + Math.floor(rnd(s + k * 5.7) * 3)
        : style === "concrete" ? 5 + Math.floor(rnd(s + k * 5.7) * 7) : style === "stone" ? 6 + Math.floor(rnd(s + k * 5.7) * 9)
          : 12 + Math.floor(rnd(s + k * 5.7) * 22);
      floors = Math.round(floors * (0.85 + far * 0.6));
      var pal = FACADE[style];
      out.push({
        x0: a0, x1: a1, z0: b0, z1: b1, front: front, style: style, floors: floors,
        fh: style === "concrete" || style === "glass" ? 3.4 : 3.1, ground: style === "stucco" ? 3.8 : 4.4,
        tint: col(pal[Math.floor(rnd(s + k * 9.1) * pal.length)]), frame: col(FRAMES[Math.floor(rnd(s + k * 2.3) * FRAMES.length)]),
        seed: rnd(s + k * 13.3) * 100, shop: rnd(s + k * 4.4) > 0.2, escape: rnd(s + k * 6.6) > 0.45,
        board: style !== "glass" && floors < 9 && rnd(s + k * 8.8) > 0.82, near: near
      });
    };
    var k = 0;
    var row = function (/** @type {number} */ from, /** @type {number} */ to, /** @type {function(number, number): void} */ fn) {
      var a = from;
      while (to - a > 6) {
        var w = 8 + rnd(s + k * 1.7) * 12;
        if (to - (a + w) < 7) w = to - a;
        fn(a, a + w);
        a += w;
        k += 1;
      }
    };
    row(x0, x1, function (a, b) { add(a, b, z1 - depth, z1, 0, k); });
    row(x0, x1, function (a, b) { add(a, b, z0, z0 + depth, 2, k); });
    row(z0 + depth, z1 - depth, function (a, b) { add(x1 - depth, x1, a, b, 1, k); });
    row(z0 + depth, z1 - depth, function (a, b) { add(x0, x0 + depth, a, b, 3, k); });
  }

  /** @param {Array<Object>} plots */
  function party(plots) {
    plots.forEach(function (a) {
      a.party = [false, false, false, false];
      var top = a.ground + a.floors * a.fh;
      plots.forEach(function (b) {
        if (a === b) return;
        var bt = b.ground + b.floors * b.fh;
        if (bt < top - 0.5) return;
        var ox = Math.min(a.x1, b.x1) - Math.max(a.x0, b.x0), oz = Math.min(a.z1, b.z1) - Math.max(a.z0, b.z0);
        if (Math.abs(a.z1 - b.z0) < 0.05 && ox > (a.x1 - a.x0) * 0.8) a.party[0] = true;
        if (Math.abs(a.x1 - b.x0) < 0.05 && oz > (a.z1 - a.z0) * 0.8) a.party[1] = true;
        if (Math.abs(a.z0 - b.z1) < 0.05 && ox > (a.x1 - a.x0) * 0.8) a.party[2] = true;
        if (Math.abs(a.x0 - b.x1) < 0.05 && oz > (a.z1 - a.z0) * 0.8) a.party[3] = true;
      });
    });
  }

  /** @param {Object} b @param {number} P */
  function curbs(b, P) {
    var g = [0.62, 0.62, 0.6], ringOut = P + RING, lim = K * PITCH + 40;
    var seg = function (/** @type {number} */ x0, /** @type {number} */ x1, /** @type {number} */ z0, /** @type {number} */ z1) {
      if (x1 - x0 < 0.05 || z1 - z0 < 0.05) return;
      box(b, x0, x1, 0, 0.15, z0, z1, g, 32);
    };
    for (var k = -K; k <= K; k++) {
      var h = half(k);
      for (var m = -K - 1; m <= K; m++) {
        var hm0 = half(m), hm1 = half(m + 1);
        var a0 = m * PITCH + hm0.road, a1 = (m + 1) * PITCH - hm1.road;
        a0 = Math.max(a0, -lim); a1 = Math.min(a1, lim);
        [-1, 1].forEach(function (sgn) {
          var c = k * PITCH + sgn * h.road, lo = sgn > 0 ? c : c - 0.3, hi = sgn > 0 ? c + 0.3 : c;
          clipRing(a0, a1, c, ringOut, function (s0, s1) { seg(lo, hi, s0, s1); });
          clipRing(a0, a1, c, ringOut, function (s0, s1) { seg(s0, s1, lo, hi); });
        });
      }
    }
    var n = 160;
    for (var i = 0; i < n; i++) {
      var t0 = i / n * Math.PI * 2, t1 = (i + 1) / n * Math.PI * 2, tm = (t0 + t1) / 2;
      [[P - 0.3, P], [ringOut, ringOut + 0.3]].forEach(function (rr, outer) {
        var mx = Math.cos(tm) * rr[1], mz = Math.sin(tm) * rr[1];
        if (outer) {
          var kx = Math.round(mx / PITCH), kz = Math.round(mz / PITCH);
          if (Math.abs(mx - kx * PITCH) < half(kx).road + 0.2 || Math.abs(mz - kz * PITCH) < half(kz).road + 0.2) return;
        }
        var p = [[Math.cos(t0) * rr[0], Math.sin(t0) * rr[0]], [Math.cos(t1) * rr[0], Math.sin(t1) * rr[0]],
                 [Math.cos(t1) * rr[1], Math.sin(t1) * rr[1]], [Math.cos(t0) * rr[1], Math.sin(t0) * rr[1]]];
        quad(b, [[p[0][0], 0.15, p[0][1]], [p[1][0], 0.15, p[1][1]], [p[2][0], 0.15, p[2][1]], [p[3][0], 0.15, p[3][1]]], [0, 1, 0],
          [[p[0][0], p[0][1]], [p[1][0], p[1][1]], [p[2][0], p[2][1]], [p[3][0], p[3][1]]], g);
        var r = outer ? rr[0] : rr[1], nx = Math.cos(tm) * (outer ? -1 : 1), nz = Math.sin(tm) * (outer ? -1 : 1);
        var e0 = [Math.cos(t0) * r, Math.sin(t0) * r], e1 = [Math.cos(t1) * r, Math.sin(t1) * r];
        if (outer) quad(b, [[e0[0], 0, e0[1]], [e1[0], 0, e1[1]], [e1[0], 0.15, e1[1]], [e0[0], 0.15, e0[1]]], [nx, 0, nz], [[t0 * r, 0], [t1 * r, 0], [t1 * r, 0.15], [t0 * r, 0.15]], g);
        else quad(b, [[e1[0], 0, e1[1]], [e0[0], 0, e0[1]], [e0[0], 0.15, e0[1]], [e1[0], 0.15, e1[1]]], [nx, 0, nz], [[t1 * r, 0], [t0 * r, 0], [t0 * r, 0.15], [t1 * r, 0.15]], g);
      });
    }
  }

  /** @param {number} a0 @param {number} a1 @param {number} c @param {number} R @param {function(number, number): void} fn */
  function clipRing(a0, a1, c, R, fn) {
    if (Math.abs(c) >= R) { fn(a0, a1); return; }
    var e = Math.sqrt(R * R - c * c);
    if (a0 < -e) fn(a0, Math.min(a1, -e));
    if (a1 > e) fn(Math.max(a0, e), a1);
  }

  var GLASS_V = "attribute vec4 aWin; attribute vec4 aDim; varying vec4 vWin; varying vec4 vDim; varying vec3 vCw; varying vec3 vNw;\n";
  var GLASS_F = [
    "vec3 gN = normalize( vNw ); vec3 gT = normalize( cross( vec3( 0.0, 1.0, 0.0 ), gN ) );",
    "vec3 gV = normalize( vCw - cameraPosition );",
    "vec3 gd = vec3( dot( gV, gT ), gV.y, -dot( gV, gN ) ); gd.z = max( gd.z, 0.02 );",
    "vec2 gl = vWin.xy; float gk = floor( vWin.w + 0.001 ); float gwh = fract( vWin.w + 0.001 ) * 100.0; vec2 gcell = vec2( 0.0 );",
    "float grw; float grh; float grl; float grb;",
    "float gpw = 0.0;",
    "if ( gk > 2.5 ) { vec2 cs = vDim.xy; gcell = floor( gl / cs ); vec2 cl = gl - gcell * cs; vec2 wo = vec2( ( cs.x - vDim.z ) * 0.5, ( cs.y - vDim.w ) * 0.45 );",
    "  vec2 fw3 = fwidth( gl ) + 1e-3; vec2 inw = smoothstep( wo - fw3, wo + fw3, cl ) * ( 1.0 - smoothstep( wo + vDim.zw - fw3, wo + vDim.zw + fw3, cl ) );",
    "  gpw = 1.0 - inw.x * inw.y; gl = cl - wo; grw = cs.x; grh = cs.y; grl = wo.x; grb = wo.y; gk = 0.0; gwh = vDim.w; }",
    "else if ( gk > 1.5 ) { vec2 cs = vDim.xy; gcell = floor( gl / cs ); gl = gl - gcell * cs; grw = cs.x; grh = cs.y; grl = 0.0; grb = 0.0; }",
    "else { grw = vDim.x + 2.0 * vDim.y; grh = vDim.w; grl = vDim.y; grb = vDim.z; }",
    "float gs = fract( vWin.z * 7.13 + dot( gcell, vec2( 0.371, 0.713 ) ) );",
    "float gD = gk > 0.5 && gk < 1.5 ? 6.0 : 3.6 + 3.2 * fract( gs * 13.7 );",
    "vec3 go = vec3( gl.x + grl, gl.y + grb, 0.0 );",
    "float gtx = ( ( gd.x > 0.0 ? grw : 0.0 ) - go.x ) / ( abs( gd.x ) < 1e-4 ? 1e-4 : gd.x );",
    "float gty = ( ( gd.y > 0.0 ? grh : 0.0 ) - go.y ) / ( abs( gd.y ) < 1e-4 ? 1e-4 : gd.y );",
    "float gtz = ( gD - go.z ) / gd.z;",
    "float gt = min( min( gtx, gty ), gtz ); vec3 gh = go + gd * gt;",
    "vec3 gwall = mix( mix( vec3( 0.78, 0.72, 0.62 ), vec3( 0.55, 0.62, 0.58 ), step( 0.55, fract( gs * 5.1 ) ) ), vec3( 0.62, 0.6, 0.66 ), step( 0.8, fract( gs * 9.3 ) ) );",
    "if ( gk > 1.5 ) gwall = vec3( 0.7, 0.72, 0.74 );",
    "vec3 gfloor = mix( vec3( 0.3, 0.19, 0.11 ), vec3( 0.22, 0.23, 0.26 ), step( 0.5, fract( gs * 3.3 ) ) );",
    "vec3 gsc = gwall;",
    "if ( gt == gty ) gsc = gd.y < 0.0 ? gfloor : vec3( 0.86 );",
    "else if ( gt == gtz ) { vec2 pr = abs( gh.xy - vec2( grw * ( 0.3 + 0.4 * fract( gs * 21.0 ) ), grh * 0.55 ) ) - vec2( 0.5, 0.35 );",
    "  gsc *= 0.9; if ( max( pr.x, pr.y ) < 0.0 ) gsc = mix( vec3( 0.2, 0.25, 0.35 ), vec3( 0.6, 0.3, 0.2 ), fract( gs * 41.0 ) ); }",
    "else gsc *= 0.82;",
    "vec3 gbmin = vec3( grw * 0.15, 0.0, gD - 1.3 ); vec3 gbmax = vec3( grw * ( 0.55 + 0.3 * fract( gs * 17.0 ) ), 0.85, gD - 0.4 );",
    "if ( gk > 0.5 && gk < 1.5 ) { gbmin = vec3( 0.3, 0.0, 1.2 ); gbmax = vec3( grw - 0.3, 1.6, 2.0 ); }",
    "vec3 gi1 = ( gbmin - go ) / gd; vec3 gi2 = ( gbmax - go ) / gd; vec3 gtn = min( gi1, gi2 ); vec3 gtf = max( gi1, gi2 );",
    "float gn0 = max( max( gtn.x, gtn.y ), gtn.z ); float gf0 = min( min( gtf.x, gtf.y ), gtf.z );",
    "#ifndef GLASS_SIMPLE",
    "if ( gn0 < gf0 && gn0 > 0.0 && gn0 < gt && fract( gs * 3.1 ) > 0.25 ) { gt = gn0; gh = go + gd * gt; gsc = mix( vec3( 0.25, 0.18, 0.12 ), vec3( 0.32, 0.34, 0.4 ), fract( gs * 27.0 ) ) * ( gk > 0.5 && gk < 1.5 ? 1.6 : 1.0 ); }",
    "#endif",
    "float gsw = floor( uTime / 140.0 + gs * 37.0 );",
    "float glit = step( 1.0 - uLit, fract( gs * 3.7 + gsw * 0.1371 ) );",
    "if ( gk > 0.5 && gk < 1.5 ) glit = step( 0.12, fract( gs * 5.9 ) );",
    "if ( gk > 1.5 ) glit = step( 1.0 - uLit * 0.9, fract( gs * 2.3 + gcell.y * 0.17 + gsw * 0.071 ) );",
    "vec3 glamp = mix( vec3( 1.0, 0.72, 0.42 ), vec3( 0.82, 0.9, 1.0 ), step( 0.62, fract( gs * 17.0 ) ) );",
    "if ( gk > 1.5 ) glamp = vec3( 0.86, 0.93, 1.0 );",
    "float gdl = distance( gh, vec3( grw * 0.5, grh - 0.05, gD * 0.45 ) );",
    "float gill = 1.4 / ( 1.0 + gdl * gdl * 0.3 );",
    "float gvar = 0.45 + 0.85 * fract( gs * 47.0 );",
    "vec3 gnight = gsc * glamp * gill * glit * gvar * ( gk > 0.5 && gk < 1.5 ? 3.2 : 1.05 );",
    "if ( gt == gty && gd.y > 0.0 && gdl < 0.45 ) gnight += glamp * 2.6 * glit * gvar;",
    "float gtv = step( 0.93, fract( gs * 61.0 ) ) * ( 1.0 - glit ) * ( 0.5 + 0.5 * sin( uTime * 11.0 + gs * 40.0 ) * sin( uTime * 3.7 ) );",
    "gnight += vec3( 0.2, 0.35, 0.9 ) * gtv * 0.5 * smoothstep( gD - 0.01, gD, gh.z + 0.6 );",
    "vec3 gday = gsc * ( 0.32 - 0.24 * gh.z / gD ) * ( gk > 0.5 && gk < 1.5 ? 2.4 : 1.0 );",
    "vec3 gInt = gnight * uNight + gday * ( 1.0 - uNight ) * 0.55;",
    "float gww = vWin.w > 2.5 ? vDim.z : vDim.x; if ( gk > 1.5 ) gwh = vDim.y;",
    "vec2 gq = gk > 1.5 ? gl : ( vWin.w > 2.5 ? gl : vWin.xy );",
    "float gcov = fract( gs * 29.0 ) < 0.4 ? 0.15 + 0.6 * fract( gs * 41.0 ) : 0.0;",
    "if ( gk < 0.5 && gq.y > gwh * ( 1.0 - gcov ) ) {",
    "  float slat = 0.75 + 0.25 * smoothstep( 0.3, 0.5, fract( gq.y * 18.0 ) );",
    "  gInt = vec3( 0.75, 0.68, 0.55 ) * slat * ( glamp * glit * gvar * 0.8 * uNight + ( 1.0 - uNight ) * 0.35 ); }",
    "float gfw = 0.055; float gfr = 1.0 - step( gfw, gq.x ) * step( gq.x, gww - gfw ) * step( gfw, gq.y ) * step( gq.y, gwh - gfw );",
    "if ( gk < 0.5 ) gfr = max( gfr, step( abs( gq.x - gww * 0.5 ), 0.03 ) * step( 0.6, fract( vWin.z * 3.0 ) ) );",
    "if ( gk < 0.5 ) gfr = max( gfr, step( abs( gq.y - gwh * 0.66 ), 0.03 ) * step( 0.6, fract( vWin.z * 3.0 ) ) );",
    "if ( gk > 1.5 ) { gfr = max( step( gl.x, 0.05 ), step( grw - 0.05, gl.x ) ); gfr = max( gfr, step( gl.y, 0.08 ) ); float gsp = step( gl.y, 0.85 );",
    "  gInt *= 1.0 - gsp; gfr = max( gfr, gsp * 0.0 ); diffuseColor.rgb = mix( vec3( 0.03, 0.035, 0.04 ), diffuseColor.rgb, gfr ); }",
    "else diffuseColor.rgb = mix( vec3( 0.015 ), gpw > 0.5 ? diffuseColor.rgb : diffuseColor.rgb * 0.25, gfr );",
    "gfr = max( gfr, gpw );",
    "float gF = 0.04 + 0.96 * pow( 1.0 - clamp( dot( -gV, gN ), 0.0, 1.0 ), 5.0 );",
    "gInt *= ( 1.0 - gfr ) * ( 1.0 - gF );",
    "float gMetal = gk > 1.5 ? 0.85 * ( 1.0 - gfr ) : 0.0;"
  ].join("\n");

  /** @param {any} T @param {Object} lib @returns {any} */
  function glassMat(T, lib) {
    var u = WorldKit.uniforms;
    var mat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.08, metalness: 0, envMapIntensity: 1.2 });
    if (!M.lod) mat.defines = { GLASS_SIMPLE: 1 };
    return WorldKit.lit(mat, M.lod ? "glass" : "glass-simple", { porous: 0, wet: false, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.uniforms.uNight = u.night;
      sh.uniforms.uLit = u.lit;
      sh.vertexShader = GLASS_V + sh.vertexShader.replace("#include <begin_vertex>",
        "#include <begin_vertex>\nvWin = aWin; vDim = aDim; vCw = ( modelMatrix * vec4( transformed, 1.0 ) ).xyz; vNw = normalize( mat3( modelMatrix ) * objectNormal );");
      sh.fragmentShader = "uniform float uTime; uniform float uNight; uniform float uLit; varying vec4 vWin; varying vec4 vDim; varying vec3 vCw; varying vec3 vNw;\n"
        + sh.fragmentShader
          .replace("#include <color_fragment>", "#include <color_fragment>\n" + GLASS_F)
          .replace("#include <metalnessmap_fragment>", "#include <metalnessmap_fragment>\nroughnessFactor = mix( 0.05, 0.55, gfr ); metalnessFactor = max( gMetal, gfr * 0.3 );")
          .replace("#include <emissivemap_fragment>", "#include <emissivemap_fragment>\ntotalEmissiveRadiance += gInt;")
          .replace("#include <fog_fragment>", "#include <fog_fragment>\n#ifdef USE_FOG\ngl_FragColor.rgb += gInt * fogFactor * 0.3 * uNight;\n#endif");
    } });
  }

  var SIGN_F = [
    "vec2 sp = vSign.xy; float ss = vSign.z; float sty = vSign.w; vec2 sid; vec2 sf;",
    "if ( sty > 2.5 ) { sid = vec2( floor( sp.y ), ss ); sf = vec2( sp.x, fract( sp.y ) ); sty = 0.0; }",
    "else { sid = vec2( floor( sp.x ), ss ); sf = vec2( fract( sp.x ), sp.y ); }",
    "vec2 sg = ( sf - vec2( 0.18, 0.16 ) ) / vec2( 0.64, 0.68 );",
    "float sh1 = wsH( sid * 1.37 + 0.5 ); float sh2 = wsH( sid * 2.71 + 1.3 );",
    "float sm = 0.0; float sw = 0.09;",
    "if ( sg.x > -0.1 && sg.x < 1.1 && sg.y > -0.1 && sg.y < 1.1 ) {",
    "  float dxl = abs( sg.x ); float dxr = abs( sg.x - 1.0 ); float dxm = abs( sg.x - 0.5 );",
    "  float dyb = abs( sg.y ); float dyt = abs( sg.y - 1.0 ); float dym = abs( sg.y - 0.5 );",
    "  float inx = step( -0.05, sg.x ) * step( sg.x, 1.05 ); float iny = step( -0.05, sg.y ) * step( sg.y, 1.05 );",
    "  float lo = step( sg.y, 0.55 ); float hi = step( 0.45, sg.y );",
    "  sm = max( sm, step( 0.5, fract( sh1 * 2.0 ) ) * inx * step( dyt, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh1 * 4.0 ) ) * inx * step( dym, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh1 * 8.0 ) ) * inx * step( dyb, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh1 * 16.0 ) ) * hi * iny * step( dxl, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh1 * 32.0 ) ) * lo * iny * step( dxl, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh2 * 2.0 ) ) * hi * iny * step( dxr, sw ) );",
    "  sm = max( sm, step( 0.5, fract( sh2 * 4.0 ) ) * lo * iny * step( dxr, sw ) );",
    "  sm = max( sm, step( 0.6, fract( sh2 * 8.0 ) ) * iny * step( dxm, sw ) );",
    "  sm = max( sm, step( 0.7, fract( sh2 * 16.0 ) ) * inx * iny * step( abs( sg.x - sg.y ), sw * 1.3 ) );",
    "}",
    "float sfl = 1.0; if ( wsH( vec2( ss, 3.0 ) ) > 0.82 ) sfl = step( 0.12, wsH( vec2( floor( uTime * 14.0 ), ss ) ) ) * ( 0.85 + 0.15 * sin( uTime * 50.0 ) );",
    "float sk = mix( 1.2, 4.2, uNight );",
    "vec3 sc = vColor.rgb;",
    "vec3 so;",
    "if ( sty < 0.5 ) { float halo = smoothstep( 0.35, 0.0, abs( sf.y - 0.5 ) ) * 0.04; so = vec3( 0.03, 0.03, 0.035 ) + sc * ( sm * 1.15 + halo ) * sk * sfl; }",
    "else if ( sty < 1.5 ) { vec3 bg = mix( vec3( 0.95, 0.93, 0.88 ), sc, 0.55 ); so = mix( bg, vec3( 0.04 ), sm ) * sk * 0.55 * sfl; }",
    "else { vec2 bp = vec2( sp.x * 0.37, sp.y ); float band = 0.5 + 0.5 * sin( bp.x * 3.0 + bp.y * 4.0 - uTime * 0.6 + ss );",
    "  vec3 ad = mix( sc, vec3( 1.0 ) - sc * 0.5, band ) * ( 0.5 + 0.5 * smoothstep( 0.2, 0.8, wsN( bp * 3.0 + uTime * 0.1 ) ) );",
    "  so = ( ad * 0.55 + vec3( 1.0 ) * sm * 0.6 ) * mix( 1.0, 1.4, uNight ) * 0.32; }",
    "diffuseColor.rgb = so;"
  ].join("\n");

  /** @param {any} T @returns {any} */
  function signMat(T) {
    var u = WorldKit.uniforms;
    var mat = new T.MeshBasicMaterial({ vertexColors: true, side: T.DoubleSide });
    mat.onBeforeCompile = function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.uniforms.uNight = u.night;
      sh.vertexShader = "attribute vec4 aSign; varying vec4 vSign;\n" + sh.vertexShader.replace("#include <begin_vertex>", "#include <begin_vertex>\nvSign = aSign;");
      sh.fragmentShader = "uniform float uTime; uniform float uNight; varying vec4 vSign;\n" + WorldKit.NOISE + sh.fragmentShader
        .replace("#include <color_fragment>", "#include <color_fragment>\n" + SIGN_F)
        .replace("#include <fog_fragment>", "#include <fog_fragment>\n#ifdef USE_FOG\ngl_FragColor.rgb += diffuseColor.rgb * fogFactor * 0.5 * uNight;\n#endif");
    };
    mat.customProgramCacheKey = function () { return "wc-signs"; };
    return mat;
  }

  /** @param {any} T @param {Object} b @param {Array<string>} [extra] @param {Array<number>} [sizes] @returns {any} */
  function geom(T, b, extra, sizes) {
    var g = new T.BufferGeometry();
    g.setAttribute("position", new T.BufferAttribute(new Float32Array(b.p), 3));
    g.setAttribute("normal", new T.BufferAttribute(new Float32Array(b.n), 3));
    g.setAttribute("uv", new T.BufferAttribute(new Float32Array(b.u), 2));
    g.setAttribute("color", new T.BufferAttribute(new Float32Array(b.c), 3));
    if (extra && b.es) {
      var all = new Float32Array(b.e), n = b.p.length / 3, off = 0;
      extra.forEach(function (name, i) {
        var s = sizes[i], arr = new Float32Array(n * s);
        for (var v = 0; v < n; v++) for (var j = 0; j < s; j++) arr[v * s + j] = all[v * b.es + off + j];
        g.setAttribute(name, new T.BufferAttribute(arr, s));
        off += s;
      });
    }
    g.computeBoundingSphere();
    return g;
  }

  /** @param {any} T @param {Object} lib @returns {Object} */
  function materials(T, lib) {
    var S = function (/** @type {string} */ name, /** @type {string} */ key, /** @type {Object} */ o, /** @type {number} */ porous) {
      return WorldKit.lit(WorldBake.std(T, lib, name, o), key, { porous: porous });
    };
    return {
      brick: S("brick", "brick", {}, 1), stucco: S("stucco", "stucco", {}, 0.8), concrete: S("concrete", "concrete", {}, 0.9),
      stone: S("stone", "stone", {}, 0.7), roof: S("roof", "roof", {}, 1), metal: S("metal", "metal", {}, 0.2),
      wood: S("wood", "wood", {}, 0.8), curb: S("granite", "curb", {}, 0.6),
      cloth: WorldKit.lit(new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.85, side: T.DoubleSide }), "cloth", { porous: 1 }),
      glass: glassMat(T, lib), signs: signMat(T)
    };
  }

  /** @param {any} T @param {Object} lib @param {any} scene @param {number} P @param {number} lod @returns {{solids: Array<Array<number>>, lights: Array<Object>, count: number}} */
  function build(T, lib, scene, P, lod) {
    M.T = T;
    M.lod = lod;
    if (!M.mats) M.mats = materials(T, lib);
    if (M.group) {
      scene.remove(M.group);
      M.group.children.forEach(function (m) { m.geometry.dispose(); });
    }
    M.P = P;
    var B = { brick: bag(), stucco: bag(), concrete: bag(), stone: bag(), roof: bag(), metal: bag(), wood: bag(), curb: bag(),
              cloth: bag(), glass: bag(8), signs: bag(4) };
    var plots = plan(P), lights = [], solids = [];
    party(plots);
    plots.forEach(function (bd) {
      building(B, bd, lights);
      solids.push([bd.x0, bd.z0, bd.x1, bd.z1]);
    });
    curbs(B.curb, P);
    var group = new T.Group();
    Object.keys(B).forEach(function (k) {
      var b = B[k];
      if (!b.p.length) return;
      var g = k === "glass" ? geom(T, b, ["aWin", "aDim"], [4, 4]) : k === "signs" ? geom(T, b, ["aSign"], [4]) : geom(T, b);
      var mesh = new T.Mesh(g, M.mats[k]);
      mesh.frustumCulled = false;
      mesh.name = "city-" + k;
      group.add(mesh);
    });
    scene.add(group);
    M.group = group;
    return { solids: solids, lights: lights, count: plots.length };
  }

  var GROUND_F = [
    "vec2 cp = vWp.xz; float cd = length( cp ); float cfw = max( fwidth( cd ), 0.002 );",
    "float cIn = uP; float cOut = uP + " + RING.toFixed(1) + ";",
    "float ckx = floor( cp.x / " + PITCH.toFixed(1) + " + 0.5 ); float cgx = cp.x - ckx * " + PITCH.toFixed(1) + ";",
    "float ckz = floor( cp.y / " + PITCH.toFixed(1) + " + 0.5 ); float cgz = cp.y - ckz * " + PITCH.toFixed(1) + ";",
    "float chx = ckx == 0.0 ? " + AVE.road.toFixed(1) + " : " + ST.road.toFixed(1) + "; float chz = ckz == 0.0 ? " + AVE.road.toFixed(1) + " : " + ST.road.toFixed(1) + ";",
    "float cwx = ckx == 0.0 ? " + AVE.walk.toFixed(1) + " : " + ST.walk.toFixed(1) + "; float cwz = ckz == 0.0 ? " + AVE.walk.toFixed(1) + " : " + ST.walk.toFixed(1) + ";",
    "float cpx = fwidth( cp.x ) + 0.002;",
    "float conX = 1.0 - smoothstep( chx - cpx, chx + cpx, abs( cgx ) ); float conZ = 1.0 - smoothstep( chz - cpx, chz + cpx, abs( cgz ) );",
    "float cOutside = smoothstep( cOut - cfw, cOut + cfw, cd );",
    "float cRing = smoothstep( cIn - cfw, cIn + cfw, cd ) * ( 1.0 - cOutside );",
    "float cRoad = max( max( conX, conZ ) * cOutside, cRing );",
    "float cPlaza = 1.0 - smoothstep( cIn - cfw, cIn + cfw, cd );",
    "float cWalk = ( 1.0 - cRoad ) * ( 1.0 - cPlaza );",
    "vec4 ca = texture2D( uAsA, cp / AS_SIZE ) * vec4( vec3( AS_GAIN ), 1.0 ); vec4 cao = texture2D( uAsO, cp / AS_SIZE ); vec3 can = texture2D( uAsN, cp / AS_SIZE ).xyz * 2.0 - 1.0;",
    "vec4 cs = texture2D( uSwA, cp / SW_SIZE ) * vec4( vec3( SW_GAIN ), 1.0 ); vec4 cso = texture2D( uSwO, cp / SW_SIZE ); vec3 csn = texture2D( uSwN, cp / SW_SIZE ).xyz * 2.0 - 1.0;",
    "cs.rgb = mix( vec3( dot( cs.rgb, vec3( 0.3, 0.59, 0.11 ) ) ), cs.rgb, SW_SAT );",
    "float cang = atan( cp.y, cp.x ) / 6.28318 + 0.5; float cringW = 1.2; float cri = floor( cd / cringW ); float csegs = 6.0 + cri * 6.0;",
    "float cfr = fract( cd / cringW ); float cfa = fract( cang * csegs + cri * 0.37 );",
    "float cjoint = min( min( cfr, 1.0 - cfr ) * cringW, min( cfa, 1.0 - cfa ) * 6.28318 * max( cd, 0.6 ) / csegs );",
    "float cgrout = ( 1.0 - smoothstep( 0.01, 0.03, cjoint ) ) * step( 2.2, cd );",
    "vec2 cgu = vec2( cang * csegs * 1.7, cd ); vec4 cg = texture2D( uGrA, cp * 0.9 ); vec4 cgo = texture2D( uGrO, cp * 0.9 ); vec3 cgn = texture2D( uGrN, cp * 0.9 ).xyz * 2.0 - 1.0;",
    "float cstone = wsH( vec2( cri, floor( cang * csegs + cri * 0.37 ) ) );",
    "vec3 cpave = cg.rgb * ( 0.78 + 0.35 * cstone ) * mix( vec3( 1.0 ), vec3( 1.04, 0.98, 0.92 ), step( 0.5, fract( cri * 0.5 ) ) );",
    "float cinlay = smoothstep( 1.62, 1.6, cd ) - smoothstep( 1.92, 1.9, cd );",
    "float cstar = step( cd, 1.45 * ( 0.18 + 0.82 * pow( abs( cos( cang * 6.28318 * 2.0 ) ), 18.0 ) ) );",
    "float cdisc = step( cd, 2.2 ) * ( 1.0 - cstar ) * ( 1.0 - cinlay );",
    "cpave = mix( cpave, cg.rgb * 0.6, cdisc ); float cbrass = max( cinlay, cstar );",
    "cpave = mix( cpave * ( 1.0 - 0.55 * cgrout ), vec3( 0.62, 0.46, 0.22 ), cbrass );",
    "float cmark = 0.0; vec3 cmc = vec3( 0.85, 0.85, 0.8 );",
    "float cdash = step( fract( cp.y / 9.0 ), 0.4 ); float cdashx = step( fract( cp.x / 9.0 ), 0.4 );",
    "float cinter = step( abs( cgz ), chz + cwz + 1.2 ); float cinterx = step( abs( cgx ), chx + cwx + 1.2 );",
    "float cmz = 0.0; float cmx = 0.0;",
    "if ( ckx == 0.0 ) { cmz = max( step( abs( abs( cgx ) - 0.18 ), 0.07 ) * 2.0, step( abs( abs( cgx ) - 4.0 ), 0.07 ) * cdash ); }",
    "else { cmz = step( abs( cgx ), 0.07 ) * cdash; }",
    "if ( ckz == 0.0 ) { cmx = max( step( abs( abs( cgz ) - 0.18 ), 0.07 ) * 2.0, step( abs( abs( cgz ) - 4.0 ), 0.07 ) * cdashx ); }",
    "else { cmx = step( abs( cgz ), 0.07 ) * cdashx; }",
    "cmark = max( cmz * conX * ( 1.0 - conZ ) * ( 1.0 - cinter ), cmx * conZ * ( 1.0 - conX ) * ( 1.0 - cinterx ) );",
    "float cyel = step( 1.5, max( cmz * conX * ( 1.0 - conZ ), cmx * conZ * ( 1.0 - conX ) ) );",
    "float czeb = conX * ( 1.0 - conZ ) * step( chz + 0.6, abs( cgz ) ) * step( abs( cgz ), chz + cwz - 0.4 ) * step( 0.5, fract( cgx / 1.1 ) ) * step( abs( cgx ), chx - 0.4 );",
    "czeb = max( czeb, conZ * ( 1.0 - conX ) * step( chx + 0.6, abs( cgx ) ) * step( abs( cgx ), chx + cwx - 0.4 ) * step( 0.5, fract( cgz / 1.1 ) ) * step( abs( cgz ), chz - 0.4 ) );",
    "float cstop = conX * ( 1.0 - conZ ) * step( abs( abs( cgz ) - ( chz + cwz + 0.6 ) ), 0.22 ) * step( 0.0, cgx * sign( cgz ) );",
    "cstop = max( cstop, conZ * ( 1.0 - conX ) * step( abs( abs( cgx ) - ( chx + cwx + 0.6 ) ), 0.22 ) * step( 0.0, -cgz * sign( cgx ) ) );",
    "float cringM = cRing * max( step( abs( cd - cIn - 6.0 ), 0.07 ) * step( fract( cang * 6.28318 * ( cIn + 6.0 ) / 9.0 ), 0.4 ),",
    "  max( step( abs( cd - cIn - 0.45 ), 0.08 ), step( abs( cd - cOut + 0.45 ), 0.08 ) ) );",
    "cmark = max( max( cmark, czeb ), max( cstop, cringM ) ) * cRoad;",
    "float cwear = 0.55 + 0.45 * wsF( cp * 1.7 );",
    "cmark *= cwear;",
    "vec2 cmh = vec2( floor( cp.y / 23.0 ), ckx ); vec2 cmc2 = vec2( ckx * " + PITCH.toFixed(1) + " + ( wsH( cmh ) - 0.5 ) * chx, ( floor( cp.y / 23.0 ) + 0.5 ) * 23.0 );",
    "float chole = step( length( cp - cmc2 ), 0.62 ) * conX * ( 1.0 - conZ ) * step( 0.55, wsH( cmh + 3.0 ) );",
    "float cring2 = step( abs( length( cp - cmc2 ) - 0.45 ), 0.02 );",
    "float cgut = max( conX * smoothstep( chx - 0.6, chx, abs( cgx ) ), conZ * smoothstep( chz - 0.6, chz, abs( cgz ) ) ) * cOutside;",
    "cgut = max( cgut, cRing * max( smoothstep( cIn + 0.6, cIn, cd ), smoothstep( cOut - 0.6, cOut, cd ) ) );",
    "vec3 croad = ca.rgb * ( 1.0 - 0.35 * cgut );",
    "croad = mix( croad, mix( cmc, vec3( 0.85, 0.62, 0.15 ), cyel ), cmark );",
    "croad = mix( croad, vec3( 0.07, 0.07, 0.075 ) * ( 0.8 + 0.4 * step( 0.5, fract( ( cp.x + cp.y ) * 5.0 ) ) ), chole );",
    "vec3 cbase = croad * cRoad + cs.rgb * cWalk + cpave * cPlaza;",
    "cbase *= 0.86 + 0.28 * wsF( cp * 0.045 + 3.0 );",
    "float cpn = wsF( cp * 0.21 + 11.0 ); float cpud = smoothstep( 0.56, 0.64, cpn + cgut * 0.22 + cgrout * cPlaza * 0.05 - cmark * 0.1 ) * uWet;",
    "cpud *= 1.0 - chole;",
    "diffuseColor.rgb = cbase * mix( 1.0, 0.45, cpud ) * mix( 1.0, 0.82, uWet );",
    "float crough = cao.g * cRoad + cso.g * cWalk + cgo.g * cPlaza;",
    "crough = mix( crough, 0.5, cmark * 0.6 ); crough = mix( crough, 0.35, cbrass * cPlaza );",
    "crough = mix( crough, crough * mix( 0.5, 0.44, cRoad ), uWet );",
    "float cao2 = cao.r * cRoad + cso.r * cWalk + cgo.r * cPlaza;",
    "vec3 cnm = can * vec3( 0.55, 0.55, 1.0 ) * cRoad + csn * cWalk + cgn * cPlaza;"
  ].join("\n");

  /** @param {any} T @param {Object} lib @param {number} lod @returns {any} */
  function ground(T, lib, lod) {
    var u = WorldKit.uniforms;
    var mat = new T.MeshStandardMaterial({ color: 0xffffff, roughness: 1, metalness: 0, envMapIntensity: 0.9 });
    mat.defines = { AS_SIZE: lib.asphalt.size[0].toFixed(2), SW_SIZE: lib.sidewalk.size[0].toFixed(2), AS_GAIN: lib.asphalt.gain.toFixed(2),
                    SW_GAIN: (lib.sidewalk.photo ? lib.sidewalk.gain : 0.74).toFixed(2), SW_SAT: lib.sidewalk.photo ? "0.45" : "1.0" };
    if (!lod) mat.defines.GROUND_SIMPLE = 1;
    mat.extensions = { derivatives: true };
    var P = { value: 20 };
    WorldKit.lit(mat, lod ? "cityground" : "cityground-simple", { wet: false, reflect: 1.0, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.uniforms.uP = P;
      var tex = { uAsA: lib.asphalt.map, uAsO: lib.asphalt.orm, uAsN: lib.asphalt.normal, uSwA: lib.sidewalk.map, uSwO: lib.sidewalk.orm,
                  uSwN: lib.sidewalk.normal, uGrA: lib.granite.map, uGrO: lib.granite.orm, uGrN: lib.granite.normal };
      var decl = "";
      Object.keys(tex).forEach(function (k) { sh.uniforms[k] = { value: tex[k] }; decl += "uniform sampler2D " + k + "; "; });
      sh.vertexShader = "varying vec3 vWp;\n" + sh.vertexShader.replace("#include <begin_vertex>",
        "#include <begin_vertex>\nvWp = ( modelMatrix * vec4( transformed, 1.0 ) ).xyz;");
      sh.fragmentShader = "uniform float uTime; uniform float uP; varying vec3 vWp; " + decl + "\n" + WorldKit.NOISE + sh.fragmentShader
        .replace("#include <color_fragment>", "#include <color_fragment>\n" + GROUND_F)
        .replace("#include <roughnessmap_fragment>", "#include <roughnessmap_fragment>\nroughnessFactor = mix( crough, 0.025, cpud );")
        .replace("#include <aomap_fragment>", "#include <aomap_fragment>\nreflectedLight.indirectDiffuse *= mix( cao2, 1.0, 0.3 );")
        .replace("#include <normal_fragment_maps>", "#include <normal_fragment_maps>\n"
          + "vec2 crip = vec2( 0.0 ); float cfade = 1.0 - smoothstep( 0.02, 0.08, fwidth( cp.x ) );"
          + "\n#ifndef GROUND_SIMPLE\n for ( int ck = 0; ck < 2; ck ++ ) { vec2 cq = cp * 2.6 + float( ck ) * 0.37; vec2 cid = floor( cq ); vec2 cf = fract( cq ) - 0.5;"
          + " float chh = wsH( cid + float( ck ) * 7.1 ); vec2 co = ( vec2( wsH( cid + 1.3 ), wsH( cid + 2.7 ) ) - 0.5 ) * 0.5;"
          + " float cph = fract( uTime * 0.9 + chh ); vec2 cdd = cf - co; float crr = length( cdd ) + 1e-4; float crad = cph * 0.42;"
          + " float cwv = sin( ( crr - crad ) * 70.0 ) * ( 1.0 - cph ) * ( 1.0 - cph ) * smoothstep( 0.06, 0.0, abs( crr - crad ) );"
          + " crip += cdd / crr * cwv; }\n#endif\n crip *= cfade * 1.6;"
          + " vec3 cwn = normalize( vec3( cnm.x * ( 1.0 - cpud ) * 0.9 + crip.x * ( 0.09 * cpud + 0.025 ), max( cnm.z, 0.2 ), cnm.y * ( 1.0 - cpud ) * 0.9 + crip.y * ( 0.09 * cpud + 0.025 ) ) );"
          + " normal = normalize( ( viewMatrix * vec4( cwn, 0.0 ) ).xyz );");
    } });
    var mesh = new T.Mesh(new T.PlaneGeometry(900, 900), mat);
    mesh.rotation.x = -Math.PI / 2;
    mesh.userData.P = P;
    mesh.frustumCulled = false;
    return mesh;
  }

  /** @param {number} x @param {number} z @returns {boolean} */
  function onRoad(x, z) {
    var kx = Math.round(x / PITCH), kz = Math.round(z / PITCH), d = Math.hypot(x, z);
    if (d < M.P) return false;
    if (d < M.P + RING) return true;
    return Math.abs(x - kx * PITCH) < half(kx).road || Math.abs(z - kz * PITCH) < half(kz).road;
  }

  return Object.freeze({
    K: K, PITCH: PITCH, AVE: AVE, ST: ST, RING: RING, LIMIT: LIMIT, half: half, build: build, ground: ground, onRoad: onRoad,
    bag: bag, box: box, quad: quad, geom: geom
  });
})();
