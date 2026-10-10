// app.js - the Film Room: plays recordings of the play-sports simulation back in 3D.
//
// Reads (relative to the page; README.md has the layout):
//   recordings/index.json and the replay files it lists (lane S4's demos), else the
//     SYNTHETIC sample in sample/ under a "not the game" banner;
//   replay_schema.json, the field contract shared with the Python check;
//   data/field_dimensions.json and data/sample_teams.json (the repo's Data/, staged);
//   assets/standin.glb (RawAssets/world/people/standin.glb, CC0).
// Served from the repository instead (python -m http.server at the repo root, then
// /tools/replay_viewer/), the repo's own Data/ and RawAssets/ are read in place, and
// ?recordings=<folder>/ points at a downloaded play-demos artifact.

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import * as SkeletonUtils from "three/addons/utils/SkeletonUtils.js";
import { Line2 } from "three/addons/lines/Line2.js";
import { LineGeometry } from "three/addons/lines/LineGeometry.js";
import { LineMaterial } from "three/addons/lines/LineMaterial.js";
import * as L from "./loader.js";
import { rig, solve, gaitPhases, playerPosture } from "./gait.js";
import { buildField, fieldFrame } from "./field.js";

const $ = (id) => document.getElementById(id);
const DEG = Math.PI / 180;
const REDUCED = matchMedia("(prefers-reduced-motion: reduce)").matches;
const ROLE_SHORT = {
    Quarterback: "QB", RunningBack: "RB", WideReceiver: "WR", TightEnd: "TE",
    OffensiveLineman: "OL", DefensiveLineman: "DL", Linebacker: "LB", DefensiveBack: "DB"
};
const ROLE_LONG = {
    Quarterback: "Quarterback", RunningBack: "Running back", WideReceiver: "Wide receiver", TightEnd: "Tight end",
    OffensiveLineman: "Offensive lineman", DefensiveLineman: "Defensive lineman", Linebacker: "Linebacker", DefensiveBack: "Defensive back"
};
// The world kit's look palette (RawAssets/world/reference/browser/hero.js): presentation only.
const SKIN = ["#3b2219", "#5c3a26", "#7b4a2d", "#9c6643", "#b98058", "#d39d74", "#e8bd98", "#f5d9c2"];
const HAIR = ["#1c1a22", "#3b2a20", "#6b4528", "#a5512b", "#d7b26a"];
const HAIR_STYLES = ["hair_short01", "hair_short02", "hair_curls", "hair_afro01", "hair_rehmanpolanski_hair_bun_brown"];
const DROPPED_PARTS = /^(beard_|glasses_|hair_long01|hair_ponytail01)/;
const DEFAULT_KIT = {
    Offense: { primary: "#1f5fa8", secondary: "#e8edf2", label: "Offense" },
    Defense: { primary: "#b3302a", secondary: "#f2ead9", label: "Defense" }
};
const SCENE_COLOURS = {
    velocity: "#5ef2ff", contact: "#ff6b81", tackle: "#ff2d55", catch: "#5ce07d", throw: "#ffa94d",
    los: "#4dabf7", firstDown: "#ffd43b", ball: "#7a3f1d", ballTrail: "#ffa94d", follow: "#ff7a33"
};
const EVENT_TOKEN = {
    Snap: "--ev-snap", Throw: "--ev-throw", Catch: "--ev-catch", Tackle: "--ev-tackle", Fumble: "--ev-tackle", Damage: "--ev-tackle",
    Whistle: "--ev-whistle", PlayResult: "--ev-whistle", Possession: "--ev-ball", Score: "--ev-throw", Kick: "--ev-snap",
    LooseBall: "--ev-tackle", BallGrounded: "--ev-minor", BoundaryCrossed: "--ev-snap", PassRushMove: "--ev-minor"
};

const S = {
    schema: null,
    dims: null,
    F: null,
    teams: [],
    model: null,
    R: null,
    plays: [],
    sourceNote: "",
    current: -1,
    replay: null,
    views: [],
    phases: [],
    t: 0,
    playing: false,
    speed: 1,
    cam: "sideline",
    follow: -1,
    layers: { trails: true, vectors: false, contacts: true, labels: true },
    dirty: true,
    loadToken: 0
};

// ---------------------------------------------------------------------------------------------
// Fetching
// ---------------------------------------------------------------------------------------------

const params = new URLSearchParams(location.search);

async function fetchText(url)
{
    const res = await fetch(url, { cache: "no-cache" });
    if (!res.ok)
    {
        throw new Error(`${url}: ${res.status} ${res.statusText}`);
    }
    return res.text();
}

async function firstJson(urls)
{
    for (const url of urls)
    {
        try
        {
            return { url, doc: L.parseJson(await fetchText(url)) };
        }
        catch (err)
        {
            // Try the next place.
        }
    }
    return null;
}

function setStatus(text, isError)
{
    const el = $("status");
    el.hidden = !text;
    el.textContent = text || "";
    el.classList.toggle("error", !!isError);
}

// ---------------------------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------------------------

const canvas = $("view");
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: "high-performance" });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.05;
const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(38, 1, 0.2, 900);
camera.position.set(35, 18, 60);
const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.12;
controls.maxPolarAngle = 88 * DEG;
controls.minDistance = 3;
controls.maxDistance = 260;
const hemi = new THREE.HemisphereLight("#e3edf7", "#3a5236", 1.7);
scene.add(hemi);
const sun = new THREE.DirectionalLight("#fff4e2", 2.1);
sun.position.set(-40, 80, 50);
scene.add(sun);
const overlay = new THREE.Group();
scene.add(overlay);
let fieldGroup = null;
let fieldKey = "";
const lineMaterials = [];

function cssVar(name)
{
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

function applyTheme()
{
    const sky = new THREE.Color(cssVar("--scene-sky") || "#bccbd8");
    scene.background = sky;
    scene.fog = new THREE.Fog(sky, 160, 520);
    const night = sky.getHSL({}).l < 0.25;
    hemi.intensity = night ? 1.25 : 1.7;
    sun.color.set(night ? "#eef3ff" : "#fff4e2");
    sun.intensity = night ? 2.6 : 2.1;
    const ground = fieldGroup ? fieldGroup.getObjectByName("ground") : null;
    if (ground)
    {
        ground.material.color.set(cssVar("--scene-ground") || "#2c4630");
    }
    S.dirty = true;
}
matchMedia("(prefers-color-scheme: dark)").addEventListener("change", applyTheme);
new MutationObserver(applyTheme).observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
applyTheme();

function resize()
{
    const rect = $("stage").getBoundingClientRect();
    const w = Math.max(1, Math.floor(rect.width));
    const h = Math.max(1, Math.floor(rect.height));
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
    const px = renderer.getPixelRatio();
    lineMaterials.forEach((m) => m.resolution.set(w * px, h * px));
    S.dirty = true;
}
new ResizeObserver(resize).observe($("stage"));

/** A point in the game's world (cm, Z up) in this page's metres, y up. */
function toScene(out, x, y, z)
{
    return out.set(x / 100, z / 100, y / 100);
}

// ---------------------------------------------------------------------------------------------
// Teams and kits
// ---------------------------------------------------------------------------------------------

function hashOf(text)
{
    let h = 2166136261;
    for (let i = 0; i < text.length; i++)
    {
        h ^= text.charCodeAt(i);
        h = Math.imul(h, 16777619);
    }
    return h >>> 0;
}

function findTeam(key)
{
    if (!key)
    {
        return null;
    }
    const k = String(key).toLowerCase();
    return S.teams.find((t) => [t.teamid, t.displayname, t.abbreviation].some((v) => v && String(v).toLowerCase() === k)) || null;
}

/** Each side's kit: its team's colours when the recording or the index names the team. */
function kitsFor(replay, entry)
{
    const kits = { Offense: { ...DEFAULT_KIT.Offense }, Defense: { ...DEFAULT_KIT.Defense } };
    for (const side of ["Offense", "Defense"])
    {
        // The recording's own Teams first, then the index, then the players' team ids; colours
        // the recording doesn't carry come from Data/sample_teams.json.
        const own = replay.teams.find((t) => t.side === side) || null;
        const named = side === "Offense" ? entry && entry.offenseTeam : entry && entry.defenseTeam;
        const fromPlayers = replay.players.find((p) => p.side === side && p.teamId);
        const catalog = (own && (findTeam(own.teamId) || findTeam(own.abbreviation) || findTeam(own.name)))
            || findTeam(named) || findTeam(fromPlayers && fromPlayers.teamId);
        const primary = (own && own.primaryColor) || (catalog && catalog.primarycolor);
        if (own || catalog)
        {
            kits[side] = {
                primary: primary || kits[side].primary,
                secondary: (own && own.secondaryColor) || (catalog && catalog.secondarycolor) || kits[side].secondary,
                label: (own && (own.name || own.teamId)) || (catalog && (catalog.displayname || catalog.teamid)) || kits[side].label,
                word: (own && (own.abbreviation || own.teamId)) || (catalog && (catalog.abbreviation || catalog.teamid)) || null
            };
        }
    }
    // Two teams in near colours: the defense wears its second colour.
    const a = new THREE.Color(kits.Offense.primary);
    const b = new THREE.Color(kits.Defense.primary);
    if (Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b) < 0.35)
    {
        [kits.Defense.primary, kits.Defense.secondary] = [kits.Defense.secondary, kits.Defense.primary];
    }
    return kits;
}

