// loader.js - reads a recording written by UPSReplayFormat::SerializeToJson, and the demo index
// beside it, into arrays the viewer plays back. No DOM and no three.js: Node runs it in
// tools/tests/test_replay_viewer.py. The field contract is replay_schema.json.
//
// Coordinates stay in the engine's frame here (cm; X along the field, the offense attacking +X
// from its goal line at X = 0; Y across, 0 the middle; Z up). The page converts to its own.

/** Parses JSON with every object key lower-cased: FJsonObjectConverter writes camelCase
 *  (formatVersion), hand-written data uses PascalCase, and both read the same. */
export function parseJson(text)
{
    if (typeof text !== "string")
    {
        throw new Error("Expected JSON text.");
    }
    // A UTF-8 byte order mark, which some Windows tools write, is not JSON.
    const clean = text.charCodeAt(0) === 0xfeff ? text.slice(1) : text;
    return JSON.parse(clean, (key, value) =>
    {
        if (value && typeof value === "object" && !Array.isArray(value))
        {
            const out = {};
            for (const k of Object.keys(value))
            {
                out[k.toLowerCase()] = value[k];
            }
            return out;
        }
        return value;
    });
}

/** obj[name] for any of the names (C++ or alias spelling), whatever the key's case. */
export function field(obj, ...names)
{
    if (!obj || typeof obj !== "object")
    {
        return undefined;
    }
    for (const name of names.flat())
    {
        const value = obj[String(name).toLowerCase()];
        if (value !== undefined && value !== null)
        {
            return value;
        }
    }
    return undefined;
}

/** 'EPSTeamSide::Offense' or 'Offense' -> 'Offense'. */
export function enumName(value)
{
    if (value === undefined || value === null)
    {
        return "";
    }
    const text = String(value);
    const cut = text.lastIndexOf("::");
    return cut >= 0 ? text.slice(cut + 2) : text;
}

/** An FVector as FJsonObjectConverter writes it ({x, y, z}), as an array, or as the engine's
 *  text form ('X=1.0 Y=2.0 Z=3.0'). Null when it is none of those. */
export function vec(value)
{
    if (!value)
    {
        return null;
    }
    if (Array.isArray(value) && value.length >= 3)
    {
        const out = [Number(value[0]), Number(value[1]), Number(value[2])];
        return out.every(Number.isFinite) ? out : null;
    }
    if (typeof value === "object")
    {
        const out = [Number(value.x), Number(value.y), Number(value.z)];
        return out.every(Number.isFinite) ? out : null;
    }
    if (typeof value === "string")
    {
        const m = /X=\s*([-+0-9.eE]+)\s*,?\s*Y=\s*([-+0-9.eE]+)\s*,?\s*Z=\s*([-+0-9.eE]+)/.exec(value);
        if (m)
        {
            return [Number(m[1]), Number(m[2]), Number(m[3])];
        }
    }
    return null;
}

function num(value, fallback)
{
    const n = Number(value);
    return value !== undefined && value !== null && value !== "" && Number.isFinite(n) ? n : fallback;
}

function bool(value)
{
    if (typeof value === "string")
    {
        return value.toLowerCase() === "true";
    }
    return !!value;
}

function typeMatches(type, value)
{
    if (type === "int" || type === "number")
    {
        return typeof value === "number" && Number.isFinite(value);
    }
    if (type === "bool")
    {
        return typeof value === "boolean";
    }
    if (type === "string" || type.startsWith("enum:"))
    {
        return typeof value === "string";
    }
    if (type === "vector")
    {
        return vec(value) !== null;
    }
    if (type.startsWith("struct:"))
    {
        return !!value && typeof value === "object" && !Array.isArray(value);
    }
    if (type.startsWith("array:"))
    {
        return Array.isArray(value);
    }
    return true;
}

/**
 * Problems with a parsed recording against the schema, as { errors, warnings }. Errors stop the
 * viewer (no header, no frames, a frame without pawns); warnings are shown and playback goes on.
 * Arrays are checked on their first, middle and last element: a frame list is long and uniform.
 */
