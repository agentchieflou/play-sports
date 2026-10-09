"use strict";

var WorldBots = (function () {
  var SHELL = "#eef0f3";
  var TRIM = "#9aa3ad";
  var SCREEN = "#151a21";
  var DARK = "#2a3038";
  var fill = { value: 0.2 };
  var beat = { time: { value: 0 }, opacity: { value: 0.3 } };

  /** @param {any} T @param {number} cap @returns {{shell: any, glow: any, ring: any, beacon: any}} */
  function build(T, cap) {
    var P = WorldKit.piece, list = [], lights = [];
    var body = new T.LatheGeometry([[0, 0.36], [0.2, 0.38], [0.31, 0.47], [0.36, 0.64], [0.35, 0.84], [0.29, 1.0],
      [0.18, 1.1], [0, 1.13]].map(function (p) { return new T.Vector2(p[0], p[1]); }), 28);
    list.push(P(T, body, SHELL, [0, 0, 0]));
    list.push(P(T, new T.TorusGeometry(0.355, 0.03, 8, 32), TRIM, [0, 0.62, 0], null, [Math.PI / 2, 0, 0]));
    list.push(P(T, new T.CylinderGeometry(0.09, 0.11, 0.14, 12), TRIM, [0, 1.17, 0]));
    list.push(P(T, new T.SphereGeometry(0.31, 24, 16), SHELL, [0, 1.43, 0], [1.15, 0.9, 1]));
    list.push(P(T, new T.SphereGeometry(0.3, 22, 14), SCREEN, [0, 1.42, 0.13], [0.98, 0.68, 0.72]));
    [-1, 1].forEach(function (s) {
      list.push(P(T, new T.CylinderGeometry(0.075, 0.075, 0.07, 14), TRIM, [s * 0.36, 1.43, 0], null, [0, 0, Math.PI / 2]));
      list.push(P(T, new T.CapsuleGeometry(0.065, 0.24, 4, 10), SHELL, [s * 0.4, 0.74, 0.02], null, [0.15, 0, s * 0.28]));
      list.push(P(T, new T.SphereGeometry(0.075, 12, 8), TRIM, [s * 0.46, 0.55, 0.06]));
      lights.push(P(T, new T.SphereGeometry(0.045, 14, 10), "#ffffff", [s * 0.1, 1.45, 0.335], [1, 1.45, 0.45]));
    });
    list.push(P(T, new T.CylinderGeometry(0.012, 0.016, 0.24, 6), TRIM, [0, 1.81, 0]));
    list.push(P(T, new T.CylinderGeometry(0.26, 0.36, 0.12, 24), DARK, [0, 0.2, 0]));
    list.push(P(T, new T.CylinderGeometry(0.18, 0.26, 0.08, 20), TRIM, [0, 0.29, 0]));
    lights.push(P(T, new T.SphereGeometry(0.05, 12, 8), "#ffffff", [0, 1.95, 0]));
    lights.push(P(T, new T.CylinderGeometry(0.065, 0.065, 0.015, 16), "#ffffff", [0, 0.86, 0.34], null, [Math.PI / 2 - 0.1, 0, 0]));
    lights.push(P(T, new T.TorusGeometry(0.3, 0.025, 6, 32), "#ffffff", [0, 0.13, 0], null, [Math.PI / 2, 0, 0]));
    lights.push(P(T, new T.TorusGeometry(0.08, 0.011, 6, 16, Math.PI), "#ffffff", [0, 1.37, 0.33], [1, 0.6, 1], [0, 0, Math.PI]));

    var smat = new T.MeshStandardMaterial({ vertexColors: true, roughness: 0.32, metalness: 0.05 });
    WorldKit.lit(smat, "bots", { porous: 0.4, extra: function (/** @type {any} */ sh) {
      sh.uniforms.uFill = fill;
      sh.fragmentShader = "uniform float uFill;\n" + sh.fragmentShader.replace("#include <emissivemap_fragment>",
        "#include <emissivemap_fragment>\ntotalEmissiveRadiance += diffuseColor.rgb * uFill;");
    } });
    var shell = new T.InstancedMesh(WorldKit.merge(T, list), smat, cap);
    var glow = new T.InstancedMesh(WorldKit.merge(T, lights), new T.MeshBasicMaterial({ vertexColors: true }), cap);
    var ring = new T.InstancedMesh(new T.TorusGeometry(0.6, 0.011, 5, 48), new T.MeshBasicMaterial({ color: 0xffffff }), cap);
    var bmat = new T.ShaderMaterial({
      uniforms: { uTime: beat.time, uOpacity: beat.opacity, uColor: { value: new T.Color(0xe8485c) } },
      vertexShader: "varying vec2 vUv; varying float vD; void main() { vUv = uv;"
        + " vec4 mv = modelViewMatrix * instanceMatrix * vec4(position, 1.0); vD = -mv.z; gl_Position = projectionMatrix * mv; }",
      fragmentShader: "uniform float uTime; uniform float uOpacity; uniform vec3 uColor; varying vec2 vUv; varying float vD;"
        + " void main() { float edge = abs(fract(vUv.x * 2.0) - 0.5) * 2.0; float core = pow(1.0 - edge, 1.6);"
        + " float fade = smoothstep(0.0, 0.06, vUv.y) * (1.0 - smoothstep(0.55, 1.0, vUv.y));"
        + " float band = 0.75 + 0.25 * sin(vUv.y * 60.0 - uTime * 4.0);"
        + " gl_FragColor = vec4(uColor * core * fade * band * uOpacity * smoothstep(2.5, 7.0, vD), 1.0); }",
      transparent: true, depthWrite: false, blending: T.AdditiveBlending, side: T.DoubleSide, fog: false
    });
    var beacon = new T.InstancedMesh(new T.CylinderGeometry(0.22, 0.34, 40, 16, 1, true), bmat, cap);
    beacon.renderOrder = 3;
    [shell, glow, ring, beacon].forEach(function (m) { m.count = 0; m.frustumCulled = false; });
    return { shell: shell, glow: glow, ring: ring, beacon: beacon };
  }

  /** @param {any} T @param {string} name @returns {any} */
  function tint(T, name) {
    var h = 0;
    for (var i = 0; i < name.length; i++) h = (h * 31 + name.charCodeAt(i)) >>> 0;
    return new T.Color().setHSL((h % 360) / 360, 0.32, 0.86);
  }

  /** @param {any} T @param {{shell: any, glow: any, ring: any, beacon: any}} p @param {Array<{repo: string, x: number, z: number, yaw: number, state: string, needs: boolean, i: number}>} list @param {Object<string, number>} colours @param {number} t @param {boolean} still @param {boolean} [people] */
  function place(T, p, list, colours, t, still, people) {
    var m = new T.Matrix4(), q = new T.Quaternion(), e = new T.Euler(), at = new T.Vector3(), one = new T.Vector3(1, 1, 1);
    var c = new T.Color(), beacons = 0;
    list.forEach(function (ag, k) {
      var busy = ag.state === "running" || ag.state === "starting";
      var bob = still ? 0 : Math.sin(t * (busy ? 3.2 : 1.6) + ag.i * 1.7) * (busy ? 0.05 : 0.035);
      var sway = still ? 0 : Math.sin(t * 0.7 + ag.i) * 0.06;
      q.setFromEuler(e.set(0, ag.yaw + sway, 0));
      m.compose(at.set(ag.x, bob, ag.z), q, one);
      p.shell.setMatrixAt(k, m);
      p.glow.setMatrixAt(k, m);
      p.shell.setColorAt(k, tint(T, ag.repo));
      var pulse = ag.needs && !still ? 0.7 + 0.3 * Math.sin(t * 4) : 1;
      c.setHex(colours[ag.state] || colours.idle).multiplyScalar(pulse * 1.15);
      p.glow.setColorAt(k, c);
      p.ring.setColorAt(k, c.setHex(colours[ag.state] || colours.idle));
      var spin = busy && !still ? t * 1.8 : 0;
      q.setFromEuler(e.set(Math.PI / 2 - (people ? 0 : 0.22), 0, spin + ag.i));
      m.compose(at.set(ag.x, people ? 0.03 : 0.95 + bob, ag.z), q, one);
      p.ring.setMatrixAt(k, m);
      if (ag.needs) p.beacon.setMatrixAt(beacons++, m.compose(at.set(ag.x, 20, ag.z), q.identity(), one));
    });
    p.shell.count = p.glow.count = people ? 0 : list.length;
    p.ring.count = list.length;
    p.beacon.count = beacons;
    [p.shell, p.glow, p.ring, p.beacon].forEach(function (mm) {
      mm.instanceMatrix.needsUpdate = true;
      if (mm.instanceColor) mm.instanceColor.needsUpdate = true;
    });
  }

  return Object.freeze({ build: build, place: place, tint: tint, fill: fill, beat: beat });
})();