// ---------------------------------------------------------------------------------------------
// Players
// ---------------------------------------------------------------------------------------------

const materialCache = new Map();

function tinted(base, hex)
{
    const key = base.uuid + hex;
    if (!materialCache.has(key))
    {
        const m = base.clone();
        const tone = (base.userData && base.userData.tone) || [0.5, 0.5, 0.5];
        m.color.set(hex).multiply(new THREE.Color(1 / tone[0], 1 / tone[1], 1 / tone[2]));
        materialCache.set(key, m);
    }
    return materialCache.get(key);
}

/** A jersey number when the recording has one, else role and index (WR2; QB alone). */
function shortLabel(p)
{
    if (p.jersey)
    {
        return p.jersey;
    }
    const role = ROLE_SHORT[p.role] || (p.role ? p.role.slice(0, 2).toUpperCase() : "P");
    return p.roleCount > 1 ? `${role}${p.roleIndex}` : role;
}

function labelSprite(text, colour)
{
    const c = document.createElement("canvas");
    c.width = 160;
    c.height = 112;
    const g = c.getContext("2d");
    g.fillStyle = colour;
    g.strokeStyle = "rgba(255,255,255,0.95)";
    g.lineWidth = 8;
    g.beginPath();
    if (g.roundRect)
    {
        g.roundRect(6, 6, 148, 100, 22);
    }
    else
    {
        g.rect(6, 6, 148, 100);
    }
    g.fill();
    g.stroke();
    g.fillStyle = "#ffffff";
    g.font = `${text.length > 2 ? 52 : 66}px "Graduate", "Rockwell", serif`;
    g.textAlign = "center";
    g.textBaseline = "middle";
    g.fillText(text, 80, 60);
    const tex = new THREE.CanvasTexture(c);
    tex.colorSpace = THREE.SRGBColorSpace;
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, sizeAttenuation: false, depthTest: false, transparent: true }));
    sprite.renderOrder = 10;
    sprite.scale.set(0.034 * (160 / 112), 0.034, 1);
    return sprite;
}

function blobTexture()
{
    const c = document.createElement("canvas");
    c.width = c.height = 64;
    const g = c.getContext("2d");
    const grad = g.createRadialGradient(32, 32, 2, 32, 32, 31);
    grad.addColorStop(0, "rgba(0,0,0,0.75)");
    grad.addColorStop(1, "rgba(0,0,0,0)");
    g.fillStyle = grad;
    g.fillRect(0, 0, 64, 64);
    return new THREE.CanvasTexture(c);
}
const BLOB = blobTexture();
const blobMaterial = new THREE.MeshBasicMaterial({ map: BLOB, transparent: true, depthWrite: false, opacity: 0.55 });

function makeArrow(colour)
{
    const group = new THREE.Group();
    const mat = new THREE.MeshBasicMaterial({ color: colour, depthTest: false, transparent: true, opacity: 0.95 });
    const shaft = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 1, 8).rotateZ(-Math.PI / 2).translate(0.5, 0, 0), mat);
    const head = new THREE.Mesh(new THREE.ConeGeometry(0.22, 0.5, 12).rotateZ(-Math.PI / 2), mat);
    group.add(shaft, head);
    group.renderOrder = 8;
    shaft.renderOrder = head.renderOrder = 8;
    group.userData = { shaft, head };
    group.visible = false;
    overlay.add(group);
    return group;
}

function setArrow(arrow, from, vx, vz, scale)
{
    const len = Math.hypot(vx, vz) * scale;
    if (len < 0.15)
    {
        arrow.visible = false;
        return;
    }
    arrow.visible = true;
    arrow.position.copy(from);
    arrow.rotation.set(0, -Math.atan2(vz, vx), 0);
    const shaftLen = Math.max(0.01, len - 0.4);
    arrow.userData.shaft.scale.set(shaftLen, 1, 1);
    arrow.userData.head.position.set(shaftLen + 0.2, 0, 0);
}