export function validateRecording(doc, schema)
{
    const errors = [];
    const warnings = [];
    const structs = schema.structs;

    function check(obj, structName, path)
    {
        const spec = structs[structName];
        if (!spec)
        {
            return;
        }
        for (const [name, f] of Object.entries(spec.fields))
        {
            const value = field(obj, name);
            const at = `${path}.${name}`;
            if (value === undefined)
            {
                if (f.required)
                {
                    errors.push(`${at} is missing.`);
                }
                continue;
            }
            if (!typeMatches(f.type, value))
            {
                (f.required ? errors : warnings).push(`${at} is not a ${f.type}.`);
                continue;
            }
            if (f.min !== undefined && value < f.min)
            {
                errors.push(`${at} is ${value}, below ${f.min}.`);
            }
            if (f.type.startsWith("struct:"))
            {
                check(value, f.type.slice(7), at);
            }
            else if (f.type.startsWith("array:"))
            {
                if (f.nonEmpty && value.length === 0)
                {
                    errors.push(`${at} is empty.`);
                }
                const picks = new Set([0, Math.floor(value.length / 2), value.length - 1]);
                for (const i of picks)
                {
                    if (i >= 0 && i < value.length)
                    {
                        check(value[i], f.type.slice(6), `${at}[${i}]`);
                    }
                }
            }
        }
    }

    if (!doc || typeof doc !== "object" || Array.isArray(doc))
    {
        return { errors: ["The file is not a JSON object."], warnings };
    }
    check(doc, "FPSReplayRecording", "recording");
    const version = num(field(field(doc, "Header"), "FormatVersion"), 0);
    if (version > schema.knownFormatVersion)
    {
        warnings.push(`Format version ${version} is newer than ${schema.knownFormatVersion}, the newest this viewer knows; fields it doesn't know are ignored.`);
    }
    return { errors, warnings };
}

/** True when a recording (by file name, header or index entry) is not the game. */
export function isSynthetic(schema, fileName, doc, entry)
{
    const prefix = schema.synthetic.filePrefix;
    const marker = schema.synthetic.buildMarker;
    const base = String(fileName || "").split(/[\\/]/).pop();
    if (base.startsWith(prefix))
    {
        return true;
    }
    const build = String(field(field(doc, "Header"), "GameBuildVersion") || "");
    if (build.toUpperCase().includes(marker))
    {
        return true;
    }
    return !!(entry && entry.synthetic);
}

/** The demo index as a list of { file, name, offenseCall, defenseCall, result, yards,
 *  durationSeconds, seed, offenseTeam, defenseTeam, synthetic }; missing values are null. */
export function parseIndex(doc, schema)
{
    const spec = schema.index;
    let list = Array.isArray(doc) ? doc : field(doc, spec.listKeys);
    if (!Array.isArray(list))
    {
        // Any one array of objects with a file in it.
        list = Object.values(doc || {}).find((v) => Array.isArray(v) && v.some((e) => field(e, spec.fields.file) !== undefined)) || [];
    }
    const out = [];
    for (const entry of list)
    {
        if (typeof entry === "string")
        {
            out.push({ file: entry, name: null, offenseCall: null, defenseCall: null, result: null, yards: null, durationSeconds: null, seed: null, offenseTeam: null, defenseTeam: null, synthetic: false });
            continue;
        }
        const row = {};
        for (const [key, aliases] of Object.entries(spec.fields))
        {
            let value = field(entry, aliases);
            if (value !== undefined && typeof value === "object")
            {
                // A call given as an object: its display name.
                value = field(value, "DisplayName", "Name", "PlayId") ?? null;
            }
            row[key] = value === undefined ? null : value;
        }
        if (!row.file)
        {
            continue;
        }
        row.yards = row.yards === null ? null : num(row.yards, null);
        row.durationSeconds = row.durationSeconds === null ? null : num(row.durationSeconds, null);
        row.synthetic = bool(row.synthetic);
        out.push(row);
    }
    return out;
}

