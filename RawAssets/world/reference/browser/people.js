"use strict";

var WorldPeople = (function () {
  var D = { hero: [], crowd: [], styles: {}, morphs: {}, clips: {}, fits: [] };
  var S = { T: null, fill: { value: 0.25 }, tex: new Map(), mats: new Map(), crowd: null, parts: [] };
  var FRAMES = 24;
  var BREATH = Math.PI * 2 / 1.7;
  var MOTIONS = 6;
  var SIDES = [["l", -1], ["r", 1]];
  var FINGERS = ["thumb", "index", "middle", "ring", "pinky"];
  var SCATTER = [
    "#define PK_THROUGH vec3( 0.62, 0.24, 0.14 )",
    "float pkThin = 0.0; float pkSkin = 1.0;",
    "void RE_Direct_Skin( const in IncidentLight directLight, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight ) {",
    "  RE_Direct_Physical( directLight, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight );",
    "  float pkNl = dot( geometryNormal, directLight.direction );",
    "  float pkWrap = max( ( pkNl + 0.45 ) / 1.45, 0.0 ) - max( pkNl, 0.0 );",
    "  reflectedLight.directDiffuse += directLight.color * ( pkWrap * vec3( 0.55, 0.22, 0.14 ) * pkSkin + max( -pkNl, 0.0 ) * pkThin * PK_THROUGH ) * BRDF_Lambert( material.diffuseColor );",
    "}",
    "#undef RE_Direct",
    "#define RE_Direct RE_Direct_Skin"
  ].join("\n");
  var CARDS = ["hair", "beard", "brows", "lashes"];
  var GRAZE = "material.specularF90 = 0.25;";
  var MATTE = "material.specularColor = vec3( 0.0 ); material.specularF90 = 0.0;";
  var THIN = "pkThin = texelRoughness.b;";
  var CROWD = "pkSkin = step( vRole, 0.5 ); material.specularF90 = abs( vRole - 3.0 ) < 0.5 ? 0.25 : 1.0;";
  var BEHIND = [
    "#if defined( USE_ENVMAP ) && defined( ENVMAP_TYPE_CUBE_UV )",
    "iblIrradiance += pkThin * PK_THROUGH * getIBLIrradiance( -geometryNormal );",
    "#endif",
    "#if NUM_HEMI_LIGHTS > 0",
    "for ( int pkI = 0; pkI < NUM_HEMI_LIGHTS; pkI ++ ) irradiance += pkThin * PK_THROUGH * getHemisphereLightIrradiance( hemisphereLights[ pkI ], -geometryNormal );",
    "#endif"
  ].join("\n");

  /** @param {Object} data */
  function use(data) {
    var m = (data && data.manifest) || {};
    D.hero = (data && data.hero) || [];
    D.crowd = (data && data.crowd) || [];
    D.styles = { hair: ["hair", m.hair || {}], beard: ["face", m.face || {}], glasses: ["glasses", m.glasses || {}] };
    D.morphs = m.morphs || {};
    D.clips = m.clips || {};
    D.fits = (m.hero || []).map(function (h) { return h.fits || {}; });
  }

  /** @returns {boolean} */
  function ready() {
    return D.hero.length > 0;
  }

  /** @returns {Array<{role: string, tinted: boolean, rough: boolean, ao: boolean, thin: boolean, colour: string}>} */
  function parts() {
    return S.parts.slice();
  }

  /** @param {any} T @param {Array<number>} a @returns {any} */
  function v3(T, a) {
    return new T.Vector3(a[0], a[1], a[2]);
  }

  /** @param {any} T @param {any} aim @param {any} pole @returns {any} */
  function frameQ(T, aim, pole) {
    var x = aim.clone().normalize(), y = pole.clone().addScaledVector(x, -pole.dot(x));
    if (y.lengthSq() < 1e-8) y.set(0, 0, -1).addScaledVector(x, x.z);
    y.normalize();
    var z = new T.Vector3().crossVectors(x, y);
    return new T.Quaternion().setFromRotationMatrix(new T.Matrix4().makeBasis(x, y, z));
  }

  /** @param {any} T @param {number} p @param {number} y @param {number} r @returns {any} */
  function euler(T, p, y, r) {
    return new T.Quaternion().setFromEuler(new T.Euler(p, y, r, "YXZ"));
  }

  /** @param {any} T @param {Object} R @param {string} name @returns {number} */
  function at(T, R, name) {
    var i = R.index[name];
    return i === undefined ? -1 : i;
  }

  /** @param {any} T @param {Object} R @param {number} a @param {number} b @returns {any} */
  function aimOf(T, R, a, b) {
    return R.wp[b].clone().sub(R.wp[a]).normalize();
  }

  /** @param {any} T @param {any} aim @returns {any} */
  function ahead(T, aim) {
    return new T.Vector3(0, 0, -1).addScaledVector(aim, aim.z).normalize();
  }

  /** @param {any} T @param {Array<Object>} joints @param {Array<any>} [move] @param {Object} [anchors] @returns {Object} */
  function rig(T, joints, move, anchors) {
    var R = { n: joints.length, names: [], index: {}, parent: [], lq: [], lt: [], wp: [], wq: [], legs: [], arms: [], spine: [], neck: [], fingers: [] };
    joints.forEach(function (j, i) {
      var p = j.parent;
      R.names.push(j.name);
      R.index[j.name] = i;
      R.parent.push(p);
      R.lq.push(new T.Quaternion().fromArray(j.q));
      R.wq.push(p >= 0 ? R.wq[p].clone().multiply(R.lq[i]) : R.lq[i].clone());
      R.wp.push(p >= 0 ? v3(T, j.t).applyQuaternion(R.wq[p]).add(R.wp[p]) : v3(T, j.t));
    });
    if (move) R.wp.forEach(function (w, i) { if (move[i]) w.add(move[i]); });
    R.wp.forEach(function (w, i) {
      var p = R.parent[i];
      R.lt.push(p >= 0 ? w.clone().sub(R.wp[p]).applyQuaternion(R.wq[p].clone().invert()) : w.clone());
    });
    ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"].forEach(function (n) { if (at(T, R, n) >= 0) R.spine.push(at(T, R, n)); });
    ["neck_01", "neck_02"].forEach(function (n) { if (at(T, R, n) >= 0) R.neck.push(at(T, R, n)); });
    R.pelvis = Math.max(0, at(T, R, "pelvis"));
    R.head = at(T, R, "head");
    SIDES.forEach(function (sd) {
      var s = sd[0], th = at(T, R, "thigh_" + s), ca = at(T, R, "calf_" + s), ft = at(T, R, "foot_" + s);
      if (th >= 0 && ca >= 0 && ft >= 0) {
        var la = aimOf(T, R, th, ca), lb = aimOf(T, R, ca, ft);
        R.legs.push({ s: sd[1], th: th, ca: ca, ft: ft, ball: at(T, R, "ball_" + s), rest: [frameQ(T, la, ahead(T, la)).invert(), frameQ(T, lb, ahead(T, lb)).invert()] });
      }
      var ua = at(T, R, "upperarm_" + s), lo = at(T, R, "lowerarm_" + s), hd = at(T, R, "hand_" + s);
      if (ua >= 0 && lo >= 0 && hd >= 0) {
        var aa = aimOf(T, R, ua, lo), ab = aimOf(T, R, lo, hd), palm = palmOf(T, R, s, hd, anchors);
        R.arms.push({ s: sd[1], cl: at(T, R, "clavicle_" + s), ua: ua, lo: lo, hd: hd, palm: palm,
                      rest: [frameQ(T, aa, ahead(T, aa)).invert(), frameQ(T, ab, palm).invert()] });
        var side = new T.Vector3().crossVectors(ab, palm).normalize();
        FINGERS.forEach(function (f) {
          for (var k = 1; k <= 3; k++) {
            var fi = at(T, R, f + "_0" + k + "_" + s);
            if (fi < 0) continue;
            var ax = (f === "thumb" ? palm.clone().cross(ab).normalize().negate() : side.clone()).applyQuaternion(R.wq[fi].clone().invert());
            R.fingers.push({ i: fi, s: sd[1], ax: ax, k: f === "thumb" ? 0.35 : 1 });
          }
        });
      }
    });
    R.ibm = R.wp.map(function (w, i) { return new T.Matrix4().compose(w, R.wq[i], new T.Vector3(1, 1, 1)).invert(); });
    R.ankle = R.legs.map(function (l) { return [R.wp[l.ft].y, l.ball >= 0 ? R.wp[l.ball].y : R.wp[l.ft].y]; });
    var hipY = R.legs.length ? (R.wp[R.legs[0].th].y + R.wp[R.legs[R.legs.length - 1].th].y) / 2 : R.wp[R.pelvis].y;
    R.seatLift = R.wp[R.pelvis].clone().sub(new T.Vector3(0, hipY, R.legs.length ? R.wp[R.legs[0].th].z : R.wp[R.pelvis].z));
    return R;
  }

  /** @param {any} T @param {Object} R @param {string} s @param {number} hd @param {Object} [anchors] @returns {any} */
  function palmOf(T, R, s, hd, anchors) {
    var key = "palm" + s.toUpperCase();
    if (anchors && anchors[key]) return v3(T, anchors[key]).normalize();
    var ix = at(T, R, "index_01_" + s), pk = at(T, R, "pinky_01_" + s), md = at(T, R, "middle_01_" + s), tb = at(T, R, "thumb_02_" + s);
    if (ix >= 0 && pk >= 0 && md >= 0) {
      var f = R.wp[md].clone().sub(R.wp[hd]), k = R.wp[pk].clone().sub(R.wp[ix]), n = new T.Vector3().crossVectors(f, k).normalize();
      if (tb >= 0 && R.wp[tb].clone().sub(R.wp[hd]).dot(n) < 0) n.negate();
      return n;
    }
    return new T.Vector3(0, -1, 0);
  }

  /** @param {number} a @param {number} b @param {number} t @returns {number} */
  function mix(a, b, t) {
    return a + (b - a) * t;
  }

  /** @param {number} x @returns {number} */
  function pos(x) {
    return x > 0 ? x : 0;
  }

  /** @param {{gait: number, amp: number, run: number, talk: number, t: number, still: boolean, seated: boolean, rolled: number, hold: boolean, type: boolean}} s @returns {Object} */
  function posture(s) {
    var g = s.gait, a = s.amp, r = s.run, tk = s.talk, br = s.still ? 0 : Math.sin(s.t * 1.7);
    var P = { hip: [0, 0, 0], pel: [0, 0, 0], spine: [0.02 * br * 0.4, 0, 0], head: [0, 0, 0], clav: [0, 0], legs: [], arms: [], seated: s.seated, ground: !s.seated };
    var sway = s.still || s.sway === false ? 0 : Math.sin(s.t * 0.45);
    if (s.seated) {
      P.spine = [-0.06 + 0.01 * br, 0, 0];
      P.legs = [[1.42, 0.1, 1.45, 0.05], [1.42, 0.1, 1.45, 0.05]];
      var push = Math.sin(s.rolled / 0.35) * Math.min(1, a);
      P.arms = [[-0.15 + 0.35 * push, 0.32, 0.55 - 0.25 * push, [-1, -0.4, 0], 0.55, 0], [-0.15 + 0.35 * push, 0.32, 0.55 - 0.25 * push, [1, -0.4, 0], 0.55, 0]];
      if (s.type) {
        var tap = 0.05 * Math.sin(s.t * 1.7 * 6);
        P.spine = [0.05 + 0.005 * br, 0, 0];
        P.head = [0.1, 0, 0];
        P.arms = [[0.5 + tap, 0.18, 1.45 - tap, [-0.2, -1, 0.3], 0.3, 0], [0.5 - tap, 0.18, 1.45 + tap, [0.2, -1, 0.3], 0.3, 0]];
      }
    } else {
      var L = [g, g + Math.PI].map(function (ph) {
        var th = mix(0.06 + 0.4 * Math.sin(ph), 0.2 + 0.72 * Math.sin(ph), r);
        var kn = mix(0.08 + 1.05 * Math.pow(pos(Math.cos(ph + 0.45)), 2) + 0.2 * Math.pow(pos(Math.sin(ph - 0.5)), 3),
                     0.25 + 1.75 * Math.pow(pos(Math.cos(ph + 0.75)), 1.4), r);
        var ft = 0.28 * Math.pow(pos(Math.sin(ph + 0.1)), 6) - mix(0.55, 0.75, r) * Math.pow(pos(-Math.sin(ph + 0.65)), 5) + 0.05 * pos(Math.cos(ph));
        return [th * a, 0.03 + 0.02 * (1 - a), kn * a + 0.04 * (1 - a), ft * a];
      });
      P.legs = L;
      P.hip = [0.012 * sway * (1 - a), 0, 0];
      P.pel = [mix(0.02, 0.1, r) * a, -mix(0.07, 0.12, r) * Math.sin(g) * a, 0.03 * Math.cos(g) * a * (1 - r)];
      P.spine[0] += mix(0.03, 0.2, r) * a;
      P.spine[1] = mix(0.13, 0.2, r) * Math.sin(g) * a;
      P.head = [-mix(0.03, 0.18, r) * a, -0.05 * Math.sin(g) * a + 0.04 * sway * (1 - a), 0];
      P.arms = SIDES.map(function (sd, i) {
        var ph = g + (i ? Math.PI : 0), sw = -Math.sin(ph);
        var el = mix(0.25 + 0.2 * pos(sw), 1.45 + 0.2 * sw, r) * a + 0.18 * (1 - a);
        return [mix(0.32, 0.75, r) * sw * a + 0.02, 0.1 + 0.04 * r * a, el, [-sd[1], 0, 0.2], 0.4, 0];
      });
    }
    if (s.hold) {
      P.arms[1] = [0.3, 0.12, 1.42, [-1, 0.15, 0], 0.9, -0.1];
    }
    if (tk > 0) {
      var out = [0.55, 1.15, 0.3, [0, 1, -0.2], 0.08, -0.1];
      var fore = [0.12, 0.12, 1.2, [0.4, 1, 0], 0.25, 0];
      P.arms = P.arms.map(function (arm, i) {
        var to = i ? out : fore;
        return [mix(arm[0], to[0], tk), mix(arm[1], to[1], tk), mix(arm[2], to[2], tk),
                [mix(arm[3][0], to[3][0] * (i ? 1 : -1), tk), mix(arm[3][1], to[3][1], tk), mix(arm[3][2], to[3][2], tk)],
                mix(arm[4], to[4], tk), mix(arm[5], to[5], tk)];
      });
      P.head = [P.head[0] - 0.04 * tk, P.head[1], P.head[2] + 0.08 * tk];
    }
    return P;
  }

  /** @param {any} T @param {Object} R @param {Object} P @param {Array<any>} Q @param {any} hip @returns {Object} */
  function solve(T, R, P, Q, hip) {
    var W = new Array(R.n), done = new Array(R.n), X = new T.Vector3(1, 0, 0), DOWN = new T.Vector3(0, -1, 0), FWD = new T.Vector3(0, 0, -1);
    var dp = euler(T, P.pel[0], P.pel[1], P.pel[2]), yaw = euler(T, 0, P.pel[1], 0);
    W[R.pelvis] = dp.clone().multiply(R.wq[R.pelvis]);
    done[R.pelvis] = true;
    var chest = dp.clone();
    R.spine.forEach(function (i, k) {
      var f = (k + 1) / R.spine.length;
      chest = euler(T, P.spine[0] * f, P.spine[1] * f, P.spine[2] * f).multiply(dp);
      W[i] = chest.clone().multiply(R.wq[i]);
      done[i] = true;
    });
    var dh = euler(T, P.head[0], P.head[1], P.head[2]).multiply(chest);
    R.neck.forEach(function (i, k) {
      W[i] = chest.clone().slerp(dh, (k + 1) / (R.neck.length + 1)).multiply(R.wq[i]);
      done[i] = true;
    });
    if (R.head >= 0) { W[R.head] = dh.clone().multiply(R.wq[R.head]); done[R.head] = true; }
    var out = { legL: 0, armR: 0 };
    R.legs.forEach(function (l, i) {
      var p = P.legs[i] || [0, 0.03, 0.04, 0], A = yaw.clone().multiply(euler(T, p[0], 0, l.s * p[1]));
      var aim = DOWN.clone().applyQuaternion(A), bend = FWD.clone().applyQuaternion(A), hinge = X.clone().applyQuaternion(A);
      W[l.th] = frameQ(T, aim, bend).multiply(l.rest[0]).multiply(R.wq[l.th]);
      var calf = aim.clone().applyAxisAngle(hinge, -p[2]), cpole = bend.clone().applyAxisAngle(hinge, -p[2]);
      W[l.ca] = frameQ(T, calf, cpole).multiply(l.rest[1]).multiply(R.wq[l.ca]);
      var foot = yaw.clone().multiply(euler(T, p[3], 0, 0));
      W[l.ft] = foot.clone().multiply(R.wq[l.ft]);
      if (l.ball >= 0) W[l.ball] = yaw.clone().multiply(euler(T, Math.max(0, p[3]), 0, 0)).multiply(R.wq[l.ball]);
      done[l.th] = done[l.ca] = done[l.ft] = true;
      if (l.ball >= 0) done[l.ball] = true;
      if (l.s < 0) out.legL = Math.atan2(-aim.z, -aim.y);
    });
    R.arms.forEach(function (m, i) {
      var p = P.arms[i] || [0, 0.1, 0.2, [-m.s, 0, 0], 0.3, 0];
      if (m.cl >= 0) { W[m.cl] = euler(T, 0, 0, m.s * (P.clav[i] || 0)).multiply(chest).multiply(R.wq[m.cl]); done[m.cl] = true; }
      var A = chest.clone().multiply(euler(T, p[0], 0, m.s * p[1]));
      var aim = DOWN.clone().applyQuaternion(A), bend = FWD.clone().applyQuaternion(A), hinge = X.clone().applyQuaternion(A);
      W[m.ua] = frameQ(T, aim, bend).multiply(m.rest[0]).multiply(R.wq[m.ua]);
      var fore = aim.clone().applyAxisAngle(hinge, p[2]), palm = v3(T, p[3]).applyQuaternion(chest);
      W[m.lo] = frameQ(T, fore, palm).multiply(m.rest[1]).multiply(R.wq[m.lo]);
      var wrist = new T.Vector3().crossVectors(fore, palm).normalize();
      W[m.hd] = new T.Quaternion().setFromAxisAngle(wrist, p[5]).multiply(W[m.lo]).multiply(R.wq[m.lo].clone().invert()).multiply(R.wq[m.hd]);
      done[m.ua] = done[m.lo] = done[m.hd] = true;
      if (m.s > 0) out.armR = Math.atan2(Math.abs(aim.x), -aim.y);
      m.curl = p[4];
    });
    for (var i = 0; i < R.n; i++) {
      var par = R.parent[i];
      if (!done[i]) W[i] = (par >= 0 ? W[par].clone() : new T.Quaternion()).multiply(R.lq[i]);
      Q[i].copy(par >= 0 ? W[par].clone().invert().multiply(W[i]) : W[i]);
    }
    R.fingers.forEach(function (f) {
      var arm = R.arms.filter(function (m) { return m.s === f.s; })[0];
      Q[f.i].multiply(new T.Quaternion().setFromAxisAngle(f.ax, (arm ? arm.curl : 0.3) * f.k));
    });
    var wp = new Array(R.n);
    hip.copy(R.wp[R.pelvis]).add(v3(T, P.hip));
    if (P.seated) hip.copy(new T.Vector3(0, 0.62, 0.06).add(R.seatLift));
    for (var j = 0; j < R.n; j++) {
      var pj = R.parent[j];
      wp[j] = pj >= 0 ? R.lt[j].clone().applyQuaternion(W[pj]).add(wp[pj]) : hip.clone();
    }
    if (P.ground && R.legs.length) {
      var low = Infinity;
      R.legs.forEach(function (l, k) {
        low = Math.min(low, wp[l.ft].y - R.ankle[k][0]);
        if (l.ball >= 0) low = Math.min(low, wp[l.ball].y - R.ankle[k][1]);
      });
      hip.y -= low;
      wp.forEach(function (w) { w.y -= low; });
    }
    out.hips = hip.y;
    out.wp = wp;
    out.W = W;
    return out;
  }

  /** @param {Object} look @param {string} key @returns {Object} */
  function weights(look, key) {
    var m = D.morphs[key] || {};
    return m[look[key]] || {};
  }

  /** @param {Object} look @returns {number} */
  function choose(look) {
    var best = 0, score = -1;
    D.fits.forEach(function (f, i) {
      var n = 0;
      Object.keys(f).forEach(function (k) { if (look[k] === f[k]) n++; });
      if (n > score) { score = n; best = i; }
    });
    return best;
  }

  /** @param {any} T @param {any} img @param {boolean} srgb @returns {any} */
  function texture(T, img, srgb) {
    if (!img) return null;
    if (S.tex.has(img)) return S.tex.get(img);
    var t = new T.Texture(img);
    t.flipY = false;
    t.anisotropy = 4;
    t.colorSpace = srgb ? T.SRGBColorSpace : T.NoColorSpace;
    t.needsUpdate = true;
    S.tex.set(img, t);
    return t;
  }

  /** @param {any} T @param {Object} part @returns {any} */
  function material(T, part) {
    if (S.mats.has(part)) return S.mats.get(part);
    var orm = texture(T, part.orm, false), skin = part.role === "skin", thin = skin && !!orm && !!part.thin;
    var mat = new T.MeshStandardMaterial({
      map: texture(T, part.map, true), normalMap: texture(T, part.normal, false), roughness: part.rough, metalness: 0,
      roughnessMap: orm, aoMap: part.ao ? orm : null,
      alphaTest: part.alpha || 0, side: part.two ? T.DoubleSide : T.FrontSide
    });
    WorldKit.lit(mat, "person-" + part.role + (part.normal ? "-n" : "") + (part.alpha ? "-a" : "") + (orm ? "-r" : "") + (part.ao ? "-o" : "") + (thin ? "-t" : ""), { porous: skin ? 0.3 : 0.8, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uFill = S.fill;
      sh.fragmentShader = "uniform float uFill;\n" + sh.fragmentShader.replace("#include <emissivemap_fragment>",
        "#include <emissivemap_fragment>\ntotalEmissiveRadiance += diffuseColor.rgb * uFill;");
      if (skin) sh.fragmentShader = sh.fragmentShader.replace("#include <lights_physical_pars_fragment>", "#include <lights_physical_pars_fragment>\n" + SCATTER);
      if (CARDS.indexOf(part.role) >= 0) sh.fragmentShader = sh.fragmentShader.replace("#include <lights_physical_fragment>", "#include <lights_physical_fragment>\n" + (part.role === "lashes" ? MATTE : GRAZE));
      if (thin) {
        sh.fragmentShader = sh.fragmentShader.replace("#include <roughnessmap_fragment>", "#include <roughnessmap_fragment>\n" + THIN)
          .replace("#include <lights_fragment_maps>", "#include <lights_fragment_maps>\n" + BEHIND);
      }
    } });
    S.mats.set(part, mat);
    return mat;
  }

  /** @param {Object} part @param {Object} look @returns {string} */
  function tint(part, look) {
    if (part.tint === false) return "";
    var covered = look.hair === "none" || look.hair === "scarf" || look.hair === "wrap";
    if (part.role === "skin") return look.skin;
    if (part.role === "top") return look.top;
    if (part.role === "bottom") return look.bottom;
    if (part.role === "lashes") return "#1c1a22";
    if (part.role === "brows") return covered ? "#3b2a20" : look.hairColour;
    if (part.role === "hair" || part.role === "beard") return look.hairColour;
    return "";
  }

  /** @param {any} T @param {Object} part @param {Object<string, number>} w @returns {any} */
  function geometry(T, part, w) {
    var p = part.pos;
    var names = Object.keys(w).filter(function (k) { return w[k] && part.morph[k]; });
    if (names.length) {
      p = Float32Array.from(part.pos);
      names.forEach(function (k) { var d = part.morph[k], s = w[k]; for (var i = 0; i < p.length; i++) p[i] += d[i] * s; });
    }
    var g = new T.BufferGeometry();
    g.setAttribute("position", new T.BufferAttribute(p, 3));
    g.setAttribute("normal", new T.BufferAttribute(part.nor, 3));
    g.setAttribute("uv", new T.BufferAttribute(part.uv, 2));
    g.setAttribute("skinIndex", new T.BufferAttribute(part.joint, 4));
    g.setAttribute("skinWeight", new T.BufferAttribute(part.weight, 4));
    g.setIndex(new T.BufferAttribute(part.idx, 1));
    return g;
  }

  /** @param {Object} ex @param {Object<string, number>} w @returns {Object} */
  function anchored(ex, w) {
    var out = JSON.parse(JSON.stringify(ex.anchors || {})), at = ex.anchorTargets || {};
    Object.keys(w).forEach(function (k) {
      var d = at[k] || {};
      Object.keys(d).forEach(function (a) {
        if (Array.isArray(out[a])) out[a] = out[a].map(function (/** @type {number} */ v, /** @type {number} */ i) { return v + d[a][i] * w[k]; });
        else if (out[a] !== undefined) out[a] += d[a] * w[k];
      });
    });
    return out;
  }

  /** @param {Array<any>} geos @param {Object} a @param {Object} R @returns {function(number, number): number} */
  function hull(geos, a, R) {
    var lo = a.chin - 0.4, cz = 0, rows = 44, bins = 72, t = new Float32Array(rows * bins);
    geos.forEach(function (g) {
      var p = g.attributes.position.array, j = g.attributes.skinIndex.array;
      for (var i = 0; i < p.length; i += 3) {
        var r = Math.round((p[i + 1] - lo) / 0.01);
        if (r < 0 || r >= rows || /arm|hand|_0[123]_/.test(R.names[j[i / 3 * 4]] || "")) continue;
        var dx = p[i], dz = p[i + 2] - cz, f = Math.atan2(dx, -dz), b = Math.floor((f / (Math.PI * 2) + 1) % 1 * bins);
        t[r * bins + b] = Math.max(t[r * bins + b], Math.hypot(dx, dz));
      }
    });
    return function (y, f) {
      var r = Math.round((y - lo) / 0.01), b = Math.floor((f / (Math.PI * 2) + 1) % 1 * bins), m = 0;
      for (var i = r - 1; i <= r + 1; i++) for (var j = b - 2; j <= b + 2; j++) if (i >= 0 && i < rows) m = Math.max(m, t[i * bins + (j + bins) % bins]);
      return m;
    };
  }

  /** @param {any} T @param {any} geo @param {Array<any>} geos @param {number} fallback */
  function transfer(T, geo, geos, fallback) {
    var cell = 0.03, grid = new Map(), key = function (/** @type {number} */ x, /** @type {number} */ y, /** @type {number} */ z) { return x + "," + y + "," + z; };
    geos.forEach(function (g) {
      var p = g.attributes.position.array;
      for (var i = 0; i < p.length; i += 3) {
        var k = key(Math.floor(p[i] / cell), Math.floor(p[i + 1] / cell), Math.floor(p[i + 2] / cell));
        if (!grid.has(k)) grid.set(k, []);
        grid.get(k).push([g, i / 3]);
      }
    });
    var q = geo.attributes.position.array, n = q.length / 3, J = new Uint16Array(n * 4), W = new Float32Array(n * 4);
    for (var v = 0; v < n; v++) {
      var x = q[v * 3], y = q[v * 3 + 1], z = q[v * 3 + 2], cx = Math.floor(x / cell), cy = Math.floor(y / cell), cz = Math.floor(z / cell), best = null, bd = Infinity;
      for (var r = 1; r <= 4 && !best; r++) {
        for (var a = -r; a <= r; a++) for (var b = -r; b <= r; b++) for (var c = -r; c <= r; c++) {
          (grid.get(key(cx + a, cy + b, cz + c)) || []).forEach(function (/** @type {Array<any>} */ e) {
            var p = e[0].attributes.position.array, d = Math.pow(p[e[1] * 3] - x, 2) + Math.pow(p[e[1] * 3 + 1] - y, 2) + Math.pow(p[e[1] * 3 + 2] - z, 2);
            if (d < bd) { bd = d; best = e; }
          });
        }
      }
      if (best) {
        J.set(best[0].attributes.skinIndex.array.slice(best[1] * 4, best[1] * 4 + 4), v * 4);
        W.set(best[0].attributes.skinWeight.array.slice(best[1] * 4, best[1] * 4 + 4), v * 4);
      } else { J[v * 4] = fallback; W[v * 4] = 1; }
    }
    geo.setAttribute("skinIndex", new T.BufferAttribute(J, 4));
    geo.setAttribute("skinWeight", new T.BufferAttribute(W, 4));
  }

  /** @param {any} T @param {Object} look @returns {Object|null} */
  function hero(T, look) {
    if (!ready()) return null;
    S.T = T;
    var fig = D.hero[choose(look)], skin = fig.skins[0];
    if (!skin) return null;
    var w = Object.assign({}, weights(look, "figure"), weights(look, "build"), weights(look, "age"));
    var jt = skin.extras.jointTargets || {}, move = skin.joints.map(function (_, i) {
      var v = new T.Vector3();
      Object.keys(w).forEach(function (k) { if (jt[k] && jt[k][i]) v.addScaledVector(v3(T, jt[k][i]), w[k]); });
      return v;
    });
    var R = rig(T, skin.joints, move, skin.extras.anchors);
    var group = new T.Group(), bones = R.names.map(function (n) { var b = new T.Bone(); b.name = n; return b; });
    bones.forEach(function (b, i) {
      b.position.copy(R.lt[i]);
      b.quaternion.copy(R.lq[i]);
      if (R.parent[i] >= 0) bones[R.parent[i]].add(b); else group.add(b);
    });
    group.updateMatrixWorld(true);
    var skel = new T.Skeleton(bones), found = {}, body = [], drawn = S.parts = [];
    fig.parts.forEach(function (part) {
      var opt = D.styles[part.role];
      if (opt && part.style !== opt[1][look[opt[0]]]) return;
      if (opt) found[part.role] = true;
      var mat = material(T, part), hex = tint(part, look);
      if (hex) mat.color.set(hex).multiply(new T.Color(1 / part.tone[0], 1 / part.tone[1], 1 / part.tone[2]));
      drawn.push({ role: part.role, tinted: !!hex, rough: !!mat.roughnessMap, ao: !!mat.aoMap, thin: !!part.thin && !!mat.roughnessMap, colour: mat.color.getHexString() });
      var mesh = new T.SkinnedMesh(geometry(T, part, w), mat);
      if (part.role === "skin" || part.role === "top") body.push(mesh.geometry);
      mesh.frustumCulled = false;
      mesh.bind(skel, new T.Matrix4());
      group.add(mesh);
    });
    var an = anchored(skin.extras, w);
    if (an.chin !== undefined && body.length) an.hull = hull(body, an, R);
    var extra = WorldHero.dress(T, look, an, { hair: !found.hair, face: !found.beard, glasses: !found.glasses });
    if (extra && body.length) {
      transfer(T, extra.geometry, body, Math.max(0, R.head));
      var worn = new T.SkinnedMesh(extra.geometry, extra.material);
      worn.frustumCulled = false;
      worn.bind(skel, new T.Matrix4());
      group.add(worn);
    }
    var Q = bones.map(function () { return new T.Quaternion(); });
    return { kind: "skinned", group: group, bones: bones, R: R, Q: Q, anchors: an, hip: new T.Vector3(), seated: look.move === "wheelchair",
             state: { gait: 0, amp: 0, phase: null, t: null }, measure: { legL: 0, armR: 0, hips: 0 }, clip: fig.clips.filter(function (c) { return c.name === D.clips.idle; })[0] || null };
  }

  /** @param {Object} clip @param {Object} R @param {number} t @param {Array<any>} Q @param {number} k */
  function sample(clip, R, t, Q, k) {
    clip.channels.forEach(function (c) {
      var i = R.index[c.joint];
      if (i === undefined || c.path !== "rotation") return;
      var n = c.times.length, end = c.times[n - 1] || 1, x = t % end, j = 0;
      while (j < n - 2 && c.times[j + 1] < x) j++;
      var f = n > 1 ? Math.min(1, Math.max(0, (x - c.times[j]) / ((c.times[j + 1] - c.times[j]) || 1))) : 0;
      var a = Q[i].clone().fromArray(c.values, j * 4), b = Q[i].clone().fromArray(c.values, Math.min(n - 1, j + 1) * 4);
      Q[i].slerp(a.slerp(b, f), k);
    });
  }

  /** @param {Object} h @param {{phase: number, speed: number, talk: number, t: number, reduced: boolean, rolled: number, turn: number}} s */
  function pose(h, s) {
    var T = S.T, st = h.state, dt = st.t == null ? 0 : Math.max(0, Math.min(0.1, s.t - st.t));
    var went = st.phase == null ? 0 : (s.phase - st.phase) / 3.4;
    st.phase = s.phase;
    st.t = s.t;
    var speed = Math.min(1.8, s.speed), turning = speed < 0.15 && Math.abs(s.turn || 0) > 0.4;
    var want = turning ? Math.min(0.45, Math.abs(s.turn) * 0.25) : Math.min(1, speed * 1.4);
    st.amp = s.reduced ? want : st.amp + (want - st.amp) * Math.min(1, dt * 7);
    var run = Math.max(0, Math.min(1, (speed - 0.35) / 1.2));
    var stride = mix(1.5, 3.4, run);
    st.gait += Math.abs(went) / stride * Math.PI * 2 * Math.sign(went || 1) + (turning ? Math.abs(s.turn) * dt * 2.2 : 0);
    var P = posture({ gait: st.gait, amp: st.amp, run: run * Math.min(1, speed), talk: s.talk, t: s.t, still: s.reduced, seated: h.seated, rolled: s.rolled, hold: false });
    var out = solve(T, h.R, P, h.Q, h.hip);
    if (h.clip && st.amp < 0.99 && !h.seated) sample(h.clip, h.R, s.t, h.Q, 1 - st.amp);
    h.bones.forEach(function (b, i) { b.quaternion.copy(h.Q[i]); });
    h.bones[h.R.pelvis].position.copy(h.hip);
    h.measure.legL = out.legL;
    h.measure.armR = out.armR;
    h.measure.hips = out.hips;
  }

  /** @param {Object} h */
  function dispose(h) {
    h.group.traverse(function (o) { if (o.geometry) o.geometry.dispose(); });
  }

  /** @param {number} v */
  function fill(v) {
    S.fill.value = v;
  }

  /** @param {any} T @param {Object} R @param {boolean} hold @param {number} [motion] @returns {{bones: Float32Array, hand: Float32Array}} */
  function bake(T, R, hold, motion) {
    var Q = R.lq.map(function () { return new T.Quaternion(); }), hip = new T.Vector3(), m = new T.Matrix4(), one = new T.Vector3(1, 1, 1);
    var bones = new Float32Array(FRAMES * R.n * 16), hand = new Float32Array(FRAMES * 3), hr = R.index.hand_r;
    for (var f = 0; f < FRAMES; f++) {
      var still = { gait: 0, amp: 0, run: 0, talk: motion === 2 ? 1 : 0, t: f / FRAMES * BREATH, still: false, sway: false, seated: motion === 3 || motion === 5, type: motion === 5, rolled: 0, hold: false };
      var walk = { gait: f / FRAMES * Math.PI * 2, amp: 1, run: 0, talk: 0, t: 0, still: true, seated: false, rolled: 0, hold: motion === 4 ? false : hold };
      var out = solve(T, R, posture(motion && motion !== 4 ? still : walk), Q, hip);
      for (var i = 0; i < R.n; i++) {
        m.compose(out.wp[i], out.W[i], one).multiply(R.ibm[i]);
        bones.set(m.elements, (f * R.n + i) * 16);
      }
      if (hr !== undefined) hand.set([out.wp[hr].x, out.wp[hr].y, out.wp[hr].z], f * 3);
    }
    return { bones: bones, hand: hand };
  }

  /** @param {any} T @param {number} n @param {boolean} gl2 @returns {Object|null} */
  function crowd(T, n, gl2) {
    if (!D.crowd.length || !gl2 || !n) return null;
    S.T = T;
    var chars = [], width = 0;
    D.crowd.forEach(function (file) {
      file.skins.forEach(function (skin, si) {
        var lods = file.parts.filter(function (p) { return p.skin === si; }).sort(function (a, b) { return (a.extras.lod || 0) - (b.extras.lod || 0); });
        if (!lods.length) return;
        var R = rig(T, skin.joints, null, skin.extras.anchors), baked = bake(T, R, true);
        width = Math.max(width, R.n * 4);
        chars.push({ R: R, lods: lods, baked: baked, motions: [baked, bake(T, R, false, 1), bake(T, R, false, 2), bake(T, R, false, 3), bake(T, R, false, 4), bake(T, R, false, 5)] });
      });
    });
    if (!chars.length) return null;
    var data = new Float32Array(width * 4 * chars.length * FRAMES * MOTIONS);
    chars.forEach(function (c, ci) {
      c.motions.forEach(function (b, mo) {
        for (var f = 0; f < FRAMES; f++) data.set(b.bones.subarray(f * c.R.n * 16, (f + 1) * c.R.n * 16), (((mo * chars.length + ci) * FRAMES + f) * width) * 4);
      });
    });
    var tex = new T.DataTexture(data, width, chars.length * FRAMES * MOTIONS, T.RGBAFormat, T.FloatType);
    tex.minFilter = tex.magFilter = T.NearestFilter;
    tex.generateMipmaps = false;
    tex.needsUpdate = true;
    var first = chars[0].lods[0];
    var mat = new T.MeshStandardMaterial({ map: texture(T, first.map, true), roughness: 0.78, metalness: 0, alphaTest: 0.5 });
    WorldKit.lit(mat, "crowd", { porous: 0.8, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uBones = { value: tex };
      sh.uniforms.uTime = WorldKit.uniforms.time;
      sh.uniforms.uFrames = { value: FRAMES };
      sh.uniforms.uFill = S.fill;
      sh.vertexShader = [
        "uniform highp sampler2D uBones; uniform float uTime; uniform float uFrames;",
        "attribute vec4 aJoint; attribute vec4 aWeight; attribute float aRole;",
        "attribute vec3 aWalk; attribute vec3 aSkin; attribute vec3 aTop; attribute vec3 aBottom; attribute vec3 aHair;",
        "varying vec3 vTint; varying float vRole;",
        "mat4 wpBone( float j, float f ) { ivec2 c = ivec2( int( j ) * 4, int( aWalk.z + f ) );",
        "  return mat4( texelFetch( uBones, c, 0 ), texelFetch( uBones, c + ivec2( 1, 0 ), 0 ), texelFetch( uBones, c + ivec2( 2, 0 ), 0 ), texelFetch( uBones, c + ivec2( 3, 0 ), 0 ) ); }",
        "mat4 wpSkin( float f ) { return wpBone( aJoint.x, f ) * aWeight.x + wpBone( aJoint.y, f ) * aWeight.y + wpBone( aJoint.z, f ) * aWeight.z + wpBone( aJoint.w, f ) * aWeight.w; }",
        sh.vertexShader.replace("#include <beginnormal_vertex>", [
          "#include <beginnormal_vertex>",
          "float wpF = fract( uTime * aWalk.y + aWalk.x ) * uFrames; float wp0 = floor( wpF );",
          "mat4 wpM = wpSkin( wp0 ) * ( 1.0 - ( wpF - wp0 ) ) + wpSkin( mod( wp0 + 1.0, uFrames ) ) * ( wpF - wp0 );",
          "objectNormal = mat3( wpM ) * objectNormal;",
          "vTint = aRole < 0.5 ? aSkin : aRole < 1.5 ? aTop : aRole < 2.5 ? aBottom : aRole < 3.5 ? aHair : vec3( 1.0 ); vRole = aRole;"
        ].join("\n")).replace("#include <begin_vertex>", "#include <begin_vertex>\ntransformed = ( wpM * vec4( transformed, 1.0 ) ).xyz;")
      ].join("\n");
      sh.fragmentShader = "uniform float uFill; varying vec3 vTint; varying float vRole;\n" + sh.fragmentShader
        .replace("#include <map_fragment>", "#include <map_fragment>\ndiffuseColor.rgb *= vTint;")
        .replace("#include <emissivemap_fragment>", "#include <emissivemap_fragment>\ntotalEmissiveRadiance += diffuseColor.rgb * uFill * 0.5;")
        .replace("#include <lights_physical_pars_fragment>", "#include <lights_physical_pars_fragment>\n" + SCATTER)
        .replace("#include <lights_physical_fragment>", "#include <lights_physical_fragment>\n" + CROWD);
    } });
    var meshes = [];
    chars.forEach(function (c, ci) {
      c.meshes = c.lods.map(function (part, li) {
        var g = new T.InstancedBufferGeometry();
        g.setAttribute("position", new T.BufferAttribute(part.pos, 3));
        g.setAttribute("normal", new T.BufferAttribute(part.nor, 3));
        g.setAttribute("uv", new T.BufferAttribute(part.uv, 2));
        g.setAttribute("aJoint", new T.BufferAttribute(part.joint, 4));
        g.setAttribute("aWeight", new T.BufferAttribute(part.weight, 4));
        g.setAttribute("aRole", new T.BufferAttribute(part.roles || new Float32Array(part.pos.length / 3).fill(5), 1));
        g.setIndex(new T.BufferAttribute(part.idx, 1));
        [["aWalk", 3], ["aSkin", 3], ["aTop", 3], ["aBottom", 3], ["aHair", 3]].forEach(function (a) {
          g.setAttribute(a[0], new T.InstancedBufferAttribute(new Float32Array(n * 3), 3).setUsage(T.DynamicDrawUsage));
        });
        var mesh = new T.InstancedMesh(g, mat, n);
        mesh.frustumCulled = false;
        mesh.count = 0;
        mesh.name = "crowd-" + ci + "-" + li;
        meshes.push(mesh);
        return mesh;
      });
    });
    S.crowd = { chars: chars, meshes: meshes, mat: mat, agents: null };
    return { meshes: meshes, count: chars.length };
  }

  /** @param {any} T @param {number} cap @returns {Array<any>|null} */
  function agents(T, cap) {
    if (!S.crowd) return null;
    S.crowd.agents = S.crowd.chars.map(function (c, ci) {
      var g0 = c.meshes[0].geometry, g = new T.InstancedBufferGeometry();
      ["position", "normal", "uv", "aJoint", "aWeight", "aRole"].forEach(function (a) { g.setAttribute(a, g0.attributes[a]); });
      g.setIndex(g0.index);
      [["aWalk", 3], ["aSkin", 3], ["aTop", 3], ["aBottom", 3], ["aHair", 3]].forEach(function (a) {
        g.setAttribute(a[0], new T.InstancedBufferAttribute(new Float32Array(cap * 3), 3).setUsage(T.DynamicDrawUsage));
      });
      var mesh = new T.InstancedMesh(g, S.crowd.mat, cap);
      mesh.frustumCulled = false;
      mesh.count = 0;
      mesh.name = "agent-" + ci;
      return mesh;
    });
    return S.crowd.agents;
  }

  /** @param {string} key @returns {number} */
  function cast(key) {
    var h = 0;
    for (var i = 0; i < key.length; i++) h = (h * 31 + key.charCodeAt(i)) >>> 0;
    return S.crowd ? h % S.crowd.chars.length : 0;
  }

  /** @param {Array<{key: string, m: any, motion: number, phase: number, rate: number, tints: Array<any>}>} list @returns {number} */
  function placeAgents(list) {
    var A = S.crowd && S.crowd.agents;
    if (!A) return 0;
    var n = S.crowd.chars.length;
    A.forEach(function (m) { m.count = 0; });
    list.forEach(function (a) {
      var ci = cast(a.key), mesh = A[ci], k = mesh.count++, g = mesh.geometry;
      mesh.setMatrixAt(k, a.m);
      g.attributes.aWalk.setXYZ(k, a.phase, a.motion % 4 ? 1 / BREATH : a.rate, (a.motion * n + ci) * FRAMES);
      ["aSkin", "aTop", "aBottom", "aHair"].forEach(function (at, i) { g.attributes[at].setXYZ(k, a.tints[i].r, a.tints[i].g, a.tints[i].b); });
    });
    A.forEach(function (m) {
      m.visible = m.count > 0;
      m.instanceMatrix.needsUpdate = true;
      ["aWalk", "aSkin", "aTop", "aBottom", "aHair"].forEach(function (at) { m.geometry.attributes[at].needsUpdate = true; });
    });
    return list.length;
  }

  /** @param {number} ci @param {number} f @returns {Array<number>} */
  function hand(ci, f) {
    var c = S.crowd && S.crowd.chars[ci];
    if (!c) return [0.18, 1.2, -0.18];
    var k = ((Math.floor(f * FRAMES) % FRAMES) + FRAMES) % FRAMES, h = c.baked.hand;
    return [h[k * 3], h[k * 3 + 1], h[k * 3 + 2]];
  }

  /** @param {Object} c @param {number} slot @param {number} ci @param {any} mtx @param {Array<number>} walk @param {Array<any>} tints */
  function place(c, slot, ci, mtx, walk, tints) {
    var mesh = c.meshes[slot], k = mesh.count++, g = mesh.geometry;
    mesh.setMatrixAt(k, mtx);
    g.attributes.aWalk.setXYZ(k, walk[0], walk[1], ci * FRAMES);
    ["aSkin", "aTop", "aBottom", "aHair"].forEach(function (a, i) { g.attributes[a].setXYZ(k, tints[i].r, tints[i].g, tints[i].b); });
  }

  /** @param {Array<{c: number, lod: number, m: any, walk: Array<number>, tints: Array<any>}>} list */
  function draw(list) {
    if (!S.crowd) return;
    S.crowd.meshes.forEach(function (m) { m.count = 0; });
    list.forEach(function (w) {
      var c = S.crowd.chars[w.c];
      if (c) place(c, Math.min(w.lod, c.meshes.length - 1), w.c, w.m, w.walk, w.tints);
    });
    S.crowd.meshes.forEach(function (m) {
      m.visible = m.count > 0;
      m.instanceMatrix.needsUpdate = true;
      ["aWalk", "aSkin", "aTop", "aBottom", "aHair"].forEach(function (a) { m.geometry.attributes[a].needsUpdate = true; });
    });
  }

  return Object.freeze({ FRAMES: FRAMES, use: use, ready: ready, parts: parts, hero: hero, pose: pose, dispose: dispose, fill: fill, crowd: crowd, hand: hand, draw: draw, rig: rig, solve: solve, posture: posture,
                         agents: agents, placeAgents: placeAgents, cast: cast });
})();