/** The players of a replay, built from the stand-in. */
function buildViews(replay, kits)
{
    for (const v of S.views)
    {
        overlay.remove(v.arrow);
        scene.remove(v.holder);
        overlay.remove(v.trail);
        // The skinned meshes share the template's geometry and materials; the rest is this play's.
        v.trail.geometry.dispose();
        v.trail.material.dispose();
        lineMaterials.splice(lineMaterials.indexOf(v.trail.material), 1);
        v.label.material.map.dispose();
        v.label.material.dispose();
        v.blob.geometry.dispose();
        v.arrow.children.forEach((c) => c.geometry.dispose());
        v.arrow.userData.shaft.material.dispose();
    }
    S.views = [];
    const template = S.model;
    replay.players.forEach((p) =>
    {
        const kit = kits[p.side];
        const h = hashOf(p.id);
        const root = SkeletonUtils.clone(template);
        const hair = HAIR_STYLES[h % HAIR_STYLES.length];
        const skin = SKIN[(h >>> 4) % SKIN.length];
        const hairColour = HAIR[(h >>> 9) % HAIR.length];
        let bones = null;
        const meshes = [];
        root.traverse((o) =>
        {
            if (o.isSkinnedMesh)
            {
                meshes.push(o);
            }
        });
        // Weight from the roster sets the build: linemen broad, backs slim. Presentation only.
        const broad = p.weightKg ? Math.min(1, Math.max(0, (p.weightKg - 100) / 40)) : 0;
        const slim = p.weightKg ? Math.min(0.6, Math.max(0, (92 - p.weightKg) / 15)) : 0;
        for (const m of meshes)
        {
            const name = m.material.name || "";
            if (name.startsWith("hair_") && name !== hair)
            {
                m.parent.remove(m);
                continue;
            }
            const role = (m.material.userData && m.material.userData.role) || "";
            if (role === "skin")
            {
                m.material = tinted(m.material, skin);
            }
            else if (role === "top")
            {
                m.material = tinted(m.material, kit.primary);
            }
            else if (role === "bottom")
            {
                m.material = tinted(m.material, kit.secondary);
            }
            else if (role === "shoes")
            {
                m.material = tinted(m.material, "#1d2127");
            }
            else if (role === "hair" || role === "brows")
            {
                m.material = tinted(m.material, hairColour);
            }
            else if (role === "lashes")
            {
                m.material = tinted(m.material, "#1c1a22");
            }
            m.frustumCulled = false;
            if (m.morphTargetDictionary && m.morphTargetInfluences)
            {
                const d = m.morphTargetDictionary;
                if (d.broad !== undefined)
                {
                    m.morphTargetInfluences[d.broad] = broad * 0.85;
                }
                if (d.slim !== undefined)
                {
                    m.morphTargetInfluences[d.slim] = slim;
                }
            }
            if (!bones)
            {
                bones = m.skeleton.bones;
            }
        }
        const holder = new THREE.Group();
        holder.add(root);
        const scale = p.heightCm ? Math.min(1.15, Math.max(0.9, p.heightCm / 183)) : 1;
        holder.scale.setScalar(scale);
        const blob = new THREE.Mesh(new THREE.PlaneGeometry(1.3, 1.3).rotateX(-Math.PI / 2), blobMaterial);
        blob.position.y = 0.02;
        blob.renderOrder = 1;
        holder.add(blob);
        const label = labelSprite(shortLabel(p), kit.primary);
        label.position.set(0, 2.3, 0);
        holder.add(label);
        scene.add(holder);

        const trailMat = new LineMaterial({ color: kit.primary, linewidth: 2.5, transparent: true, opacity: 0.85, depthWrite: false });
        trailMat.resolution.set(canvas.width, canvas.height);
        lineMaterials.push(trailMat);
        const trailGeo = new LineGeometry();
        const pts = [];
        const v = new THREE.Vector3();
        const pos = replay.pos[p.index];
        for (let i = 0; i < replay.times.length; i++)
        {
            toScene(v, pos[i * 3], pos[i * 3 + 1], 0);
            pts.push(v.x, 0.05, v.z);
        }
        if (pts.length < 6)
        {
            pts.push(pts[0], pts[1], pts[2]);
        }
        trailGeo.setPositions(pts);
        const trail = new Line2(trailGeo, trailMat);
        trail.renderOrder = 2;
        overlay.add(trail);

        S.views.push({
            p, kit, holder, root, bones, label, trail, blob,
            Q: bones.map(() => new THREE.Quaternion()),
            hip: new THREE.Vector3(),
            hand: new THREE.Vector3(),
            arrow: makeArrow(SCENE_COLOURS.velocity),
            at: new THREE.Vector3(),
            speed: 0
        });
    });
    S.phases = gaitPhases(replay.gait, (s, i) => Math.hypot(replay.vel[s][i * 3], replay.vel[s][i * 3 + 1]));
}

// ---------------------------------------------------------------------------------------------
// The ball, lines and effects
// ---------------------------------------------------------------------------------------------

// A regulation football, about 28 cm long and 17 cm across (the engine's 15 cm sphere is its
// collision, not its size). Where it is comes from the recording.
const ballRadius = 0.085;
const ball = new THREE.Mesh(new THREE.SphereGeometry(ballRadius, 20, 14).scale(1.65, 1, 1), new THREE.MeshStandardMaterial({ color: SCENE_COLOURS.ball, roughness: 0.55 }));
const laces = new THREE.Mesh(new THREE.BoxGeometry(0.09, 0.008, 0.018), new THREE.MeshBasicMaterial({ color: "#f8f4ea" }));
laces.position.y = ballRadius * 0.98;
ball.add(laces);
scene.add(ball);
const ballGlow = new THREE.Sprite(new THREE.SpriteMaterial({ map: (() =>
{
    const c = document.createElement("canvas");
    c.width = c.height = 64;
    const g = c.getContext("2d");
    const grad = g.createRadialGradient(32, 32, 0, 32, 32, 32);
    grad.addColorStop(0, "rgba(255,243,191,0.9)");
    grad.addColorStop(0.35, "rgba(255,243,191,0.35)");
    grad.addColorStop(1, "rgba(255,243,191,0)");
    g.fillStyle = grad;
    g.fillRect(0, 0, 64, 64);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    return t;
})(), sizeAttenuation: false, depthTest: false, transparent: true }));
ballGlow.scale.set(0.045, 0.045, 1);
ballGlow.renderOrder = 9;
scene.add(ballGlow);
const ballShadow = new THREE.Mesh(new THREE.PlaneGeometry(0.9, 0.9).rotateX(-Math.PI / 2), blobMaterial.clone());
ballShadow.renderOrder = 1;
scene.add(ballShadow);
const ballArrow = makeArrow(SCENE_COLOURS.velocity);
const ballTrailMat = new LineMaterial({ color: SCENE_COLOURS.ballTrail, linewidth: 3.5, transparent: true, opacity: 0.95, depthWrite: false });
lineMaterials.push(ballTrailMat);
let ballTrail = null;

function bandMesh(colour, width)
{
    const m = new THREE.Mesh(new THREE.PlaneGeometry(width, 1).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: colour, transparent: true, opacity: 0.8, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -3, polygonOffsetUnits: -3 }));
    m.renderOrder = 1;
    m.visible = false;
    scene.add(m);
    return m;
}
const losLine = bandMesh(SCENE_COLOURS.los, 0.28);
const firstDownLine = bandMesh(SCENE_COLOURS.firstDown, 0.28);

function ringMesh(colour, inner, outer)
{
    const m = new THREE.Mesh(new THREE.RingGeometry(inner, outer, 48).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: colour, transparent: true, depthWrite: false, depthTest: false }));
    m.renderOrder = 6;
    m.visible = false;
    scene.add(m);
    return m;
}
const followRing = ringMesh(SCENE_COLOURS.follow, 0.7, 0.9);
const targetRing = ringMesh(SCENE_COLOURS.throw, 0.55, 0.75);
const landingMark = (() =>
{
    const g = new THREE.Group();
    const mat = new THREE.MeshBasicMaterial({ color: SCENE_COLOURS.throw, depthTest: false, transparent: true });
    for (const a of [45, -45])
    {
        const bar = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 0.16).rotateX(-Math.PI / 2), mat);
        bar.rotation.y = a * DEG;
        g.add(bar);
    }
    g.renderOrder = 6;
    g.visible = false;
    scene.add(g);
    return g;
})();
const pulsePool = [];
for (let k = 0; k < 8; k++)
{
    pulsePool.push(ringMesh("#ffffff", 0.8, 1.0));
}
const contactPool = [];
for (let k = 0; k < 28; k++)
{
    const link = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, 1, 8).rotateZ(-Math.PI / 2).translate(0.5, 0, 0), new THREE.MeshBasicMaterial({ color: SCENE_COLOURS.contact, transparent: true, opacity: 0.9, depthTest: false }));
    link.renderOrder = 7;
    link.visible = false;
    const glow = new THREE.Mesh(new THREE.CircleGeometry(0.6, 24).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: SCENE_COLOURS.contact, transparent: true, opacity: 0.45, depthWrite: false }));
    glow.renderOrder = 2;
    glow.visible = false;
    scene.add(link, glow);
    contactPool.push({ link, glow });
}

// ---------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------

