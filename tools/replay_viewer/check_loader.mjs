// check_loader.mjs - runs the page's own loader (loader.js) on recordings, for the Python test.
//   node tools/replay_viewer/check_loader.mjs <recording.json> [...]
//   node tools/replay_viewer/check_loader.mjs --index <index.json>
// Prints one JSON object: file -> the loader's summary (or the index as the page reads it), or
// { error } when it can't read the file.

import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";
import * as L from "./loader.js";

const here = path.dirname(fileURLToPath(import.meta.url));
const schema = JSON.parse(readFileSync(path.join(here, "replay_schema.json"), "utf8"));
const out = {};
const args = process.argv.slice(2);
if (args[0] === "--index")
{
    for (const file of args.slice(1))
    {
        out[file] = L.readIndex(L.parseJson(readFileSync(file, "utf8")), schema);
    }
    process.stdout.write(JSON.stringify(out));
    process.exit(0);
}
for (const file of args)
{
    try
    {
        const replay = L.buildReplay(L.parseJson(readFileSync(file, "utf8")), schema, { fileName: path.basename(file) });
        out[file] = {
            ...L.summarize(replay),
            start: replay.start,
            standZ: replay.standZ,
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
