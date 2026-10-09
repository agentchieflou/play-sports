"use strict";

var WorldAssets = (function () {
  var DIR = "/static/world/cc0/";
  var PEOPLE = "/static/world/people/";
  var TREES = "/static/world/trees/";
  var CARS = "/static/world/cars/";
  var OFFICE = "/static/world/office/";
  var TEX = {
    brick: ["brick_wall_001", 3, 3, 1], stucco: ["painted_plaster_wall", 2, 2, 1], concrete: ["concrete_slab_wall", 2.3, 2.3, 1],
    asphalt: ["asphalt_02", 3, 3, 0.6], sidewalk: ["concrete_pavement", 1.8, 1.8, 0.78]
  };
  var SKY = { day: ["potsdamer_platz", 1.15], night: ["hansaplatz", 0.05] };
  var PROPS = ["fire_hydrant", "metal_trash_can", "trashbag", "cardboard_box_01", "utility_box_01", "utility_box_02",
               "exterior_aircon_unit", "concrete_road_barrier", "covered_car"];

  /** @param {string} name @returns {Promise<any>} */
  function image(name) {
    var img = new Image();
    img.decoding = "async";
    img.src = q(DIR + name);
    return img.decode().then(function () { return img; }, function () { return null; });
  }

  /** @param {string} name @returns {Promise<{w: number, h: number, data: Uint8Array}|null>} */
  function rgbe(name) {
    return fetch(q(DIR + name)).then(function (r) { return r.ok ? r.arrayBuffer() : null; }).then(function (buf) {
      if (!buf) return null;
      var b = new Uint8Array(buf), at = 0, line = "", m = null;
      while (at < b.length && at < 1024 && !m) {
        var c = b[at++];
        if (c !== 10) { line += String.fromCharCode(c); continue; }
        m = /^-Y (\d+) \+X (\d+)$/.exec(line);
        line = "";
      }
      if (!m) return null;
      var h = +m[1], w = +m[2];
      return b.length - at === w * h * 4 ? { w: w, h: h, data: b.subarray(at) } : null;
    }).catch(function () { return null; });
  }

  /** @param {string} url @returns {Promise<Object|null>} */
  function gltf(url) {
    return fetch(q(url)).then(function (r) { return r.ok ? r.arrayBuffer() : null; }).then(function (buf) {
      if (!buf) return null;
      var dv = new DataView(buf);
      if (dv.getUint32(0, true) !== 0x46546C67) return null;
      var jl = dv.getUint32(12, true), json = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 20, jl))), bin = 28 + jl, pics = {};
      var span = function (/** @type {number} */ i) { var v = json.bufferViews[i]; return [bin + (v.byteOffset || 0), v.byteLength]; };
      var read = function (/** @type {number} */ i) {
        var a = json.accessors[i], at = span(a.bufferView), n = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 }[a.type];
        var K = { 5126: Float32Array, 5125: Uint32Array, 5123: Uint16Array, 5121: Uint8Array, 5122: Int16Array, 5120: Int8Array }[a.componentType];
        var start = at[0] + (a.byteOffset || 0), step = json.bufferViews[a.bufferView].byteStride || n * K.BYTES_PER_ELEMENT, out;
        if (step === n * K.BYTES_PER_ELEMENT) out = new K(buf, start, a.count * n);
        else {
          out = new K(a.count * n);
          for (var j = 0; j < a.count; j++) out.set(new K(buf, start + j * step, n), j * n);
        }
        if (a.normalized) {
          var d = { 5121: 255, 5123: 65535, 5120: 127, 5122: 32767 }[a.componentType], f = new Float32Array(out.length);
          for (var k = 0; k < out.length; k++) f[k] = Math.max(-1, out[k] / d);
          out = f;
        }
        return { a: out, n: n };
      };
      var pic = function (/** @type {Object} */ info) {
        if (!info) return Promise.resolve(null);
        var t = json.textures[info.index], src = t.source !== undefined ? t.source : t.extensions.EXT_texture_webp.source;
        if (!pics[src]) {
          var im = json.images[src], at = span(im.bufferView);
          pics[src] = createImageBitmap(new Blob([new Uint8Array(buf, at[0], at[1])], { type: im.mimeType }),
            { premultiplyAlpha: "none", colorSpaceConversion: "none" });
        }
        return pics[src];
      };
      return { json: json, read: read, pic: pic };
    }).catch(function () { return null; });
  }

  /** @param {string} url @param {boolean} [named] @returns {Promise<any>} */
  function glb(url, named) {
    return gltf(url).then(function (g) {
      if (!g) return null;
      var parts = [], names = [];
      g.json.meshes.forEach(function (/** @type {Object} */ m) {
        m.primitives.forEach(function (/** @type {Object} */ p) {
          var mt = g.json.materials[p.material], pbr = mt.pbrMetallicRoughness, a = p.attributes;
          names.push(m.name);
          parts.push(Promise.all([g.pic(pbr.baseColorTexture), g.pic(mt.normalTexture), g.pic(pbr.metallicRoughnessTexture)]).then(function (im) {
            return { pos: g.read(a.POSITION), nor: g.read(a.NORMAL), uv: a.TEXCOORD_0 === undefined ? null : g.read(a.TEXCOORD_0), idx: g.read(p.indices),
                     col: a.COLOR_0 === undefined ? null : g.read(a.COLOR_0),
                     mat: mt.name, map: im[0], normal: im[1], orm: im[2] };
          }));
        });
      });
      return Promise.all(parts).then(function (got) {
        if (!named) return got;
        var out = {};
        got.forEach(function (/** @type {Object} */ p, /** @type {number} */ i) { (out[names[i]] = out[names[i]] || []).push(p); });
        return out;
      });
    }).catch(function () { return null; });
  }

  /** @param {string} name @returns {Promise<Object|null>} */
  function figure(name) {
    return gltf(PEOPLE + name).then(function (g) {
      if (!g || !g.json.skins) return null;
      var J = g.json, up = {}, jobs = [];
      J.nodes.forEach(function (/** @type {Object} */ n, /** @type {number} */ i) { (n.children || []).forEach(function (/** @type {number} */ c) { up[c] = i; }); });
      var skins = J.skins.map(function (/** @type {Object} */ s) {
        var slot = {};
        s.joints.forEach(function (/** @type {number} */ ni, /** @type {number} */ k) { slot[ni] = k; });
        return { extras: s.extras || {}, joints: s.joints.map(function (/** @type {number} */ ni) {
          var n = J.nodes[ni], p = up[ni];
          return { name: n.name, parent: p !== undefined && slot[p] !== undefined ? slot[p] : -1, t: n.translation || [0, 0, 0], q: n.rotation || [0, 0, 0, 1] };
        }) };
      });
      var parts = [];
      J.nodes.forEach(function (/** @type {Object} */ n) {
        if (n.mesh === undefined || n.skin === undefined) return;
        var m = J.meshes[n.mesh], names = (m.extras && m.extras.targetNames) || [];
        m.primitives.forEach(function (/** @type {Object} */ p) {
          var mt = J.materials[p.material] || {}, pbr = mt.pbrMetallicRoughness || {}, ex = mt.extras || {}, a = p.attributes, morph = {};
          (p.targets || []).forEach(function (/** @type {Object} */ t, /** @type {number} */ k) { morph[names[k] || k] = g.read(t.POSITION).a; });
          var part = { skin: n.skin, extras: m.extras || {}, role: ex.role || "other", style: ex.style || "", tone: ex.tone || [0.5, 0.5, 0.5],
                       rough: pbr.roughnessFactor === undefined ? 0.7 : pbr.roughnessFactor, alpha: mt.alphaMode === "MASK" ? (mt.alphaCutoff || 0.5) : 0,
                       two: !!mt.doubleSided, pos: g.read(a.POSITION).a, nor: g.read(a.NORMAL).a, uv: g.read(a.TEXCOORD_0).a,
                       joint: g.read(a.JOINTS_0).a, weight: g.read(a.WEIGHTS_0).a, roles: a._ROLE === undefined ? null : Float32Array.from(g.read(a._ROLE).a),
                       idx: g.read(p.indices).a, morph: morph, map: null, normal: null, orm: null, ao: !!mt.occlusionTexture, tint: ex.tint !== false, thin: ex.thin === true };
          jobs.push(Promise.all([g.pic(pbr.baseColorTexture), g.pic(mt.normalTexture), g.pic(pbr.metallicRoughnessTexture)]).then(function (im) { part.map = im[0]; part.normal = im[1]; part.orm = im[2]; }));
          parts.push(part);
        });
      });
      var clips = (J.animations || []).map(function (/** @type {Object} */ an) {
        return { name: an.name, channels: an.channels.map(function (/** @type {Object} */ c) {
          var s = an.samplers[c.sampler];
          return { joint: J.nodes[c.target.node].name, path: c.target.path, times: g.read(s.input).a, values: g.read(s.output).a };
        }) };
      });
      return Promise.all(jobs).then(function () { return { skins: skins, parts: parts, clips: clips }; });
    }).catch(function () { return null; });
  }

  /** @returns {Promise<Object>} */
  function people() {
    var out = { manifest: null, hero: [], crowd: [] };
    return fetch(q(PEOPLE + "people.json")).then(function (r) { return r.ok ? r.json() : null; }).then(function (m) {
      if (!m) return out;
      out.manifest = m;
      var list = function (/** @type {Array<Object>} */ xs) {
        return Promise.all((xs || []).map(function (/** @type {Object} */ x) { return figure(x.file); })).then(function (got) { return got.filter(Boolean); });
      };
      return Promise.all([list(m.hero), list(m.crowd)]).then(function (got) {
        out.hero = got[0].length === (m.hero || []).length ? got[0] : [];
        out.crowd = got[1].length === (m.crowd || []).length ? got[1] : [];
        return out;
      });
    }).catch(function () { return out; });
  }

  /** @returns {Promise<{tex: Object<string, Object>, sky: Object<string, Object>, props: Object<string, Array<Object>>, trees: Object<string, Array<Object>>|null, cars: Object<string, Array<Object>>|null, office: Object<string, Array<Object>>|null, people: Object}>} */
  function load() {
    var out = { tex: {}, sky: {}, props: {}, trees: null, cars: null, office: null, people: null }, jobs = [];
    Object.keys(TEX).forEach(function (k) {
      var id = TEX[k][0];
      jobs.push(Promise.all([image(id + "_diff.webp"), image(id + "_arm.webp"), image(id + "_nor.webp")]).then(function (im) {
        if (im[0] && im[1] && im[2]) out.tex[k] = { map: im[0], orm: im[1], normal: im[2], size: [TEX[k][1], TEX[k][2]], gain: TEX[k][3] };
      }));
    });
    PROPS.forEach(function (id) { jobs.push(glb(DIR + id + ".glb").then(function (parts) { if (parts) out.props[id] = parts; })); });
    jobs.push(glb(TREES + "trees.glb", true).then(function (t) { out.trees = t; }));
    jobs.push(glb(CARS + "cars.glb", true).then(function (t) { out.cars = t; }));
    jobs.push(glb(OFFICE + "office.glb", true).then(function (t) { out.office = t; }));
    Object.keys(SKY).forEach(function (k) {
      jobs.push(rgbe(SKY[k][0] + ".hdr").then(function (s) { if (s) out.sky[k] = Object.assign(s, { k: SKY[k][1] }); }));
    });
    jobs.push(people().then(function (p) { out.people = p; }));
    return Promise.all(jobs).then(function () { return out; });
  }

  /** @param {any} T @param {any} img @param {boolean} srgb @param {Array<number>} size @param {number} aniso @returns {any} */
  function texture(T, img, srgb, size, aniso) {
    var t = new T.Texture(img);
    t.wrapS = t.wrapT = T.RepeatWrapping;
    t.anisotropy = aniso;
    t.colorSpace = srgb ? T.SRGBColorSpace : T.NoColorSpace;
    t.repeat.set(1 / size[0], 1 / size[1]);
    t.needsUpdate = true;
    return t;
  }

  /** @param {any} T @param {Object} s @returns {any} */
  function data(T, s) {
    var t = new T.DataTexture(s.data, s.w, s.h, T.RGBAFormat, T.UnsignedByteType);
    t.minFilter = t.magFilter = T.NearestFilter;
    t.wrapS = T.RepeatWrapping;
    t.generateMipmaps = false;
    t.needsUpdate = true;
    return t;
  }

  /** @param {any} T @param {Object<string, Object>} sky @returns {any} */
  function dome(T, sky) {
    if (!sky.day || !sky.night) return null;
    var mat = new T.ShaderMaterial({
      uniforms: {
        uDay: { value: data(T, sky.day) }, uNight: { value: data(T, sky.night) }, uSize: { value: new T.Vector2(sky.day.w, sky.day.h) },
        uMix: { value: 0 }, uDayK: { value: sky.day.k }, uNightK: { value: sky.night.k }, uTint: { value: new T.Color(1, 1, 1) }
      },
      vertexShader: "varying vec3 vDir; void main() { vDir = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }",
      fragmentShader: [
        "uniform sampler2D uDay; uniform sampler2D uNight; uniform vec2 uSize; uniform float uMix; uniform float uDayK; uniform float uNightK;",
        "uniform vec3 uTint; varying vec3 vDir;",
        "vec3 px(sampler2D t, vec2 i) { vec4 c = floor(texture2D(t, (vec2(mod(i.x, uSize.x), clamp(i.y, 0.0, uSize.y - 1.0)) + 0.5) / uSize) * 255.0 + 0.5);",
        "  return c.a > 0.0 ? (c.rgb + 0.5) * exp2(c.a - 136.0) : vec3(0.0); }",
        "vec3 tap(sampler2D t, vec2 uv) { vec2 p = uv * uSize - 0.5; vec2 i = floor(p); vec2 f = p - i;",
        "  return mix(mix(px(t, i), px(t, i + vec2(1.0, 0.0)), f.x), mix(px(t, i + vec2(0.0, 1.0)), px(t, i + vec2(1.0, 1.0)), f.x), f.y); }",
        "void main() { vec3 d = normalize(vDir);",
        "  vec2 uv = vec2(atan(d.z, d.x) / 6.2831853 + 0.5, 0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.1415927);",
        "  vec3 c = uMix < 0.999 ? tap(uDay, uv) * uDayK : vec3(0.0); vec3 n = uMix > 0.001 ? tap(uNight, uv) * uNightK : vec3(0.0);",
        "  gl_FragColor = vec4(min(mix(c, n, uMix) * uTint, vec3(40.0)), 1.0); }"
      ].join("\n"),
      side: T.BackSide, depthWrite: false, depthTest: false
    });
    var mesh = new T.Mesh(new T.SphereGeometry(50, 48, 24), mat);
    mesh.frustumCulled = false;
    return mesh;
  }

  /** @param {any} T @param {any} img @param {boolean} srgb @param {number} aniso @returns {any} */
  function bitmap(T, img, srgb, aniso) {
    var t = new T.Texture(img);
    t.flipY = false;
    t.anisotropy = aniso;
    t.colorSpace = srgb ? T.SRGBColorSpace : T.NoColorSpace;
    t.needsUpdate = true;
    return t;
  }

  /** @param {any} T @param {Object<string, Array<Object>>|null} props @param {number} aniso @param {boolean} [bare] @returns {Object<string, Array<{geo: any, mat: any}>>} */
  function scans(T, props, aniso, bare) {
    var out = {}, mats = {};
    Object.keys(props || {}).forEach(function (id) {
      out[id] = props[id].map(function (/** @type {Object} */ p) {
        var g = new T.BufferGeometry();
        g.setAttribute("position", new T.BufferAttribute(p.pos.a, 3));
        g.setAttribute("normal", new T.BufferAttribute(p.nor.a, 3));
        if (p.uv) g.setAttribute("uv", new T.BufferAttribute(p.uv.a, 2));
        if (p.col) g.setAttribute("color", new T.BufferAttribute(p.col.a, 3));
        g.setIndex(new T.BufferAttribute(p.idx.a, 1));
        g.computeBoundingSphere();
        if (bare) return { geo: g, mat: null };
        var key = p.col ? p.mat : "";
        if (!mats[key]) {
          var orm = bitmap(T, p.orm, false, aniso);
          var mat = new T.MeshStandardMaterial({
            map: bitmap(T, p.map, true, aniso), normalMap: bitmap(T, p.normal, false, aniso), roughnessMap: orm, metalnessMap: p.col ? null : orm,
            roughness: 1, metalness: p.col ? 0 : 1, vertexColors: !!p.col
          });
          if (p.col && key !== "leaves") [mat.map, mat.normalMap, orm].forEach(function (t) { t.wrapS = t.wrapT = T.RepeatWrapping; });
          mat = key === "leaves" ? WorldKit.crown(T, mat) : WorldKit.lit(mat, "scan", { porous: 0.5 });
          if (!key) return { geo: g, mat: mat };
          mats[key] = mat;
        }
        return { geo: g, mat: mats[key] };
      });
    });
    return out;
  }

  return Object.freeze({ DIR: DIR, PEOPLE: PEOPLE, TREES: TREES, CARS: CARS, OFFICE: OFFICE, TEX: TEX, SKY: SKY, PROPS: PROPS, load: load, texture: texture, dome: dome, scans: scans });
})();