async function loadStatic()
{
    const schemaDoc = await fetchText("replay_schema.json");
    S.schema = JSON.parse(schemaDoc);
    const dims = await firstJson(["data/field_dimensions.json", "../../Data/field_dimensions.json"]);
    const raw = dims ? dims.doc : null;
    // Back to the C++ spelling the field code uses.
    const defaults = S.schema.fieldDefaults;
    S.dims = {};
    for (const key of Object.keys(defaults))
    {
        if (key === "about")
        {
            continue;
        }
        const value = raw ? L.field(raw, key) : undefined;
        S.dims[key] = Number.isFinite(value) ? value : defaults[key];
    }
    S.F = fieldFrame(S.dims);
    const teams = await firstJson(["data/sample_teams.json", "../../Data/sample_teams.json"]);
    S.teams = teams ? (L.field(teams.doc, "Teams") || []) : [];

    try
    {
        await Promise.race([document.fonts.load('64px "Graduate"'), new Promise((r) => setTimeout(r, 2500))]);
    }
    catch (err)
    {
        // A fallback face draws the numbers.
    }

    const loader = new GLTFLoader();
    let gltf = null;
    for (const url of ["assets/standin.glb", "../../RawAssets/world/people/standin.glb"])
    {
        try
        {
            gltf = await loader.loadAsync(url);
            break;
        }
        catch (err)
        {
            gltf = null;
        }
    }
    if (!gltf)
    {
        throw new Error("The player model (assets/standin.glb) could not be loaded.");
    }
    const model = gltf.scene;
    const drop = [];
    let templateBones = null;
    model.traverse((o) =>
    {
        if (o.isSkinnedMesh)
        {
            if (DROPPED_PARTS.test(o.material.name || ""))
            {
                drop.push(o);
            }
            else if (!templateBones)
            {
                templateBones = o.skeleton.bones;
            }
        }
    });
    drop.forEach((o) => o.parent.remove(o));
    S.R = rig(templateBones);
    S.model = model;
}

async function loadIndex()
{
    const custom = params.get("recordings");
    const tries = [];
    if (custom)
    {
        tries.push(custom.replace(/\/?$/, "/") + "index.json");
    }
    tries.push("recordings/index.json");
    for (const url of tries)
    {
        const found = await firstJson([url]);
        if (found)
        {
            const base = url.slice(0, url.lastIndexOf("/") + 1);
            const entries = L.parseIndex(found.doc, S.schema);
            if (entries.length)
            {
                S.plays = entries.map((e) => ({ entry: e, url: base + e.file, text: null }));
                S.sourceNote = `Recordings from ${base}index.json`;
                return;
            }
        }
    }
    const sample = await firstJson(["sample/SYNTHETIC_index.json"]);
    if (sample)
    {
        S.plays = L.parseIndex(sample.doc, S.schema).map((e) => ({ entry: { ...e, synthetic: true }, url: "sample/" + e.file, text: null }));
        S.sourceNote = "No recordings were found next to this page (recordings/index.json), so it shows the synthetic test sample.";
        return;
    }
    S.plays = [];
}

function playTitle(play, k)
{
    const e = play.entry;
    const parts = [e.name || e.file.replace(/\.json$/i, "").replace(/_/g, " ")];
    if (e.result)
    {
        parts.push(e.yards === null || e.yards === undefined ? e.result : `${e.result} ${e.yards >= 0 ? "+" : ""}${e.yards}`);
    }
    return `${k + 1}. ${parts.join(" · ")}`;
}

function renderPlayList()
{
    const sel = $("play-select");
    sel.innerHTML = "";
    S.plays.forEach((play, k) =>
    {
        const o = document.createElement("option");
        o.value = String(k);
        o.textContent = playTitle(play, k);
        sel.appendChild(o);
    });
    sel.disabled = S.plays.length === 0;
    if (!S.plays.length)
    {
        const o = document.createElement("option");
        o.textContent = "No plays";
        sel.appendChild(o);
    }
}

async function selectPlay(k)
{
    const play = S.plays[k];
    if (!play)
    {
        return;
    }
    const token = ++S.loadToken;
    S.current = k;
    $("play-select").value = String(k);
    setPlaying(false);
    setStatus(`Loading ${play.entry.file}.`);
    let replay;
    try
    {
        const text = play.text !== null ? play.text : await fetchText(play.url);
        const doc = L.parseJson(text);
        replay = L.buildReplay(doc, S.schema, { fileName: play.entry.file, entry: play.entry });
    }
    catch (err)
    {
        if (token === S.loadToken)
        {
            setStatus(`${play.entry.file} could not be played. ${err.message}`, true);
        }
        return;
    }
    if (token !== S.loadToken)
    {
        return;
    }
    S.replay = replay;
    S.kits = kitsFor(replay, play.entry);
    rebuildField();
    buildViews(replay, S.kits);
    buildBallTrail(replay);
    placeLines(replay);
    S.follow = -1;
    $("card").hidden = true;
    $("synthetic-banner").hidden = !replay.synthetic && !play.entry.synthetic;
    renderHud(play, replay);
    renderTimeline(replay);
    renderAbout(play, replay);
    S.t = 0;
    const snap = replay.events.find((e) => e.type === "Snap");
    if (snap)
    {
        S.t = Math.max(0, snap.t - replay.start - 0.4);
    }
    setStatus("");
    setCamera(S.cam === "follow" ? "sideline" : S.cam, true);
    update(true);
    setPlaying(!REDUCED);
}

function rebuildField()
{
    const key = `${S.kits.Offense.primary}|${S.kits.Defense.primary}|${S.kits.Offense.word || ""}|${S.kits.Defense.word || ""}`;
    if (key === fieldKey)
    {
        return;
    }
    fieldKey = key;
    if (fieldGroup)
    {
        scene.remove(fieldGroup);
    }
    const built = buildField(S.dims, {
        font: '"Graduate", "Rockwell", serif',
        ends: { offense: S.kits.Offense.primary, defense: S.kits.Defense.primary },
        words: { offense: S.kits.Offense.word, defense: S.kits.Defense.word },
        groundColor: cssVar("--scene-ground") || "#2c4630"
    });
    fieldGroup = built.group;
    scene.add(fieldGroup);
}

function buildBallTrail(replay)
{
    if (ballTrail)
    {
        overlay.remove(ballTrail);
        ballTrail.geometry.dispose();
    }
    const pts = [];
    const v = new THREE.Vector3();
    for (let i = 0; i < replay.times.length; i++)
    {
        const k = replay.ball.sampled[i] ? i : -1;
        if (k < 0)
        {
            continue;
        }
        toScene(v, replay.ball.pos[i * 3], replay.ball.pos[i * 3 + 1], replay.ball.pos[i * 3 + 2]);
        pts.push(v.x, Math.max(0.05, v.y), v.z);
    }
    while (pts.length < 6)
    {
        pts.push(0, 0, 0);
    }
    const geo = new LineGeometry();
    geo.setPositions(pts);
    ballTrail = new Line2(geo, ballTrailMat);
    ballTrail.renderOrder = 3;
    overlay.add(ballTrail);
}

function placeLines(replay)
{
    const snap = replay.events.find((e) => e.type === "Snap");
    let losX = null;
    const los = snap && L.vec(L.field(snap.payload, "LineOfScrimmage"));
    if (los && (los[0] !== 0 || los[1] !== 0))
    {
        losX = los[0] / 100;
    }
    else if (replay.playState && replay.playState.yardLine > 0)
    {
        losX = replay.playState.yardLine * S.F.yard;
    }
    const width = S.F.halfWidth * 2;
    losLine.visible = losX !== null;
    if (losX !== null)
    {
        losLine.scale.set(1, 1, width);
        losLine.position.set(losX, 0.02, 0);
    }
    const distance = replay.playState ? replay.playState.distance : 0;
    firstDownLine.visible = losX !== null && distance > 0;
    if (firstDownLine.visible)
    {
        firstDownLine.scale.set(1, 1, width);
        firstDownLine.position.set(losX + distance * S.F.yard, 0.02, 0);
    }
    S.losX = losX;
}

// ---------------------------------------------------------------------------------------------
// HUD, timeline, about
// ---------------------------------------------------------------------------------------------

function ordinal(n)
{
    return ["", "1st", "2nd", "3rd", "4th"][n] || `${n}th`;
}

