"use strict";

var WorldHero = (function () {
  var SKIN = ["#3b2219", "#5c3a26", "#7b4a2d", "#9c6643", "#b98058", "#d39d74", "#e8bd98", "#f5d9c2"];
  var HAIR = ["#1c1a22", "#3b2a20", "#6b4528", "#a5512b", "#d7b26a", "#c9c6c2", "#3b5bb5"];
  var TOP = ["#c9ccd3", "#2f8f8a", "#d6a23a", "#e07a62", "#34446b", "#f2f2ee", "#4f8a4b", "#7a59b0"];
  var BOTTOM = ["#2e3450", "#1f2124", "#b29a72", "#4a6d9c", "#6b7078"];
  var SCARF = ["#2f8f8a", "#7a2f4f", "#d6a23a", "#34446b", "#e07a62", "#f2f2ee"];
  var STYLES = ["part", "curls", "coils", "long", "bun", "locs", "buzz", "none", "scarf", "wrap"];
  var FACES = ["none", "beard", "moustache"];
  var GLASSES = ["none", "round", "square"];
  var BUILDS = ["slim", "regular", "broad"];
  var FIGURES = ["angular", "between", "curved"];
  var AGES = ["young", "middle", "older"];
  var MOVES = ["walk", "wheelchair"];
  var OPTIONS = [
    { key: "skin", label: "skin", values: SKIN, swatch: true },
    { key: "hair", label: "hair", values: STYLES },
    { key: "hairColour", label: "hair colour", values: HAIR, swatch: true },
    { key: "scarf", label: "scarf or wrap", values: SCARF, swatch: true },
    { key: "face", label: "face", values: FACES },
    { key: "glasses", label: "glasses", values: GLASSES },
    { key: "top", label: "shirt", values: TOP, swatch: true },
    { key: "bottom", label: "trousers", values: BOTTOM, swatch: true },
    { key: "figure", label: "figure", values: FIGURES },
    { key: "build", label: "build", values: BUILDS },
    { key: "age", label: "age", values: AGES },
    { key: "move", label: "moves by", values: MOVES }
  ];
  var BASE = { skin: SKIN[5], hair: "part", hairColour: HAIR[0], scarf: SCARF[0], face: "none", glasses: "none",
               top: TOP[0], bottom: BOTTOM[0], figure: "angular", build: "regular", age: "young", move: "walk" };
  var PRESETS = [
    { name: "side part", look: {} },
    { name: "curls", look: { skin: SKIN[2], hair: "curls", hairColour: HAIR[0], top: TOP[1], bottom: BOTTOM[1], figure: "curved" } },
    { name: "headscarf", look: { skin: SKIN[4], hair: "scarf", scarf: SCARF[1], top: TOP[5], bottom: BOTTOM[3], glasses: "round", figure: "curved" } },
    { name: "locs", look: { skin: SKIN[0], hair: "locs", hairColour: HAIR[1], top: TOP[2], bottom: BOTTOM[0], build: "broad" } },
    { name: "silver bun", look: { skin: SKIN[6], hair: "bun", hairColour: HAIR[5], top: TOP[7], bottom: BOTTOM[4], glasses: "square", figure: "curved", age: "older" } },
    { name: "beard", look: { skin: SKIN[3], hair: "buzz", hairColour: HAIR[1], face: "beard", top: TOP[4], bottom: BOTTOM[2], age: "middle" } },
    { name: "wheelchair", look: { skin: SKIN[1], hair: "coils", hairColour: HAIR[0], top: TOP[3], bottom: BOTTOM[1], move: "wheelchair", figure: "between" } },
    { name: "long hair", look: { skin: SKIN[7], hair: "long", hairColour: HAIR[3], top: TOP[6], bottom: BOTTOM[3], build: "slim", figure: "curved" } },
    { name: "wrap", look: { skin: SKIN[0], hair: "wrap", scarf: SCARF[2], top: TOP[1], bottom: BOTTOM[0], figure: "curved", age: "middle" } },
    { name: "bald", look: { skin: SKIN[5], hair: "none", face: "moustache", top: TOP[4], bottom: BOTTOM[4], build: "broad", glasses: "round", age: "older" } }
  ];
  var EYE = "#241a16";
  var MOUTH = "#8e2c35";
  var SHOE = "#3a4466";
  var SOLE = "#f4f1ea";
  var CHAIR = "#2b2f36";
  var TYRE = "#15181c";
  var shared = { T: null, mat: null, fill: { value: 0.25 } };

  /** @param {any} T @returns {any} */
  function material(T) {
    if (shared.T !== T || !shared.mat) {
      shared.T = T;
      shared.mat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.62, metalness: 0 });
      WorldKit.lit(shared.mat, "hero", { porous: 0.6, extra: function (/** @type {any} */ sh) {
        sh.uniforms.uFill = shared.fill;
        sh.fragmentShader = "uniform float uFill;\n" + sh.fragmentShader.replace("#include <emissivemap_fragment>",
          "#include <emissivemap_fragment>\ntotalEmissiveRadiance += diffuseColor.rgb * uFill;");
      } });
    }
    return shared.mat;
  }

  /** @param {Object} look @returns {Object} */
  function normal(look) {
    var out = Object.assign({}, BASE);
    OPTIONS.forEach(function (o) {
      var v = look && look[o.key];
      if (o.values.indexOf(v) >= 0) out[o.key] = v;
    });
    return out;
  }

  /** @param {number} i @returns {Object} */
  function preset(i) {
    var p = PRESETS[((i % PRESETS.length) + PRESETS.length) % PRESETS.length];
    return normal(Object.assign({}, BASE, p.look));
  }

  /** @param {any} T @param {any} geo @param {string} hex @param {Array<number>} at @param {Array<number>} [scale] @param {Array<number>} [turn] @returns {any} */
  function piece(T, geo, hex, at, scale, turn) {
    var g = geo.index ? geo.toNonIndexed() : geo;
    var s = scale || [1, 1, 1], r = turn || [0, 0, 0];
    var m = new T.Matrix4().compose(new T.Vector3(at[0], at[1], at[2]),
                                    new T.Quaternion().setFromEuler(new T.Euler(r[0], r[1], r[2])),
                                    new T.Vector3(s[0], s[1], s[2]));
    g.applyMatrix4(m);
    var c = new T.Color(hex), n = g.attributes.position.count, col = new Float32Array(n * 3);
    for (var i = 0; i < n; i++) { col[i * 3] = c.r; col[i * 3 + 1] = c.g; col[i * 3 + 2] = c.b; }
    g.setAttribute("color", new T.BufferAttribute(col, 3));
    return g;
  }

  /** @param {any} T @param {Array<any>} list @param {any} mat @returns {any} */
  function mesh(T, list, mat) {
    var total = 0;
    list.forEach(function (g) { total += g.attributes.position.count; });
    var pos = new Float32Array(total * 3), nor = new Float32Array(total * 3), col = new Float32Array(total * 3), at = 0;
    list.forEach(function (g) {
      pos.set(g.attributes.position.array, at * 3);
      nor.set(g.attributes.normal.array, at * 3);
      col.set(g.attributes.color.array, at * 3);
      at += g.attributes.position.count;
      g.dispose();
    });
    var out = new T.BufferGeometry();
    out.setAttribute("position", new T.BufferAttribute(pos, 3));
    out.setAttribute("normal", new T.BufferAttribute(nor, 3));
    out.setAttribute("color", new T.BufferAttribute(col, 3));
    out.computeBoundingSphere();
    var m = new T.Mesh(out, mat);
    m.frustumCulled = false;
    return m;
  }

  /** @param {any} T @param {Object} L @param {Array<any>} out */
  function hair(T, L, out) {
    var h = L.hairColour, sc = L.scarf;
    var cap = function (k) { out.push(piece(T, new T.SphereGeometry(0.222, 18, 12), h, [0, 0.225, 0.055], [0.99, k || 0.72, 1])); };
    if (L.hair === "part") {
      cap();
      out.push(piece(T, new T.CapsuleGeometry(0.07, 0.2, 4, 10), h, [0.03, 0.31, -0.1], [1, 0.8, 1], [0, 0.3, Math.PI / 2]));
      out.push(piece(T, new T.SphereGeometry(0.09, 10, 8), h, [-0.1, 0.3, -0.07]));
    } else if (L.hair === "curls" || L.hair === "coils") {
      var big = L.hair === "coils" ? 0.3 : 0.26;
      out.push(piece(T, new T.SphereGeometry(big, 18, 12), h, [0, 0.27, 0.1], [1.05, 0.85, 1]));
      for (var i = 0; i < 8; i++) {
        var a = i / 8 * Math.PI * 2;
        out.push(piece(T, new T.SphereGeometry(big * 0.36, 8, 6), h,
                       [Math.cos(a) * big * 0.8, 0.33 + (i % 2) * 0.05, 0.1 + Math.sin(a) * big * 0.75]));
      }
    } else if (L.hair === "long") {
      cap();
      out.push(piece(T, new T.CapsuleGeometry(0.12, 0.28, 4, 12), h, [0, -0.04, 0.12], [1.6, 1, 0.6]));
      [-1, 1].forEach(function (s) {
        out.push(piece(T, new T.CapsuleGeometry(0.05, 0.26, 4, 8), h, [s * 0.19, 0.02, 0.0]));
      });
    } else if (L.hair === "bun") {
      cap(0.7);
      out.push(piece(T, new T.SphereGeometry(0.1, 12, 10), h, [0, 0.38, 0.13]));
    } else if (L.hair === "locs") {
      cap();
      for (var j = 0; j < 11; j++) {
        var b = Math.PI * (1.85 + j / 10 * 1.3);
        out.push(piece(T, new T.CapsuleGeometry(0.03, 0.3, 3, 6), h,
                       [Math.cos(b) * 0.2, -0.02, Math.sin(b) * 0.2 + 0.03], [1, 1, 1], [Math.sin(b) * 0.2, 0, -Math.cos(b) * 0.2]));
      }
    } else if (L.hair === "buzz") {
      out.push(piece(T, new T.SphereGeometry(0.216, 18, 12), h, [0, 0.2, 0.06], [1, 0.7, 1]));
    } else if (L.hair === "scarf") {
      out.push(piece(T, new T.SphereGeometry(0.245, 18, 14), sc, [0, 0.19, 0.08], [1.04, 0.92, 1.04]));
      out.push(piece(T, new T.CylinderGeometry(0.15, 0.24, 0.2, 18, 1), sc, [0, -0.08, 0.03]));
    } else if (L.hair === "wrap") {
      out.push(piece(T, new T.SphereGeometry(0.235, 18, 12), sc, [0, 0.24, 0.06], [1.05, 0.8, 1.04]));
      out.push(piece(T, new T.SphereGeometry(0.16, 14, 10), sc, [0, 0.42, 0.06], [1.2, 0.8, 1.05]));
      out.push(piece(T, new T.TorusGeometry(0.2, 0.04, 8, 20), sc, [0, 0.3, 0.06], [1.05, 1, 1], [Math.PI / 2, 0, 0]));
    }
  }

  /** @param {any} T @param {Object} L @param {Array<any>} out */
  function face(T, L, out) {
    var dark = L.hair === "none" || L.hair === "scarf" || L.hair === "wrap" ? "#3b2a20" : L.hairColour;
    [-1, 1].forEach(function (s) {
      out.push(piece(T, new T.SphereGeometry(0.042, 12, 8), "#f6f3ec", [s * 0.075, 0.17, -0.172], [1, 1.12, 0.6]));
      out.push(piece(T, new T.SphereGeometry(0.03, 10, 8), EYE, [s * 0.075, 0.17, -0.183]));
      out.push(piece(T, new T.SphereGeometry(0.008, 6, 4), "#ffffff", [s * 0.075 + 0.01, 0.182, -0.208]));
      out.push(piece(T, new T.CapsuleGeometry(0.012, 0.05, 3, 6), dark, [s * 0.075, 0.24, -0.186], [1, 1, 1], [0, 0, Math.PI / 2 + s * 0.12]));
    });
    out.push(piece(T, new T.SphereGeometry(1, 14, 10), MOUTH, [0, 0.04, -0.19], [0.062, 0.03, 0.02]));
    out.push(piece(T, new T.SphereGeometry(1, 12, 8), "#ffffff", [0, 0.053, -0.2], [0.046, 0.012, 0.012]));
    if (L.face === "beard") {
      out.push(piece(T, new T.SphereGeometry(0.17, 16, 12), L.hairColour, [0, -0.04, -0.06], [1.05, 0.85, 0.9]));
      out.push(piece(T, new T.CapsuleGeometry(0.016, 0.07, 3, 6), L.hairColour, [0, 0.078, -0.2], [1, 1, 1], [0, 0, Math.PI / 2]));
    } else if (L.face === "moustache") {
      out.push(piece(T, new T.CapsuleGeometry(0.018, 0.08, 3, 6), L.hairColour, [0, 0.078, -0.2], [1, 1, 1], [0, 0, Math.PI / 2]));
    }
    if (L.glasses !== "none") {
      var seg = L.glasses === "square" ? 4 : 18, spin = L.glasses === "square" ? Math.PI / 4 : 0;
      [-1, 1].forEach(function (s) {
        out.push(piece(T, new T.TorusGeometry(0.047, 0.008, 6, seg), "#202226", [s * 0.077, 0.17, -0.205], [1, 1, 1], [0, 0, spin]));
      });
      out.push(piece(T, new T.CapsuleGeometry(0.006, 0.04, 2, 4), "#202226", [0, 0.18, -0.21], [1, 1, 1], [0, 0, Math.PI / 2]));
    }
  }

  /** @param {any} T @param {Object} L @returns {Object} */
  function doll(T, L) {
    var bw = L.build === "slim" ? 0.86 : L.build === "broad" ? 1.2 : 1;
    var mat = material(T);
    var root = new T.Group(), hips = new T.Group(), head = new T.Group();
    hips.position.set(0, 0.86, 0);
    root.add(hips);
    hips.add(mesh(T, [
      piece(T, new T.CapsuleGeometry(0.2, 0.22, 6, 18), L.top, [0, 0.3, 0], [bw, 1, 0.78 * (0.85 + bw * 0.15)]),
      piece(T, new T.CylinderGeometry(0.17, 0.16, 0.1, 18), L.bottom, [0, 0.02, 0], [bw, 1, 0.78 * (0.85 + bw * 0.15)]),
      piece(T, new T.BoxGeometry(0.1, 0.05, 0.02), L.top, [-0.05, 0.6, -0.14], [1, 1, 1], [0.5, 0, 0.35]),
      piece(T, new T.BoxGeometry(0.1, 0.05, 0.02), L.top, [0.05, 0.6, -0.14], [1, 1, 1], [0.5, 0, -0.35]),
      piece(T, new T.SphereGeometry(0.012, 6, 4), "#2a3040", [0, 0.16, -0.165 * (0.85 + bw * 0.15)]),
      piece(T, new T.SphereGeometry(0.012, 6, 4), "#2a3040", [0, 0.29, -0.168 * (0.85 + bw * 0.15)]),
      piece(T, new T.SphereGeometry(0.012, 6, 4), "#2a3040", [0, 0.42, -0.165 * (0.85 + bw * 0.15)]),
      piece(T, new T.CylinderGeometry(0.065, 0.07, 0.14, 12), L.skin, [0, 0.64, 0])
    ], mat));
    head.position.set(0, 0.7, 0);
    hips.add(head);
    var hp = [
      piece(T, new T.SphereGeometry(0.21, 24, 18), L.skin, [0, 0.14, 0], [1, 1.04, 0.96]),
      piece(T, new T.SphereGeometry(0.055, 10, 8), L.skin, [-0.205, 0.12, 0.01], [0.6, 1, 0.8]),
      piece(T, new T.SphereGeometry(0.055, 10, 8), L.skin, [0.205, 0.12, 0.01], [0.6, 1, 0.8]),
      piece(T, new T.SphereGeometry(0.03, 10, 8), L.skin, [0, 0.1, -0.2])
    ];
    face(T, L, hp);
    hair(T, L, hp);
    head.add(mesh(T, hp, mat));
    var limbs = {};
    [-1, 1].forEach(function (s) {
      var side = s < 0 ? "L" : "R";
      var arm = new T.Group(), fore = new T.Group(), leg = new T.Group(), shin = new T.Group();
      arm.position.set(s * 0.25 * bw, 0.52, 0);
      arm.add(mesh(T, [piece(T, new T.CapsuleGeometry(0.068 * (0.8 + bw * 0.2), 0.17, 4, 12), L.top, [0, -0.12, 0])], mat));
      fore.position.set(0, -0.26, 0);
      fore.add(mesh(T, [
        piece(T, new T.CapsuleGeometry(0.062 * (0.8 + bw * 0.2), 0.12, 4, 12), L.top, [0, -0.08, 0]),
        piece(T, new T.SphereGeometry(0.065, 12, 10), L.skin, [0, -0.22, -0.01], [0.85, 1, 0.62]),
        piece(T, new T.SphereGeometry(0.025, 8, 6), L.skin, [-s * 0.045, -0.19, -0.03])
      ], mat));
      arm.add(fore);
      hips.add(arm);
      leg.position.set(s * 0.1 * bw, 0, 0);
      leg.add(mesh(T, [piece(T, new T.CapsuleGeometry(0.09 * (0.8 + bw * 0.2), 0.2, 4, 12), L.bottom, [0, -0.19, 0])], mat));
      shin.position.set(0, -0.4, 0);
      shin.add(mesh(T, [
        piece(T, new T.CapsuleGeometry(0.08 * (0.8 + bw * 0.2), 0.2, 4, 12), L.bottom, [0, -0.17, 0]),
        piece(T, new T.SphereGeometry(1, 14, 10), SHOE, [0, -0.375, -0.045], [0.068, 0.055, 0.135]),
        piece(T, new T.BoxGeometry(0.13, 0.03, 0.27), SOLE, [0, -0.42, -0.045]),
        piece(T, new T.BoxGeometry(0.07, 0.012, 0.05), SOLE, [0, -0.33, -0.1])
      ], mat));
      leg.add(shin);
      hips.add(leg);
      limbs["arm" + side] = arm;
      limbs["fore" + side] = fore;
      limbs["leg" + side] = leg;
      limbs["shin" + side] = shin;
    });
    var seat = L.move === "wheelchair" ? chair(T, root, mat) : { chair: null, wheels: null };
    return { kind: "doll", group: root, hips: hips, head: head, limbs: limbs, chair: seat.chair, wheels: seat.wheels, look: L, mat: mat,
             seated: L.move === "wheelchair" };
  }

  /** @param {any} T @param {any} root @param {any} mat @returns {{chair: any, wheels: any}} */
  function chair(T, root, mat) {
    var seat = mesh(T, [
      piece(T, new T.BoxGeometry(0.48, 0.06, 0.46), "#3a3f47", [0, 0.5, 0]),
      piece(T, new T.BoxGeometry(0.46, 0.44, 0.05), "#3a3f47", [0, 0.78, 0.23]),
      piece(T, new T.CylinderGeometry(0.018, 0.018, 0.5, 6), CHAIR, [-0.24, 0.52, 0.12], [1, 1, 1], [0.2, 0, 0]),
      piece(T, new T.CylinderGeometry(0.018, 0.018, 0.5, 6), CHAIR, [0.24, 0.52, 0.12], [1, 1, 1], [0.2, 0, 0]),
      piece(T, new T.BoxGeometry(0.36, 0.03, 0.14), CHAIR, [0, 0.1, -0.32]),
      piece(T, new T.CylinderGeometry(0.015, 0.015, 0.42, 6), CHAIR, [-0.17, 0.3, -0.26], [1, 1, 1], [0.35, 0, 0]),
      piece(T, new T.CylinderGeometry(0.015, 0.015, 0.42, 6), CHAIR, [0.17, 0.3, -0.26], [1, 1, 1], [0.35, 0, 0]),
      piece(T, new T.SphereGeometry(0.05, 8, 6), TYRE, [-0.2, 0.05, -0.3]),
      piece(T, new T.SphereGeometry(0.05, 8, 6), TYRE, [0.2, 0.05, -0.3])
    ], mat);
    root.add(seat);
    var wheels = new T.Group();
    wheels.position.set(0, 0.3, 0.05);
    wheels.add(mesh(T, [
      piece(T, new T.TorusGeometry(0.29, 0.028, 8, 28), TYRE, [-0.29, 0, 0], [1, 1, 1], [0, Math.PI / 2, 0]),
      piece(T, new T.TorusGeometry(0.29, 0.028, 8, 28), TYRE, [0.29, 0, 0], [1, 1, 1], [0, Math.PI / 2, 0]),
      piece(T, new T.TorusGeometry(0.25, 0.008, 4, 24), "#9aa3ad", [-0.32, 0, 0], [1, 1, 1], [0, Math.PI / 2, 0]),
      piece(T, new T.TorusGeometry(0.25, 0.008, 4, 24), "#9aa3ad", [0.32, 0, 0], [1, 1, 1], [0, Math.PI / 2, 0]),
      piece(T, new T.BoxGeometry(0.01, 0.5, 0.02), "#9aa3ad", [-0.29, 0, 0]),
      piece(T, new T.BoxGeometry(0.01, 0.02, 0.5), "#9aa3ad", [-0.29, 0, 0]),
      piece(T, new T.BoxGeometry(0.01, 0.5, 0.02), "#9aa3ad", [0.29, 0, 0]),
      piece(T, new T.BoxGeometry(0.01, 0.02, 0.5), "#9aa3ad", [0.29, 0, 0])
    ], mat));
    root.add(wheels);
    return { chair: seat, wheels: wheels };
  }

  /** @param {any} T @param {Object} look @returns {Object} */
  function build(T, look) {
    var L = normal(look), h = WorldPeople.ready() ? WorldPeople.hero(T, L) : null;
    if (!h) return doll(T, L);
    var seat = L.move === "wheelchair" ? chair(T, h.group, material(T)) : { chair: null, wheels: null };
    return Object.assign(h, { chair: seat.chair, wheels: seat.wheels, look: L });
  }

  /** @param {any} T @param {string} hex @param {Object} a @param {Array<number>} c @param {Array<number>} r @param {number} ey @returns {any} */
  function veil(T, hex, a, c, r, ey) {
    var rows = 44, cols = 72, k = 1.13, pos = [], idx = [];
    var top = ey + 0.045, bot = a.chin + 0.01, mid = (top + bot) / 2, half = (top - bot) / 2;
    var jaw = Math.PI * 0.62, y0 = c[1] + Math.cos(jaw) * r[1] * k, w0 = Math.sin(jaw), low = a.chin - 0.3;
    for (var i = 0; i <= rows; i++) {
      var t = i / rows;
      for (var j = 0; j <= cols; j++) {
        var f = j / cols * Math.PI * 2, sx = Math.sin(f), sz = -Math.cos(f);
        if (t <= 0.5) {
          var th = t / 0.5 * jaw;
          pos.push(sx * Math.sin(th) * r[0] * k, c[1] + Math.cos(th) * r[1] * k, c[2] + 0.012 + sz * Math.sin(th) * r[2] * k);
        } else {
          var u = (t - 0.5) / 0.5, g = Math.pow(u, 1.5), y = y0 - u * (y0 - low);
          var px = sx * mix(w0 * r[0] * k, 0.17, g), pz = mix(c[2] + 0.012, 0, g) + sz * mix(w0 * r[2] * k, 0.12, g), d = Math.hypot(px, pz) || 1;
          var e = a.hull ? Math.max(1, (a.hull(y, Math.atan2(px, -pz)) + 0.03) / d) : 1;
          pos.push(px * e, y, pz * e);
        }
      }
    }
    for (var m = 0; m < rows; m++) {
      for (var n = 0; n < cols; n++) {
        var A = m * (cols + 1) + n, B = A + cols + 1, C = B + 1, D = A + 1;
        var y = (pos[A * 3 + 1] + pos[B * 3 + 1]) / 2, fr = Math.min((n + 0.5) / cols, 1 - (n + 0.5) / cols) * Math.PI * 2;
        if (y < top && y > bot && fr < 1.0 * Math.sqrt(Math.max(0, 1 - Math.pow((y - mid) / half, 2)))) continue;
        idx.push(A, C, B, A, D, C);
      }
    }
    var g2 = new T.BufferGeometry();
    g2.setAttribute("position", new T.Float32BufferAttribute(pos, 3));
    g2.setIndex(idx);
    g2.computeVertexNormals();
    return piece(T, g2, hex, [0, 0, 0]);
  }

  /** @param {any} T @param {Object} look @param {Object} a @param {{hair: boolean, face: boolean, glasses: boolean}} need @returns {any} */
  function dress(T, look, a, need) {
    if (!a.eyeL || !a.eyeR || a.top === undefined) return null;
    var L = normal(look), out = [], h = L.hairColour, sc = L.scarf;
    var ey = (a.eyeL[1] + a.eyeR[1]) / 2, ez = (a.eyeL[2] + a.eyeR[2]) / 2, ex = Math.abs(a.eyeR[0] - a.eyeL[0]) / 2;
    var c = [0, (a.top + a.chin) / 2 + 0.01, (a.front + a.back) / 2], rx = a.side, ry = (a.top - a.chin) / 2, rz = (a.back - a.front) / 2;
    var brow = Math.acos(Math.max(-1, Math.min(1, (ey + 0.035 - c[1]) / (ry * 1.1))));
    var cap = function (/** @type {string} */ hex, /** @type {number} */ k, /** @type {number} */ to, /** @type {Array<number>} */ lift) {
      out.push(piece(T, new T.SphereGeometry(1, 28, 16, 0, Math.PI * 2, 0, to), hex, [c[0], c[1] + lift[0], c[2] + lift[1]], [rx * k, ry * k, rz * k]));
    };
    var mouth = ey - (ey - a.chin) * 0.62;
    if (need.hair && L.hair === "scarf") {
      out.push(veil(T, sc, a, c, [rx, ry, rz], ey));
    } else if (need.hair && L.hair === "wrap") {
      cap(sc, 1.17, 1.45, [0.02, 0.012]);
      for (var b = 0; b < 3; b++) {
        out.push(piece(T, new T.TorusGeometry(1, 0.07, 8, 32), sc, [0, ey + 0.07 + b * 0.03, c[2] + 0.008 + b * 0.008],
                       [rx * (1.2 - b * 0.06), rz * (1.2 - b * 0.06), 1], [Math.PI / 2 + 0.2 - b * 0.14, 0, (b % 2 ? 1 : -1) * 0.05]));
      }
      out.push(piece(T, new T.SphereGeometry(1, 16, 12), sc, [0, a.top + 0.02, c[2] + 0.01], [rx * 0.98, ry * 0.32, rz * 0.98]));
      out.push(piece(T, new T.SphereGeometry(0.03, 12, 10), sc, [0, ey + 0.115, a.front - 0.008]));
    } else if (need.hair && L.hair !== "none") {
      cap(h, L.hair === "coils" ? 1.3 : L.hair === "curls" ? 1.14 : 1.05, L.hair === "locs" || L.hair === "buzz" ? brow * 1.05 : brow * 1.25, [0, 0.005]);
      if (L.hair === "bun") out.push(piece(T, new T.SphereGeometry(0.055, 14, 10), h, [0, a.top - 0.01, a.back + 0.01]));
      if (L.hair === "long") out.push(piece(T, new T.CapsuleGeometry(0.06, 0.2, 4, 12), h, [0, a.chin - 0.02, a.back - 0.02], [rx * 15, 1, 0.5]));
      for (var k = 0; L.hair === "locs" && k < 28; k++) {
        var f = 0.62 + k / 27 * (Math.PI * 2 - 1.24), ox = Math.sin(f), oz = -Math.cos(f), y0 = ey + 0.02 + 0.05 * Math.max(0, -oz);
        var rr = Math.sqrt(Math.max(0.2, 1 - Math.pow((y0 - c[1]) / ry, 2))), len = 0.24 + 0.1 * ((k * 7) % 5) / 4;
        out.push(piece(T, new T.CapsuleGeometry(0.012, len, 3, 6), h,
                       [ox * rx * rr * 1.06 + ox * 0.03, y0 - len / 2, c[2] + oz * rz * rr * 1.06 + oz * 0.03], [1, 1, 1], [-0.18 * oz, 0, 0.18 * ox]));
      }
    }
    if (need.face && L.face === "beard") {
      out.push(piece(T, new T.SphereGeometry(1, 22, 12, Math.PI * 1.5 - 1.35, 2.7, Math.PI * 0.5, Math.PI * 0.42), h,
                     [0, mouth + 0.012, c[2] + 0.004], [rx * 1.04, (mouth - a.chin) * 1.6 + 0.02, rz * 1.04]));
    }
    if (need.face && L.face !== "none") {
      out.push(piece(T, new T.CapsuleGeometry(0.008, 0.042, 3, 6), h, [0, mouth + 0.016, a.front + 0.012], [1, 1, 0.8], [0, 0, Math.PI / 2]));
    }
    if (need.glasses && L.glasses !== "none") {
      var seg = L.glasses === "square" ? 4 : 20, spin = L.glasses === "square" ? Math.PI / 4 : 0, lz = ez - 0.03, ring = 0.023;
      [-1, 1].forEach(function (s) {
        out.push(piece(T, new T.TorusGeometry(ring, 0.0022, 6, seg), "#202226", [s * ex, ey, lz], [1, 1, 1], [0, 0, spin]));
        out.push(piece(T, new T.BoxGeometry(0.003, 0.004, 0.1), "#202226", [s * (ex + ring), ey + 0.006, lz + 0.05]));
      });
      out.push(piece(T, new T.CapsuleGeometry(0.0022, ex * 2 - ring * 2, 2, 4), "#202226", [0, ey + 0.006, lz], [1, 1, 1], [0, 0, Math.PI / 2]));
    }
    if (!out.length) return null;
    return mesh(T, out, material(T));
  }

  /** @param {Object} h @returns {{legL: number, armR: number, hips: number}} */
  function measure(h) {
    if (h.kind === "skinned") return h.measure;
    return { legL: h.limbs.legL.rotation.x, armR: h.limbs.armR.rotation.z, hips: h.hips.position.y };
  }

  /** @param {string} key @param {Object} look @returns {boolean} */
  function shown(key, look) {
    if (key === "scarf") return look.hair === "scarf" || look.hair === "wrap";
    if (key === "figure" || key === "age") return WorldPeople.ready();
    return true;
  }

  /** @param {number} a @param {number} b @param {number} t @returns {number} */
  function mix(a, b, t) { return a + (b - a) * t; }

  /** @param {Object} h @param {{phase: number, speed: number, talk: number, t: number, reduced: boolean, rolled: number, turn: number}} s */
  function pose(h, s) {
    if (h.wheels) h.wheels.rotation.x = -s.rolled / 0.31;
    if (h.kind === "skinned") { WorldPeople.pose(h, s); return; }
    var b = h.limbs, sw = Math.sin(s.phase), k = Math.min(1, s.speed), tk = s.talk;
    var breathe = s.reduced ? 0 : Math.sin(s.t * 1.7) * 0.006;
    if (h.seated) {
      h.hips.position.y = 0.55 + breathe;
      b.legL.rotation.set(1.45, 0, 0.04);
      b.legR.rotation.set(1.45, 0, -0.04);
      b.shinL.rotation.set(-1.45, 0, 0);
      b.shinR.rotation.set(-1.45, 0, 0);
      var push = 0.35 + sw * 0.3 * k;
      b.armL.rotation.set(mix(push, 0.5, tk), 0, mix(-0.28, -0.1, tk));
      b.armR.rotation.set(mix(push, 0.3, tk), 0, mix(0.28, 1.1, tk));
      b.foreL.rotation.set(mix(0.5, 1.2, tk), 0, 0);
      b.foreR.rotation.set(mix(0.5, 0.1, tk), 0, mix(0, 0.35, tk));
    } else {
      h.hips.position.y = 0.86 + Math.abs(sw) * 0.035 * k + breathe;
      b.legL.rotation.set(sw * 0.55 * k, 0, 0.02);
      b.legR.rotation.set(-sw * 0.55 * k, 0, -0.02);
      b.shinL.rotation.set(-Math.max(0, -sw) * 0.75 * k, 0, 0);
      b.shinR.rotation.set(-Math.max(0, sw) * 0.75 * k, 0, 0);
      b.armL.rotation.set(mix(-sw * 0.5 * k, 0.5, tk), 0, mix(-0.1, -0.1, tk));
      b.armR.rotation.set(mix(sw * 0.5 * k, 0.3, tk), 0, mix(0.1, 1.1, tk));
      b.foreL.rotation.set(mix(0.15 + 0.2 * k, 1.25, tk), 0, 0);
      b.foreR.rotation.set(mix(0.15 + 0.2 * k, 0.1, tk), 0, mix(0, 0.35, tk));
    }
    h.head.rotation.set(0, 0, tk * 0.08);
  }

  /** @param {number} v */
  function fill(v) {
    shared.fill.value = v;
    WorldPeople.fill(v);
  }

  /** @param {Object} h */
  function dispose(h) {
    if (!h) return;
    h.group.traverse(function (o) {
      if (o.geometry) o.geometry.dispose();
      if (o.isSkinnedMesh) o.skeleton.dispose();
    });
    if (h.group.parent) h.group.parent.remove(h.group);
  }

  return Object.freeze({ OPTIONS: OPTIONS, PRESETS: PRESETS, normal: normal, preset: preset, build: build, pose: pose,
                         fill: fill, dispose: dispose, dress: dress, measure: measure, shown: shown });
})();