function median(values)
{
    if (!values.length)
    {
        return 0;
    }
    const s = values.slice().sort((a, b) => a - b);
    const m = s.length >> 1;
    return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
}

/** The last frame index whose time is at or before t (0 when t is before the first). */
export function frameAtOrBefore(times, t)
{
    let lo = 0;
    let hi = times.length - 1;
    if (t <= times[0])
    {
        return 0;
    }
    if (t >= times[hi])
    {
        return hi;
    }
    while (lo < hi)
    {
        const mid = (lo + hi + 1) >> 1;
        if (times[mid] <= t)
        {
            lo = mid;
        }
        else
        {
            hi = mid - 1;
        }
    }
    return lo;
}

/** The two frames either side of t and how far between them, as the engine's own replay
 *  blends them (UPSReplaySubsystem::SampleClip): { i, j, a }. */
export function bracket(times, t)
{
    const i = frameAtOrBefore(times, t);
    const j = Math.min(i + 1, times.length - 1);
    const span = times[j] - times[i];
    const a = span > 1e-6 ? Math.min(1, Math.max(0, (t - times[i]) / span)) : 0;
    return { i, j, a };
}

const PAYLOAD_LABEL = {
    Snap: () => "Snap",
    Throw: (p, name) => `Throw${name(p, "PasserName", " by ")}${name(p, "TargetReceiverName", " to ")}`,
    Catch: (p, name) => (bool(field(p, "bIsInterception")) ? "Interception" : "Catch") + name(p, "ReceiverName", " by "),
    Tackle: (p, name) => (bool(field(p, "bIsSack")) ? "Sack" : "Tackle") + name(p, "TacklerName", " by "),
    Fumble: (p, name) => "Fumble" + name(p, "FumblerName", " by "),
    Score: (p) => String(field(p, "ScoreType") || "Score"),
    Damage: (p, name) => "Hit" + name(p, "TargetName", " on "),
    PassRushMove: (p) => `Rush move${field(p, "Move") ? ": " + field(p, "Move") : ""}${bool(field(p, "bWon")) ? " (won)" : ""}`,
    Kick: (p) => String(field(p, "KickType") || "Kick"),
    LooseBall: (p) => "Loose ball" + (field(p, "Kind") ? ": " + enumName(field(p, "Kind")) : ""),
    BallGrounded: () => "Ball on the ground",
    BoundaryCrossed: (p, name) => (bool(field(p, "bEndZone")) ? "Into the end zone" : "Out of bounds") + name(p, "CarrierName", ": "),
    PumpFake: () => "Pump fake",
    PlayResult: (p) =>
    {
        const result = String(field(p, "Result") || "Result");
        const yards = num(field(p, "YardsGained"), null);
        return yards === null ? result : `${result}, ${yards >= 0 ? "+" : ""}${yards} yd`;
    }
};

/** How an event shows on the timeline: 'key' moments get a labelled marker, 'minor' ones a tick,
 *  'hidden' ones only the list. */
const EVENT_KIND = {
    Snap: "key", Throw: "key", Catch: "key", Tackle: "key", Fumble: "key", Score: "key", PlayResult: "key",
    Kick: "key", LooseBall: "key", BoundaryCrossed: "key", BallGrounded: "minor", PumpFake: "minor",
    Damage: "minor", PassRushMove: "minor", PhaseChange: "hidden", GameState: "hidden", PlayCall: "hidden"
};

const LIVE_PHASES = new Set(["Snap", "PassRush", "BallCarrierMovement"]);

/**
 * The recording as the viewer plays it back. Throws an Error naming the problem when the
 * recording can't be played (see validateRecording).
 *
 * Returns { header, synthetic, playState, times, start, end, duration, sampleRateHz,
 * keyframeCount, players, pos, vel, acc, yaw, hasBall, present, ball, events, calls, result,
 * carrier, contacts, gait, warnings, eventClock }. Per-player arrays are indexed [player][frame*3+k]
 * (vectors) or [player][frame].
 */
