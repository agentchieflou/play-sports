// check_loader.mjs - runs the page's own loader (loader.js) on recordings, for the Python test.
//   node tools/replay_viewer/check_loader.mjs <recording.json> [...]
// Prints one JSON object: file -> the loader's summary, or { error } when it can't play the file.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import * as L from "./loader.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const schema = JSON.parse(readFileSync(path.join(here, "replay_schema.json"), "utf8"));
const out = {};
for (const file of process.argv.slice(2))
{
    try
    {
        const replay = L.buildReplay(L.parseJson(readFileSync(file, "utf8")), schema, { fileName: path.basename(file) });
        out[file] = {
            ...L.summarize(replay),
            start: replay.start,
            players: replay.players.map((p) => ({ id: p.id, side: p.side, role: p.role, name: p.name, jersey: p.jersey, teamId: p.teamId, roleIndex: p.roleIndex, roleCount: p.roleCount })),
            teams: replay.teams,
            timeline: replay.events.map((e) => ({ t: Math.round((e.t - replay.start) * 1000) / 1000, type: e.type, label: e.label, kind: e.kind, clock: e.clock })),
            carriers: Array.from(replay.carrier),
            contactFrames: replay.contacts.filter((c) => c.length > 0).length,
            calls: replay.calls,
            result: replay.result,
            warnings: replay.warnings
        };
    }
    catch (err)
    {
        out[file] = { error: err.message };
    }
}
process.stdout.write(JSON.stringify(out));