function renderHud(play, replay)
{
    const hud = $("hud");
    hud.innerHTML = "";
    const add = (text, cls) =>
    {
        const el = document.createElement("span");
        el.className = "chip" + (cls ? " " + cls : "");
        el.textContent = text;
        hud.appendChild(el);
    };
    const ps = replay.playState;
    if (ps && ps.down > 0)
    {
        const spot = ps.yardLine <= 50 ? `own ${ps.yardLine}` : `opp ${100 - ps.yardLine}`;
        add(`${ordinal(ps.down)} & ${ps.distance} · ${spot}`);
    }
    const r = replay.result;
    const resultText = r && r.result ? `${r.sack ? "Sack" : r.interception ? "Interception" : r.result}${r.yards !== null ? ` ${r.yards >= 0 ? "+" : ""}${r.yards} yd` : ""}`
        : play.entry.result ? `${play.entry.result}${play.entry.yards !== null ? ` ${play.entry.yards >= 0 ? "+" : ""}${play.entry.yards} yd` : ""}` : "";
    if (resultText)
    {
        add(resultText, "result");
    }
    const off = (replay.calls.offense && replay.calls.offense.name) || play.entry.offenseCall;
    const def = (replay.calls.defense && replay.calls.defense.name) || play.entry.defenseCall;
    if (off || def)
    {
        add([off, def].filter(Boolean).join(" vs "), "quiet calls");
    }
}

function eventColour(e)
{
    return `var(${EVENT_TOKEN[e.type] || "--ev-minor"})`;
}

function renderTimeline(replay)
{
    const track = $("track");
    track.querySelectorAll(".mk").forEach((m) => m.remove());
    const d = Math.max(0.001, replay.duration);
    for (const e of replay.events)
    {
        if (e.kind === "hidden" || e.t < replay.start - 1e-6)
        {
            continue;
        }
        const mk = document.createElement("i");
        mk.className = "mk" + (e.kind === "minor" ? " minor" : "") + (e.derived ? " derived" : "");
        mk.style.left = `${((e.t - replay.start) / d) * 100}%`;
        mk.style.setProperty("--c", eventColour(e));
        mk.title = e.label;
        track.appendChild(mk);
    }
    const scrub = $("scrub");
    scrub.max = String(d);
    const box = $("moments");
    box.innerHTML = "";
    S.momentButtons = [];
    for (const e of replay.events)
    {
        if (e.kind !== "key" || e.t < replay.start - 1e-6)
        {
            continue;
        }
        const b = document.createElement("button");
        b.type = "button";
        b.className = "moment";
        b.style.setProperty("--c", eventColour(e));
        const dot = document.createElement("i");
        if (e.derived)
        {
            dot.className = "derived";
        }
        const label = document.createElement("span");
        label.textContent = e.label;
        const when = document.createElement("b");
        when.textContent = `${(e.t - replay.start).toFixed(1)}s`;
        b.append(dot, label, when);
        b.addEventListener("click", () =>
        {
            seek(Math.max(0, e.t - replay.start - (e.type === "Snap" ? 0.3 : 0.6)));
            setPlaying(true);
        });
        box.appendChild(b);
        S.momentButtons.push({ b, t: e.t - replay.start });
    }
}

