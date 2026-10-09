"use strict";

var WorldStreet = (function () {
  var C = WorldCity;
  var rnd = WorldKit.rnd;
  var PAINT = ["#0d0f12", "#e9ebee", "#7d8288", "#1d2a44", "#5a0f14", "#2c3e2f", "#b8b2a6", "#3a3f46", "#c9a227", "#16232f", "#8a1c1c"];
  var COAT = ["#2b2d33", "#4a3b2c", "#1e2a3a", "#5a5d61", "#6b2a24", "#2f3b2a", "#c2b49a", "#3b3247"];
  var BRELLA = ["#111317", "#1b2a4a", "#7a1a24", "#e8e3d8", "#2e4a35", "#c9a227", "#3a3a40", "#5b2a6b"];
  var SKIN = ["#3b2219", "#5c3a26", "#7b4a2d", "#9c6643", "#b98058", "#d39d74", "#e8bd98", "#f5d9c2"];
  var BRIGHT = ["#c9ccd3", "#2f8f8a", "#d6a23a", "#e07a62", "#34446b", "#4f8a4b", "#7a59b0", "#f2f2ee"];
  var LEGS = ["#1f2124", "#2e3450", "#3a3f46", "#4a3b2c", "#22303f", "#6b7078", "#2b2d33"];
  var HAIRS = ["#1c1a22", "#2b1d16", "#3b2a20", "#6b4528", "#a5512b", "#c9c6c2", "#d7b26a"];
  var GRIP = [0.18, 1.42, 0.18];
  var BARK = "float wkb = wsN( vec2( vUv.x * 18.0, vUv.y * 2.5 ) ) * 0.6 + wsN( vec2( vUv.x * 43.0, vUv.y * 7.0 ) ) * 0.4; diffuseColor.rgb *= 0.58 + 0.62 * wkb;";
  var SODIUM = [[1.0, 0.72, 0.42], [1.0, 0.578, 0.255]];
  var LED = [[0.78, 0.88, 1.0], [0.62, 0.74, 1.0]];
  var REACH = 200;
  var M = { T: null, mats: null, group: null, cars: null, people: null, loops: [], walkers: [], signals: null, P: -1, light: [], scans: {}, pr: {}, reach: 110 };

  /** @param {any} T @param {boolean} suv @returns {{body: any, glass: any, trim: any, lamp: any}} */
  function car(T, suv) {
    var P = WorldKit.piece, L = suv ? 4.7 : 4.55, H = suv ? 0.25 : 0;
    var s = new T.Shape(), wx = 1.42, wr = suv ? 0.42 : 0.38, y0 = 0.3;
    s.moveTo(-L / 2, y0 + 0.02);
    s.lineTo(-L / 2 - 0.04, 0.58 + H * 0.5);
    s.lineTo(-L / 2 + 0.06, 0.95 + H);
    s.lineTo(-1.55, 1.0 + H);
    s.lineTo(1.0, 0.98 + H);
    s.lineTo(L / 2 - 0.12, 0.84 + H * 0.6);
    s.lineTo(L / 2 + 0.02, 0.58 + H * 0.4);
    s.lineTo(L / 2 - 0.05, y0 + 0.02);
    s.lineTo(wx + wr + 0.06, y0);
    s.absarc(wx, y0, wr + 0.06, 0, Math.PI, false);
    s.lineTo(-wx + wr + 0.06, y0);
    s.absarc(-wx, y0, wr + 0.06, 0, Math.PI, false);
    s.lineTo(-L / 2, y0 + 0.02);
    var body = new T.ExtrudeGeometry(s, { depth: 1.62, bevelEnabled: true, bevelThickness: 0.1, bevelSize: 0.07, bevelSegments: 2, curveSegments: 6 });
    body.translate(0, 0, -0.81);
    shape(body, L, 0.7 + H, 1.0 + H, 0.1);
    var g = new T.Shape(), r0 = suv ? -2.0 : -1.55, r1 = suv ? -1.95 : -1.05, top = (suv ? 1.72 : 1.43);
    g.moveTo(r0, 0.97 + H);
    g.lineTo(r1, top);
    g.lineTo(suv ? 0.55 : 0.35, top + 0.01);
    g.lineTo(0.98, 0.97 + H);
    g.lineTo(r0, 0.97 + H);
    var cab = new T.ExtrudeGeometry(g, { depth: 1.36, bevelEnabled: true, bevelThickness: 0.06, bevelSize: 0.05, bevelSegments: 2, curveSegments: 4 });
    cab.translate(0, 0, -0.68);
    shape(cab, L, 0.97 + H, top, 0.24);
    var bodies = [P(T, body, "#ffffff", [0, 0, 0])];
    var roofL = (suv ? 0.55 : 0.35) - r1 - 0.05;
    bodies.push(P(T, new T.BoxGeometry(roofL, 0.05, 1.36), "#ffffff", [r1 + 0.02 + roofL / 2, top + 0.045, 0]));
    [-0.71, 0.71].forEach(function (z) {
      bodies.push(P(T, new T.BoxGeometry(0.1, top - 0.97 - H + 0.02, 0.06), "#ffffff", [-0.3 + (suv ? 0.05 : 0), (0.97 + H + top) / 2, z]));
      bodies.push(P(T, new T.BoxGeometry(0.16, 0.08, 0.22), "#ffffff", [0.82, 1.0 + H, z * 1.38]));
    });
    var trim = [];
    [[wx, 1], [-wx, 1], [wx, -1], [-wx, -1]].forEach(function (w) {
      trim.push(P(T, new T.CylinderGeometry(wr, wr, 0.26, 14), "#0b0c0e", [w[0], y0 + 0.08, w[1] * 0.76], null, [Math.PI / 2, 0, 0]));
      trim.push(P(T, new T.CylinderGeometry(wr * 0.62, wr * 0.62, 0.27, 9, 1, true), "#9aa0a6", [w[0], y0 + 0.08, w[1] * 0.765], null, [Math.PI / 2, 0, 0]));
      trim.push(P(T, new T.CircleGeometry(wr * 0.62, 9), "#9aa0a6", [w[0], y0 + 0.08, w[1] * 0.9], null, w[1] > 0 ? null : [0, Math.PI, 0]));
    });
    trim.push(P(T, new T.BoxGeometry(0.06, 0.16, 1.5), "#14161a", [L / 2 + 0.03, 0.44 + H * 0.4, 0]));
    trim.push(P(T, new T.BoxGeometry(0.06, 0.16, 1.5), "#14161a", [-L / 2 - 0.05, 0.42 + H * 0.4, 0]));
    trim.push(P(T, new T.BoxGeometry(0.03, 0.12, 0.42), "#e8e8e0", [L / 2 + 0.07, 0.44 + H * 0.4, 0]));
    trim.push(P(T, new T.BoxGeometry(0.03, 0.12, 0.42), "#e8e8e0", [-L / 2 - 0.09, 0.42 + H * 0.4, 0]));
    trim.push(P(T, new T.BoxGeometry(0.04, 0.12, 0.7), "#101214", [L / 2 - 0.02, 0.66 + H * 0.45, 0]));
    var lamps = [];
    [-0.62, 0.62].forEach(function (z) {
      lamps.push(P(T, new T.BoxGeometry(0.08, 0.11, 0.34), "#fff6e0", [L / 2 - 0.05, 0.7 + H * 0.45, z]));
      lamps.push(P(T, new T.BoxGeometry(0.08, 0.1, 0.36), "#ff1a0a", [-L / 2 - 0.02, 0.82 + H * 0.55, z]));
    });
    lamps.push(P(T, new T.BoxGeometry(0.06, 0.04, 0.9), "#ff1a0a", [-L / 2 - 0.0, 0.84 + H * 0.55, 0]));
    WorldKit.smooth(bodies[0], 38);
    var cabin = WorldKit.smooth(P(T, cab, "#ffffff", [0, 0, 0]), 38);
    return {
      body: WorldKit.merge(T, bodies), glass: WorldKit.merge(T, [cabin]),
      trim: WorldKit.merge(T, trim), lamp: WorldKit.merge(T, lamps)
    };
  }

  /** @param {any} g @param {number} L @param {number} y0 @param {number} y1 @param {number} tuck */
  function shape(g, L, y0, y1, tuck) {
    var p = g.attributes.position.array;
    for (var i = 0; i < p.length; i += 3) {
      var x = p[i], y = p[i + 1], ex = Math.max(0, Math.abs(x) - (L / 2 - 0.75)) / 0.75;
      var up = Math.min(1, Math.max(0, (y - y0) / Math.max(0.01, y1 - y0)));
      p[i + 2] *= (1 - 0.14 * ex * ex) * (1 - tuck * up);
    }
    g.attributes.position.needsUpdate = true;
  }

  /** @param {any} T @returns {any} */
  function person(T) {
    var P = WorldKit.piece, parts = [], limb = [];
    var add = function (/** @type {any} */ g, /** @type {number} */ l) { parts.push(g); limb.push([g.attributes.position.count, l]); };
    add(P(T, new T.CapsuleGeometry(0.075, 0.72, 2, 6), "#22252b", [0.1, 0.5, 0]), 1);
    add(P(T, new T.CapsuleGeometry(0.075, 0.72, 2, 6), "#22252b", [-0.1, 0.5, 0]), -1);
    add(P(T, new T.BoxGeometry(0.13, 0.08, 0.26), "#111111", [0.1, 0.04, 0.05]), 1);
    add(P(T, new T.BoxGeometry(0.13, 0.08, 0.26), "#111111", [-0.1, 0.04, 0.05]), -1);
    add(P(T, new T.CapsuleGeometry(0.2, 0.55, 2, 8), "#ffffff", [0, 1.22, 0], [1, 1, 0.75]), 0);
    add(P(T, new T.CylinderGeometry(0.24, 0.3, 0.45, 8, 1, true), "#ffffff", [0, 0.92, 0]), 0);
    add(P(T, new T.SphereGeometry(0.115, 10, 8), "#c9a184", [0, 1.7, 0.01]), 0);
    add(P(T, new T.SphereGeometry(0.12, 10, 6, 0, Math.PI * 2, 0, Math.PI * 0.55), "#2a2018", [0, 1.73, -0.01]), 0);
    add(P(T, new T.CapsuleGeometry(0.06, 0.5, 2, 6), "#ffffff", [-0.27, 1.2, 0]), 2);
    add(P(T, new T.CapsuleGeometry(0.06, 0.34, 2, 6), "#ffffff", [0.22, 1.36, 0.16], null, [-1.1, 0, 0.25]), 0);
    var geo = WorldKit.merge(T, parts), n = geo.attributes.position.count, a = new Float32Array(n), at = 0;
    limb.forEach(function (l) { for (var i = 0; i < l[0]; i++) a[at + i] = l[1]; at += l[0]; });
    geo.setAttribute("aLimb", new T.BufferAttribute(a, 1));
    return geo;
  }

  /** @param {any} T @returns {any} */
  function umbrella(T) {
    var P = WorldKit.piece;
    var canopy = new T.ConeGeometry(0.62, 0.3, 12, 1, true);
    var under = new T.ConeGeometry(0.6, 0.28, 12, 1, true);
    under.scale(1, -1, 1);
    return WorldKit.merge(T, [P(T, canopy, "#ffffff", [0.18, 2.12, 0.18]), P(T, under, "#bbbbbb", [0.18, 2.12, 0.18]),
      P(T, new T.CylinderGeometry(0.012, 0.012, 0.9, 5), "#1a1a1a", [0.18, 1.72, 0.18])]);
  }

  /** @param {any} T @param {Object} lib @returns {Object} */
  function materials(T, lib) {
    var u = WorldKit.uniforms;
    var paint = new T.MeshPhysicalMaterial({ vertexColors: true, roughness: 0.32, metalness: 0.55, clearcoat: 1, clearcoatRoughness: 0.06 });
    var people = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.8 });
    var brolly = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.35, side: T.DoubleSide });
    var lamps = new T.MeshBasicMaterial({ vertexColors: true });
    var sig = new T.MeshBasicMaterial({ vertexColors: true });
    sig.onBeforeCompile = function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.vertexShader = "attribute vec2 aSig; varying vec2 vSig;\n" + sh.vertexShader.replace("#include <begin_vertex>", "#include <begin_vertex>\nvSig = aSig;");
      sh.fragmentShader = "uniform float uTime; varying vec2 vSig;\n" + sh.fragmentShader.replace("#include <color_fragment>",
        "#include <color_fragment>\nfloat ph = mod( uTime + vSig.y * 15.0, 30.0 ); float st = ph < 12.0 ? 2.0 : ph < 15.0 ? 1.0 : 0.0;"
        + " float on = step( abs( vSig.x - 1.0 - st ), 0.1 ); diffuseColor.rgb *= vSig.x < 0.5 ? 2.5 : 0.05 + 7.0 * on;");
    };
    sig.customProgramCacheKey = function () { return "ws-signals"; };
    people.onBeforeCompile = function (/** @type {any} */ sh) {
      sh.uniforms.uTime = u.time;
      sh.vertexShader = "uniform float uTime; attribute float aLimb; attribute vec2 aWalk;\n" + sh.vertexShader.replace("#include <begin_vertex>",
        "#include <begin_vertex>\nfloat wph = uTime * 5.2 * aWalk.y + aWalk.x; float wsw = sin( wph ) * 0.42 * aWalk.y;"
        + " if ( abs( aLimb ) > 0.5 ) { float wpy = aLimb > 1.5 ? 1.45 : 0.9; float wa = aLimb > 1.5 ? -wsw * 0.8 : wsw * sign( aLimb );"
        + " vec3 wp = transformed - vec3( 0.0, wpy, 0.0 ); transformed = vec3( wp.x, wp.y * cos( wa ) - wp.z * sin( wa ), wp.y * sin( wa ) + wp.z * cos( wa ) ) + vec3( 0.0, wpy, 0.0 ); }"
        + " transformed.y += abs( sin( wph ) ) * 0.03 * aWalk.y;");
    };
    var furn = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.45, metalness: 0.35 });
    var shelter = new T.MeshStandardMaterial({ color: 0x9fb3c0, roughness: 0.05, metalness: 0.1, transparent: true, opacity: 0.25, depthWrite: false });
    var bark = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.92, metalness: 0 });
    bark.defines.USE_UV = "";
    var cone = new T.ShaderMaterial({
      uniforms: { uNight: u.night },
      vertexShader: "varying float vH; varying float vF; varying vec3 vC; void main() { vH = uv.y; vec4 wp = modelMatrix * instanceMatrix * vec4( position, 1.0 );"
        + " vec3 n = normalize( mat3( modelMatrix * instanceMatrix ) * normal ); vF = abs( dot( n, normalize( cameraPosition - wp.xyz ) ) );"
        + " vC = vec3( 1.0, 0.72, 0.42 );\n#ifdef USE_INSTANCING_COLOR\nvC = instanceColor;\n#endif\n"
        + " gl_Position = projectionMatrix * viewMatrix * wp; }",
      fragmentShader: "uniform float uNight; varying float vH; varying float vF; varying vec3 vC;"
        + " void main() { float a = pow( clamp( vH, 0.0, 1.0 ), 1.6 ) * pow( clamp( vF, 0.0, 1.0 ), 1.5 ) * uNight * 0.05; gl_FragColor = vec4( vC * a, 1.0 ); }",
      transparent: true, depthWrite: false, blending: T.AdditiveBlending, side: T.DoubleSide, fog: false
    });
    return {
      paint: WorldKit.lit(paint, "paint", { porous: 0 }), people: WorldKit.lit(people, "people", { porous: 1 }),
      brolly: WorldKit.lit(brolly, "brolly", { porous: 0 }), leaves: WorldKit.foliage(T, lib),
      bark: WorldKit.lit(bark, "bark", { porous: 0.6, extra: function (/** @type {any} */ sh) {
        sh.fragmentShader = WorldKit.NOISE + sh.fragmentShader.replace("#include <color_fragment>", "#include <color_fragment>\n" + BARK);
      } }),
      furn: WorldKit.lit(furn, "furn", { porous: 0.5 }), glass: WorldKit.lit(new T.MeshStandardMaterial({ color: 0x0a0d10, roughness: 0.04, metalness: 0.2 }), "carglass", { porous: 0 }),
      trim: WorldKit.lit(new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.55, metalness: 0.4 }), "cartrim", { porous: 0.2 }),
      lamps: lamps, sig: sig, shelter: shelter, cone: cone
    };
  }

  /** @param {number} P @returns {Array<Object>} */
  function segments(P) {
    var out = [], K = C.K, PITCH = C.PITCH, ringOut = P + C.RING + 2;
    for (var k = -K; k <= K; k++) {
      for (var m = -K - 1; m <= K; m++) {
        var h0 = C.half(m), h1 = C.half(m + 1), a0 = m * PITCH + h0.road + h0.walk, a1 = (m + 1) * PITCH - h1.road - h1.walk;
        [0, 1].forEach(function (axis) {
          [-1, 1].forEach(function (side) {
            var parts = [];
            var c = k * PITCH, hw = C.half(k);
            var off = c + side * (hw.road + 0.65);
            if (Math.abs(off) < ringOut) {
              var e = Math.sqrt(ringOut * ringOut - off * off);
              if (a0 < -e) parts.push([a0, Math.min(a1, -e)]);
              if (a1 > e) parts.push([Math.max(a0, e), a1]);
            } else parts.push([a0, a1]);
            parts.forEach(function (pp) {
              if (pp[1] - pp[0] < 6) return;
              out.push({ axis: axis, k: k, side: side, c: c, road: hw.road, walk: hw.walk, a0: pp[0], a1: pp[1] });
            });
          });
        });
      }
    }
    return out;
  }

  /** @param {Object} sg @param {number} a @param {number} d @returns {Array<number>} */
  function spot(sg, a, d) {
    var o = sg.c + sg.side * d;
    return sg.axis === 0 ? [o, a] : [a, o];
  }

  /** @param {any} T @param {Array<any>} f @param {Array<any>} glowAt @param {Array<Object>} lights @param {Array<Array<number>>} solids @param {Object} sg @param {number} a */
  function lampPost(T, f, glowAt, lights, solids, sg, a) {
    var P = WorldKit.piece, p = spot(sg, a, sg.road + 0.55), iron = "#2a2e33";
    var dir = sg.axis === 0 ? [-sg.side, 0] : [0, -sg.side];
    var yaw = Math.atan2(dir[0], dir[1]);
    f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.09, 0.14, 7.2, 8), iron, [0, 3.6, 0]), p[0], p[1]));
    f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.18, 0.22, 0.5, 8), iron, [0, 0.25, 0]), p[0], p[1]));
    f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.05, 0.06, 1.9, 6), iron, [0, 7.1, 0.85], null, [Math.PI / 2 - 0.12, 0, 0], yaw), p[0], p[1]));
    f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.42, 0.16, 0.85), "#3a3f45", [0, 7.18, 1.9], null, null, yaw), p[0], p[1]));
    var hx = p[0] + dir[0] * 1.9, hz = p[1] + dir[1] * 1.9;
    var sodium = rnd(sg.k * 7 + a) > 0.45;
    glowAt.push([hx, 7.02, hz, sodium ? SODIUM : LED]);
    lights.push({ p: [hx, 6.8, hz], c: sodium ? [44, 25, 10] : [28, 31, 36], r: 20, f: rnd(a * 3.3 + sg.k) > 0.97 ? 1 : 0 });
    solids.push([p[0], p[1], 0.25]);
  }

  /** @param {any} T @param {Array<any>} bark @param {Array<any>|null} leaf @param {Array<Array<number>>} solids @param {number} x @param {number} z @param {number} seed */
  function tree(T, bark, leaf, solids, x, z, seed) {
    var P = WorldKit.piece;
    bark.push(WorldKit.moved(T, P(T, new T.BoxGeometry(1.3, 0.06, 1.3), "#2b2520", [0, 0.03, 0]), x, z));
    solids.push([x, z, 0.35]);
    if (!leaf) return;
    bark.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.11, 0.17, 3.2, 7), "#56473a", [0, 1.6, 0], null, [0.03 * (seed - 0.5), 0, 0.04]), x, z));
    bark.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.05, 0.08, 1.4, 5), "#56473a", [0.35, 3.1, 0], null, [0, 0, -0.7]), x, z));
    bark.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.05, 0.08, 1.3, 5), "#56473a", [-0.3, 3.2, 0.1], null, [0.2, 0, 0.75]), x, z));
    WorldKit.canopy(T, leaf, x, 4.9, z, 2.0, seed * 17 + 3, 52);
    WorldKit.canopy(T, leaf, x + 0.6, 4.1, z - 0.3, 1.2, seed * 23 + 5, 18);
  }

  /** @param {string} id @param {number} x @param {number} z @param {number} yaw @returns {boolean} */
  function put(id, x, z, yaw) {
    if (!M.scans[id] || Math.hypot(x, z) > M.reach) return false;
    (M.pr[id] = M.pr[id] || []).push([x, z, yaw]);
    return true;
  }

  /** @param {Object} sg @returns {number} */
  function facing(sg) {
    return sg.axis === 0 ? (sg.side > 0 ? -Math.PI / 2 : Math.PI / 2) : (sg.side > 0 ? Math.PI : 0);
  }

  /** @param {Object} sg @param {number} a @param {number} seed @param {Array<Array<number>>} solids */
  function litter(sg, a, seed, solids) {
    var n = 1 + Math.floor(rnd(seed * 7.1) * 3);
    for (var i = 0; i < n; i++) {
      var q = spot(sg, a + 0.7 + i * 0.5, sg.road + sg.walk - 0.4 - rnd(seed * 3 + i) * 0.3);
      put(rnd(seed * 13 + i) > 0.3 ? "trashbag" : "cardboard_box_01", q[0], q[1], rnd(seed * 19 + i) * 6.28);
    }
    var e = spot(sg, a + 0.6 + n * 0.25, sg.road + sg.walk - 0.5);
    solids.push([e[0], e[1], 0.3 + n * 0.2]);
  }

  /** @param {Object} sg @param {number} len @param {number} seed @param {Array<Array<number>>} solids */
  function backStreet(sg, len, seed, solids) {
    if (rnd(seed * 53) > 0.55) {
      var w = spot(sg, sg.a0 + 4 + rnd(seed * 59) * (len - 8), sg.road + sg.walk - 0.22);
      if (put("exterior_aircon_unit", w[0], w[1], facing(sg))) solids.push([w[0], w[1], 0.45]);
    }
    if (rnd(seed * 61) > 0.8) {
      [0.9, 2.3].forEach(function (d) {
        var b = spot(sg, sg.a0 + d, sg.road - 0.9);
        if (put("concrete_road_barrier", b[0], b[1], sg.axis === 0 ? Math.PI / 2 : 0)) solids.push([b[0], b[1], 0.7]);
      });
    }
  }

  /** @param {any} T @param {Array<any>} f @param {Array<Array<number>>} solids @param {Object} sg @param {number} a @param {number} seed */
  function clutter(T, f, solids, sg, a, seed) {
    var P = WorldKit.piece, kind = Math.floor(seed * 5), p = spot(sg, a, sg.road + 0.75);
    var yaw = sg.axis === 0 ? (sg.side > 0 ? -Math.PI / 2 : Math.PI / 2) : (sg.side > 0 ? Math.PI : 0);
    if (kind === 0) {
      if (!put("fire_hydrant", p[0], p[1], seed * 40)) {
        f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.13, 0.15, 0.55, 10), "#b3201c", [0, 0.28, 0]), p[0], p[1]));
        f.push(WorldKit.moved(T, P(T, new T.SphereGeometry(0.13, 10, 6, 0, Math.PI * 2, 0, Math.PI / 2), "#b3201c", [0, 0.55, 0]), p[0], p[1]));
        f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.05, 0.05, 0.42, 6), "#b3201c", [0, 0.4, 0], null, [0, 0, Math.PI / 2]), p[0], p[1]));
      }
      solids.push([p[0], p[1], 0.2]);
    } else if (kind === 1) {
      var q = spot(sg, a, sg.road + sg.walk - 0.6);
      if (put("metal_trash_can", q[0], q[1], seed * 50)) {
        if (sg.k !== 0) litter(sg, a, seed, solids);
      } else {
        f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.3, 0.27, 0.95, 12), "#203427", [0, 0.48, 0]), q[0], q[1]));
        f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.32, 0.32, 0.06, 12), "#16241b", [0, 0.97, 0]), q[0], q[1]));
      }
      solids.push([q[0], q[1], 0.35]);
    } else if (kind === 2) {
      var b = spot(sg, a, sg.road + sg.walk - 0.7), box = rnd(seed * 31) > 0.5 ? (rnd(seed * 37) > 0.5 ? "utility_box_01" : "utility_box_02") : "";
      if (!box || !put(box, b[0], b[1], yaw)) {
        f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.5, 1.05, 0.45), ["#1f4f8a", "#c4362e", "#d9a21b", "#2e6b3a"][Math.floor(seed * 40) % 4], [0, 0.52, 0], null, null, yaw), b[0], b[1]));
        f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.42, 0.25, 0.04), "#b7c3cc", [0, 0.82, 0.23], null, null, yaw), b[0], b[1]));
      }
      solids.push([b[0], b[1], box === "utility_box_02" ? 0.5 : 0.35]);
    } else {
      var m = spot(sg, a, sg.road + 0.45);
      f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.035, 0.04, 1.1, 6), "#2a2e33", [0, 0.55, 0]), m[0], m[1]));
      f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.16, 0.3, 0.12), "#4b5258", [0, 1.22, 0], null, null, yaw), m[0], m[1]));
    }
  }

  /** @param {any} T @param {Array<any>} f @param {Array<any>} emit @param {Array<Array<number>>} solids @param {number} cx @param {number} cz @param {number} hx @param {number} hz */
  function signals(T, f, emit, solids, cx, cz, hx, hz) {
    var P = WorldKit.piece, iron = "#23272c";
    [[1, 1], [-1, 1], [-1, -1], [1, -1]].forEach(function (q, i) {
      var px = cx + q[0] * (hx + 0.7), pz = cz + q[1] * (hz + 0.7);
      var along = i % 2 === 0 ? 0 : 1;
      var dx = along === 0 ? 0 : -q[0], dz = along === 0 ? -q[1] : 0;
      var len = (along === 0 ? hz : hx) * 0.9;
      f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.1, 0.13, 6.2, 8), iron, [0, 3.1, 0]), px, pz));
      var yaw = Math.atan2(dx, dz);
      f.push(WorldKit.moved(T, P(T, new T.CylinderGeometry(0.06, 0.07, len, 6), iron, [0, 5.9, len / 2], null, [Math.PI / 2, 0, 0], yaw), px, pz));
      var sx = px + dx * len * 0.85, sz = pz + dz * len * 0.85;
      var fy = along === 0 ? (q[0] > 0 ? Math.PI / 2 : -Math.PI / 2) : (q[1] > 0 ? 0 : Math.PI);
      f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.36, 1.05, 0.3), "#16191c", [0, 5.35, 0], null, null, fy), sx, sz));
      var axis = along === 0 ? 0 : 1;
      for (var k = 0; k < 3; k++) {
        var hex = ["#ff2414", "#ffb21a", "#28ff6a"][k];
        var g = P(T, new T.CircleGeometry(0.11, 12), hex, [0, 5.7 - k * 0.32, 0.16], null, null, fy);
        WorldKit.moved(T, g, sx, sz);
        emit.push(tag(T, g, 1 + k, axis));
      }
      solids.push([px, pz, 0.2]);
    });
  }

  /** @param {any} T @param {any} g @param {number} x @param {number} y @returns {any} */
  function tag(T, g, x, y) {
    var n = g.attributes.position.count, a = new Float32Array(n * 2);
    for (var i = 0; i < n; i++) { a[i * 2] = x; a[i * 2 + 1] = y; }
    g.setAttribute("aSig", new T.BufferAttribute(a, 2));
    return g;
  }

  /** @param {any} T @param {Array<any>} f @param {Array<any>} glassy @param {Array<any>} emit @param {Array<Array<number>>} solids @param {Object} sg @param {number} a */
  function shelter(T, f, glassy, emit, solids, sg, a) {
    var P = WorldKit.piece, p = spot(sg, a, sg.road + 2.2), yaw = sg.axis === 0 ? (sg.side > 0 ? -Math.PI / 2 : Math.PI / 2) : (sg.side > 0 ? Math.PI : 0);
    var iron = "#30363c";
    [[-1.8, -0.7], [1.8, -0.7], [-1.8, 0.7], [1.8, 0.7]].forEach(function (q) {
      f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(0.08, 2.5, 0.08), iron, [q[0], 1.25, q[1]], null, null, yaw), p[0], p[1]));
    });
    f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(3.9, 0.08, 1.7), iron, [0, 2.54, 0], null, null, yaw), p[0], p[1]));
    f.push(WorldKit.moved(T, P(T, new T.BoxGeometry(3.4, 0.06, 0.45), "#6b4a32", [0, 0.48, -0.45], null, null, yaw), p[0], p[1]));
    glassy.push(WorldKit.moved(T, P(T, new T.BoxGeometry(3.6, 2.2, 0.03), "#ffffff", [0, 1.3, -0.72], null, null, yaw), p[0], p[1]));
    emit.push(tag(T, WorldKit.moved(T, P(T, new T.BoxGeometry(0.06, 1.6, 1.1), "#ffe9c9", [1.85, 1.3, 0], null, null, yaw), p[0], p[1]), 0, 0));
    solids.push([p[0], p[1], 1.0]);
  }

  /** @param {number} P @param {number} lane @returns {Array<Array<number>>} */
  function loopPath(P, lane) {
    var R = P + C.RING - 1.2 - lane * 0.7, far = REACH, pts = [], ringOut = P + C.RING + 1;
    var dirs = [[0, 1], [1, 0], [0, -1], [-1, 0]];
    for (var q = 0; q < 4; q++) {
      var d = dirs[q], nd = dirs[(q + 1) % 4];
      var inOff = [lane * d[1], -lane * d[0]], outOff = [-lane * nd[1], lane * nd[0]];
      var end = [d[0] * ringOut + inOff[0], d[1] * ringOut + inOff[1]];
      pts.push([d[0] * far + inOff[0], d[1] * far + inOff[1]], end);
      var start = [nd[0] * ringOut + outOff[0], nd[1] * ringOut + outOff[1]];
      var t0 = Math.atan2(end[1], end[0]), t1 = Math.atan2(start[1], start[0]);
      while (t1 > t0) t1 -= Math.PI * 2;
      for (var i = 0; i <= 18; i++) { var t = t0 + (t1 - t0) * i / 18; pts.push([Math.cos(t) * R, Math.sin(t) * R]); }
      pts.push(start, [nd[0] * far + outOff[0], nd[1] * far + outOff[1]]);
    }
    return pts;
  }

  /** @param {Array<Array<number>>} pts @returns {{pts: Array<Array<number>>, cum: Array<number>, len: number}} */
  function measure(pts) {
    var cum = [0];
    for (var i = 1; i < pts.length; i++) cum.push(cum[i - 1] + Math.hypot(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]));
    var last = Math.hypot(pts[0][0] - pts[pts.length - 1][0], pts[0][1] - pts[pts.length - 1][1]);
    return { pts: pts, cum: cum, len: cum[cum.length - 1] + last };
  }

  /** @param {Object} path @param {number} s @param {Array<number>} out @returns {Array<number>} */
  function along(path, s, out) {
    var L = path.len, p = path.pts, c = path.cum, n = p.length;
    s = ((s % L) + L) % L;
    var lo = 0, hi = n - 1;
    if (s >= c[n - 1]) { lo = n - 1; }
    else { while (hi - lo > 1) { var mid = (lo + hi) >> 1; if (c[mid] <= s) lo = mid; else hi = mid; } }
    var a = p[lo], b = p[(lo + 1) % n], seg = (lo + 1 < n ? c[lo + 1] : L) - c[lo], t = seg > 0 ? (s - c[lo]) / seg : 0;
    out[0] = a[0] + (b[0] - a[0]) * t; out[1] = a[1] + (b[1] - a[1]) * t;
    out[2] = Math.atan2(b[0] - a[0], b[1] - a[1]);
    return out;
  }

  /** @param {any} T @param {Object} lib @param {any} scene @param {number} P @param {number} lod @param {Object<string, Array<{geo: any, mat: any}>>} [scans] @param {Object<string, Array<{geo: any, mat: any}>>} [woods] @param {Object<string, Array<{geo: any}>>} [fleet] @returns {{solids: Array<Array<number>>, lights: Array<Object>, cars: number, parked: number, people: number, scans: number, crowd: number, trees: number}} */
  function build(T, lib, scene, P, lod, scans, woods, fleet) {
    M.T = T;
    M.scans = scans || {};
    M.pr = {};
    M.reach = lod ? 110 : 75;
    var far = lod ? 1 : 0.5;
    if (!M.mats) M.mats = materials(T, lib);
    if (M.group) {
      scene.remove(M.group);
      M.group.traverse(function (m) { if (m.geometry && !m.userData.shared) m.geometry.dispose(); });
    }
    M.P = P;
    var group = new T.Group(), f = [], bark = [], leaf = [], emit = [], glassy = [], glowAt = [], lights = [], solids = [], wood = [];
    var grown = woods && woods.plane_0_lod0 && woods.linden_0_lod0;
    var segs = segments(P), parked = [];
    var near = function (/** @type {Array<number>} */ q, /** @type {number} */ r) { return Math.hypot(q[0], q[1]) < r; };
    segs.forEach(function (sg, i) {
      var len = sg.a1 - sg.a0, seed = rnd(i * 3.7 + sg.k * 11 + sg.axis * 5), mid = spot(sg, (sg.a0 + sg.a1) / 2, 0);
      for (var a = sg.a0 + 6; a < sg.a1 - 4; a += 24) if (near(spot(sg, a, 0), 215 * far)) lampPost(T, f, glowAt, lights, solids, sg, a);
      if (sg.k === 0) {
        for (var b = sg.a0 + 14; b < sg.a1 - 4; b += 16) {
          var tp = spot(sg, b, sg.road + 1.4);
          if (!near(tp, 160 * far)) continue;
          if (grown && !(Math.round(b - sg.a0 - 6) % 24)) continue;
          tree(T, bark, grown ? null : leaf, solids, tp[0], tp[1], rnd(b * 1.3 + i));
          wood.push([tp[0], 0, tp[1], rnd(b * 2.1 + i) * 6.28, 0.9 + rnd(b * 1.7 + i) * 0.22, (rnd(i * 5.7 + 2) > 0.5 ? "plane_" : "linden_") + (rnd(b * 3.3 + i) > 0.5 ? 1 : 0)]);
        }
      }
      if (!near(mid, 150 * far)) return;
      for (var c = 0; c < Math.max(1, Math.floor(len / 18)); c++) clutter(T, f, solids, sg, sg.a0 + 3 + rnd(seed * 17 + c) * (len - 6), rnd(seed * 29 + c));
      if (sg.k === 0 && len > 30 && rnd(seed * 41) > 0.4) shelter(T, f, glassy, emit, solids, sg, (sg.a0 + sg.a1) / 2 + 5);
      if (sg.k !== 0 && lod) backStreet(sg, len, seed, solids);
      if (sg.k !== 0) {
        for (var d = sg.a0 + 3; d < sg.a1 - 5; d += 6.2 + rnd(d + i) * 2) {
          var q = spot(sg, d + 2.3, sg.road - 1.15);
          if (rnd(d * 0.7 + i * 1.9) > 0.5 || !lod || !near(q, 120)) continue;
          if (rnd(d * 5.3 + i) > 0.92 && put("covered_car", q[0], q[1], sg.axis === 0 ? (sg.side > 0 ? 0 : Math.PI) : (sg.side > 0 ? Math.PI / 2 : -Math.PI / 2))) continue;
          parked.push([q[0], q[1], sg.axis === 0 ? (sg.side > 0 ? 0 : Math.PI) : (sg.side > 0 ? Math.PI / 2 : -Math.PI / 2), rnd(d * 3.1 + i)]);
        }
      }
    });
    for (var kx = -C.K; kx <= C.K; kx++) {
      for (var kz = -C.K; kz <= C.K; kz++) {
        var cx = kx * C.PITCH, cz = kz * C.PITCH;
        if (Math.hypot(cx, cz) < P + C.RING + 30) continue;
        signals(T, f, emit, solids, cx, cz, C.half(kx).road, C.half(kz).road);
      }
    }
    var add = function (/** @type {Array<any>} */ list, /** @type {any} */ mat, /** @type {string} */ name, /** @type {Array<string>|undefined} */ extra) {
      if (!list.length) return null;
      var mesh = new T.Mesh(WorldKit.merge(T, list, extra), mat);
      mesh.frustumCulled = false;
      mesh.name = "street-" + name;
      group.add(mesh);
      return mesh;
    };
    add(f, M.mats.furn, "furniture");
    add(bark, M.mats.bark, "bark");
    add(leaf, M.mats.leaves, "trees");
    add(glassy, M.mats.shelter, "shelters");
    add(emit, M.mats.sig, "emit", ["aSig"]);
    var cone = new T.CylinderGeometry(0.2, 2.9, 6.6, 18, 1, true);
    cone.translate(0, -3.3, 0);
    var cones = new T.InstancedMesh(cone, M.mats.cone, glowAt.length), mtx = new T.Matrix4();
    var tint = new T.Color();
    glowAt.forEach(function (g, i) { cones.setMatrixAt(i, mtx.makeTranslation(g[0], g[1] + 0.05, g[2])); cones.setColorAt(i, tint.fromArray(g[3][0])); });
    cones.frustumCulled = false;
    cones.renderOrder = 3;
    cones.name = "street-cones";
    group.add(cones);
    var glows = WorldScenery.glows(T, glowAt.length);
    WorldScenery.placeGlows(glows, T, glowAt);
    glows.name = "street-glows";
    group.add(glows);
    M.glows = glows;
    var placed = 0, mx = new T.Matrix4(), rot = new T.Quaternion(), up = new T.Vector3(0, 1, 0), one = new T.Vector3(1, 1, 1), at = new T.Vector3();
    Object.keys(M.pr).forEach(function (id) {
      var list = M.pr[id];
      placed += list.length;
      M.scans[id].forEach(function (part, pi) {
        var mesh = new T.InstancedMesh(part.geo, part.mat, list.length);
        list.forEach(function (it, i) { mesh.setMatrixAt(i, mx.compose(at.set(it[0], 0, it[1]), rot.setFromAxisAngle(up, it[2]), one)); });
        mesh.frustumCulled = false;
        mesh.userData.shared = true;
        mesh.name = "scan-" + id + (pi ? "-" + pi : "");
        group.add(mesh);
      });
    });
    var grove = new T.Group();
    grove.name = "street-trees";
    group.add(grove);
    M.grove = grown ? WorldKit.grove(T, grove, /** @type {Object} */ (woods), wood) : null;
    traffic(T, group, P, parked, fleet);
    walkers(T, group, segs, lod ? 46 : 0);
    scene.add(group);
    M.group = group;
    var moving = 0;
    M.cars.forEach(function (set) { moving += set.moving.length; });
    return { solids: solids, lights: lights, cars: moving, parked: parked.length, people: M.walkers.length, scans: placed,
             crowd: M.people && M.people.crowd ? M.people.crowd.count : 0, trees: M.grove ? M.grove.placed : 0 };
  }

  /** @param {any} T @param {any} group @param {number} P @param {Array<Array<number>>} parked @param {Object<string, Array<{geo: any}>>} [fleet] */
  function traffic(T, group, P, parked, fleet) {
    var kinds = ["sedan", "hatch", "suv", "van"].filter(function (k) { return fleet && fleet[k + "_lod0"] && fleet[k + "_lod1"]; });
    var made = function (/** @type {Array<{geo: any}>} */ p) { return p.length > 2 ? { body: p[0].geo, glass: p[1].geo, trim: p[2].geo, lamp: p[3].geo } : { body: p[0].geo, lamp: p[1].geo }; };
    var types = kinds.length ? kinds.map(function (k) { return [made(fleet[k + "_lod0"]), made(fleet[k + "_lod1"])]; }) : [[car(T, false)], [car(T, true)]];
    var loops = [], moving = types.map(function () { return []; });
    [2, 5.8].forEach(function (lane, li) {
      var path = measure(loopPath(P, lane));
      var n = Math.round(path.len / 75);
      for (var i = 0; i < n; i++) {
        var type = Math.floor(rnd(i * 7 + li) * types.length);
        moving[type].push({ loop: li, s: path.len * i / n + rnd(i) * 20, v: 9 + rnd(i * 3 + li) * 3, max: 10 + rnd(i * 5 + li) * 4, k: moving[type].length });
      }
      loops.push(path);
    });
    var ring = measure((function () {
      var pts = [], R = P + 2.2;
      for (var i = 0; i < 96; i++) { var t = -i / 96 * Math.PI * 2; pts.push([Math.cos(t) * R, Math.sin(t) * R]); }
      return pts;
    })());
    loops.push(ring);
    for (var r = 0; r < Math.max(3, Math.round(ring.len / 40)); r++) moving[0].push({ loop: 2, s: ring.len * r / Math.round(ring.len / 40), v: 8, max: 9, k: moving[0].length });
    M.loops = loops;
    M.cars = types.map(function (geos, ti) {
      var still = parked.filter(function (p) { return Math.min(types.length - 1, Math.floor(p[3] * types.length)) === ti; });
      var cap = Math.max(1, still.length + moving[ti].length), q = new T.Quaternion(), e = new T.Euler(), at = new T.Vector3(), one = new T.Vector3(1, 1, 1);
      var lods = geos.map(function (geo, l) {
        var mk = function (/** @type {any} */ g, /** @type {any} */ mat, /** @type {string} */ name) {
          if (!g) return null;
          var m = new T.InstancedMesh(g, mat, cap);
          m.frustumCulled = false;
          m.name = "car-" + name + ti + (l ? "-far" : "");
          if (name === "body" || name === "lamp") m.setColorAt(0, new T.Color(1, 1, 1));
          group.add(m);
          return m;
        };
        return { body: mk(geo.body, M.mats.paint, "body"), glass: mk(geo.glass, M.mats.glass, "glass"), trim: mk(geo.trim, M.mats.trim, "trim"),
                 lamp: mk(geo.lamp, M.mats.lamps, "lamp") };
      });
      moving[ti].forEach(function (mv, j) { mv.paint = new T.Color(PAINT[Math.floor(rnd(j * 13.1 + ti * 3) * PAINT.length)]); });
      return { lods: lods, moving: moving[ti], parked: still.map(function (p) {
        return { m: new T.Matrix4().compose(at.set(p[0], 0, p[1]), q.setFromEuler(e.set(0, p[2] - Math.PI / 2, 0)), one),
                 paint: new T.Color(PAINT[Math.floor(rnd(p[0] * 0.37 + p[1] * 0.11) * PAINT.length)]) };
      }) };
    });
  }

  /** @param {any} T @param {any} group @param {Array<Object>} segs @param {number} n */
  function walkers(T, group, segs, n) {
    var list = [];
    var walkable = segs.filter(function (s) { return s.a1 - s.a0 > 20 && Math.abs(s.c) < 140; });
    for (var i = 0; i < n && walkable.length; i++) {
      var sg = walkable[Math.floor(rnd(i * 5.1) * walkable.length)];
      list.push({ sg: sg, d: sg.road + 1.6 + rnd(i * 2.3) * (sg.walk - 2.6), s: rnd(i * 7.7) * (sg.a1 - sg.a0), dir: rnd(i * 9.1) > 0.5 ? 1 : -1, v: 1.1 + rnd(i * 3.9) * 0.5 });
    }
    M.walkers = list;
    var cap = Math.max(1, list.length), c = new T.Color();
    var brolly = new T.InstancedMesh(umbrella(T), M.mats.brolly, cap);
    list.forEach(function (w, i) { brolly.setColorAt(i, c.set(BRELLA[Math.floor(rnd(i * 8.8) * BRELLA.length)])); });
    brolly.frustumCulled = false;
    brolly.name = "street-umbrellas";
    group.add(brolly);
    var crowd = WorldPeople.crowd(T, cap, n > 0);
    if (crowd) {
      crowd.meshes.forEach(function (m) { group.add(m); });
      list.forEach(function (w, i) {
        var pick = function (/** @type {Array<string>} */ xs, /** @type {number} */ k) { return new T.Color(xs[Math.floor(rnd(i * k) * xs.length)]).multiplyScalar(2); };
        w.c = Math.floor(rnd(i * 3.3) * crowd.count);
        w.tints = [pick(SKIN, 5.7), pick(COAT.concat(BRIGHT), 6.6), pick(LEGS, 7.4), pick(HAIRS, 9.2)];
        w.ph = rnd(i * 4.4);
        w.m = new T.Matrix4();
      });
      M.people = { crowd: crowd, brolly: brolly, body: null };
      return;
    }
    var body = person(T);
    var walk = new Float32Array(cap * 2);
    list.forEach(function (w, i) { walk[i * 2] = rnd(i * 4.4) * 6.28; walk[i * 2 + 1] = w.v / 1.3; });
    body.setAttribute("aWalk", new T.InstancedBufferAttribute(walk, 2));
    var people = new T.InstancedMesh(body, M.mats.people, cap);
    list.forEach(function (w, i) { people.setColorAt(i, c.set(COAT[Math.floor(rnd(i * 6.6) * COAT.length)])); });
    people.frustumCulled = false;
    group.add(people);
    people.name = "street-people";
    M.people = { body: people, brolly: brolly, crowd: null };
  }

  var tmp = [0, 0, 0], ahead = [0, 0, 0];

  /** @param {number} t @param {number} dt @param {number} night @param {boolean} still @param {any} [eye] */
  function frame(t, dt, night, still, eye) {
    var T = M.T;
    if (!T || !M.cars) return;
    if (M.grove) M.grove.update(eye);
    var mtx = new T.Matrix4(), q = new T.Quaternion(), e = new T.Euler(), at = new T.Vector3(), one = new T.Vector3(1, 1, 1);
    var moving = [];
    M.cars.forEach(function (set) { set.moving.forEach(function (mv) { moving.push(mv); }); });
    var byLoop = [[], [], []];
    moving.forEach(function (mv) { byLoop[mv.loop].push(mv); });
    var phase = (t % 30);
    byLoop.forEach(function (cars, li) {
      var path = M.loops[li];
      if (!path) return;
      cars.sort(function (a, b) { return a.s - b.s; });
      cars.forEach(function (mv, i) {
        var next = cars[(i + 1) % cars.length], gap = next === mv ? 999 : ((next.s - mv.s) % path.len + path.len) % path.len;
        var target = mv.max;
        if (gap < 22) target = Math.min(target, Math.max(0, (gap - 7) * 0.9));
        if (li < 2) {
          along(path, mv.s, tmp);
          along(path, mv.s + 14, ahead);
          var nsAxis = Math.abs(Math.sin(tmp[2])) < 0.5;
          var red = (nsAxis ? (phase + 15) % 30 : phase) >= 11.5;
          var stopAt = stopLine(tmp, ahead);
          if (red && stopAt >= 0) target = Math.min(target, Math.max(0, (stopAt - 1) * 0.8));
        }
        if (!still) {
          mv.v += Math.max(-6 * dt, Math.min(2.5 * dt, target - mv.v));
          mv.s += Math.max(0, mv.v) * dt;
        }
      });
    });
    var light = [], off = new T.Color(0.04, 0.04, 0.04), on = new T.Color(1, 1, 1);
    M.cars.forEach(function (set) {
      var n = [0, 0];
      var put = function (/** @type {any} */ m, /** @type {any} */ paint, /** @type {any} */ lit) {
        var l = set.lods[1] && eye && Math.hypot(m.elements[12] - eye.x, m.elements[14] - eye.z) > 36 ? 1 : 0, s = set.lods[l], k = n[l]++;
        [s.body, s.glass, s.trim, s.lamp].forEach(function (x) { if (x) x.setMatrixAt(k, m); });
        s.body.setColorAt(k, paint);
        s.lamp.setColorAt(k, lit);
      };
      set.parked.forEach(function (c) { put(c.m, c.paint, off); });
      set.moving.forEach(function (mv) {
        along(M.loops[mv.loop], mv.s, tmp);
        along(M.loops[mv.loop], mv.s + 2.5, ahead);
        var yaw = Math.atan2(ahead[0] - tmp[0], ahead[1] - tmp[1]);
        put(mtx.compose(at.set(tmp[0], 0, tmp[1]), q.setFromEuler(e.set(0, yaw - Math.PI / 2, 0)), one), mv.paint, on);
        var fx = Math.sin(yaw), fz = Math.cos(yaw);
        if (night > 0.05) {
          light.push({ p: [tmp[0] + fx * 7, 0.9, tmp[1] + fz * 7], c: [14, 13, 11], r: 13, d: 0, f: 0, day: false });
          light.push({ p: [tmp[0] - fx * 2.8, 0.7, tmp[1] - fz * 2.8], c: [5, 0.25, 0.1], r: 5, d: 0, f: 0, day: false });
        }
      });
      set.lods.forEach(function (s, l) {
        [s.body, s.glass, s.trim, s.lamp].forEach(function (m) {
          if (!m) return;
          m.count = n[l];
          m.visible = n[l] > 0;
          m.instanceMatrix.needsUpdate = true;
          if (m.instanceColor) m.instanceColor.needsUpdate = true;
        });
      });
      set.lods[0].lamp.material.color.setScalar(1.2 + 6 * night);
    });
    WorldKit.L.moving = light;
    var P = M.people;
    if (P) {
      var drawn = [], now = WorldKit.uniforms.time.value, grip = new T.Vector3(), shift = new T.Matrix4();
      M.walkers.forEach(function (w, i) {
        var len = w.sg.a1 - w.sg.a0;
        if (!still) w.s += w.v * dt * w.dir;
        if (w.s > len) { w.s = len; w.dir = -1; }
        if (w.s < 0) { w.s = 0; w.dir = 1; }
        var p = spot(w.sg, w.sg.a0 + w.s, w.d);
        var yaw = w.sg.axis === 0 ? (w.dir > 0 ? 0 : Math.PI) : (w.dir > 0 ? Math.PI / 2 : -Math.PI / 2);
        if (!P.crowd) {
          mtx.compose(at.set(p[0], 0, p[1]), q.setFromEuler(e.set(0, yaw, 0)), one);
          P.body.setMatrixAt(i, mtx);
          P.brolly.setMatrixAt(i, mtx);
          return;
        }
        var rate = still ? 0 : w.v / 1.5, f = now * rate + w.ph, hand = WorldPeople.hand(w.c, f - Math.floor(f));
        w.m.compose(at.set(p[0], 0, p[1]), q.setFromEuler(e.set(0, yaw + Math.PI, 0)), one);
        P.brolly.setMatrixAt(i, mtx.copy(w.m).multiply(shift.makeTranslation(grip.set(hand[0] - GRIP[0], hand[1] - GRIP[1], hand[2] - GRIP[2]))));
        var far = eye ? Math.hypot(p[0] - eye.x, p[1] - eye.z) : 0;
        drawn.push({ c: w.c, lod: far > 24 ? 1 : 0, m: w.m, walk: [w.ph, rate], tints: w.tints });
      });
      if (P.crowd) WorldPeople.draw(drawn);
      else P.body.count = M.walkers.length;
      P.brolly.count = M.walkers.length;
      if (P.body) P.body.instanceMatrix.needsUpdate = true;
      P.brolly.instanceMatrix.needsUpdate = true;
    }
    if (M.glows) {
      M.glows.material.uniforms.uOpacity.value = night * 0.75;
      M.glows.visible = night > 0.05;
    }
  }

  /** @param {Array<number>} p @param {Array<number>} a @returns {number} */
  function stopLine(p, a) {
    var PITCH = C.PITCH, dx = a[0] - p[0], dz = a[1] - p[1], best = -1;
    if (Math.abs(dz) > Math.abs(dx)) {
      var dir = Math.sign(dz);
      for (var k = -C.K; k <= C.K; k++) {
        var cz = k * PITCH, stop = cz - dir * (C.half(k).road + C.half(k).walk + 0.8), d = (stop - p[1]) * dir;
        if (Math.abs(p[0]) > 20 || k === 0) continue;
        if (d > 0 && d < 16 && (best < 0 || d < best)) best = d;
      }
    } else {
      var dirx = Math.sign(dx);
      for (var m = -C.K; m <= C.K; m++) {
        var cx = m * PITCH, stopx = cx - dirx * (C.half(m).road + C.half(m).walk + 0.8), dd = (stopx - p[0]) * dirx;
        if (Math.abs(p[1]) > 20 || m === 0) continue;
        if (dd > 0 && dd < 16 && (best < 0 || dd < best)) best = dd;
      }
    }
    return best;
  }

  return Object.freeze({ build: build, frame: frame });
})();
