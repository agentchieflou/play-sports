// gait.js - procedural locomotion for the stand-in character, on Unreal Engine bone names.
//
// Ported from the world kit's browser reference, RawAssets/world/reference/browser/people.js
// (frameQ, rig, posture, solve), whose reasoning is in people.js.md (### rig, ### frameQ,
// ### posture, ### solve). Kept: the gait (a walk blending into a run with speed, laid against
// the ground covered so feet don't slide), idle breathing, and the one-arm carry (the
// reference's umbrella hold, here the ball tucked in the right arm). Dropped: sitting, the
// wheelchair, presenting and typing, which a football play has no use for.
//
// Every limb is posed by frames (an aim and a pole), never Euler angles on its own axes, so
// the animation doesn't care how the exporter rolled the bones or whether the rest pose is an A
// or a T.

import * as THREE from "three";

const SIDES = [["l", -1], ["r", 1]];
const FINGERS = ["thumb", "index", "middle", "ring", "pinky"];

function v3(a)
{
    return new THREE.Vector3(a[0], a[1], a[2]);
}

function frameQ(aim, pole)
{
    const x = aim.clone().normalize();
    const y = pole.clone().addScaledVector(x, -pole.dot(x));
    if (y.lengthSq() < 1e-8)
    {
        y.set(0, 0, -1).addScaledVector(x, x.z);
    }
    y.normalize();
    const z = new THREE.Vector3().crossVectors(x, y);
    return new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(x, y, z));
}

function euler(p, y, r)
{
    return new THREE.Quaternion().setFromEuler(new THREE.Euler(p, y, r, "YXZ"));
}

function at(R, name)
{
    const i = R.index[name];
    return i === undefined ? -1 : i;
}

function aimOf(R, a, b)
{
    return R.wp[b].clone().sub(R.wp[a]).normalize();
}

function ahead(aim)
{
    return new THREE.Vector3(0, 0, -1).addScaledVector(aim, aim.z).normalize();
}

function palmOf(R, s, hd)
{
    const ix = at(R, "index_01_" + s);
    const pk = at(R, "pinky_01_" + s);
    const md = at(R, "middle_01_" + s);
    const tb = at(R, "thumb_02_" + s);
    if (ix >= 0 && pk >= 0 && md >= 0)
    {
        const f = R.wp[md].clone().sub(R.wp[hd]);
        const k = R.wp[pk].clone().sub(R.wp[ix]);
        const n = new THREE.Vector3().crossVectors(f, k).normalize();
        if (tb >= 0 && R.wp[tb].clone().sub(R.wp[hd]).dot(n) < 0)
        {
            n.negate();
        }
        return n;
    }
    return new THREE.Vector3(0, -1, 0);
}

/** The skeleton as the animation needs it, from the skin's bones in their rest pose. Shared by
 *  every player built from the same file. */