export function buildReplay(doc, schema, options = {})
{
    const { errors, warnings } = validateRecording(doc, schema);
    if (errors.length)
    {
        throw new Error("This recording can't be played: " + errors.slice(0, 4).join(" "));
    }

    const headerDoc = field(doc, "Header");
    const header = {
        formatVersion: num(field(headerDoc, "FormatVersion"), 0),
        gameBuildVersion: String(field(headerDoc, "GameBuildVersion") ?? ""),
        recordedAtUtc: String(field(headerDoc, "RecordedAtUtc") ?? ""),
        randomSeed: num(field(headerDoc, "RandomSeed"), 0),
        fixedDeltaSeconds: num(field(headerDoc, "FixedDeltaSeconds"), 0)
    };
    const initial = field(doc, "InitialState") || {};
    const playStateDoc = field(initial, "PlayState");
    const playState = playStateDoc ? {
        down: num(field(playStateDoc, "Down"), 0),
        distance: num(field(playStateDoc, "Distance"), 0),
        yardLine: num(field(playStateDoc, "YardLine"), 0),
        quarter: num(field(playStateDoc, "Quarter"), 0)
    } : null;

    // Frames in time order (a keyframe can share its instant with a scheduled frame).
    const rawFrames = field(doc, "Frames").map((f, k) => ({ f, k, t: num(field(f, "Time"), NaN) })).filter((x) => Number.isFinite(x.t));
    rawFrames.sort((a, b) => a.t - b.t || a.k - b.k);
    const F = rawFrames.length;
    if (!F)
    {
        throw new Error("This recording can't be played: no frame has a time.");
    }
    const times = new Float64Array(F);
    rawFrames.forEach((x, i) => { times[i] = x.t; });

    // Who is on the field: every PlayerId in any frame, offense first, then by id.
    const info = new Map();
    const infoSpec = schema.playerInfo;
    function remember(source, side, idKeys)
    {
        const id = String(field(source, idKeys || "PlayerId") ?? "");
        if (!id || id === "None")
        {
            return;
        }
        const row = info.get(id) || { id };
        const put = (key, value) =>
        {
            if ((row[key] === undefined || row[key] === "" || row[key] === null) && value !== undefined && value !== null && value !== "" && typeof value !== "object")
            {
                row[key] = value;
            }
        };
        put("name", field(source, infoSpec.displayName));
        put("jersey", field(source, infoSpec.jerseyNumber));
        put("teamId", field(source, infoSpec.teamId));
        put("side", enumName(field(source, infoSpec.teamSide)) || side);
        put("role", enumName(field(source, infoSpec.role)));
        put("heightCm", num(field(source, infoSpec.heightCm), undefined));
        put("weightKg", num(field(source, infoSpec.weightKg), undefined));
        info.set(id, row);
    }
    // Participants (an additive extension) first: they say who each pawn is for this play.
    const listed = new Set(infoSpec.lists.map((k) => k.toLowerCase()));
    for (const key of listed)
    {
        const list = doc[key];
        if (Array.isArray(list))
        {
            list.forEach((p) => { if (p && typeof p === "object") { remember(p, undefined, infoSpec.id); } });
        }
    }
    const teamLists = new Set(schema.teamInfo.lists.map((k) => k.toLowerCase()));
    for (const container of [doc, initial])
    {
        for (const [key, value] of Object.entries(container))
        {
            if (key === "frames" || key === "events" || key === "offenseroster" || key === "defenseroster" || listed.has(key) || teamLists.has(key) || !Array.isArray(value))
            {
                continue;
            }
            value.forEach((p) => { if (p && typeof p === "object" && field(p, "PlayerId") !== undefined) { remember(p); } });
        }
    }
    (field(initial, "OffenseRoster") || []).forEach((p) => remember(p, "Offense"));
    (field(initial, "DefenseRoster") || []).forEach((p) => remember(p, "Defense"));
    const seenIds = [];
    const seen = new Set();
    for (const { f } of rawFrames)
    {
        for (const p of field(f, "Pawns"))
        {
            const id = String(field(p, "PlayerId") ?? "");
            if (id && !seen.has(id))
            {
                seen.add(id);
                seenIds.push(id);
            }
            remember(p);
        }
    }
    const sideRank = (id) => ((info.get(id) || {}).side === "Defense" ? 1 : 0);
    seenIds.sort((a, b) => sideRank(a) - sideRank(b) || (a < b ? -1 : a > b ? 1 : 0));
    const players = seenIds.map((id, index) =>
    {
        const row = info.get(id) || { id };
        const jersey = row.jersey === undefined ? null : String(row.jersey);
        return {
            index, id,
            side: row.side === "Defense" ? "Defense" : "Offense",
            role: row.role || "",
            name: row.name ? String(row.name) : id,
            jersey: jersey && jersey !== "0" && jersey !== "-1" ? jersey : null,
            teamId: row.teamId ? String(row.teamId) : null,
            heightCm: Number.isFinite(row.heightCm) && row.heightCm > 0 ? row.heightCm : null,
            weightKg: Number.isFinite(row.weightKg) && row.weightKg > 0 ? row.weightKg : null
        };
    });
    // Without a jersey a player is labelled by role and his index among his side's players in
    // that role (WR2), in PlayerId order.
    const roleCounts = new Map();
    for (const p of players)
    {
        const key = p.side + "|" + p.role;
        roleCounts.set(key, (roleCounts.get(key) || 0) + 1);
        p.roleIndex = roleCounts.get(key);
    }
    for (const p of players)
    {
        p.roleCount = roleCounts.get(p.side + "|" + p.role);
    }
    const slot = new Map(players.map((p) => [p.id, p.index]));
    const P = players.length;
    const teams = readTeams(doc, schema, players);

    const pos = players.map(() => new Float32Array(F * 3));
    const vel = players.map(() => new Float32Array(F * 3));
    const acc = players.map(() => new Float32Array(F * 3));
    const yaw = players.map(() => new Float32Array(F));
    const hasBall = players.map(() => new Uint8Array(F));
    const present = players.map(() => new Uint8Array(F));
    const ball = { pos: new Float32Array(F * 3), vel: new Float32Array(F * 3), sampled: new Uint8Array(F) };
    let keyframeCount = 0;
    const keyframes = [];

    rawFrames.forEach(({ f }, i) =>
    {
        if (bool(field(f, "bKeyframe")))
        {
            keyframeCount++;
            keyframes.push({ i, type: enumName(field(f, "KeyframeEventType")) });
        }
        const bl = vec(field(f, "BallLocation"));
        const sampled = field(f, "bBallSampled");
        if (bl && (sampled === undefined || bool(sampled)))
        {
            ball.sampled[i] = 1;
            ball.pos.set(bl, i * 3);
            ball.vel.set(vec(field(f, "BallVelocity")) || [0, 0, 0], i * 3);
        }
        for (const p of field(f, "Pawns"))
        {
            const s = slot.get(String(field(p, "PlayerId") ?? ""));
            const loc = vec(field(p, "Location"));
            if (s === undefined || !loc)
            {
                continue;
            }
            present[s][i] = 1;
            pos[s].set(loc, i * 3);
            vel[s].set(vec(field(p, "Velocity")) || [0, 0, 0], i * 3);
            acc[s].set(vec(field(p, "Acceleration")) || [0, 0, 0], i * 3);
            yaw[s][i] = num(field(p, "FacingYaw"), 0);
            hasBall[s][i] = bool(field(p, "bHasBall")) ? 1 : 0;
        }
    });

    // A pawn missing from some frames holds its nearest captured pose there.
    for (let s = 0; s < P; s++)
    {
        let last = -1;
        for (let i = 0; i < F; i++)
        {
            if (present[s][i])
            {
                if (last < 0)
                {
                    for (let k = 0; k < i; k++)
                    {
                        copyFrame(s, i, k);
                    }
                }
                last = i;
            }
            else if (last >= 0)
            {
                copyFrame(s, last, i);
            }
        }
    }
    function copyFrame(s, from, to)
    {
        pos[s].copyWithin(to * 3, from * 3, from * 3 + 3);
        vel[s].fill(0, to * 3, to * 3 + 3);
        acc[s].fill(0, to * 3, to * 3 + 3);
        yaw[s][to] = yaw[s][from];
        hasBall[s][to] = 0;
    }

    // Who has the ball, frame by frame (-1: nobody), and where it changes hands.
    const carrier = new Int16Array(F).fill(-1);
    for (let i = 0; i < F; i++)
    {
        for (let s = 0; s < P; s++)
        {
            if (present[s][i] && hasBall[s][i])
            {
                carrier[i] = s;
                break;
            }
        }
    }

    // Events, on the frames' clock.
    const rawEvents = (field(doc, "Events") || []).map((e, k) =>
    {
        const type = enumName(field(e, "EventType"));
        let payload = {};
        const text = field(e, "PayloadJson");
        if (typeof text === "string" && text.trim())
        {
            try
            {
                payload = parseJson(text);
            }
            catch (err)
            {
                payload = {};
            }
        }
        else if (text && typeof text === "object")
        {
            payload = text;
        }
        return { k, type, ts: num(field(e, "TimestampSeconds"), NaN), tick: num(field(e, "TickIndex"), -1), payload };
    });

    // The recorder stamps events with the world's clock; the sampler keeps its own. A keyframe
    // is taken at the instant of its event, so matching the n-th keyframe of a type to the n-th
    // event of that type measures any offset between the two clocks.
    const offsets = [];
    const byType = new Map();
    for (const kf of keyframes)
    {
        if (!byType.has(kf.type))
        {
            byType.set(kf.type, []);
        }
        byType.get(kf.type).push(kf.i);
    }
    const nth = new Map();
    for (const e of rawEvents)
    {
        const list = byType.get(e.type);
        if (!list || !Number.isFinite(e.ts))
        {
            continue;
        }
        const n = nth.get(e.type) || 0;
        nth.set(e.type, n + 1);
        if (n < list.length)
        {
            offsets.push(times[list[n]] - e.ts);
        }
    }
    const start = times[0];
    const end = times[F - 1];
    let shift = 0;
    let eventClock = "frames";
    if (offsets.length && Math.abs(median(offsets)) > 0.02)
    {
        shift = median(offsets);
        eventClock = "shifted by keyframes";
    }
    const events = [];
    for (const e of rawEvents)
    {
        let t = e.ts + shift;
        let clock = eventClock;
        if (!Number.isFinite(t) || t < start - 0.5 || t > end + 0.5)
        {
            if (e.tick >= 0 && e.tick < F)
            {
                t = times[e.tick];
                clock = "tick";
            }
            else
            {
                t = Math.min(end, Math.max(start, Number.isFinite(t) ? t : start));
                clock = "clamped";
            }
        }
        const label = labelFor(e.type, e.payload);
        let kind = EVENT_KIND[e.type] || "minor";
        if (e.type === "PhaseChange")
        {
            const to = enumName(field(e.payload, "NewPhase"));
            const from = enumName(field(e.payload, "OldPhase"));
            if (to === "Scoring" || (to === "PreSnap" && LIVE_PHASES.has(from)))
            {
                events.push({ t, type: "Whistle", source: e.type, label: "Whistle", kind: "key", payload: e.payload, clock });
                continue;
            }
        }
        events.push({ t, type: e.type, source: e.type, label, kind, payload: e.payload, clock });
    }

    // The ball changing hands, read off the frames' possession flags (no bus event says it).
    let previous = -1;
    for (let i = 0; i < F; i++)
    {
        const c = carrier[i];
        if (c >= 0 && c !== previous)
        {
            const from = previous >= 0 ? players[previous] : null;
            const caughtNearby = events.some((e) => (e.type === "Catch" || e.type === "Snap") && Math.abs(e.t - times[i]) < 0.12);
            if (from && !caughtNearby)
            {
                events.push({ t: times[i], type: "Possession", source: "frames", label: `Ball to ${players[c].name}`, kind: "key", payload: { from: from.id, to: players[c].id }, clock: "frames", derived: true });
            }
        }
        if (c >= 0)
        {
            previous = c;
        }
    }
    events.sort((a, b) => a.t - b.t);
    // One whistle per instant: a PhaseChange and a GameState can both announce it.
    for (let k = events.length - 1; k > 0; k--)
    {
        if (events[k].type === "Whistle" && events.slice(0, k).some((e) => e.type === "Whistle" && Math.abs(e.t - events[k].t) < 0.1))
        {
            events.splice(k, 1);
        }
    }

    // The two calls and the result, from the recording's own events.
    const calls = { offense: null, defense: null };
    for (const e of rawEvents)
    {
        if (e.type !== "PlayCall")
        {
            continue;
        }
        const call = {
            name: String(field(e.payload, "DisplayName") || field(e.payload, "PlayId") || ""),
            formation: String(field(e.payload, "Formation") || ""),
            category: String(field(e.payload, "PlayCategory") || ""),
            front: String(field(e.payload, "Front") || ""),
            coverage: String(field(e.payload, "CoverageShell") || "")
        };
        const offense = field(e.payload, "bOffense");
        if (offense === undefined || bool(offense))
        {
            calls.offense = call;
        }
        else
        {
            calls.defense = call;
        }
    }
    let result = null;
    const resultEvent = rawEvents.filter((e) => e.type === "PlayResult").pop();
    if (resultEvent)
    {
        const p = resultEvent.payload;
        result = {
            result: String(field(p, "Result") || ""),
            yards: num(field(p, "YardsGained"), null),
            pass: bool(field(p, "bPass")),
            complete: bool(field(p, "bComplete")),
            interception: bool(field(p, "bInterception")),
            sack: bool(field(p, "bSack")),
            firstDown: bool(field(p, "bFirstDown"))
        };
    }

    // Steps between scheduled frames, for the rate the recording was sampled at.
    const steps = [];
    for (let i = 1; i < F; i++)
    {
        const dt = times[i] - times[i - 1];
        if (dt > 1e-4)
        {
            steps.push(dt);
        }
    }
    const step = median(steps);

    const units = schema.units;
    const replay = {
        header, playState, times, start, end, duration: end - start,
        sampleRateHz: step > 0 ? 1 / step : 0, keyframeCount, players, teams, pos, vel, acc, yaw, hasBall, present, ball,
        events, calls, result, carrier, warnings, eventClock,
        synthetic: isSynthetic(schema, options.fileName, doc, options.entry)
    };
    replay.contacts = findContacts(replay, 2 * units.capsuleRadiusCm + 2);
    replay.gait = gaitDistance(replay);
    return replay;

    function labelFor(type, payload)
    {
        const make = PAYLOAD_LABEL[type];
        const name = (p, key, joiner) =>
        {
            const value = field(p, key);
            return value ? joiner + value : "";
        };
        return make ? make(payload, name) : type.replace(/([a-z])([A-Z])/g, "$1 $2");
    }
}

