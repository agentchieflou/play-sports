"use strict";

var WorldKit = (function () {
  var NOISE = "float wsH(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n"
    + "float wsN(vec2 p) { vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);"
    + " return mix(mix(wsH(i), wsH(i + vec2(1.0, 0.0)), f.x), mix(wsH(i + vec2(0.0, 1.0)), wsH(i + vec2(1.0, 1.0)), f.x), f.y); }\n"
    + "float wsF(vec2 p) { return wsN(p) * 0.55 + wsN(p * 2.03 + 7.1) * 0.3 + wsN(p * 4.1 + 3.3) * 0.15; }\n";
  var u = {
    time: { value: 0 }, night: { value: 0 }, wet: { value: 1 }, radius: { value: 10 }, lit: { value: 0.35 },
    refl: { value: null }, reflMat: { value: null }, reflOn: { value: 0 }
  };
  var L = { n: 32, all: [], moving: [], pos: [], col: [], T: null, gl2: false };

  /** @param {number} n @returns {number} */
  function rnd(n) {
    var x = Math.sin(n * 127.1 + 311.7) * 43758.5453;
    return x - Math.floor(x);
  }

  /** @param {any} T @param {any} g @param {string|number|any} hex @returns {any} */
  function paint(T, g, hex) {
    var c = hex && hex.isColor ? hex : new T.Color(hex), n = g.attributes.position.count, col = new Float32Array(n * 3);
    for (var i = 0; i < n; i++) { col[i * 3] = c.r; col[i * 3 + 1] = c.g; col[i * 3 + 2] = c.b; }
    g.setAttribute("color", new T.BufferAttribute(col, 3));
    if (!g.attributes.uv) g.setAttribute("uv", new T.BufferAttribute(new Float32Array(n * 2), 2));
    return g;
  }

  /** @param {any} T @param {any} geo @param {string|number|any} hex @param {Array<number>} at @param {Array<number>} [scale] @param {Array<number>} [turn] @param {number} [yaw] @returns {any} */
  function piece(T, geo, hex, at, scale, turn, yaw) {
    var g = geo.index ? geo.toNonIndexed() : geo;
    var s = scale || [1, 1, 1], r = turn || [0, 0, 0];
    g.applyMatrix4(new T.Matrix4().compose(new T.Vector3(at[0], at[1], at[2]),
      new T.Quaternion().setFromEuler(new T.Euler(r[0], r[1], r[2])), new T.Vector3(s[0], s[1], s[2])));
    if (yaw) g.applyMatrix4(new T.Matrix4().makeRotationY(yaw));
    return paint(T, g, hex);
  }

  /** @param {any} T @param {any} g @param {number} x @param {number} z @param {number} [y] @returns {any} */
  function moved(T, g, x, z, y) {
    g.applyMatrix4(new T.Matrix4().makeTranslation(x, y || 0, z));
    return g;
  }

  /** @param {any} T @param {Array<any>} list @param {Array<string>} [extra] @returns {any} */
  function merge(T, list, extra) {
    var keys = ["position", "normal", "color", "uv"].concat(extra || []), total = 0;
    list.forEach(function (g) { total += g.attributes.position.count; });
    var out = new T.BufferGeometry();
    keys.forEach(function (k) {
      var size = list[0].attributes[k].itemSize, arr = new Float32Array(total * size), at = 0;
      list.forEach(function (g) {
        if (g.attributes[k]) arr.set(g.attributes[k].array, at * size);
        at += g.attributes.position.count;
      });
      out.setAttribute(k, new T.BufferAttribute(arr, size));
    });
    list.forEach(function (g) { g.dispose(); });
    out.computeBoundingSphere();
    return out;
  }

  /** @param {Array<number>} p @param {Array<number>} c @param {number} r @param {string} tag @param {{f?: number, day?: boolean}} [o] @returns {Object} */
  function light(p, c, r, tag, o) {
    var l = { p: p, c: c, r: r, tag: tag, f: (o && o.f) || 0, day: !!(o && o.day), d: 0 };
    L.all.push(l);
    return l;
  }

  /** @param {string} tag */
  function forget(tag) {
    L.all = L.all.filter(function (l) { return l.tag !== tag; });
  }

  /** @param {any} T @param {number} n @param {boolean} gl2 */
  function lights(T, n, gl2) {
    L.T = T;
    L.n = n;
    L.gl2 = gl2;
    u.reflMat.value = new T.Matrix4();
    L.pos = Array.from({ length: n }, function () { return new T.Vector4(0, -999, 0, 0); });
    L.col = Array.from({ length: n }, function () { return new T.Vector3(); });
  }

  /** @param {any} cam @param {number} t @param {number} night */
  function pick(cam, t, night) {
    var cx = cam.position.x, cz = cam.position.z, fx = -Math.sin(cam.rotation.y), fz = -Math.cos(cam.rotation.y);
    var list = L.all.concat(L.moving);
    for (var i = 0; i < list.length; i++) {
      var l = list[i], dx = l.p[0] - cx, dz = l.p[2] - cz, d = Math.hypot(dx, dz);
      var ahead = d > 0.001 ? (dx * fx + dz * fz) / d : 1;
      l.d = (d - l.r * 0.6) * (ahead < -0.3 ? 1.8 : 1);
    }
    list.sort(function (a, b) { return a.d - b.d; });
    for (var k = 0; k < L.n; k++) {
      var m = list[k];
      if (!m || night <= 0.02 && !m.day) { L.pos[k].set(0, -999, 0, 0); L.col[k].set(0, 0, 0); continue; }
      var f = m.f ? 0.75 + 0.25 * Math.sin(t * 23 + m.p[0]) * Math.sin(t * 7.3 + m.p[2]) : 1;
      var k2 = m.day ? 1 : night;
      L.pos[k].set(m.p[0], m.p[1], m.p[2], m.r);
      L.col[k].set(m.c[0] * f * k2, m.c[1] * f * k2, m.c[2] * f * k2);
    }
  }

  var LOOP = [
    "#if defined( RE_Direct )",
    "for ( int wki = 0; wki < WK_N; wki ++ ) {",
    "  vec4 wkp = uWkP[ wki ];",
    "  vec3 wklv = ( viewMatrix * vec4( wkp.xyz, 1.0 ) ).xyz - geometryPosition;",
    "  float wkd2 = dot( wklv, wklv ); float wkr2 = wkp.w * wkp.w;",
    "  if ( wkd2 < wkr2 ) {",
    "    float wkq = wkd2 / wkr2; float wkw = clamp( 1.0 - wkq * wkq, 0.0, 1.0 );",
    "    directLight.direction = wklv * inversesqrt( wkd2 );",
    "    directLight.color = uWkC[ wki ] * wkw * wkw / ( wkd2 + 1.0 );",
    "    directLight.visible = true;",
    "    RE_Direct( directLight, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight );",
    "  }",
    "}",
    "#endif"
  ].join("\n");

  var WET = [
    "vec3 wkWn = normalize( ( vec4( normal, 0.0 ) * viewMatrix ).xyz );",
    "float wkUp = smoothstep( 0.55, 0.95, wkWn.y );",
    "float wkWet = uWet * ( 0.35 + 0.65 * wkUp ) * WK_POROUS;",
    "diffuseColor.rgb *= 1.0 - 0.32 * wkWet;",
    "roughnessFactor = mix( roughnessFactor, roughnessFactor * 0.38, wkWet );"
  ].join("\n");

  var REFLECT = [
    "#if defined( WK_REFLECT ) && defined( USE_ENVMAP )",
    "if ( uReflOn > 0.5 ) {",
    "  vec3 wkN = normalize( ( vec4( normal, 0.0 ) * viewMatrix ).xyz );",
    "  vec4 wkRc = uReflMat * vec4( vWkW, 1.0 );",
    "  vec2 wkUv = wkRc.xy / wkRc.w + wkN.xz * 0.05;",
    "  float wkLod = clamp( material.roughness * 8.0 - 0.4, 0.0, 6.0 );",
    "  vec3 wkR = vec3( 0.0 ); float wkS = 0.0;",
    "  for ( int i = -2; i <= 2; i ++ ) { float o = float( i ); float w = 1.0 - abs( o ) * 0.3;",
    "    wkR += textureLod( uRefl, wkUv + vec2( 0.0, o * material.roughness * 0.014 ), wkLod ).rgb * w; wkS += w; }",
    "  wkR /= wkS;",
    "  float wkE = smoothstep( 0.0, 0.05, wkUv.x ) * smoothstep( 1.0, 0.95, wkUv.x ) * smoothstep( 0.0, 0.05, wkUv.y ) * smoothstep( 1.0, 0.95, wkUv.y );",
    "  radiance = mix( radiance, wkR * WK_REFLGAIN, wkE );",
    "}",
    "#endif"
  ].join("\n");

  /** @param {any} mat @param {string} key @param {{porous?: number, wet?: boolean, reflect?: number, extra?: function(any): void}} [opts] @returns {any} */
  function lit(mat, key, opts) {
    var o = opts || {};
    var before = o.extra || null;
    var defs = { WK_N: L.n, WK_POROUS: (o.porous == null ? 1 : o.porous).toFixed(2) };
    if (o.reflect && L.gl2) { defs.WK_REFLECT = 1; defs.WK_REFLGAIN = o.reflect.toFixed(2); }
    mat.defines = Object.assign({}, mat.defines || {}, defs);
    mat.onBeforeCompile = function (/** @type {any} */ sh) {
      sh.uniforms.uWkP = { value: L.pos };
      sh.uniforms.uWkC = { value: L.col };
      sh.uniforms.uWet = u.wet;
      if (o.reflect) {
        sh.uniforms.uRefl = u.refl;
        sh.uniforms.uReflMat = u.reflMat;
        sh.uniforms.uReflOn = u.reflOn;
        sh.vertexShader = "varying vec3 vWkW;\n" + sh.vertexShader.replace("#include <begin_vertex>",
          "#include <begin_vertex>\nvWkW = ( modelMatrix * vec4( transformed, 1.0 ) ).xyz;");
        sh.fragmentShader = "varying vec3 vWkW; uniform sampler2D uRefl; uniform mat4 uReflMat; uniform float uReflOn;\n" + sh.fragmentShader;
      }
      if (before) before(sh);
      if (o.reflect) sh.fragmentShader = sh.fragmentShader.replace("#include <lights_fragment_maps>", "#include <lights_fragment_maps>\n" + REFLECT);
      sh.fragmentShader = "uniform vec4 uWkP[ WK_N ]; uniform vec3 uWkC[ WK_N ]; uniform float uWet;\n" + sh.fragmentShader
        .replace("#include <lights_fragment_begin>", "#include <lights_fragment_begin>\n" + LOOP);
      if (o.wet !== false && sh.fragmentShader.indexOf("#include <lights_physical_fragment>") >= 0) {
        sh.fragmentShader = sh.fragmentShader.replace("#include <lights_physical_fragment>", WET + "\n#include <lights_physical_fragment>");
      }
    };
    mat.customProgramCacheKey = function () { return "wk-" + key + "-" + L.n; };
    return mat;
  }

  /** @param {any} g @param {number} deg @returns {any} */
  function smooth(g, deg) {
    var p = g.attributes.position.array, n = g.attributes.normal.array, cos = Math.cos(deg * Math.PI / 180), map = {}, fn = [];
    for (var f = 0; f < p.length / 9; f++) {
      var o = f * 9, ax = p[o + 3] - p[o], ay = p[o + 4] - p[o + 1], az = p[o + 5] - p[o + 2];
      var bx = p[o + 6] - p[o], by = p[o + 7] - p[o + 1], bz = p[o + 8] - p[o + 2];
      var nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx, l = Math.hypot(nx, ny, nz) || 1;
      fn.push([nx / l, ny / l, nz / l]);
      for (var v = 0; v < 3; v++) {
        var k = Math.round(p[o + v * 3] * 1000) + "," + Math.round(p[o + v * 3 + 1] * 1000) + "," + Math.round(p[o + v * 3 + 2] * 1000);
        (map[k] = map[k] || []).push(f);
      }
    }
    for (var g2 = 0; g2 < p.length / 9; g2++) {
      for (var w = 0; w < 3; w++) {
        var i = g2 * 9 + w * 3, key = Math.round(p[i] * 1000) + "," + Math.round(p[i + 1] * 1000) + "," + Math.round(p[i + 2] * 1000);
        var me = fn[g2], sx = 0, sy = 0, sz = 0;
        map[key].forEach(function (f2) {
          var o2 = fn[f2];
          if (o2[0] * me[0] + o2[1] * me[1] + o2[2] * me[2] >= cos) { sx += o2[0]; sy += o2[1]; sz += o2[2]; }
        });
        var sl = Math.hypot(sx, sy, sz) || 1;
        n[i] = sx / sl; n[i + 1] = sy / sl; n[i + 2] = sz / sl;
      }
    }
    g.attributes.normal.needsUpdate = true;
    return g;
  }

  /** @param {any} T @param {Array<any>} out @param {number} x @param {number} y @param {number} z @param {number} r @param {number} seed @param {number} [count] */
  function canopy(T, out, x, y, z, r, seed, count) {
    var greens = ["#5f7f3e", "#6e8c45", "#56743a", "#7d9450", "#4d6b36"], n = count || 46;
    for (var i = 0; i < n; i++) {
      var a = rnd(seed * 13 + i * 1.7) * Math.PI * 2, b = Math.acos(rnd(seed * 7 + i * 2.3) * 1.6 - 0.6), d = Math.pow(rnd(seed * 3 + i * 3.1), 0.5) * r;
      var px = Math.cos(a) * Math.sin(b) * d, py = Math.cos(b) * d * 0.72, pz = Math.sin(a) * Math.sin(b) * d;
      var s = 1.1 + rnd(seed * 5 + i) * 0.8;
      var g = piece(T, new T.PlaneGeometry(s, s), greens[Math.floor(rnd(seed * 11 + i) * greens.length)], [px, py, pz], null,
        [rnd(i * 3.3 + seed) * Math.PI, rnd(i * 5.1 + seed) * Math.PI, rnd(i * 7.7 + seed) * Math.PI]);
      var ps = g.attributes.position.array, ns = g.attributes.normal.array;
      for (var v = 0; v < ps.length / 3; v++) {
        var qx = ps[v * 3], qy = ps[v * 3 + 1] + r * 0.25, qz = ps[v * 3 + 2], l = Math.hypot(qx, qy, qz) || 1;
        ns[v * 3] = qx / l; ns[v * 3 + 1] = qy / l; ns[v * 3 + 2] = qz / l;
      }
      out.push(moved(T, g, x, z, y));
    }
  }

  var F = { mat: null };
  var LEAF = [
    "#if defined( USE_ENVMAP ) && defined( ENVMAP_TYPE_CUBE_UV )",
    "iblIrradiance += vec3( 0.3, 0.38, 0.1 ) * getIBLIrradiance( -geometryNormal );",
    "#endif",
    "#if NUM_HEMI_LIGHTS > 0",
    "for ( int wkh = 0; wkh < NUM_HEMI_LIGHTS; wkh ++ ) irradiance += vec3( 0.3, 0.38, 0.1 ) * getHemisphereLightIrradiance( hemisphereLights[ wkh ], -geometryNormal );",
    "#endif"
  ].join("\n");

  /** @param {any} T @param {any} mat @returns {any} */
  function crown(T, mat) {
    mat.alphaTest = 0.42;
    mat.side = T.DoubleSide;
    return lit(mat, "foliage", { porous: 0.3, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.fragmentShader = sh.fragmentShader.replace("#include <lights_physical_fragment>", "#include <lights_physical_fragment>\nmaterial.specularF90 = 0.3;")
        .replace("#include <lights_fragment_maps>", "#include <lights_fragment_maps>\n" + LEAF)
        .replace("#include <normal_fragment_begin>", T.ShaderChunk.normal_fragment_begin.replace(/(normal|tbn\[[01]\]) \*= faceDirection;/g, ""))
        .replace("#include <alphatest_fragment>", "diffuseColor.a *= smoothstep( 0.08, 0.3, abs( dot( normalize( cross( dFdx( vViewPosition ), dFdy( vViewPosition ) ) ),"
          + " normalize( vViewPosition ) ) ) ) * ( 1.0 + 0.22 * max( 0.0, log2( max( fwidth( vMapUv.x ), fwidth( vMapUv.y ) ) * 1024.0 ) ) );\n#include <alphatest_fragment>");
      sh.vertexShader = "uniform float uTime;\n" + sh.vertexShader.replace("#include <begin_vertex>",
        "#include <begin_vertex>\nvec4 lw = modelMatrix * vec4( transformed, 1.0 );\n#ifdef USE_INSTANCING\nlw = modelMatrix * instanceMatrix * vec4( transformed, 1.0 );\n#endif\n"
        + "float lsw = max( transformed.y - 2.2, 0.0 ) * 0.03;"
        + " transformed.x += sin( uTime * 1.3 + lw.x * 0.3 + lw.z * 0.2 ) * lsw; transformed.z += cos( uTime * 1.1 + lw.z * 0.3 ) * lsw;");
    } });
  }

  /** @param {any} T @param {Object} lib @returns {any} */
  function foliage(T, lib) {
    return F.mat || (F.mat = crown(T, new T.MeshStandardMaterial({ vertexColors: true, map: lib.leaf.map, roughnessMap: lib.leaf.orm, roughness: 1, metalness: 0 })));
  }

  /** @param {any} T @param {any} group @param {Object<string, Array<{geo: any, mat: any}>>} woods @param {Array<Array<any>>} list @returns {{update: function(any, boolean=): void, placed: number, near: function(): number}} */
  function grove(T, group, woods, list) {
    var sets = [], by = {}, mx = new T.Matrix4(), q = new T.Quaternion(), at = new T.Vector3(), s = new T.Vector3(), up = new T.Vector3(0, 1, 0), was = null;
    list.forEach(function (t) { if (woods[t[5] + "_lod0"]) (by[t[5]] = by[t[5]] || []).push(t); });
    Object.keys(by).forEach(function (k) {
      sets.push({ list: by[k], lods: [0, 1].map(function (l) {
        return (woods[k + "_lod" + l] || []).map(function (part, pi) {
          var m = new T.InstancedMesh(part.geo, part.mat, by[k].length);
          m.count = 0;
          m.frustumCulled = false;
          m.userData.shared = true;
          m.name = "tree-" + k + "-" + l + "-" + pi;
          group.add(m);
          return m;
        });
      }) });
    });
    var update = function (/** @type {any} */ eye, /** @type {boolean=} */ force) {
      if (!force && was && Math.hypot(eye.x - was[0], eye.z - was[1]) < 4) return;
      was = [eye.x, eye.z];
      sets.forEach(function (set) {
        var n = [0, 0];
        set.list.forEach(function (t) {
          var l = Math.hypot(t[0] - eye.x, t[2] - eye.z) < 42 || !set.lods[1].length ? 0 : 1;
          mx.compose(at.set(t[0], t[1], t[2]), q.setFromAxisAngle(up, t[3]), s.setScalar(t[4]));
          set.lods[l].forEach(function (m) { m.setMatrixAt(n[l], mx); });
          n[l]++;
        });
        set.lods.forEach(function (ms, l) { ms.forEach(function (m) { m.count = n[l]; m.visible = n[l] > 0; m.instanceMatrix.needsUpdate = true; }); });
      });
    };
    update({ x: 0, z: 0 }, true);
    return { update: update, placed: sets.reduce(function (a, set) { return a + set.list.length; }, 0),
             near: function () { return sets.reduce(function (a, set) { return a + (set.lods[0][0] ? set.lods[0][0].count : 0); }, 0); } };
  }

  return Object.freeze({ NOISE: NOISE, REFLECT: REFLECT, uniforms: u, smooth: smooth, canopy: canopy, foliage: foliage, crown: crown, grove: grove, rnd: rnd, paint: paint,
                         piece: piece, moved: moved, merge: merge, light: light, forget: forget, lights: lights, pick: pick, lit: lit, L: L });
})();
