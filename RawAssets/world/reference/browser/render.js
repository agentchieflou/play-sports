"use strict";

var WorldRender = (function () {
  var TIERS = {
    low: { msaa: 0, ao: 0, bloom: 0, reflect: 0, cap: 1, lights: 8 },
    medium: { msaa: 0, ao: 0.5, bloom: 1, reflect: 0.33, cap: 1.25, lights: 16 },
    high: { msaa: 4, ao: 0.5, bloom: 1, reflect: 0.5, cap: 1.5, lights: 24 },
    ultra: { msaa: 4, ao: 1, bloom: 1, reflect: 0.6, cap: 2, lights: 32 }
  };
  var ORDER = ["low", "medium", "high", "ultra"];
  var VERT = "varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }";
  var LEVELS = 6;
  var R = {
    T: null, renderer: null, scene: null, camera: null, tier: "low", cfg: TIERS.low, gl2: false, soft: false, w: 1, h: 1,
    hdr: null, ao: [], down: [], up: [], ldr: null, refl: null, mirror: null, fs: null, m: {},
    exposure: 1, night: 0, frame: 0, calls: 0, triangles: 0
  };

  /** @param {any} T @param {any} renderer @param {string} wanted @returns {string} */
  function choose(T, renderer, wanted) {
    var gl = renderer.getContext(), info = gl.getExtension("WEBGL_debug_renderer_info");
    var name = String(info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER));
    R.soft = /swiftshader|llvmpipe|software|basic render/i.test(name);
    if (!renderer.capabilities.isWebGL2) return "low";
    if (TIERS[wanted]) return wanted;
    if (R.soft) return "low";
    if (/intel|mali|adreno|powervr|apple gpu|videocore/i.test(name)) return "medium";
    return "high";
  }

  /** @param {any} T @param {Object} uniforms @param {string} frag @param {Object} [extra] @returns {any} */
  function pass(T, uniforms, frag, extra) {
    return new T.ShaderMaterial(Object.assign({
      uniforms: uniforms, vertexShader: VERT, fragmentShader: frag, depthTest: false, depthWrite: false, toneMapped: false
    }, extra || {}));
  }

  /** @param {any} mat @param {any} target */
  function draw(mat, target) {
    R.fs.mesh.material = mat;
    R.renderer.setRenderTarget(target);
    R.renderer.render(R.fs.scene, R.fs.cam);
  }

  /** @param {any} T */
  function materials(T) {
    var px = function () { return { value: new T.Vector2() }; };
    R.m.ao = pass(T, {
      tDepth: { value: null }, uTexel: px(), uInv: { value: new T.Matrix4() }, uScale: { value: 1 },
      uNear: { value: 0.1 }, uFar: { value: 400 }, uRadius: { value: 0.9 }, uPower: { value: 1.4 }
    }, "#include <packing>\nuniform sampler2D tDepth; uniform vec2 uTexel; uniform mat4 uInv; uniform float uScale;"
      + " uniform float uNear; uniform float uFar; uniform float uRadius; uniform float uPower; varying vec2 vUv;"
      + " vec3 vp(vec2 uv) { float d = texture2D(tDepth, uv).x; vec4 c = uInv * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0); return c.xyz / c.w; }"
      + " float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }"
      + " void main() { float d0 = texture2D(tDepth, vUv).x; if (d0 >= 0.9999) { gl_FragColor = vec4(1.0); return; }"
      + " vec3 p = vp(vUv); vec3 pr = vp(vUv + vec2(uTexel.x, 0.0)); vec3 pl = vp(vUv - vec2(uTexel.x, 0.0));"
      + " vec3 pu = vp(vUv + vec2(0.0, uTexel.y)); vec3 pd = vp(vUv - vec2(0.0, uTexel.y));"
      + " vec3 dx = abs(pr.z - p.z) < abs(p.z - pl.z) ? pr - p : p - pl; vec3 dy = abs(pu.z - p.z) < abs(p.z - pd.z) ? pu - p : p - pd;"
      + " vec3 n = normalize(cross(dx, dy)); if (dot(n, p) > 0.0) n = -n;"
      + " float r = min(uRadius * uScale / max(-p.z, 0.1), 90.0); float a0 = hash(gl_FragCoord.xy) * 6.2832; float occ = 0.0; float r2 = uRadius * uRadius;"
      + " for (int i = 0; i < 14; i++) { float t = (float(i) + 0.5) / 14.0; float a = a0 + t * 37.7;"
      + " vec2 o = vec2(cos(a), sin(a)) * t * r * uTexel; vec3 v = vp(vUv + o) - p; float vv = dot(v, v);"
      + " occ += max(dot(v, n) - 0.012 * -p.z, 0.0) / (vv + 0.02) * (1.0 - smoothstep(r2 * 0.6, r2 * 1.6, vv)); }"
      + " float ao = clamp(1.0 - occ / 14.0 * 1.6, 0.0, 1.0); gl_FragColor = vec4(pow(ao, uPower), 0.0, 0.0, 1.0); }");
    R.m.blur = pass(T, {
      tAo: { value: null }, tDepth: { value: null }, uDir: px(), uNear: { value: 0.1 }, uFar: { value: 400 }
    }, "#include <packing>\nuniform sampler2D tAo; uniform sampler2D tDepth; uniform vec2 uDir; uniform float uNear; uniform float uFar; varying vec2 vUv;"
      + " float z(vec2 uv) { return -perspectiveDepthToViewZ(texture2D(tDepth, uv).x, uNear, uFar); }"
      + " void main() { float z0 = z(vUv); float s = 0.0; float w = 0.0;"
      + " for (int i = -4; i <= 4; i++) { vec2 uv = vUv + uDir * float(i); float k = exp(-float(i * i) / 10.0)"
      + " * exp(-abs(z(uv) - z0) / (0.04 * z0 + 0.05)); s += texture2D(tAo, uv).r * k; w += k; }"
      + " gl_FragColor = vec4(s / w, 0.0, 0.0, 1.0); }");
    R.m.pre = pass(T, { tSrc: { value: null }, uTexel: px(), uKnee: { value: 1.2 } },
      "uniform sampler2D tSrc; uniform vec2 uTexel; uniform float uKnee; varying vec2 vUv;"
      + " vec3 k(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b)) * 0.25); }"
      + " void main() { vec3 a = texture2D(tSrc, vUv + uTexel * vec2(-1.0, -1.0)).rgb; vec3 b = texture2D(tSrc, vUv + uTexel * vec2(1.0, -1.0)).rgb;"
      + " vec3 c = texture2D(tSrc, vUv + uTexel * vec2(-1.0, 1.0)).rgb; vec3 d = texture2D(tSrc, vUv + uTexel * vec2(1.0, 1.0)).rgb;"
      + " vec3 m = (k(a) + k(b) + k(c) + k(d)) * 0.25; float l = max(m.r, max(m.g, m.b));"
      + " float soft = clamp(l - uKnee * 0.5, 0.0, uKnee); soft = soft * soft / (4.0 * uKnee + 1e-4);"
      + " gl_FragColor = vec4(m * max(soft, l - uKnee) / max(l, 1e-4), 1.0); }");
    R.m.down = pass(T, { tSrc: { value: null }, uTexel: px() },
      "uniform sampler2D tSrc; uniform vec2 uTexel; varying vec2 vUv;"
      + " void main() { vec3 s = texture2D(tSrc, vUv).rgb * 4.0;"
      + " s += texture2D(tSrc, vUv + uTexel * vec2(-1.0, -1.0)).rgb + texture2D(tSrc, vUv + uTexel * vec2(1.0, -1.0)).rgb;"
      + " s += texture2D(tSrc, vUv + uTexel * vec2(-1.0, 1.0)).rgb + texture2D(tSrc, vUv + uTexel * vec2(1.0, 1.0)).rgb;"
      + " gl_FragColor = vec4(s / 8.0, 1.0); }");
    R.m.up = pass(T, { tLow: { value: null }, tHigh: { value: null }, uTexel: px(), uMix: { value: 0.75 } },
      "uniform sampler2D tLow; uniform sampler2D tHigh; uniform vec2 uTexel; uniform float uMix; varying vec2 vUv;"
      + " void main() { vec2 o = uTexel; vec3 s = texture2D(tLow, vUv + vec2(-o.x * 2.0, 0.0)).rgb + texture2D(tLow, vUv + vec2(o.x * 2.0, 0.0)).rgb"
      + " + texture2D(tLow, vUv + vec2(0.0, -o.y * 2.0)).rgb + texture2D(tLow, vUv + vec2(0.0, o.y * 2.0)).rgb;"
      + " s += (texture2D(tLow, vUv + vec2(-o.x, o.y)).rgb + texture2D(tLow, vUv + vec2(o.x, o.y)).rgb"
      + " + texture2D(tLow, vUv + vec2(-o.x, -o.y)).rgb + texture2D(tLow, vUv + vec2(o.x, -o.y)).rgb) * 2.0;"
      + " gl_FragColor = vec4(texture2D(tHigh, vUv).rgb + s / 12.0 * uMix, 1.0); }");
    var grade = "vec3 aces(vec3 x) { mat3 i = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);"
      + " mat3 o = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);"
      + " vec3 v = i * x; vec3 a = v * (v + 0.0245786) - 0.000090537; vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;"
      + " return clamp(o * (a / b), 0.0, 1.0); }"
      + " vec3 srgb(vec3 c) { return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }"
      + " float h(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }";
    R.m.comp = pass(T, {
      tHdr: { value: null }, tAo: { value: null }, tBloom: { value: null }, uAo: { value: 0 }, uBloom: { value: 0 },
      uExposure: { value: 1 }, uTime: { value: 0 }, uNight: { value: 0 }, uGrain: { value: 0.03 }, uRes: px()
    }, "uniform sampler2D tHdr; uniform sampler2D tAo; uniform sampler2D tBloom; uniform float uAo; uniform float uBloom;"
      + " uniform float uExposure; uniform float uTime; uniform float uNight; uniform float uGrain; uniform vec2 uRes; varying vec2 vUv;\n" + grade
      + " void main() { vec2 c = vUv - 0.5; float r2 = dot(c, c); vec2 ca = c * r2 * 0.0035;"
      + " vec3 col = vec3(texture2D(tHdr, vUv - ca).r, texture2D(tHdr, vUv).g, texture2D(tHdr, vUv + ca).b);"
      + " float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));"
      + " float ao = texture2D(tAo, vUv).r; col *= mix(1.0, ao, uAo * (1.0 - smoothstep(0.6, 3.0, lum)));"
      + " col += texture2D(tBloom, vUv).rgb * uBloom;"
      + " col *= uExposure; col = aces(col / 0.6);"
      + " float l = dot(col, vec3(0.2126, 0.7152, 0.0722));"
      + " vec3 tintLo = mix(vec3(0.98, 1.0, 1.03), vec3(0.88, 0.98, 1.12), uNight); vec3 tintHi = mix(vec3(1.02, 1.0, 0.97), vec3(1.08, 0.98, 0.9), uNight);"
      + " col *= mix(tintLo, tintHi, smoothstep(0.05, 0.6, l)); col = mix(vec3(l), col, 1.06 - 0.08 * uNight);"
      + " col = clamp(col, 0.0, 1.0); col = srgb(col);"
      + " col *= mix(1.0, 1.0 - 0.9 * r2, 0.55 + 0.25 * uNight);"
      + " col += (h(vUv * uRes + fract(uTime) * 91.0) - 0.5) * uGrain;"
      + " gl_FragColor = vec4(col, 1.0); }");
    R.m.fxaa = pass(T, { tSrc: { value: null }, uTexel: px(), uTime: { value: 0 }, uGrain: { value: 0.03 }, uRes: px() },
      "uniform sampler2D tSrc; uniform vec2 uTexel; uniform float uTime; uniform float uGrain; uniform vec2 uRes; varying vec2 vUv;"
      + " float h(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }"
      + " void main() { vec3 L = vec3(0.299, 0.587, 0.114);"
      + " vec3 nw = texture2D(tSrc, vUv + vec2(-1.0, -1.0) * uTexel).rgb; vec3 ne = texture2D(tSrc, vUv + vec2(1.0, -1.0) * uTexel).rgb;"
      + " vec3 sw = texture2D(tSrc, vUv + vec2(-1.0, 1.0) * uTexel).rgb; vec3 se = texture2D(tSrc, vUv + vec2(1.0, 1.0) * uTexel).rgb;"
      + " vec3 m = texture2D(tSrc, vUv).rgb; float lnw = dot(nw, L); float lne = dot(ne, L); float lsw = dot(sw, L); float lse = dot(se, L); float lm = dot(m, L);"
      + " float lo = min(lm, min(min(lnw, lne), min(lsw, lse))); float hi = max(lm, max(max(lnw, lne), max(lsw, lse)));"
      + " vec2 dir = vec2(-((lnw + lne) - (lsw + lse)), (lnw + lsw) - (lne + lse));"
      + " float red = max((lnw + lne + lsw + lse) * 0.03125, 1.0 / 128.0); float rcp = 1.0 / (min(abs(dir.x), abs(dir.y)) + red);"
      + " dir = clamp(dir * rcp, vec2(-8.0), vec2(8.0)) * uTexel;"
      + " vec3 a = 0.5 * (texture2D(tSrc, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture2D(tSrc, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);"
      + " vec3 b = a * 0.5 + 0.25 * (texture2D(tSrc, vUv - dir * 0.5).rgb + texture2D(tSrc, vUv + dir * 0.5).rgb);"
      + " float lb = dot(b, L); vec3 col = (lb < lo || lb > hi) ? a : b;"
      + " col += (h(vUv * uRes + fract(uTime) * 91.0) - 0.5) * uGrain; gl_FragColor = vec4(col, 1.0); }");
  }

  /** @param {any} T @param {number} w @param {number} h @param {Object} [o] @returns {any} */
  function target(T, w, h, o) {
    return new T.WebGLRenderTarget(Math.max(1, Math.round(w)), Math.max(1, Math.round(h)), Object.assign({
      type: T.HalfFloatType, format: T.RGBAFormat, minFilter: T.LinearFilter, magFilter: T.LinearFilter,
      depthBuffer: false, generateMipmaps: false
    }, o || {}));
  }

  function free() {
    [R.hdr, R.ldr, R.refl].concat(R.ao, R.down, R.up).forEach(function (t) {
      if (t) { if (t.depthTexture) t.depthTexture.dispose(); t.dispose(); }
    });
    R.hdr = R.ldr = R.refl = null;
    R.ao = []; R.down = []; R.up = [];
  }

  function alloc() {
    var T = R.T, c = R.cfg, w = R.w, h = R.h;
    free();
    if (R.tier === "low") return;
    R.hdr = target(T, w, h, { depthBuffer: true, samples: c.msaa, depthTexture: new T.DepthTexture(w, h) });
    if (c.ao) R.ao = [target(T, w * c.ao, h * c.ao, { type: T.UnsignedByteType }), target(T, w * c.ao, h * c.ao, { type: T.UnsignedByteType })];
    if (c.bloom) {
      for (var i = 0; i < LEVELS; i++) {
        var s = Math.pow(0.5, i + 1);
        R.down.push(target(T, w * s, h * s));
        R.up.push(target(T, w * s, h * s));
      }
    }
    if (!c.msaa) R.ldr = target(T, w, h, { type: T.UnsignedByteType });
    if (c.reflect) {
      R.refl = target(T, w * c.reflect, h * c.reflect, {
        depthBuffer: true, generateMipmaps: true, minFilter: T.LinearMipmapLinearFilter
      });
    }
  }

  /** @param {any} T @param {any} renderer @param {any} scene @param {any} camera @param {string} wanted @returns {string} */
  function create(T, renderer, scene, camera, wanted) {
    R.T = T; R.renderer = renderer; R.scene = scene; R.camera = camera;
    R.gl2 = renderer.capabilities.isWebGL2;
    renderer.info.autoReset = false;
    var g = new T.BufferGeometry();
    g.setAttribute("position", new T.BufferAttribute(new Float32Array([-1, -1, 0, 3, -1, 0, -1, 3, 0]), 3));
    g.setAttribute("uv", new T.BufferAttribute(new Float32Array([0, 0, 2, 0, 0, 2]), 2));
    R.fs = { scene: new T.Scene(), cam: new T.OrthographicCamera(-1, 1, 1, -1, 0, 1), mesh: null };
    R.fs.mesh = new T.Mesh(g, null);
    R.fs.mesh.frustumCulled = false;
    R.fs.scene.add(R.fs.mesh);
    R.mirror = new T.PerspectiveCamera();
    R.mirror.layers.set(0);
    camera.layers.enable(1);
    if (R.gl2) materials(T);
    var name = choose(T, renderer, wanted);
    setTier(name);
    return name;
  }

  /** @param {string} name */
  function setTier(name) {
    var T = R.T;
    R.tier = TIERS[name] && (R.gl2 || name === "low") ? name : "low";
    R.cfg = TIERS[R.tier];
    var low = R.tier === "low";
    R.renderer.toneMapping = low ? T.ACESFilmicToneMapping : T.NoToneMapping;
    resize();
  }

  function resize() {
    var v = R.renderer.getDrawingBufferSize(new R.T.Vector2());
    R.w = v.x; R.h = v.y;
    alloc();
  }

  /** @returns {number} */
  function step(dir) {
    var i = ORDER.indexOf(R.tier) + dir;
    if (i < 0 || i >= ORDER.length) return 0;
    setTier(ORDER[i]);
    return 1;
  }

  function mirror() {
    var T = R.T, cam = R.camera, m = R.mirror;
    cam.updateMatrixWorld();
    var n = new T.Vector3(0, 1, 0), at = new T.Vector3(0, 0, 0), cp = new T.Vector3().setFromMatrixPosition(cam.matrixWorld);
    var rot = new T.Matrix4().extractRotation(cam.matrixWorld);
    var view = at.clone().sub(cp).reflect(n).negate().add(at);
    var look = new T.Vector3(0, 0, -1).applyMatrix4(rot).add(cp);
    var tgt = at.clone().sub(look).reflect(n).negate().add(at);
    m.position.copy(view);
    m.up.set(0, 1, 0).applyMatrix4(rot).reflect(n);
    m.lookAt(tgt);
    m.near = cam.near; m.far = cam.far;
    m.updateMatrixWorld();
    m.projectionMatrix.copy(cam.projectionMatrix);
    var plane = new T.Plane().setFromNormalAndCoplanarPoint(n, at).applyMatrix4(m.matrixWorldInverse);
    var clip = new T.Vector4(plane.normal.x, plane.normal.y, plane.normal.z, plane.constant), e = m.projectionMatrix.elements;
    var q = new T.Vector4((Math.sign(clip.x) + e[8]) / e[0], (Math.sign(clip.y) + e[9]) / e[5], -1, (1 + e[10]) / e[14]);
    clip.multiplyScalar(2 / clip.dot(q));
    e[2] = clip.x; e[6] = clip.y; e[10] = clip.z + 1 - 0.003; e[14] = clip.w;
    m.projectionMatrixInverse.copy(m.projectionMatrix).invert();
    var u = WorldKit.uniforms;
    if (!u.reflMat.value) u.reflMat.value = new T.Matrix4();
    u.reflMat.value.set(0.5, 0, 0, 0.5, 0, 0.5, 0, 0.5, 0, 0, 0.5, 0.5, 0, 0, 0, 1)
      .multiply(cam.projectionMatrix).multiply(m.matrixWorldInverse);
  }

  /** @param {any} target */
  function warmPasses(target) {
    if (R.tier === "low") return;
    Object.keys(R.m).forEach(function (k) { draw(R.m[k], target); });
  }

  /** @returns {Promise<any>} */
  function prepare() {
    var r = R.renderer;
    if (!r.compileAsync) return Promise.resolve();
    var passes = new R.T.Scene();
    Object.keys(R.m).forEach(function (k) {
      var m = new R.T.Mesh(R.fs.mesh.geometry, R.m[k]);
      m.frustumCulled = false;
      passes.add(m);
    });
    var was = r.getRenderTarget();
    r.setRenderTarget(R.tier === "low" ? null : R.hdr);
    var scene = r.compileAsync(R.scene, R.camera);
    r.setRenderTarget(was);
    return Promise.all([scene, r.compileAsync(passes, R.fs.cam)]);
  }

  /** @param {number} t @param {number} night @param {number} exposure */
  function render(t, night, exposure) {
    var r = R.renderer, c = R.cfg, T = R.T, u = WorldKit.uniforms;
    r.info.reset();
    R.frame += 1;
    if (R.tier === "low") {
      u.reflOn.value = 0;
      r.toneMappingExposure = exposure;
      r.setRenderTarget(null);
      r.render(R.scene, R.camera);
      R.calls = r.info.render.calls; R.triangles = r.info.render.triangles;
      return;
    }
    if (c.reflect && R.refl) {
      mirror();
      u.reflOn.value = 0;
      r.setRenderTarget(R.refl);
      r.render(R.scene, R.mirror);
      u.refl.value = R.refl.texture;
      u.reflOn.value = 1;
    } else u.reflOn.value = 0;
    r.setRenderTarget(R.hdr);
    r.render(R.scene, R.camera);
    R.calls = r.info.render.calls; R.triangles = r.info.render.triangles;
    var cam = R.camera;
    if (c.ao && R.ao.length) {
      var a = R.m.ao.uniforms;
      a.tDepth.value = R.hdr.depthTexture;
      a.uTexel.value.set(1 / R.ao[0].width, 1 / R.ao[0].height);
      a.uInv.value.copy(cam.projectionMatrixInverse);
      a.uScale.value = cam.projectionMatrix.elements[5] * R.ao[0].height * 0.5;
      a.uNear.value = cam.near; a.uFar.value = cam.far;
      draw(R.m.ao, R.ao[0]);
      var b = R.m.blur.uniforms;
      b.tDepth.value = R.hdr.depthTexture; b.uNear.value = cam.near; b.uFar.value = cam.far;
      b.tAo.value = R.ao[0].texture; b.uDir.value.set(1 / R.ao[0].width, 0);
      draw(R.m.blur, R.ao[1]);
      b.tAo.value = R.ao[1].texture; b.uDir.value.set(0, 1 / R.ao[0].height);
      draw(R.m.blur, R.ao[0]);
    }
    if (c.bloom && R.down.length) {
      var p = R.m.pre.uniforms;
      p.tSrc.value = R.hdr.texture; p.uTexel.value.set(1 / R.w, 1 / R.h); p.uKnee.value = 1.1 - 0.3 * night;
      draw(R.m.pre, R.down[0]);
      for (var i = 1; i < LEVELS; i++) {
        R.m.down.uniforms.tSrc.value = R.down[i - 1].texture;
        R.m.down.uniforms.uTexel.value.set(1 / R.down[i - 1].width, 1 / R.down[i - 1].height);
        draw(R.m.down, R.down[i]);
      }
      var low = R.down[LEVELS - 1];
      for (var j = LEVELS - 2; j >= 0; j--) {
        var up = R.m.up.uniforms;
        up.tLow.value = low.texture; up.tHigh.value = R.down[j].texture;
        up.uTexel.value.set(1 / low.width, 1 / low.height);
        draw(R.m.up, R.up[j]);
        low = R.up[j];
      }
    }
    var k = R.m.comp.uniforms;
    k.tHdr.value = R.hdr.texture;
    k.tAo.value = R.ao.length ? R.ao[0].texture : null;
    k.uAo.value = R.ao.length ? 1 : 0;
    k.tBloom.value = R.up.length ? R.up[0].texture : null;
    k.uBloom.value = R.up.length ? 0.075 + 0.06 * night : 0;
    k.uExposure.value = exposure;
    k.uTime.value = t; k.uNight.value = night;
    k.uRes.value.set(R.w, R.h);
    k.uGrain.value = c.msaa ? 0.035 : 0;
    draw(R.m.comp, c.msaa ? null : R.ldr);
    if (!c.msaa) {
      var f = R.m.fxaa.uniforms;
      f.tSrc.value = R.ldr.texture; f.uTexel.value.set(1 / R.w, 1 / R.h); f.uTime.value = t; f.uRes.value.set(R.w, R.h);
      draw(R.m.fxaa, null);
    }
  }

  return Object.freeze({
    TIERS: TIERS, ORDER: ORDER, create: create, setTier: setTier, resize: resize, step: step, prepare: prepare, warmPasses: warmPasses, render: render,
    get tier() { return R.tier; }, get cfg() { return R.cfg; }, get calls() { return R.calls; }, get soft() { return R.soft; },
    get triangles() { return R.triangles; }, get frameCalls() { return R.renderer ? R.renderer.info.render.calls : 0; }
  });
})();