function renderAbout(play, replay)
{
    const facts = $("about-facts");
    facts.innerHTML = "";
    const add = (k, v) =>
    {
        if (v === undefined || v === null || v === "")
        {
            return;
        }
        const dt = document.createElement("dt");
        dt.textContent = k;
        const dd = document.createElement("dd");
        dd.textContent = String(v);
        facts.append(dt, dd);
    };
    const h = replay.header;
    $("about-title").textContent = replay.synthetic ? "Synthetic test play" : "About this play";
    add("Source", replay.synthetic ? "Hand-made test data, not the game" : S.sourceNote || "Opened from this device");
    add("File", play.entry.file);
    add("Build", h.gameBuildVersion);
    add("Recorded", h.recordedAtUtc);
    add("Seed", play.entry.seed ?? (h.randomSeed || null));
    add("Format", `version ${h.formatVersion}`);
    add("Frames", `${replay.times.length} over ${replay.duration.toFixed(2)} s, about ${replay.sampleRateHz.toFixed(1)} per second${replay.keyframeCount ? `, ${replay.keyframeCount} keyframes` : ""}`);
    add("Players", `${replay.players.filter((p) => p.side === "Offense").length} offense, ${replay.players.filter((p) => p.side === "Defense").length} defense${replay.players.some((p) => p.jersey) ? "" : ", labelled by position (no jersey numbers in the recording)"}`);
    if (S.kits.Offense.word || S.kits.Defense.word)
    {
        add("Teams", `${S.kits.Offense.label} on offense, ${S.kits.Defense.label} on defense`);
    }
    add("Events", `${replay.events.filter((e) => !e.derived).length}${replay.eventClock !== "frames" ? ` (times ${replay.eventClock})` : ""}`);
    const off = replay.calls.offense;
    const def = replay.calls.defense;
    add("Offense call", off ? [off.name, off.formation].filter(Boolean).join(", ") : play.entry.offenseCall);
    add("Defense call", def ? [def.name, def.front, def.coverage].filter(Boolean).join(", ") : play.entry.defenseCall);
    $("about-synthetic").hidden = !replay.synthetic;
    const warn = $("about-warnings");
    warn.hidden = !replay.warnings.length;
    warn.textContent = replay.warnings.join(" ");
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

function setPlaying(on)
{
    if (on && S.replay && S.t >= S.replay.duration - 1e-3)
    {
        S.t = 0;
    }
    S.playing = !!on && !!S.replay;
    $("play-toggle").setAttribute("aria-label", S.playing ? "Pause" : "Play");
    $("play-icon").innerHTML = S.playing ? '<path d="M7 5h4v14H7zm6 0h4v14h-4z"/>' : '<path d="M8 5v14l11-7z"/>';
    S.dirty = true;
}

function seek(t)
{
    if (!S.replay)
    {
        return;
    }
    S.t = Math.min(S.replay.duration, Math.max(0, t));
    update(true);
}

const tmpA = new THREE.Vector3();
const tmpB = new THREE.Vector3();

function lerpAt(arr, i, j, a, k)
{
    return arr[i * 3 + k] + (arr[j * 3 + k] - arr[i * 3 + k]) * a;
}

function lerpAngle(a, b, t)
{
    let d = ((b - a + 540) % 360) - 180;
    return a + d * t;
}

/** Puts everyone where the recording has them at the playhead. */
function update(force)
{
    const r = S.replay;
    if (!r)
    {
        return;
    }
    const abs = r.start + S.t;
    const { i, j, a } = L.bracket(r.times, abs);
    const near = a < 0.5 ? i : j;
    const carrier = r.carrier[near];
    for (const v of S.views)
    {
        const s = v.p.index;
        const pos = r.pos[s];
        const x = lerpAt(pos, i, j, a, 0);
        const y = lerpAt(pos, i, j, a, 1);
        const z = lerpAt(pos, i, j, a, 2);
        const vx = lerpAt(r.vel[s], i, j, a, 0);
        const vy = lerpAt(r.vel[s], i, j, a, 1);
        const speed = Math.hypot(vx, vy);
        v.speed = speed;
        v.vx = vx;
        v.vy = vy;
        v.ax = lerpAt(r.acc[s], i, j, a, 0);
        v.ay = lerpAt(r.acc[s], i, j, a, 1);
        const yaw = lerpAngle(r.yaw[s][i], r.yaw[s][j], a);
        v.yaw = yaw;
        toScene(v.at, x, y, z - S.schema.units.capsuleHalfHeightCm);
        v.holder.position.copy(v.at);
        v.holder.rotation.y = -yaw * DEG - Math.PI / 2;
        const phase = S.phases[s][i] + (S.phases[s][j] - S.phases[s][i]) * a;
        const carrying = carrier === s;
        const P = playerPosture(phase, speed, abs, carrying);
        const handLocal = solve(S.R, P, v.Q, v.hip);
        for (let b = 0; b < v.bones.length; b++)
        {
            v.bones[b].quaternion.copy(v.Q[b]);
        }
        v.bones[S.R.pelvis].position.copy(v.hip);
        if (carrying && handLocal)
        {
            v.holder.updateMatrixWorld(true);
            v.hand.copy(handLocal).applyMatrix4(v.holder.matrixWorld);
        }
        v.label.visible = S.layers.labels || S.follow === s;
        // Trails: the path run so far.
        v.trail.visible = S.layers.trails;
        v.trail.geometry.instanceCount = Math.max(0, i);
        if (S.layers.vectors)
        {
            setArrow(v.arrow, tmpA.set(v.at.x, 0.12, v.at.z), vx / 100, vy / 100, 0.35);
        }
        else
        {
            v.arrow.visible = false;
        }
    }

    // The ball: where it was recorded, or in the carrier's arm while he has it.
    const sampled = r.ball.sampled[i] || r.ball.sampled[j];
    ball.visible = !!sampled;
    ballShadow.visible = !!sampled;
    ballGlow.visible = !!sampled;
    if (sampled)
    {
        const bi = r.ball.sampled[i] ? i : j;
        const bj = r.ball.sampled[j] ? j : i;
        const ba = bi === bj ? 0 : a;
        const bx = lerpAt(r.ball.pos, bi, bj, ba, 0);
        const by = lerpAt(r.ball.pos, bi, bj, ba, 1);
        const bz = lerpAt(r.ball.pos, bi, bj, ba, 2);
        const bvx = lerpAt(r.ball.vel, bi, bj, ba, 0);
        const bvy = lerpAt(r.ball.vel, bi, bj, ba, 1);
        const bvz = lerpAt(r.ball.vel, bi, bj, ba, 2);
        toScene(tmpA, bx, by, bz);
        const carrierView = carrier >= 0 ? S.views[carrier] : null;
        if (carrierView && tmpA.distanceTo(tmpB.set(carrierView.at.x, tmpA.y, carrierView.at.z)) < 1.2)
        {
            ball.position.copy(carrierView.hand).add(tmpB.set(0, 0.02, 0));
            ball.rotation.set(0, carrierView.holder.rotation.y + Math.PI / 2, 0.5);
        }
        else
        {
            ball.position.set(tmpA.x, Math.max(ballRadius, tmpA.y), tmpA.z);
            const flat = Math.hypot(bvx, bvy);
            if (flat > 150)
            {
                ball.rotation.set(0, -Math.atan2(bvy, bvx), Math.atan2(bvz, flat));
            }
        }
        ballGlow.position.copy(ball.position);
        ballGlow.visible = S.layers.labels;
        ballShadow.position.set(ball.position.x, 0.025, ball.position.z);
        const height = Math.max(0, ball.position.y);
        ballShadow.scale.setScalar(0.45 + Math.min(1.4, height * 0.1));
        ballShadow.material.opacity = Math.max(0.15, 0.6 - height * 0.04);
        if (S.layers.vectors && Math.hypot(bvx, bvy, bvz) > 150)
        {
            setArrow(ballArrow, tmpB.set(ball.position.x, ball.position.y, ball.position.z), bvx / 100, bvy / 100, 0.35);
        }
        else
        {
            ballArrow.visible = false;
        }
    }
    if (ballTrail)
    {
        ballTrail.visible = S.layers.trails;
        let n = 0;
        for (let k = 0; k <= i; k++)
        {
            n += r.ball.sampled[k];
        }
        ballTrail.geometry.instanceCount = Math.max(0, n - 1);
    }

    updateContacts(r, i, j, a);
    updateEffects(r, abs);
    updateFollow();
    updateClock();
    S.dirty = true;
}

function updateContacts(r, i, j, a)
{
    const pairs = S.layers.contacts ? r.contacts[a < 0.5 ? i : j] : [];
    let k = 0;
    for (const [pa, pb, closing] of pairs)
    {
        if (k >= contactPool.length)
        {
            break;
        }
        const { link, glow } = contactPool[k++];
        const A = S.views[pa].at;
        const B = S.views[pb].at;
        const dx = B.x - A.x;
        const dz = B.z - A.z;
        const len = Math.hypot(dx, dz);
        link.visible = glow.visible = true;
        link.position.set(A.x, 1.05, A.z);
        link.rotation.set(0, -Math.atan2(dz, dx), 0);
        link.scale.set(Math.max(0.05, len), 1, 1);
        const heat = Math.min(1, Math.max(0, closing / 400));
        link.material.color.set(SCENE_COLOURS.contact).lerp(new THREE.Color(SCENE_COLOURS.tackle), heat);
        glow.position.set((A.x + B.x) / 2, 0.03, (A.z + B.z) / 2);
    }
    for (; k < contactPool.length; k++)
    {
        contactPool[k].link.visible = contactPool[k].glow.visible = false;
    }
}

function viewByName(name)
{
    if (!name)
    {
        return null;
    }
    const n = String(name).toLowerCase();
    return S.views.find((v) => v.p.name.toLowerCase() === n || v.p.id.toLowerCase() === n) || null;
}

/** Rings for the moments the bus announced: a tackle, a catch, a hit, the throw's target and
 *  landing spot. Each is a function of the playhead, so scrubbing shows them too. */
function updateEffects(r, abs)
{
    let used = 0;
    const pulse = (x, z, colour, age, life, size) =>
    {
        if (used >= pulsePool.length || age < 0 || age > life)
        {
            return;
        }
        const m = pulsePool[used++];
        const f = age / life;
        m.visible = true;
        m.position.set(x, 0.05, z);
        m.scale.setScalar(size * (REDUCED ? 1 : 0.6 + f * 1.6));
        m.material.color.set(colour);
        m.material.opacity = 1 - f;
    };
    targetRing.visible = false;
    landingMark.visible = false;
    const throwEvent = r.events.filter((e) => e.type === "Throw" && e.t <= abs + 1e-6).pop();
    if (throwEvent)
    {
        const end = r.events.find((e) => e.t > throwEvent.t && ["Catch", "BallGrounded", "LooseBall", "Tackle", "Whistle"].includes(e.type));
        if (!end || abs <= end.t + 0.4)
        {
            const target = L.vec(L.field(throwEvent.payload, "TargetLocation"));
            const landing = L.vec(L.field(throwEvent.payload, "LandingLocation"));
            if (target)
            {
                targetRing.visible = true;
                targetRing.position.set(target[0] / 100, 0.04, target[1] / 100);
            }
            if (landing)
            {
                landingMark.visible = true;
                landingMark.position.set(landing[0] / 100, 0.045, landing[1] / 100);
            }
        }
    }
    for (const e of r.events)
    {
        const age = abs - e.t;
        if (age < 0 || age > 1.2)
        {
            continue;
        }
        if (e.type === "Tackle" || e.type === "Fumble")
        {
            const v = viewByName(L.field(e.payload, "BallCarrierName", "FumblerName")) || (r.carrier[L.frameAtOrBefore(r.times, e.t)] >= 0 ? S.views[r.carrier[L.frameAtOrBefore(r.times, e.t)]] : null);
            if (v)
            {
                pulse(v.at.x, v.at.z, SCENE_COLOURS.tackle, age, 1.0, 1.4);
            }
        }
        else if (e.type === "Catch")
        {
            const at = L.vec(L.field(e.payload, "CatchLocation"));
            const v = viewByName(L.field(e.payload, "ReceiverName"));
            if (at)
            {
                pulse(at[0] / 100, at[1] / 100, SCENE_COLOURS.catch, age, 0.9, 1.1);
            }
            else if (v)
            {
                pulse(v.at.x, v.at.z, SCENE_COLOURS.catch, age, 0.9, 1.1);
            }
        }
        else if (e.type === "Damage")
        {
            const v = viewByName(L.field(e.payload, "TargetName"));
            if (v)
            {
                pulse(v.at.x, v.at.z, SCENE_COLOURS.tackle, age, 0.5, 0.9);
            }
        }
        else if (e.type === "PassRushMove")
        {
            const v = viewByName(L.field(e.payload, "RusherName"));
            if (v)
            {
                pulse(v.at.x, v.at.z, SCENE_COLOURS.contact, age, 0.8, 0.9);
            }
        }
    }
    for (let k = used; k < pulsePool.length; k++)
    {
        pulsePool[k].visible = false;
    }
}

function updateClock()
{
    const r = S.replay;
    const d = r ? r.duration : 0;
    const frame = r ? L.frameAtOrBefore(r.times, r.start + S.t) + 1 : 0;
    $("clock").innerHTML = `${S.t.toFixed(2)} s<small>of ${d.toFixed(2)} s · frame ${frame}/${r ? r.times.length : 0}</small>`;
    const scrub = $("scrub");
    if (document.activeElement !== scrub || !scrubbing)
    {
        scrub.value = String(S.t);
    }
    $("fill").style.width = `${d > 0 ? (S.t / d) * 100 : 0}%`;
    if (S.momentButtons)
    {
        for (const m of S.momentButtons)
        {
            m.b.classList.toggle("passed", m.t <= S.t + 1e-3);
        }
    }
}

function updateFollow()
{
    const v = S.follow >= 0 ? S.views[S.follow] : null;
    followRing.visible = !!v;
    if (!v)
    {
        return;
    }
    followRing.position.set(v.at.x, 0.04, v.at.z);
    const mps = v.speed / 100;
    const acc = Math.hypot(v.ax, v.ay) / 100;
    $("card-speed").textContent = `${mps.toFixed(1)} m/s (${(mps * 2.23694).toFixed(1)} mph) · accel ${acc.toFixed(1)} m/s²${S.replay.carrier[L.frameAtOrBefore(S.replay.times, S.replay.start + S.t)] === S.follow ? " · has the ball" : ""}`;
}

function followPlayer(s)
{
    S.follow = s;
    const card = $("card");
    if (s < 0)
    {
        card.hidden = true;
        if (S.cam === "follow")
        {
            setCamera("sideline");
        }
        update(true);
        return;
    }
    const v = S.views[s];
    card.hidden = false;
    card.style.setProperty("--team", v.kit.primary);
    $("card-num").textContent = shortLabel(v.p);
    $("card-who").textContent = v.p.name;
    const bits = [ROLE_LONG[v.p.role] || v.p.role, v.p.side === "Offense" ? "offense" : "defense"];
    if (v.p.heightCm)
    {
        bits.push(`${Math.round(v.p.heightCm)} cm`);
    }
    if (v.p.weightKg)
    {
        bits.push(`${Math.round(v.p.weightKg)} kg`);
    }
    $("card-what").textContent = bits.join(" · ");
    setCamera("follow");
    update(true);
}

// ---------------------------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------------------------

const camState = { pos: new THREE.Vector3(), target: new THREE.Vector3(), wantPos: new THREE.Vector3(), wantTarget: new THREE.Vector3(), snap: true, dir: new THREE.Vector3(1, 0, 0) };

function setCamera(mode, instant)
{
    S.cam = mode;
    document.querySelectorAll("#cameras .pill").forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.cam === mode)));
    if (mode === "free")
    {
        controls.target.copy(camState.target);
        controls.update();
    }
    camState.snap = !!instant || REDUCED;
    S.dirty = true;
}