export function rig(bones)
{
    const joints = bones.map((b) => ({
        name: b.name,
        parent: bones.indexOf(b.parent),
        t: b.position.toArray(),
        q: b.quaternion.toArray()
    }));
    const R = { n: joints.length, names: [], index: {}, parent: [], lq: [], lt: [], wp: [], wq: [], legs: [], arms: [], spine: [], neck: [], fingers: [] };
    joints.forEach((j, i) =>
    {
        const p = j.parent;
        R.names.push(j.name);
        R.index[j.name] = i;
        R.parent.push(p);
        R.lq.push(new THREE.Quaternion().fromArray(j.q));
        R.wq.push(p >= 0 ? R.wq[p].clone().multiply(R.lq[i]) : R.lq[i].clone());
        R.wp.push(p >= 0 ? v3(j.t).applyQuaternion(R.wq[p]).add(R.wp[p]) : v3(j.t));
    });
    R.wp.forEach((w, i) =>
    {
        const p = R.parent[i];
        R.lt.push(p >= 0 ? w.clone().sub(R.wp[p]).applyQuaternion(R.wq[p].clone().invert()) : w.clone());
    });
    ["spine_01", "spine_02", "spine_03", "spine_04", "spine_05"].forEach((n) => { if (at(R, n) >= 0) R.spine.push(at(R, n)); });
    ["neck_01", "neck_02"].forEach((n) => { if (at(R, n) >= 0) R.neck.push(at(R, n)); });
    R.pelvis = Math.max(0, at(R, "pelvis"));
    R.head = at(R, "head");
    SIDES.forEach((sd) =>
    {
        const s = sd[0];
        const th = at(R, "thigh_" + s);
        const ca = at(R, "calf_" + s);
        const ft = at(R, "foot_" + s);
        if (th >= 0 && ca >= 0 && ft >= 0)
        {
            const la = aimOf(R, th, ca);
            const lb = aimOf(R, ca, ft);
            R.legs.push({ s: sd[1], th, ca, ft, ball: at(R, "ball_" + s), rest: [frameQ(la, ahead(la)).invert(), frameQ(lb, ahead(lb)).invert()] });
        }
        const ua = at(R, "upperarm_" + s);
        const lo = at(R, "lowerarm_" + s);
        const hd = at(R, "hand_" + s);
        if (ua >= 0 && lo >= 0 && hd >= 0)
        {
            const aa = aimOf(R, ua, lo);
            const ab = aimOf(R, lo, hd);
            const palm = palmOf(R, s, hd);
            R.arms.push({ s: sd[1], cl: at(R, "clavicle_" + s), ua, lo, hd, palm, rest: [frameQ(aa, ahead(aa)).invert(), frameQ(ab, palm).invert()] });
            const side = new THREE.Vector3().crossVectors(ab, palm).normalize();
            FINGERS.forEach((f) =>
            {
                for (let k = 1; k <= 3; k++)
                {
                    const fi = at(R, f + "_0" + k + "_" + s);
                    if (fi < 0)
                    {
                        continue;
                    }
                    const ax = (f === "thumb" ? palm.clone().cross(ab).normalize().negate() : side.clone()).applyQuaternion(R.wq[fi].clone().invert());
                    R.fingers.push({ i: fi, s: sd[1], ax, k: f === "thumb" ? 0.35 : 1 });
                }
            });
        }
    });
    R.ankle = R.legs.map((l) => [R.wp[l.ft].y, l.ball >= 0 ? R.wp[l.ball].y : R.wp[l.ft].y]);
    return R;
}

function mix(a, b, t)
{
    return a + (b - a) * t;
}

function pos(x)
{
    return x > 0 ? x : 0;
}

/** A pose as a handful of numbers: gait phase (radians), amplitude (0 still .. 1 striding), run
 *  blend (0 walk .. 1 sprint), time for breathing, and whether the right arm carries the ball. */
export function posture(s)
{
    const g = s.gait;
    const a = s.amp;
    const r = s.run;
    const br = s.still ? 0 : Math.sin(s.t * 1.7);
    const sway = s.still ? 0 : Math.sin(s.t * 0.45);
    const P = { hip: [0, 0, 0], pel: [0, 0, 0], spine: [0.02 * br * 0.4, 0, 0], head: [0, 0, 0], clav: [0, 0], legs: [], arms: [] };
    P.legs = [g, g + Math.PI].map((ph) =>
    {
        const th = mix(0.06 + 0.4 * Math.sin(ph), 0.2 + 0.72 * Math.sin(ph), r);
        const kn = mix(0.08 + 1.05 * Math.pow(pos(Math.cos(ph + 0.45)), 2) + 0.2 * Math.pow(pos(Math.sin(ph - 0.5)), 3),
            0.25 + 1.75 * Math.pow(pos(Math.cos(ph + 0.75)), 1.4), r);
        const ft = 0.28 * Math.pow(pos(Math.sin(ph + 0.1)), 6) - mix(0.55, 0.75, r) * Math.pow(pos(-Math.sin(ph + 0.65)), 5) + 0.05 * pos(Math.cos(ph));
        return [th * a, 0.03 + 0.02 * (1 - a), kn * a + 0.04 * (1 - a), ft * a];
    });
    P.hip = [0.012 * sway * (1 - a), 0, 0];
    P.pel = [mix(0.02, 0.1, r) * a, -mix(0.07, 0.12, r) * Math.sin(g) * a, 0.03 * Math.cos(g) * a * (1 - r)];
    P.spine[0] += mix(0.03, 0.2, r) * a;
    P.spine[1] = mix(0.13, 0.2, r) * Math.sin(g) * a;
    P.head = [-mix(0.03, 0.18, r) * a, -0.05 * Math.sin(g) * a + 0.04 * sway * (1 - a), 0];
    P.arms = SIDES.map((sd, i) =>
    {
        const ph = g + (i ? Math.PI : 0);
        const sw = -Math.sin(ph);
        const el = mix(0.25 + 0.2 * pos(sw), 1.45 + 0.2 * sw, r) * a + 0.18 * (1 - a);
        return [mix(0.32, 0.75, r) * sw * a + 0.02, 0.1 + 0.04 * r * a, el, [-sd[1], 0, 0.2], 0.4, 0];
    });
    if (s.hold)
    {
        P.arms[1] = [0.3, 0.12, 1.42, [-1, 0.15, 0], 0.9, -0.1];
    }
    return P;
}

