// field.js - the field, built from Data/field_dimensions.json in the game's own frame.
//
// PSField (Source/PlaySports/Public/PSFieldDimensions.h): yard line N, from the offense's goal
// line, is at X = N * CentimetresPerYard; the offense attacks +X; Y = 0 is the middle of the
// field. This page draws in metres with y up, so a world point (X, Y, Z) cm is (X, Z, Y) / 100.
//
// The game's yard is a metre. Lines, hash marks and numbers follow the NFL layout scaled to that
// yard: hash marks 18 ft 6 in apart (6.17 yd), numbers 2 yd tall with their middles 10 yd in from
// the sidelines, a 2-yard try line. The game itself has no markings data; these are drawing only.

import * as THREE from "three";

const HASH_HALF_SPAN_YD = 6.1667 / 2;
const LINE_M = 0.15;
const GOAL_LINE_M = 0.25;
const BORDER_M = 1.2;

/** Metres per game yard, and the field's extents in metres, from the dimensions file. */
export function fieldFrame(dims)
{
    const m = (dims.CentimetresPerYard || 100) / 100;
    return {
        yard: m,
        length: dims.FieldLengthYards * m,
        endZone: dims.EndZoneDepthYards * m,
        halfWidth: (dims.FieldWidthYards * m) / 2,
        outOfBounds: dims.OutOfBoundsDepthYards * m
    };
}

/** Two triangles facing up (+y) over [x0, x1] x [z0, z1]. */
function quad(positions, colors, x0, z0, x1, z1, y, color)
{
    positions.push(x0, y, z0, x0, y, z1, x1, y, z1, x0, y, z0, x1, y, z1, x1, y, z0);
    for (let k = 0; k < 6; k++)
    {
        colors.push(color.r, color.g, color.b);
    }
}

function flatMesh(positions, colors, material)
{
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.Float32BufferAttribute(positions, 3));
    g.setAttribute("color", new THREE.Float32BufferAttribute(colors, 3));
    g.computeVertexNormals();
    return new THREE.Mesh(g, material);
}