function fitDistance(points, target, forward, aspect)
{
    const up = new THREE.Vector3(0, 1, 0);
    const right = new THREE.Vector3().crossVectors(forward, up).normalize();
    const camUp = new THREE.Vector3().crossVectors(right, forward).normalize();
    const tv = Math.tan((camera.fov * DEG) / 2) * 0.82;
    const th = tv * aspect;
    let D = 0;
    const d = new THREE.Vector3();
    for (const p of points)
    {
        d.subVectors(p, target);
        const px = d.dot(right);
        const py = d.dot(camUp);
        const pz = d.dot(forward);
        D = Math.max(D, Math.abs(px) / th - pz, Math.abs(py) / tv - pz);
    }
    return D;
}

function actionPoints(radius)
{
    const pts = [];
    const center = ball.visible ? new THREE.Vector3(ball.position.x, 0, ball.position.z) : null;
    for (const v of S.views)
    {
        if (!center || Math.hypot(v.at.x - center.x, v.at.z - center.z) <= radius)
        {
            pts.push(new THREE.Vector3(v.at.x, 0, v.at.z), new THREE.Vector3(v.at.x, 2, v.at.z));
        }
    }
    if (center)
    {
        pts.push(center, ball.position.clone());
    }
    return pts;
}

function boxCenter(points)
{
    const box = new THREE.Box3().setFromPoints(points);
    const c = box.getCenter(new THREE.Vector3());
    c.y = 0;
    return c;
}

function cameraWant(dt)
{
    const aspect = camera.aspect;
    let forward = null;
    let target = null;
    let points = null;
    let minD = 12;
    let maxD = 110;
    if (S.cam === "sideline")
    {
        points = actionPoints(16);
        target = boxCenter(points);
        forward = new THREE.Vector3(0, -Math.sin(17 * DEG), -Math.cos(17 * DEG));
        minD = 18;
        maxD = 95;
    }
    else if (S.cam === "endzone")
    {
        points = actionPoints(13);
        target = boxCenter(points);
        forward = new THREE.Vector3(Math.cos(16 * DEG), -Math.sin(16 * DEG), 0);
        minD = 16;
        maxD = 95;
    }
    else if (S.cam === "all22")
    {
        points = actionPoints(Infinity);
        target = boxCenter(points);
        forward = new THREE.Vector3(Math.cos(58 * DEG), -Math.sin(58 * DEG), 0);
        minD = 25;
        maxD = 170;
    }
    else if (S.cam === "ball" || S.cam === "follow")
    {
        let at;
        let vx;
        let vz;
        if (S.cam === "follow" && S.follow >= 0)
        {
            const v = S.views[S.follow];
            at = new THREE.Vector3(v.at.x, 1.0, v.at.z);
            vx = v.vx / 100;
            vz = v.vy / 100;
            if (Math.hypot(vx, vz) < 1)
            {
                vx = Math.cos(v.yaw * DEG);
                vz = Math.sin(v.yaw * DEG);
            }
        }
        else
        {
            at = ball.position.clone();
            at.y = Math.max(0.8, at.y);
            const r = S.replay;
            const { i } = L.bracket(r.times, r.start + S.t);
            vx = r.ball.vel[i * 3] / 100;
            vz = r.ball.vel[i * 3 + 1] / 100;
            if (Math.hypot(vx, vz) < 2)
            {
                vx = camState.dir.x;
                vz = camState.dir.z;
            }
        }
        const want = new THREE.Vector3(vx, 0, vz).normalize();
        const k = camState.snap ? 1 : 1 - Math.exp(-dt * 2.5);
        camState.dir.lerp(want, k).normalize();
        const back = S.cam === "follow" ? 7.5 : 9;
        camState.wantPos.set(at.x - camState.dir.x * back, at.y + (S.cam === "follow" ? 3.2 : 4.2), at.z - camState.dir.z * back);
        camState.wantTarget.set(at.x + camState.dir.x * 5, at.y * 0.6, at.z + camState.dir.z * 5);
        return;
    }
    if (!forward)
    {
        return;
    }
    const D = Math.min(maxD, Math.max(minD, fitDistance(points, target, forward, aspect)));
    camState.wantTarget.copy(target);
    camState.wantPos.copy(target).addScaledVector(forward, -D);
}