const X_AXIS = new THREE.Vector3(1, 0, 0);
const DOWN = new THREE.Vector3(0, -1, 0);
const FWD = new THREE.Vector3(0, 0, -1);

/** Every bone's local rotation (into Q) and the pelvis position (into hip) for pose P, the
 *  lowest foot put on the ground. Returns the world position of the right hand, for the ball. */
export function solve(R, P, Q, hip)
{
    const W = new Array(R.n);
    const done = new Array(R.n);
    const dp = euler(P.pel[0], P.pel[1], P.pel[2]);
    const yaw = euler(0, P.pel[1], 0);
    W[R.pelvis] = dp.clone().multiply(R.wq[R.pelvis]);
    done[R.pelvis] = true;
    let chest = dp.clone();
    R.spine.forEach((i, k) =>
    {
        const f = (k + 1) / R.spine.length;
        chest = euler(P.spine[0] * f, P.spine[1] * f, P.spine[2] * f).multiply(dp);
        W[i] = chest.clone().multiply(R.wq[i]);
        done[i] = true;
    });
    const dh = euler(P.head[0], P.head[1], P.head[2]).multiply(chest);
    R.neck.forEach((i, k) =>
    {
        W[i] = chest.clone().slerp(dh, (k + 1) / (R.neck.length + 1)).multiply(R.wq[i]);
        done[i] = true;
    });
    if (R.head >= 0)
    {
        W[R.head] = dh.clone().multiply(R.wq[R.head]);
        done[R.head] = true;
    }
    R.legs.forEach((l, i) =>
    {
        const p = P.legs[i] || [0, 0.03, 0.04, 0];
        const A = yaw.clone().multiply(euler(p[0], 0, l.s * p[1]));
        const aim = DOWN.clone().applyQuaternion(A);
        const bend = FWD.clone().applyQuaternion(A);
        const hinge = X_AXIS.clone().applyQuaternion(A);
        W[l.th] = frameQ(aim, bend).multiply(l.rest[0]).multiply(R.wq[l.th]);
        const calf = aim.clone().applyAxisAngle(hinge, -p[2]);
        const cpole = bend.clone().applyAxisAngle(hinge, -p[2]);
        W[l.ca] = frameQ(calf, cpole).multiply(l.rest[1]).multiply(R.wq[l.ca]);
        W[l.ft] = yaw.clone().multiply(euler(p[3], 0, 0)).multiply(R.wq[l.ft]);
        if (l.ball >= 0)
        {
            W[l.ball] = yaw.clone().multiply(euler(Math.max(0, p[3]), 0, 0)).multiply(R.wq[l.ball]);
            done[l.ball] = true;
        }
        done[l.th] = done[l.ca] = done[l.ft] = true;
    });
    R.arms.forEach((m, i) =>
    {
        const p = P.arms[i] || [0, 0.1, 0.2, [-m.s, 0, 0], 0.3, 0];
        if (m.cl >= 0)
        {
            W[m.cl] = euler(0, 0, m.s * (P.clav[i] || 0)).multiply(chest).multiply(R.wq[m.cl]);
            done[m.cl] = true;
        }
        const A = chest.clone().multiply(euler(p[0], 0, m.s * p[1]));
        const aim = DOWN.clone().applyQuaternion(A);
        const bend = FWD.clone().applyQuaternion(A);
        const hinge = X_AXIS.clone().applyQuaternion(A);
        W[m.ua] = frameQ(aim, bend).multiply(m.rest[0]).multiply(R.wq[m.ua]);
        const fore = aim.clone().applyAxisAngle(hinge, p[2]);
        const palm = v3(p[3]).applyQuaternion(chest);
        W[m.lo] = frameQ(fore, palm).multiply(m.rest[1]).multiply(R.wq[m.lo]);
        const wrist = new THREE.Vector3().crossVectors(fore, palm).normalize();
        W[m.hd] = new THREE.Quaternion().setFromAxisAngle(wrist, p[5]).multiply(W[m.lo]).multiply(R.wq[m.lo].clone().invert()).multiply(R.wq[m.hd]);
        done[m.ua] = done[m.lo] = done[m.hd] = true;
        m.curl = p[4];
    });
    for (let i = 0; i < R.n; i++)
    {
        const par = R.parent[i];
        if (!done[i])
        {
            W[i] = (par >= 0 ? W[par].clone() : new THREE.Quaternion()).multiply(R.lq[i]);
        }
        Q[i].copy(par >= 0 ? W[par].clone().invert().multiply(W[i]) : W[i]);
    }
    R.fingers.forEach((f) =>
    {
        const arm = R.arms.find((m) => m.s === f.s);
        Q[f.i].multiply(new THREE.Quaternion().setFromAxisAngle(f.ax, (arm ? arm.curl : 0.3) * f.k));
    });
    const wp = new Array(R.n);
    hip.copy(R.wp[R.pelvis]).add(v3(P.hip));
    for (let j = 0; j < R.n; j++)
    {
        const pj = R.parent[j];
        wp[j] = pj >= 0 ? R.lt[j].clone().applyQuaternion(W[pj]).add(wp[pj]) : hip.clone();
    }
    if (R.legs.length)
    {
        let low = Infinity;
        R.legs.forEach((l, k) =>
        {
            low = Math.min(low, wp[l.ft].y - R.ankle[k][0]);
            if (l.ball >= 0)
            {
                low = Math.min(low, wp[l.ball].y - R.ankle[k][1]);
            }
        });
        hip.y -= low;
        wp.forEach((w) => { w.y -= low; });
    }
    const hand = R.index.hand_r;
    return hand === undefined ? null : wp[hand];
}