function grainTexture()
{
    const size = 128;
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = size;
    const ctx = canvas.getContext("2d");
    const img = ctx.createImageData(size, size);
    let seed = 7;
    const rand = () =>
    {
        seed = (seed * 16807) % 2147483647;
        return seed / 2147483647;
    };
    for (let i = 0; i < size * size; i++)
    {
        const v = 200 + Math.floor(rand() * 55);
        img.data[i * 4] = v;
        img.data[i * 4 + 1] = v;
        img.data[i * 4 + 2] = v;
        img.data[i * 4 + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
    const tex = new THREE.CanvasTexture(canvas);
    tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
    tex.colorSpace = THREE.SRGBColorSpace;
    return tex;
}

function textTexture(text, font, options = {})
{
    const canvas = document.createElement("canvas");
    const h = options.height || 256;
    const ctx = canvas.getContext("2d");
    ctx.font = `${h * 0.86}px ${font}`;
    const spacing = options.spacing || 0;
    const chars = Array.from(text);
    const widths = chars.map((c) => ctx.measureText(c).width);
    const w = Math.ceil(widths.reduce((a, b) => a + b, 0) + spacing * (chars.length - 1) + h * 0.1);
    canvas.width = w;
    canvas.height = h;
    ctx.font = `${h * 0.86}px ${font}`;
    ctx.textBaseline = "middle";
    ctx.fillStyle = options.color || "#ffffff";
    let x = h * 0.05;
    chars.forEach((c, k) =>
    {
        ctx.fillText(c, x, h * 0.54);
        x += widths[k] + spacing;
    });
    const tex = new THREE.CanvasTexture(canvas);
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.anisotropy = 4;
    return { tex, aspect: w / h };
}

/**
 * The field as a Group: grass with mowing bands, end zones in each side's colour, every line,
 * hash mark and number. ends: { offense, defense } colours, and optional end-zone words.
 */
export function buildField(dims, options)
{
    const F = fieldFrame(dims);
    const group = new THREE.Group();
    group.name = "field";
    const font = options.font || "sans-serif";
    const grass = new THREE.MeshLambertMaterial({ vertexColors: true, map: grainTexture() });
    grass.map.repeat.set(1, 1);

    // The ground beyond: one big plane under the fog.
    const ground = new THREE.Mesh(new THREE.PlaneGeometry(1200, 1200), new THREE.MeshLambertMaterial({ color: options.groundColor || "#2f4a32" }));
    ground.name = "ground";
    ground.rotation.x = -Math.PI / 2;
    ground.position.set(F.length / 2, -0.02, 0);
    group.add(ground);

    // Grass: the out-of-bounds apron, then the field in five-yard mowing bands.
    const positions = [];
    const colors = [];
    const apron = new THREE.Color(options.apronColor || "#2d6b37");
    const bandA = new THREE.Color(options.grassA || "#3d8a45");
    const bandB = new THREE.Color(options.grassB || "#357d3d");
    const pad = F.outOfBounds;
    quad(positions, colors, -F.endZone - pad, -F.halfWidth - pad, F.length + F.endZone + pad, F.halfWidth + pad, 0, apron);
    const band = 5 * F.yard;
    for (let x = 0, k = 0; x < F.length - 1e-6; x += band, k++)
    {
        quad(positions, colors, x, -F.halfWidth, Math.min(F.length, x + band), F.halfWidth, 0.004, k % 2 ? bandB : bandA);
    }
    // End zones in each side's colour, a little darker than the kit.
    const ezOffense = new THREE.Color(options.ends.offense).multiplyScalar(0.72);
    const ezDefense = new THREE.Color(options.ends.defense).multiplyScalar(0.72);
    quad(positions, colors, -F.endZone, -F.halfWidth, 0, F.halfWidth, 0.004, ezOffense);
    quad(positions, colors, F.length, -F.halfWidth, F.length + F.endZone, F.halfWidth, 0.004, ezDefense);
    const turf = flatMesh(positions, colors, grass);
    // The grain repeats every two metres.
    const uv = [];
    for (let i = 0; i < positions.length; i += 3)
    {
        uv.push(positions[i] / 2, positions[i + 2] / 2);
    }
    turf.geometry.setAttribute("uv", new THREE.Float32BufferAttribute(uv, 2));
    group.add(turf);

    // Lines: one mesh of white quads.
    const lp = [];
    const lc = [];
    const white = new THREE.Color("#f4f6f2");
    const y = 0.012;
    const hw = F.halfWidth;
    // Border (sidelines and end lines, outside the field of play).
    quad(lp, lc, -F.endZone - BORDER_M, -hw - BORDER_M, F.length + F.endZone + BORDER_M, -hw, y, white);
    quad(lp, lc, -F.endZone - BORDER_M, hw, F.length + F.endZone + BORDER_M, hw + BORDER_M, y, white);
    quad(lp, lc, -F.endZone - BORDER_M, -hw, -F.endZone, hw, y, white);
    quad(lp, lc, F.length + F.endZone, -hw, F.length + F.endZone + BORDER_M, hw, y, white);
    for (let yd = 0; yd <= dims.FieldLengthYards + 1e-6; yd += 1)
    {
        const x = yd * F.yard;
        if (yd % 5 === 0)
        {
            const w = yd === 0 || Math.abs(yd - dims.FieldLengthYards) < 1e-6 ? GOAL_LINE_M : LINE_M;
            quad(lp, lc, x - w / 2, -hw, x + w / 2, hw, y, white);
            continue;
        }
        // One-yard ticks along the sidelines and at the hash marks.
        const tick = 0.6;
        quad(lp, lc, x - LINE_M / 2, -hw + 0.3, x + LINE_M / 2, -hw + 0.3 + tick, y, white);
        quad(lp, lc, x - LINE_M / 2, hw - 0.3 - tick, x + LINE_M / 2, hw - 0.3, y, white);
        const hz = HASH_HALF_SPAN_YD * F.yard;
        quad(lp, lc, x - LINE_M / 2, -hz - tick, x + LINE_M / 2, -hz, y, white);
        quad(lp, lc, x - LINE_M / 2, hz, x + LINE_M / 2, hz + tick, y, white);
    }
    // The try lines, two yards out from each goal line.
    for (const x of [2 * F.yard, F.length - 2 * F.yard])
    {
        quad(lp, lc, x - LINE_M / 2, -0.5 * F.yard, x + LINE_M / 2, 0.5 * F.yard, y, white);
    }
    const lines = flatMesh(lp, lc, new THREE.MeshBasicMaterial({ vertexColors: true, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1 }));
    group.add(lines);

    // Numbers every ten yards, read from the near sideline; arrows point to the nearer goal.
    const numberMaterial = (tex) => new THREE.MeshBasicMaterial({ map: tex, transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
    const cache = new Map();
    const tall = 2 * F.yard;
    const middle = hw - 10 * F.yard;
    for (let yd = 10; yd < dims.FieldLengthYards; yd += 10)
    {
        const label = String(Math.min(yd, dims.FieldLengthYards - yd));
        if (!cache.has(label))
        {
            cache.set(label, textTexture(label, font, { spacing: 70, color: "#f4f6f2" }));
        }
        const { tex, aspect } = cache.get(label);
        const geo = new THREE.PlaneGeometry(tall * aspect, tall);
        for (const side of [-1, 1])
        {
            const mesh = new THREE.Mesh(geo, numberMaterial(tex));
            mesh.rotation.x = -Math.PI / 2;
            // The numbers' bottoms face their own sideline.
            mesh.rotation.z = side < 0 ? Math.PI : 0;
            mesh.position.set(yd * F.yard, 0.016, side * middle);
            group.add(mesh);
            const towardOwn = yd < dims.FieldLengthYards / 2 ? -1 : 1;
            if (yd !== dims.FieldLengthYards / 2)
            {
                const arrow = new THREE.Mesh(new THREE.CircleGeometry(0.42 * F.yard, 3), new THREE.MeshBasicMaterial({ color: "#f4f6f2", polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 }));
                arrow.rotation.x = -Math.PI / 2;
                arrow.rotation.z = towardOwn < 0 ? Math.PI : 0;
                arrow.position.set(yd * F.yard + towardOwn * (tall * aspect / 2 + 0.6 * F.yard), 0.016, side * (middle - 0.55 * F.yard));
                group.add(arrow);
            }
        }
    }

    // End-zone words, when the teams are known.
    for (const [key, x0] of [["offense", -F.endZone / 2], ["defense", F.length + F.endZone / 2]])
    {
        const word = options.words && options.words[key];
        if (!word)
        {
            continue;
        }
        const { tex, aspect } = textTexture(word.toUpperCase(), font, { spacing: 18, color: "#f4f6f2" });
        const height = Math.min(F.endZone * 0.55, (F.halfWidth * 1.6) / aspect);
        const mesh = new THREE.Mesh(new THREE.PlaneGeometry(height * aspect, height), numberMaterial(tex));
        mesh.material.opacity = 0.85;
        mesh.rotation.x = -Math.PI / 2;
        mesh.rotation.z = key === "offense" ? -Math.PI / 2 : Math.PI / 2;
        mesh.position.set(x0, 0.016, 0);
        group.add(mesh);
    }
    return { group, frame: F };
}
