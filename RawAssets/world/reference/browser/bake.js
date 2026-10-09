"use strict";

var WorldBake = (function () {
  var LIB = {
    brick: [1.8, 1.2], concrete: [2.4, 2.4], stucco: [3, 3], stone: [2.4, 1.2], asphalt: [4, 4],
    sidewalk: [3, 3], granite: [1, 1], metal: [2.1, 2.1], roof: [4, 4], wood: [1, 1], leaf: [1, 1]
  };
  var NOISE = [
    "float bh(vec2 p) { p = fract(p * vec2(0.1031, 0.1030)); p += dot(p, p.yx + 33.33); return fract((p.x + p.y) * p.x); }",
    "vec2 bh2(vec2 p) { return vec2(bh(p), bh(p + 17.31)); }",
    "float bn(vec2 p, vec2 P) { vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);",
    "  return mix(mix(bh(mod(i, P)), bh(mod(i + vec2(1.0, 0.0), P)), f.x), mix(bh(mod(i + vec2(0.0, 1.0), P)), bh(mod(i + vec2(1.0, 1.0), P)), f.x), f.y); }",
    "float bf(vec2 uv, vec2 P) { float s = 0.0; float a = 0.5; for (int i = 0; i < 5; i++) { s += a * bn(uv * P, P); P *= 2.0; a *= 0.5; } return s / 0.97; }",
    "vec3 cell(vec2 uv, vec2 P) { vec2 p = uv * P; vec2 i = floor(p); vec2 f = fract(p); float d1 = 9.0; float d2 = 9.0; float id = 0.0;",
    "  for (int y = -1; y <= 1; y++) for (int x = -1; x <= 1; x++) { vec2 o = vec2(float(x), float(y)); vec2 c = mod(i + o, P);",
    "    vec2 r = o + bh2(c) * 0.9 + 0.05 - f; float d = dot(r, r);",
    "    if (d < d1) { d2 = d1; d1 = d; id = bh(c + 3.7); } else if (d < d2) { d2 = d; } }",
    "  return vec3(sqrt(d1), sqrt(d2) - sqrt(d1), id); }"
  ].join("\n");

  var MATS = {
    brick: [
      "vec2 p = uv * vec2(8.0, 16.0); float row = floor(p.y); p.x += mod(row, 2.0) * 0.5; vec2 id = vec2(mod(floor(p.x), 8.0), row); vec2 f = fract(p);",
      "float e = bf(uv, vec2(24.0)) * 0.05; float ex = min(f.x, 1.0 - f.x) * 0.225; float ey = min(f.y, 1.0 - f.y) * 0.075;",
      "float brick = smoothstep(0.004, 0.0085, min(ex, ey) + e - 0.02);",
      "float hb = bh(id + 1.3); float hc = bh(id + 7.1);",
      "vec3 c = mix(vec3(0.42, 0.16, 0.10), vec3(0.55, 0.27, 0.17), hb); c = mix(c, vec3(0.24, 0.10, 0.07), step(0.86, hc) * 0.8);",
      "c *= 0.82 + 0.36 * bf(uv + hb, vec2(48.0)); float grime = bf(uv, vec2(3.0, 2.0)); c *= 0.8 + 0.3 * grime;",
      "vec3 mortar = vec3(0.52, 0.5, 0.47) * (0.75 + 0.3 * bn(uv * 300.0, vec2(300.0)));",
      "alb = mix(mortar, c, brick); rough = mix(0.96, 0.82 - 0.08 * hb, brick); ao = mix(0.55, 1.0, brick); metal = 0.0;",
      "h = mix(0.35, 0.85 + 0.15 * bf(uv + 3.1, vec2(64.0)), brick);"
    ],
    concrete: [
      "vec2 p = uv * vec2(1.0, 2.0); vec2 f = fract(p); vec2 id = floor(p);",
      "float seam = smoothstep(0.0, 0.004, min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) * 2.4 - 0.002);",
      "vec2 hp = fract(p * vec2(2.0, 2.0)) - 0.5; float hole = smoothstep(0.012, 0.02, length(hp * vec2(2.4, 1.2)));",
      "float m = bf(uv, vec2(6.0)); float fine = bn(uv * 400.0, vec2(400.0));",
      "float streak = bf(vec2(uv.x * 30.0, uv.y * 0.6), vec2(30.0, 1.0)); float st = smoothstep(0.45, 0.8, streak);",
      "vec3 c = vec3(0.52, 0.51, 0.49) * (0.82 + 0.22 * m + 0.06 * fine) * (1.0 - 0.18 * st) * (0.93 + 0.07 * bh(id));",
      "alb = c * mix(0.6, 1.0, seam) * mix(0.4, 1.0, hole); rough = 0.88 - 0.1 * st; ao = mix(0.6, 1.0, seam * hole); metal = 0.0;",
      "h = 0.7 + 0.12 * m + 0.03 * fine - 0.3 * (1.0 - seam) - 0.4 * (1.0 - hole);"
    ],
    stucco: [
      "float m = bf(uv, vec2(5.0)); float fine = bf(uv + 1.7, vec2(96.0)); vec3 cr = cell(uv, vec2(6.0));",
      "float crack = (1.0 - smoothstep(0.0, 0.025, cr.y)) * step(0.72, bh(vec2(cr.z * 91.0, 1.0))) * smoothstep(0.3, 0.6, bf(uv + 9.0, vec2(4.0)));",
      "vec3 c = vec3(0.8, 0.78, 0.74) * (0.88 + 0.14 * m) * (1.0 - 0.25 * smoothstep(0.55, 0.8, bf(vec2(uv.x * 20.0, uv.y * 0.5), vec2(20.0, 1.0))));",
      "alb = c * (1.0 - 0.45 * crack); rough = 0.92; ao = 1.0 - 0.4 * crack; metal = 0.0; h = 0.6 + 0.25 * fine + 0.1 * m - 0.3 * crack;"
    ],
    stone: [
      "vec2 p = uv * vec2(2.0, 2.0); float row = floor(p.y); p.x += mod(row, 2.0) * 0.5; vec2 id = vec2(mod(floor(p.x), 2.0), row); vec2 f = fract(p);",
      "float e = bf(uv + 0.3, vec2(20.0)) * 0.012; float d = min(min(f.x, 1.0 - f.x) * 1.2, min(f.y, 1.0 - f.y) * 0.6) + e;",
      "float blk = smoothstep(0.004, 0.012, d); float bev = smoothstep(0.004, 0.03, d);",
      "float hb = bh(id + 2.0); vec3 c = mix(vec3(0.6, 0.56, 0.5), vec3(0.7, 0.66, 0.6), hb) * (0.85 + 0.2 * bf(uv + hb, vec2(32.0)));",
      "c *= 0.85 + 0.2 * bf(uv, vec2(2.0, 4.0));",
      "alb = mix(vec3(0.35, 0.34, 0.32), c, blk); rough = mix(0.95, 0.78, blk); ao = mix(0.5, 1.0, bev); metal = 0.0; h = mix(0.2, 0.7 + 0.25 * bev + 0.05 * bf(uv, vec2(64.0)), blk);"
    ],
    asphalt: [
      "vec3 ag = cell(uv, vec2(220.0)); float stone = smoothstep(0.45, 0.25, ag.x) * step(0.35, ag.z);",
      "float m = bf(uv, vec2(5.0)); float fine = bn(uv * 600.0, vec2(600.0));",
      "vec3 cr = cell(uv, vec2(3.0)); float crack = (1.0 - smoothstep(0.0, 0.02, cr.y)) * step(0.6, bh(vec2(cr.z * 37.0, 3.0))) * smoothstep(0.45, 0.7, bf(uv + 4.0, vec2(3.0)));",
      "float pat = smoothstep(0.62, 0.66, bf(uv + 11.0, vec2(2.0)));",
      "vec3 c = vec3(0.075, 0.078, 0.082) * (0.8 + 0.4 * m) + vec3(0.11) * stone * (0.5 + ag.z);",
      "c = mix(c, vec3(0.045, 0.047, 0.05), pat); c *= 0.9 + 0.2 * fine;",
      "alb = c * (1.0 - 0.6 * crack); rough = mix(0.86, 0.7, pat) - 0.08 * stone; ao = 1.0 - 0.5 * crack; metal = 0.0;",
      "h = 0.55 + 0.25 * stone + 0.1 * fine - 0.4 * crack;"
    ],
    sidewalk: [
      "vec2 p = uv * 2.0; vec2 id = floor(p); vec2 f = fract(p); float d = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) * 1.5;",
      "float slab = smoothstep(0.004, 0.009, d); float hb = bh(id + 5.0);",
      "vec3 cr = cell(uv, vec2(5.0)); float crack = (1.0 - smoothstep(0.0, 0.018, cr.y)) * step(0.82, bh(vec2(cr.z * 13.0, 2.0)));",
      "vec3 gum = cell(uv, vec2(40.0)); float spot = smoothstep(0.12, 0.08, gum.x) * step(0.93, gum.z);",
      "float m = bf(uv + hb, vec2(8.0)); float fine = bn(uv * 500.0, vec2(500.0));",
      "vec3 c = vec3(0.55, 0.545, 0.53) * (0.86 + 0.08 * hb + 0.14 * m + 0.05 * fine);",
      "alb = mix(vec3(0.22, 0.22, 0.21), c, slab) * (1.0 - 0.5 * crack) * (1.0 - 0.6 * spot); rough = mix(0.97, 0.86, slab) - 0.25 * spot;",
      "ao = mix(0.55, 1.0, slab); metal = 0.0; h = mix(0.25, 0.75 + 0.1 * m + 0.05 * fine, slab) - 0.3 * crack;"
    ],
    granite: [
      "vec3 g = cell(uv, vec2(90.0)); float grain = g.z; float fine = bn(uv * 700.0, vec2(700.0));",
      "vec3 c = vec3(0.46, 0.45, 0.44) * (0.75 + 0.5 * grain); c = mix(c, vec3(0.12, 0.12, 0.13), step(0.86, grain));",
      "c = mix(c, vec3(0.62, 0.55, 0.5), step(grain, 0.06)); c *= 0.9 + 0.15 * bf(uv, vec2(4.0));",
      "alb = c * (0.95 + 0.1 * fine); rough = 0.62 + 0.2 * fine; ao = 1.0; metal = 0.0; h = 0.6 + 0.2 * fine + 0.1 * grain;"
    ],
    metal: [
      "float x = uv.x * 28.0; float rib = 0.5 + 0.5 * cos(x * 6.2832); float m = bf(uv, vec2(4.0));",
      "float rust = smoothstep(0.58, 0.75, bf(vec2(uv.x * 14.0, uv.y * 1.5), vec2(14.0, 1.0)) * (0.7 + 0.5 * m));",
      "vec3 paint = vec3(0.5, 0.52, 0.53) * (0.85 + 0.2 * m);",
      "alb = mix(paint, vec3(0.32, 0.16, 0.08) * (0.7 + 0.5 * bn(uv * 200.0, vec2(200.0))), rust);",
      "rough = mix(0.45, 0.9, rust); ao = 0.75 + 0.25 * rib; metal = mix(0.6, 0.1, rust); h = rib * 0.8 + 0.1 * rust;"
    ],
    roof: [
      "vec3 g = cell(uv, vec2(160.0)); float stone = smoothstep(0.5, 0.2, g.x);",
      "float m = bf(uv, vec2(3.0)); float pud = smoothstep(0.6, 0.66, bf(uv + 2.0, vec2(2.0)));",
      "vec3 c = mix(vec3(0.16, 0.16, 0.16), vec3(0.3, 0.29, 0.27) * (0.6 + 0.8 * g.z), stone) * (0.8 + 0.3 * m);",
      "alb = mix(c, vec3(0.06), pud * 0.7); rough = mix(0.9, 0.2, pud); ao = 0.7 + 0.3 * stone; metal = 0.0; h = 0.4 + 0.4 * stone - 0.2 * pud;"
    ],
    leaf: [
      "alb = vec3(0.0); rough = 0.6; ao = 1.0; metal = 0.0; h = 0.0; float cov = 0.0;",
      "for (int i = 0; i < 22; i++) { float fi = float(i); vec2 c = vec2(0.14 + 0.72 * bh(vec2(fi, 1.0)), 0.14 + 0.72 * bh(vec2(fi, 7.0)));",
      "  float a = bh(vec2(fi, 3.0)) * 6.2832; vec2 q = uv - c; q = vec2(cos(a) * q.x + sin(a) * q.y, -sin(a) * q.x + cos(a) * q.y);",
      "  float len = 0.09 + 0.05 * bh(vec2(fi, 5.0)); float t = q.x / len;",
      "  float wdt = 0.42 * len * pow(max(1.0 - t * t, 0.0), 0.65); float inside = step(abs(q.y), wdt) * step(abs(t), 1.0);",
      "  if (inside > 0.5) { float g = bh(vec2(fi, 9.0)); vec3 base = mix(vec3(0.16, 0.3, 0.09), vec3(0.34, 0.45, 0.14), g);",
      "    float vein = smoothstep(0.012, 0.0, abs(q.y)) * 0.35; alb = base * (0.85 + 0.3 * (1.0 - abs(q.y) / max(wdt, 1e-3))) * (1.0 - vein) + vec3(0.05, 0.06, 0.02) * vein;",
      "    h = 0.4 + 0.6 * (1.0 - abs(q.y) / max(wdt, 1e-3)); cov = 1.0; rough = 0.45 + 0.2 * g; } }",
      "metal = cov;"
    ],
    wood: [
      "vec2 p = vec2(uv.x, uv.y * 8.0); float plank = floor(p.y); vec2 f = vec2(p.x, fract(p.y)); float hb = bh(vec2(plank, 1.0));",
      "float grain = bf(vec2(uv.x * 2.0 + hb * 7.0, uv.y * 64.0 + bf(uv * vec2(1.0, 8.0), vec2(1.0, 8.0)) * 3.0), vec2(2.0, 64.0));",
      "float gap = smoothstep(0.0, 0.06, min(f.y, 1.0 - f.y));",
      "vec3 c = mix(vec3(0.33, 0.2, 0.12), vec3(0.5, 0.33, 0.2), grain) * (0.85 + 0.25 * hb);",
      "alb = c * mix(0.4, 1.0, gap); rough = 0.75; ao = mix(0.5, 1.0, gap); metal = 0.0; h = mix(0.2, 0.7 + 0.15 * grain, gap);"
    ]
  };

  /** @param {any} T @param {string} name @param {number} mode @param {number} res @returns {any} */
  function shader(T, name, mode, res) {
    return new T.ShaderMaterial({
      uniforms: { uTexel: { value: 1 / res } },
      vertexShader: "varying vec2 vUv; void main() { vUv = uv; gl_Position = vec4(position.xy, 0.0, 1.0); }",
      fragmentShader: "uniform float uTexel; varying vec2 vUv;\n" + NOISE + "\n"
        + "void mat(vec2 uv, out vec3 alb, out float rough, out float ao, out float metal, out float h) {\n"
        + MATS[name].join("\n") + "\n}\n"
        + "void main() { vec3 alb; float rough; float ao; float metal; float h; mat(vUv, alb, rough, ao, metal, h);"
        + (mode === 0 ? " gl_FragColor = vec4(clamp(alb, 0.0, 1.0), " + (name === "leaf" ? "metal" : "1.0") + "); }"
          : mode === 1 ? " gl_FragColor = vec4(ao, clamp(rough, 0.04, 1.0), " + (name === "leaf" ? "0.0" : "metal") + ", 1.0); }"
            : " vec3 a2; float r2; float o2; float m2; float hx; float hy;"
            + " mat(fract(vUv + vec2(uTexel, 0.0)), a2, r2, o2, m2, hx); mat(fract(vUv + vec2(0.0, uTexel)), a2, r2, o2, m2, hy);"
            + " vec3 n = normalize(vec3((h - hx) * NSTR, (h - hy) * NSTR, 1.0)); gl_FragColor = vec4(n * 0.5 + 0.5, 1.0); }"),
      defines: { NSTR: (6 * 512 / res * (name === "metal" ? 0.6 : 1)).toFixed(3) },
      depthTest: false, depthWrite: false
    });
  }

  /** @param {any} T @param {any} renderer @param {number} res @param {Object<string, Object>} [photos] @returns {Object<string, {map: any, orm: any, normal: any, size: Array<number>, photo: boolean, gain: number}>} */
  function make(T, renderer, res, photos) {
    var g = new T.BufferGeometry();
    g.setAttribute("position", new T.BufferAttribute(new Float32Array([-1, -1, 0, 3, -1, 0, -1, 3, 0]), 3));
    g.setAttribute("uv", new T.BufferAttribute(new Float32Array([0, 0, 2, 0, 0, 2]), 2));
    var mesh = new T.Mesh(g, null), scene = new T.Scene(), cam = new T.OrthographicCamera(-1, 1, 1, -1, 0, 1);
    mesh.frustumCulled = false;
    scene.add(mesh);
    var aniso = Math.min(8, renderer.capabilities.getMaxAnisotropy()), out = {}, was = renderer.getRenderTarget();
    Object.keys(LIB).forEach(function (name) {
      var p = photos && photos[name];
      if (p) {
        out[name] = { size: p.size, map: WorldAssets.texture(T, p.map, true, p.size, aniso), orm: WorldAssets.texture(T, p.orm, false, p.size, aniso),
                      normal: WorldAssets.texture(T, p.normal, false, p.size, aniso), photo: true, gain: p.gain };
        return;
      }
      var set = { size: LIB[name], map: null, orm: null, normal: null, photo: false, gain: 1 };
      ["map", "orm", "normal"].forEach(function (slot, mode) {
        var rt = new T.WebGLRenderTarget(res, res, {
          type: T.UnsignedByteType, format: T.RGBAFormat, depthBuffer: false, generateMipmaps: true,
          minFilter: T.LinearMipmapLinearFilter, magFilter: T.LinearFilter, wrapS: T.RepeatWrapping, wrapT: T.RepeatWrapping,
          colorSpace: mode === 0 ? T.SRGBColorSpace : T.NoColorSpace, anisotropy: aniso
        });
        var mat = shader(T, name, mode, res);
        mesh.material = mat;
        renderer.setRenderTarget(rt);
        renderer.render(scene, cam);
        mat.dispose();
        rt.texture.repeat.set(1 / LIB[name][0], 1 / LIB[name][1]);
        set[slot] = rt.texture;
      });
      out[name] = set;
    });
    renderer.setRenderTarget(was);
    g.dispose();
    return out;
  }

  /** @param {any} T @param {Object<string, any>} lib @param {string} name @param {Object} [o] @returns {any} */
  function std(T, lib, name, o) {
    var s = lib[name];
    var mat = new T.MeshStandardMaterial(Object.assign({
      vertexColors: true, map: s.map, roughnessMap: s.orm, metalnessMap: s.orm, aoMap: s.orm, normalMap: s.normal,
      roughness: 1, metalness: 1, aoMapIntensity: 0.9
    }, o || {}));
    mat.normalScale.set(1, 1);
    return mat;
  }

  return Object.freeze({ LIB: LIB, make: make, std: std });
})();