// The reference's scale: 4.5 m/s is its jog (normalised speed 1), running up to 1.8.
function gaitScale(speedCmPerS)
{
    const norm = Math.min(1.8, speedCmPerS / 100 / 4.5);
    const run = Math.max(0, Math.min(1, (norm - 0.35) / 1.2));
    return { norm, run, stride: mix(1.5, 3.4, run) };
}

/**
 * Each player's gait phase (radians) at each frame: the ground he covered between frames over
 * the stride at his recorded speed, summed. distances[player] is the ground covered up to each
 * frame (cm); speeds(player, frame) his speed there (cm/s).
 */
export function gaitPhases(distances, speeds)
{
    return distances.map((d, s) =>
    {
        const phase = new Float32Array(d.length);
        for (let i = 1; i < d.length; i++)
        {
            const { stride } = gaitScale(speeds(s, i));
            phase[i] = phase[i - 1] + ((d[i] - d[i - 1]) / 100 / stride) * Math.PI * 2;
        }
        return phase;
    });
}

/** The pose of a player at one instant: his gait phase, his speed (cm/s), the replay's clock
 *  (for breathing) and whether he carries the ball. */
export function playerPosture(phase, speedCmPerS, t, carrying)
{
    const { norm, run } = gaitScale(speedCmPerS);
    const amp = Math.min(1, norm * 1.4);
    return posture({ gait: phase, amp, run: run * Math.min(1, norm), t, still: false, hold: carrying });
}