const clock = new THREE.Clock();

function frame()
{
    const dt = Math.min(0.1, clock.getDelta());
    if (S.replay && S.playing)
    {
        S.t += dt * S.speed;
        if (S.t >= S.replay.duration)
        {
            S.t = S.replay.duration;
            setPlaying(false);
        }
        update(false);
    }
    if (S.replay && S.cam !== "free")
    {
        cameraWant(dt);
        const k = camState.snap ? 1 : 1 - Math.exp(-dt * 4);
        camState.pos.lerp(camState.wantPos, k);
        camState.target.lerp(camState.wantTarget, k);
        camState.snap = false;
        const moving = camState.pos.distanceToSquared(camState.wantPos) > 1e-4 || camState.target.distanceToSquared(camState.wantTarget) > 1e-4;
        camera.position.copy(camState.pos);
        camera.lookAt(camState.target);
        if (moving)
        {
            S.dirty = true;
        }
    }
    else if (S.cam === "free")
    {
        if (controls.update())
        {
            S.dirty = true;
        }
        camState.pos.copy(camera.position);
        camState.target.copy(controls.target);
    }
    if (S.dirty)
    {
        renderer.render(scene, camera);
        S.dirty = false;
    }
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

let scrubbing = false;
let pointerDown = null;

controls.addEventListener("change", () =>
{
    // A drag or pinch in a scripted view hands the camera to the viewer.
    if (S.cam !== "free" && pointerDown && pointerDown.moved)
    {
        controls.target.copy(camState.target);
        setCamera("free");
    }
    S.dirty = true;
});
canvas.addEventListener("pointerdown", (ev) =>
{
    pointerDown = { x: ev.clientX, y: ev.clientY, t: performance.now(), moved: false, id: ev.pointerId };
});
canvas.addEventListener("pointermove", (ev) =>
{
    if (pointerDown && Math.hypot(ev.clientX - pointerDown.x, ev.clientY - pointerDown.y) > 6)
    {
        pointerDown.moved = true;
    }
});
canvas.addEventListener("pointerup", (ev) =>
{
    const down = pointerDown;
    pointerDown = null;
    if (!down || down.moved || performance.now() - down.t > 600 || !S.replay)
    {
        return;
    }
    // Tap: the nearest player on screen, within a thumb's reach.
    const rect = canvas.getBoundingClientRect();
    let best = -1;
    let bestD = 44;
    const p = new THREE.Vector3();
    for (const v of S.views)
    {
        p.set(v.at.x, v.at.y + 1.0 * v.holder.scale.y, v.at.z).project(camera);
        if (p.z > 1)
        {
            continue;
        }
        const sx = ((p.x + 1) / 2) * rect.width + rect.left;
        const sy = ((1 - p.y) / 2) * rect.height + rect.top;
        const d = Math.hypot(sx - ev.clientX, sy - ev.clientY);
        if (d < bestD)
        {
            bestD = d;
            best = v.p.index;
        }
    }
    if (best >= 0)
    {
        followPlayer(best);
    }
});
canvas.addEventListener("pointercancel", () => { pointerDown = null; });

$("play-toggle").addEventListener("click", () => setPlaying(!S.playing));
$("scrub").addEventListener("input", (ev) =>
{
    scrubbing = true;
    setPlaying(false);
    seek(Number(ev.target.value));
});
$("scrub").addEventListener("change", () => { scrubbing = false; });
$("speeds").addEventListener("click", (ev) =>
{
    const b = ev.target.closest("button[data-speed]");
    if (!b)
    {
        return;
    }
    S.speed = Number(b.dataset.speed);
    document.querySelectorAll("#speeds button").forEach((x) => x.setAttribute("aria-pressed", String(x === b)));
});
$("cameras").addEventListener("click", (ev) =>
{
    const b = ev.target.closest("button[data-cam]");
    if (!b)
    {
        return;
    }
    if (S.follow >= 0)
    {
        S.follow = -1;
        $("card").hidden = true;
    }
    setCamera(b.dataset.cam);
    update(true);
});
$("layers").addEventListener("click", (ev) =>
{
    const b = ev.target.closest("button[data-layer]");
    if (!b)
    {
        return;
    }
    const on = b.getAttribute("aria-pressed") !== "true";
    b.setAttribute("aria-pressed", String(on));
    S.layers[b.dataset.layer] = on;
    update(true);
});
$("card-close").addEventListener("click", () => followPlayer(-1));
$("play-select").addEventListener("change", (ev) => selectPlay(Number(ev.target.value)));
$("about-open").addEventListener("click", () =>
{
    $("about").showModal();
    $("about-title").focus();
    $("about").scrollTop = 0;
});
$("about-close").addEventListener("click", () => $("about").close());
window.addEventListener("keydown", (ev) =>
{
    if (ev.target.closest && ev.target.closest("select, input, dialog"))
    {
        return;
    }
    if (ev.code === "Space")
    {
        ev.preventDefault();
        setPlaying(!S.playing);
    }
    else if ((ev.code === "ArrowRight" || ev.code === "ArrowLeft") && S.replay)
    {
        // A step to the next or previous recorded frame.
        const r = S.replay;
        const i = L.frameAtOrBefore(r.times, r.start + S.t + 1e-4);
        const k = ev.code === "ArrowRight" ? Math.min(r.times.length - 1, i + 1) : Math.max(0, i - (r.times[i] >= r.start + S.t - 1e-4 ? 1 : 0));
        setPlaying(false);
        seek(r.times[k] - r.start);
    }
});

$("open-files").addEventListener("change", async (ev) =>
{
    const files = Array.from(ev.target.files || []);
    if (!files.length)
    {
        return;
    }
    const texts = await Promise.all(files.map(async (f) => ({ name: f.name, text: await f.text() })));
    let entries = null;
    for (const f of texts)
    {
        try
        {
            const doc = L.parseJson(f.text);
            if (!L.field(doc, "Frames"))
            {
                const parsed = L.parseIndex(doc, S.schema);
                if (parsed.length)
                {
                    entries = parsed;
                }
            }
        }
        catch (err)
        {
            // Not JSON: reported when it is opened as a play.
        }
    }
    const byName = new Map(texts.map((f) => [f.name, f.text]));
    const plays = [];
    if (entries)
    {
        for (const e of entries)
        {
            const base = e.file.split(/[\\/]/).pop();
            if (byName.has(base))
            {
                plays.push({ entry: e, url: null, text: byName.get(base) });
            }
        }
    }
    for (const f of texts)
    {
        if (!plays.some((p) => p.entry.file.split(/[\\/]/).pop() === f.name) && !(entries && /index/i.test(f.name)))
        {
            plays.push({ entry: { file: f.name, name: null, result: null, yards: null, seed: null, synthetic: false }, url: null, text: f.text });
        }
    }
    $("open-note").textContent = `${plays.length} play${plays.length === 1 ? "" : "s"} opened.`;
    if (!plays.length)
    {
        return;
    }
    S.plays = plays;
    S.sourceNote = "Opened from this device";
    renderPlayList();
    $("about").close();
    selectPlay(0);
});

// ---------------------------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------------------------

async function start()
{
    resize();
    renderer.setAnimationLoop(frame);
    try
    {
        await loadStatic();
    }
    catch (err)
    {
        setStatus(`The viewer could not start. ${err.message}`, true);
        return;
    }
    setStatus("Looking for recordings.");
    await loadIndex();
    renderPlayList();
    if (!S.plays.length)
    {
        setStatus("No recordings found. Put the play-demos files in recordings/ next to this page (README.md), or open replay files from the About panel.", true);
        return;
    }
    await selectPlay(0);
}

start();