/**
 * The recording's own teams (the optional top-level Teams), each with the side it plays this
 * play: its own field, else the side most of its participants play. [] when there are none.
 */
export function readTeams(doc, schema, players)
{
    const spec = schema.teamInfo;
    const list = field(doc, spec.lists);
    if (!Array.isArray(list))
    {
        return [];
    }
    const out = [];
    for (const t of list)
    {
        if (!t || typeof t !== "object")
        {
            continue;
        }
        const teamId = field(t, spec.teamId);
        const name = field(t, spec.displayName);
        const abbreviation = field(t, spec.abbreviation);
        let side = enumName(field(t, spec.side));
        const offense = field(t, spec.offense);
        if (side !== "Offense" && side !== "Defense")
        {
            side = offense === undefined ? "" : bool(offense) ? "Offense" : "Defense";
        }
        if (!side)
        {
            const keys = [teamId, name, abbreviation].filter((v) => v !== undefined && v !== null).map((v) => String(v).toLowerCase());
            const votes = { Offense: 0, Defense: 0 };
            for (const p of players)
            {
                if (p.teamId && keys.includes(p.teamId.toLowerCase()))
                {
                    votes[p.side]++;
                }
            }
            side = votes.Offense > votes.Defense ? "Offense" : votes.Defense > votes.Offense ? "Defense" : "";
        }
        const colour = (v) => (typeof v === "string" && /^#?[0-9a-f]{6}$/i.test(v.trim()) ? (v.trim().startsWith("#") ? v.trim() : "#" + v.trim()) : null);
        out.push({
            teamId: teamId === undefined ? null : String(teamId),
            name: name === undefined ? null : String(name),
            abbreviation: abbreviation === undefined ? null : String(abbreviation),
            primaryColor: colour(field(t, spec.primaryColor)),
            secondaryColor: colour(field(t, spec.secondaryColor)),
            side
        });
    }
    return out;
}

/**
 * Opponents touching, frame by frame: pairs whose capsules (radius from the engine) overlap or
 * meet in the plane, read off the recorded positions. [frame] -> [[a, b, closingSpeedCmPerS]].
 * A measurement of where the players were, not a physics event the engine reported.
 */
export function findContacts(replay, reachCm)
{
    const { players, pos, vel, present, times } = replay;
    const F = times.length;
    const out = new Array(F);
    const reach2 = reachCm * reachCm;
    for (let i = 0; i < F; i++)
    {
        const pairs = [];
        for (let a = 0; a < players.length; a++)
        {
            if (!present[a][i] || players[a].side !== "Offense")
            {
                continue;
            }
            for (let b = 0; b < players.length; b++)
            {
                if (!present[b][i] || players[b].side !== "Defense")
                {
                    continue;
                }
                const dx = pos[b][i * 3] - pos[a][i * 3];
                const dy = pos[b][i * 3 + 1] - pos[a][i * 3 + 1];
                const d2 = dx * dx + dy * dy;
                if (d2 <= reach2)
                {
                    const d = Math.sqrt(d2) || 1;
                    const rvx = vel[a][i * 3] - vel[b][i * 3];
                    const rvy = vel[a][i * 3 + 1] - vel[b][i * 3 + 1];
                    pairs.push([a, b, (rvx * dx + rvy * dy) / d]);
                }
            }
        }
        out[i] = pairs;
    }
    return out;
}

/** Ground covered by each player up to each frame (cm), from the recorded positions: what a
 *  stride is laid against so feet don't slide. [player] -> Float32Array(frames). */
export function gaitDistance(replay)
{
    const { pos, times } = replay;
    return pos.map((p) =>
    {
        const d = new Float32Array(times.length);
        for (let i = 1; i < times.length; i++)
        {
            const dx = p[i * 3] - p[i * 3 - 3];
            const dy = p[i * 3 + 1] - p[i * 3 - 2];
            d[i] = d[i - 1] + Math.hypot(dx, dy);
        }
        return d;
    });
}

/** A short, plain summary of a replay, for the tests and the page's 'About' panel. */
export function summarize(replay)
{
    const counts = {};
    for (const e of replay.events)
    {
        counts[e.type] = (counts[e.type] || 0) + 1;
    }
    return {
        formatVersion: replay.header.formatVersion,
        frames: replay.times.length,
        players: replay.players.length,
        offense: replay.players.filter((p) => p.side === "Offense").length,
        defense: replay.players.filter((p) => p.side === "Defense").length,
        duration: Math.round(replay.duration * 1000) / 1000,
        sampleRateHz: Math.round(replay.sampleRateHz * 10) / 10,
        keyframes: replay.keyframeCount,
        ballFrames: replay.ball.sampled.reduce((a, b) => a + b, 0),
        events: counts,
        synthetic: replay.synthetic,
        eventClock: replay.eventClock
    };
}
