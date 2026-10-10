# Extracted playbook schema (provisional, Epic 132)

Core 16's play-definition schema landed first (`FPSPlayDefinition` / `FPSRoute` in
`Source/PlaySports/Public/PSPlaybookData.h`, sample data in `Data/sample_playbook.json`), so it
is authoritative. Extracted or hand-authored plays (Track O, Epics 133 and 134) use that shape.
They add only an `Extraction` block that the engine's loader ignores, so any file here loads
through `UPSPlaybookIngestion::LoadPlaysFromJson` unchanged.

## File

One JSON object per file, `Data/playbooks/<formation-or-family>.json`:

```json
{
  "Plays": [
    {
      "PlayId": "Gun_Trips_SlantFlat",
      "DisplayName": "Slant-Flat",
      "Formation": "Trips Right",
      "bIsOffensivePlay": true,
      "PlayCategory": "ShortPass",
      "Front": "",
      "CoverageShell": "",
      "Assignments": [
        { "Role": "WideReceiver", "Kind": "Route", "RouteId": "Slant" },
        { "Role": "RunningBack", "Kind": "Route", "RouteId": "Flat" },
        { "Role": "OffensiveLineman", "Kind": "PassBlock" }
      ],
      "Extraction": {
        "ConceptFamily": "Slant-Flat",
        "Personnel": "P11",
        "SourceUrl": "",
        "RetrievedAt": "",
        "Licence": "authored for play-sports",
        "Notes": ""
      }
    }
  ]
}
```

## Fields

Engine fields, exactly as in `FPSPlayDefinition` (names are case-sensitive, as everywhere in
`Data/`):

| Field | Rule |
| --- | --- |
| `PlayId` | Unique across all playbook files. `<Formation>_<Concept>` with underscores. |
| `DisplayName` | What the play-call screen shows. |
| `Formation` | The formation the play lines up in. Must be listed by a personnel package for its side in `Data/personnel_packages.json` (Epic 19.5), otherwise the side's default package comes on. |
| `bIsOffensivePlay` | `true` for offense, `false` for defense. |
| `PlayCategory` | Offense: `Run`, `ShortPass`, `DeepPass`, `PlayAction`, `Screen`. Defense: `Base`, `Blitz`, `Prevent` (the coaching AI's weights, Epic 18). |
| `Front`, `CoverageShell` | Defense only, e.g. `4-3` / `Cover2`. Empty on offense. |
| `Deception` | Optional, offense only (`FPSDeceptionDef`, Epic 72): `Type` (`None`, `PlayAction`, `RPO`, `ZoneRead`, `TripleOption`), `PlaySide` (`1` right, `-1` left), `PassRole` (the RPO's pass option, on a route), `PitchRole` (the triple option's pitch man). Play-action goes on a pass play; the run options on a `Run` play with a `RunningBack` on a `Route`. See `Data/README.md`. |
| `Assignments[]` | Per role: `Role` (an `EPlayerRole`) and `Kind` (an `EPSAssignmentKind`: `Route`, `PassBlock`, `RunBlock`, `ManCoverage`, `ZoneCoverage`, `PassRush`, `RunFit`, `Blitz`). Also `RouteId`, which must exist in `Data/sample_routes.json` (or a route file loaded beside it), and optionally `ZoneOffset` / `FormationOffset` (cm). Slots of one role apply in order; extra players of the role repeat its last slot. A route may carry a `ReadOrder` (1 for the primary read, then 2, 3, ...; Epic 27's route art colors by it): only on a `Route` with a `RouteId`, and a play's ranks run 1, 2, 3, ... with no gap or repeat. Any assignment may carry an `Art` block, the play art's annotations (Epic 35): `Color` (`#RRGGBB`), `bEmphasis`, `BadgeLetter` (one or two capitals or digits). |

Extraction block. The loader ignores it; validation and provenance use it:

| Field | Rule |
| --- | --- |
| `ConceptFamily` | The concept, independent of formation (`Slant-Flat`, `Four Verticals`, `Inside Zone`, `Cover 2`), for deduplication and Epic 121's concept grammar. |
| `Personnel` | The `PackageId` the formation implies (`P11`, `Nickel`, ...). It must agree with `Data/personnel_packages.json`. |
| `SourceUrl`, `RetrievedAt` | Where and when the play was read (UTC ISO 8601). Empty for hand-authored plays. |
| `Licence` | The terms the play is used under. A record whose source isn't cleared in `tools/playbook_scraper/COMPLIANCE.md` doesn't ship. |
| `Notes` | Free text for reviewers. |

## Forbidden

- **No team field of any kind** (`Team`, `TeamId`, `TeamName`, a team-named file or `PlayId`).
  Plays are team-agnostic by design. Team playbooks, if ever wanted, are lists of `PlayId`s
  kept elsewhere.
- No copied diagrams or prose from a source that isn't cleared.

## Validation (Epic 134)

`tools/validate_data.py` will check `Data/playbooks/*.json` against these rules once the first
file lands: unique `PlayId`s, known enum values, `RouteId`s that resolve, a formation with a
personnel package, no team field, and a non-empty `Licence`.
