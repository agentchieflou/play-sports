"use strict";

var WorldScenery = (function () {
  var NOISE = WorldKit.NOISE;
  var IRON = "#262c31";
  var STONE = "#8d9196";
  var SOIL = "#2e2620";
  var BARK = "#4a3528";
  var HIGH = 4.2;
  var DOOR = 3.2;
  var rnd = WorldKit.rnd, piece = WorldKit.piece, merge = WorldKit.merge;

  /** @param {any} T @param {any} g @param {number} x @param {number} z @returns {any} */
  function moved(T, g, x, z) {
    g.applyMatrix4(new T.Matrix4().makeTranslation(x, 0, z));
    return g;
  }

  /** @param {any} T @returns {any} */
  function sky(T) {
    var mat = new T.ShaderMaterial({
      uniforms: { uTop: { value: new T.Color() }, uHorizon: { value: new T.Color() }, uGlow: { value: new T.Color(0x000000) },
                  uCloud: { value: new T.Color() }, uTime: WorldKit.uniforms.time },
      vertexShader: "varying vec3 vDir; void main() { vDir = normalize(position);"
        + " gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }",
      fragmentShader: "uniform vec3 uTop; uniform vec3 uHorizon; uniform vec3 uGlow; uniform vec3 uCloud; uniform float uTime; varying vec3 vDir;\n" + NOISE
        + "void main() { float y = vDir.y; vec3 c = mix(uHorizon, uTop, smoothstep(-0.05, 0.6, y));"
        + " vec2 p = vDir.xz / max(y + 0.12, 0.06) * 1.4 + uTime * vec2(0.006, 0.0025); float cl = wsF(p) * 0.7 + wsF(p * 2.7 + 5.0 + uTime * 0.008) * 0.3;"
        + " c = mix(c, uCloud, smoothstep(0.38, 0.75, cl) * smoothstep(-0.02, 0.18, y) * 0.75);"
        + " c += uGlow * (1.0 - smoothstep(-0.05, 0.28, y));"
        + " gl_FragColor = vec4(c, 1.0); }",
      side: T.BackSide, depthWrite: false, fog: false
    });
    var mesh = new T.Mesh(new T.SphereGeometry(320, 32, 16), mat);
    mesh.renderOrder = -1;
    mesh.frustumCulled = false;
    return mesh;
  }

  /** @param {any} T @param {Array<any>} metal @param {Array<any>} glass @param {number} x @param {number} z @param {number} yaw */
  function lamp(T, metal, glass, x, z, yaw) {
    var at = function (/** @type {any} */ g) { return moved(T, g, x, z); };
    metal.push(at(piece(T, new T.CylinderGeometry(0.15, 0.2, 0.42, 10), IRON, [0, 0.21, 0], null, null, yaw)));
    metal.push(at(piece(T, new T.CylinderGeometry(0.055, 0.08, 4.1, 10), IRON, [0, 2.4, 0], null, null, yaw)));
    metal.push(at(piece(T, new T.TorusGeometry(0.09, 0.025, 6, 12), IRON, [0, 1.1, 0], null, [Math.PI / 2, 0, 0], yaw)));
    metal.push(at(piece(T, new T.CylinderGeometry(0.03, 0.035, 0.9, 6), IRON, [0.42, 4.38, 0], null, [0, 0, Math.PI / 2 - 0.18], yaw)));
    metal.push(at(piece(T, new T.TorusGeometry(0.22, 0.022, 6, 14, Math.PI * 0.9), IRON, [0.18, 4.2, 0], null, [0, 0, 0.3], yaw)));
    metal.push(at(piece(T, new T.CylinderGeometry(0.06, 0.34, 0.2, 6), IRON, [0.84, 4.4, 0], null, null, yaw)));
    metal.push(at(piece(T, new T.SphereGeometry(0.05, 8, 6), IRON, [0.84, 4.52, 0], null, null, yaw)));
    metal.push(at(piece(T, new T.CylinderGeometry(0.17, 0.12, 0.08, 6), IRON, [0.84, 3.88, 0], null, null, yaw)));
    glass.push(at(piece(T, new T.CylinderGeometry(0.25, 0.16, 0.42, 6), "#ffffff", [0.84, 4.1, 0], null, null, yaw)));
  }

  /** @param {any} T @param {Array<any>} out @param {Array<any>|null} leaves @param {number} x @param {number} z @param {number} seed */
  function tree(T, out, leaves, x, z, seed) {
    var at = function (/** @type {any} */ g) { return moved(T, g, x, z); };
    out.push(at(piece(T, new T.CylinderGeometry(0.72, 0.62, 0.5, 14), STONE, [0, 0.25, 0])));
    out.push(at(piece(T, new T.CylinderGeometry(0.64, 0.64, 0.04, 14), SOIL, [0, 0.49, 0])));
    if (!leaves) return;
    out.push(at(piece(T, new T.CylinderGeometry(0.07, 0.12, 2.3, 7), BARK, [0, 1.6, 0], null, [0.04 * (seed - 0.5), 0, 0.05])));
    out.push(at(piece(T, new T.CylinderGeometry(0.03, 0.05, 0.8, 5), BARK, [0.22, 2.3, 0], null, [0, 0, -0.8])));
    WorldKit.canopy(T, leaves, x, 3.55, z, 1.45, seed * 31 + 7, 40);
  }

  /** @param {number} a @param {number} half @returns {boolean} */
  function opening(a, half) {
    return Math.abs(((a % (Math.PI / 2)) + Math.PI / 2) % (Math.PI / 2) - Math.PI / 4) > Math.PI / 4 - half;
  }

  /** @param {any} T @param {number} edge @param {Object} lib @param {Object<string, Array<{geo: any, mat: any}>>} [woods] @param {Object<string, Array<{geo: any}>>} [kit] @returns {{props: any, glass: any, leaves: any, panes: any, floor: any, lamps: Array<Array<number>>, inner: Array<Array<number>>, solids: Array<Array<number>>, wall: Array<number>}} */
  function plaza(T, edge, lib, woods, kit) {
    var metal = [], glass = [], panes = [], lamps = [], inner = [], solids = [], leaves = [], wood = [], grown = woods && woods.linden_2_lod0;
    var G = edge - 0.9, R = edge + 0.5, H = HIGH, D = G - 2.3, door = DOOR / 2 / G;
    var kerb = new T.LatheGeometry([new T.Vector2(edge, 0), new T.Vector2(edge, 0.14), new T.Vector2(edge + 0.06, 0.16),
      new T.Vector2(edge + 0.4, 0.16), new T.Vector2(edge + 0.42, 0)], 120);
    metal.push(piece(T, kerb, "#9a9ea3", [0, 0, 0]));
    var gap = function (/** @type {number} */ a) { return opening(a, door); };
    var n = Math.max(24, Math.round(Math.PI * 2 * G / 2.2));
    for (var k = 0; k < n; k++) {
      var a0 = k / n * Math.PI * 2, mid = (k + 0.5) / n * Math.PI * 2, chord = 2 * G * Math.sin(Math.PI / n);
      if (!gap(mid)) panes.push(piece(T, new T.PlaneGeometry(chord, H - 0.12), "#ffffff", [0, H / 2, G * Math.cos(Math.PI / n)], null, null, Math.PI / 2 - mid));
      if (!gap(a0) || !gap(a0 - 0.01) || !gap(a0 + 0.01)) metal.push(piece(T, new T.BoxGeometry(0.07, H, 0.11), "#2d3236", [0, H / 2, G], null, null, Math.PI / 2 - a0));
    }
    [[0.06, 0.1], [H - 0.06, 0.12]].forEach(function (rail) {
      metal.push(piece(T, new T.CylinderGeometry(G + 0.04, G + 0.04, rail[1], 96, 1, true), "#2d3236", [0, rail[0], 0]));
    });
    glass.push(piece(T, new T.CircleGeometry(R, 96), "#a9aaa6", [0, H, 0], null, [Math.PI / 2, 0, 0]));
    metal.push(piece(T, new T.CircleGeometry(R, 96), "#555b60", [0, H + 0.34, 0], null, [-Math.PI / 2, 0, 0]));
    metal.push(piece(T, new T.CylinderGeometry(R, R, 0.34, 96, 1, true), "#d9d8d3", [0, H + 0.17, 0]));
    [D, D * 0.5].forEach(function (r) { glass.push(piece(T, new T.RingGeometry(r - 0.07, r + 0.07, 96), "#fff4e4", [0, H - 0.02, 0], null, [Math.PI / 2, 0, 0])); });
    var m = Math.max(4, Math.round(Math.PI * 2 * D / 3.6));
    for (var i = 0; i < m; i++) inner.push([Math.cos(i / m * Math.PI * 2) * D, H - 0.45, Math.sin(i / m * Math.PI * 2) * D]);
    for (var q = 0; q < 4; q++) {
      var qa = (q + 0.5) * Math.PI / 2, qx = Math.cos(qa) * (G - 0.55), qz = Math.sin(qa) * (G - 0.55);
      inner.push([Math.cos(qa) * D * 0.25, H - 0.45, Math.sin(qa) * D * 0.25]);
      if (kit && kit.planter) {
        metal.push(kit.planter[0].geo.clone().applyMatrix4(new T.Matrix4().makeTranslation(qx, 0, qz)));
        wood.push([qx, 0.62, qz, rnd(q + 13) * 6.28, 0.42, "linden_2"]);
        solids.push([qx, qz, 0.55]);
      }
      [-1, 1].forEach(function (sd) {
        var la = q * Math.PI / 2 + sd * (DOOR / 2 + 0.9) / (G + 2), lx = Math.cos(la) * (G + 2), lz = Math.sin(la) * (G + 2);
        lamp(T, metal, glass, lx, lz, -la + Math.PI);
        lamps.push([lx - Math.cos(la) * 0.84, 4.1, lz - Math.sin(la) * 0.84]);
        solids.push([lx, lz, 0.3]);
      });
    }
    for (var t = 0; t < 8; t++) {
      var b = (t + 0.5) / 8 * Math.PI * 2, tx = Math.cos(b) * (edge + 3.9), tz = Math.sin(b) * (edge + 3.9);
      tree(T, metal, grown ? null : leaves, tx, tz, rnd(t + 41));
      wood.push([tx, 0.5, tz, rnd(t + 7) * 6.28, 0.94 + rnd(t + 3) * 0.12, "linden_2"]);
      solids.push([tx, tz, 0.85]);
    }
    var mat = WorldKit.lit(new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, metalness: 0.08 }), "plazaprops");
    var props = new T.Mesh(merge(T, metal), mat);
    props.frustumCulled = false;
    var lights = new T.Mesh(merge(T, glass), new T.MeshBasicMaterial({ vertexColors: true, color: 0xffffff }));
    lights.frustumCulled = false;
    var sheet = new T.Mesh(merge(T, panes), WorldKit.lit(new T.MeshStandardMaterial({ color: 0x9db3bc, roughness: 0.04, metalness: 0.4, transparent: true,
      opacity: 0.2, depthWrite: false, side: T.DoubleSide }), "pane", { porous: 0 }));
    sheet.frustumCulled = false;
    sheet.renderOrder = 3;
    var ring = new T.RingGeometry(2.6, G, 120, 1);
    ring.rotateX(-Math.PI / 2);
    ring.translate(0, 0.015, 0);
    var floor = new T.Mesh(ring, WorldKit.lit(new T.MeshStandardMaterial({ color: 0xc3c0b9, roughness: 0.34, metalness: 0 }), "office", { porous: 0 }));
    floor.frustumCulled = false;
    var crowns = grown ? new T.Group() : new T.Mesh(WorldKit.merge(T, leaves), WorldKit.foliage(T, lib));
    crowns.frustumCulled = false;
    if (grown) WorldKit.grove(T, crowns, /** @type {Object} */ (woods), wood);
    return { props: props, glass: lights, leaves: crowns, panes: sheet, floor: floor, lamps: lamps, inner: inner, solids: solids, wall: [G, door, R, H] };
  }

  /** @param {any} T @param {Object<string, Array<{geo: any}>>} kit @param {Object} lib @param {number} cap @returns {Object|null} */
  function desks(T, kit, lib, cap) {
    if (!kit || !kit.workstation) return null;
    var tex = new T.Texture();
    tex.colorSpace = T.SRGBColorSpace;
    tex.anisotropy = 4;
    var screen = new T.MeshBasicMaterial({ map: tex, color: new T.Color(1.35, 1.35, 1.35) });
    screen.onBeforeCompile = function (/** @type {any} */ sh) {
      sh.vertexShader = "attribute vec2 aCell;\n" + sh.vertexShader.replace("#include <uv_vertex>", "#include <uv_vertex>\nvMapUv = vMapUv * 0.125 + aCell;");
    };
    screen.customProgramCacheKey = function () { return "screen"; };
    var mats = [WorldKit.lit(WorldBake.std(T, lib, "wood", { metalness: 0 }), "desk", { porous: 0 }),
                WorldKit.lit(new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.42, metalness: 0.12 }), "fittings", { porous: 0 }), screen];
    var cell = new T.InstancedBufferAttribute(new Float32Array(cap * 2), 2);
    var meshes = kit.workstation.map(function (part, i) {
      if (i === 2) part.geo.setAttribute("aCell", cell);
      var mesh = new T.InstancedMesh(part.geo, mats[i], cap);
      mesh.count = 0;
      mesh.frustumCulled = false;
      mesh.name = "desk-" + i;
      return mesh;
    });
    return { meshes: meshes, cell: cell, tex: tex, cells: [], drawn: {}, seq: 0 };
  }

  /** @param {Object} D @param {any} T @param {Array<{x: number, z: number, yaw: number}>} list */
  function placeDesks(D, T, list) {
    var m = new T.Matrix4(), q = new T.Quaternion(), up = new T.Vector3(0, 1, 0), one = new T.Vector3(1, 1, 1), at = new T.Vector3();
    list.forEach(function (d, i) {
      m.compose(at.set(d.x, 0, d.z), q.setFromAxisAngle(up, d.yaw), one);
      D.meshes.forEach(function (mesh) { mesh.setMatrixAt(i, m); });
      D.cell.setXY(i, (i % 8) / 8, 1 - (Math.floor(i / 8) + 1) / 8);
    });
    D.meshes.forEach(function (mesh) { mesh.count = list.length; mesh.instanceMatrix.needsUpdate = true; });
    D.cell.needsUpdate = true;
  }

  /** @param {string} s @returns {string} */
  function esc(s) {
    return String(s).replace(/[&<>"']/g, function (c) { return "&#" + c.charCodeAt(0) + ";"; });
  }

  /** @param {number} x @param {number} y @param {string} fill @param {string} font @param {string} words @param {string} [anchor] @returns {string} */
  function words(x, y, fill, font, words, anchor) {
    return '<text x="' + x + '" y="' + y + '" fill="' + esc(fill) + '" style="font:' + font + ' monospace;white-space:pre"'
      + (anchor ? ' text-anchor="' + anchor + '"' : "") + ">" + esc(words) + "</text>";
  }

  /** @param {Object} D @param {Array<{name: string, state: string, says: string, last: string, needs: boolean, color: string}>} list */
  function screens(D, list) {
    var changed = false;
    list.forEach(function (s, i) {
      var key = [s.name, s.state, s.says, s.last, s.needs, s.color].join("|");
      if (D.drawn[i] === key) return;
      D.drawn[i] = key;
      changed = true;
      var x = (i % 8) * 256, y = Math.floor(i / 8) * 144, lines = [];
      [[s.says, "#8b949e", "  "], [s.last, "#c9d1d9", "> "]].forEach(function (b) {
        String(b[0] || "").replace(/\s+/g, " ").trim().split(" ").reduce(function (line, word, k, all) {
          var next = line ? line + " " + word : b[2] + word;
          if (next.length > 35 && line) { lines.push([line, b[1]]); next = "  " + word; }
          if (k === all.length - 1) lines.push([next, b[1]]);
          return next;
        }, "");
      });
      D.cells[i] = '<rect x="' + x + '" y="' + y + '" width="256" height="144" fill="#0d1117"/>'
        + '<rect x="' + x + '" y="' + y + '" width="256" height="24" fill="#161b22"/>'
        + '<circle cx="' + (x + 13) + '" cy="' + (y + 12) + '" r="5" fill="' + esc(s.color) + '"/>'
        + words(x + 24, y + 17, "#e6edf3", "600 13px", s.name.slice(0, 20))
        + words(x + 248, y + 16, s.color, "11px", s.state, "end")
        + lines.slice(0, 8).map(function (l, k) { return words(x + 8, y + 42 + k * 13, l[1], "11px", l[0]); }).join("")
        + (s.needs ? '<rect x="' + (x + 2) + '" y="' + (y + 2) + '" width="252" height="140" fill="none" stroke="#f2b33d" stroke-width="4"/>' : "");
    });
    if (!changed) return;
    var seq = ++D.seq, img = new Image(2048, 1152);
    img.onload = function () {
      if (seq !== D.seq) return;
      D.tex.image = img;
      D.tex.needsUpdate = true;
    };
    img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="2048" height="1152">'
      + '<rect width="2048" height="1152" fill="#0d1117"/>' + D.cells.join("") + "</svg>");
  }

  /** @param {any} T @param {number} [n] @returns {any} */
  function glows(T, n) {
    var mat = new T.ShaderMaterial({
      uniforms: { uOpacity: { value: 0 }, uColor: { value: new T.Color(0xffffff) } },
      vertexShader: "varying vec2 vUv; varying vec3 vC; void main() { vUv = position.xy; vC = vec3(1.0, 0.578, 0.255);\n#ifdef USE_INSTANCING_COLOR\nvC = instanceColor;\n#endif\n"
        + " vec4 mv = modelViewMatrix * instanceMatrix * vec4(0.0, 0.0, 0.0, 1.0); mv.xy += position.xy * 1.6;"
        + " gl_Position = projectionMatrix * mv; }",
      fragmentShader: "uniform float uOpacity; uniform vec3 uColor; varying vec2 vUv; varying vec3 vC;"
        + " void main() { float r = length(vUv); float a = pow(max(0.0, 1.0 - r), 2.4) + 0.5 * pow(max(0.0, 1.0 - r * 3.0), 2.0);"
        + " gl_FragColor = vec4(uColor * vC * a * uOpacity, 1.0); }",
      transparent: true, depthWrite: false, blending: T.AdditiveBlending, fog: false
    });
    var mesh = new T.InstancedMesh(new T.PlaneGeometry(2, 2), mat, Math.max(1, n || 8));
    mesh.count = 0;
    mesh.frustumCulled = false;
    mesh.renderOrder = 4;
    return mesh;
  }

  /** @param {any} mesh @param {any} T @param {Array<Array<number>>} lamps */
  function placeGlows(mesh, T, lamps) {
    var m = new T.Matrix4(), c = new T.Color();
    lamps.forEach(function (p, i) {
      mesh.setMatrixAt(i, m.makeTranslation(p[0], p[1], p[2]));
      mesh.setColorAt(i, p[3] ? c.fromArray(p[3][1]) : c.setRGB(1, 0.578, 0.255));
    });
    mesh.count = lamps.length;
    mesh.instanceMatrix.needsUpdate = true;
    if (mesh.instanceColor) mesh.instanceColor.needsUpdate = true;
  }

  return Object.freeze({ HIGH: HIGH, DOOR: DOOR, opening: opening, sky: sky, plaza: plaza, desks: desks, placeDesks: placeDesks, screens: screens, glows: glows, placeGlows: placeGlows });
})();
