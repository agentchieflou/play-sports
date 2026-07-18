---
name: data-content-author
description: |
  Use when authoring or editing game content JSON in Data/ (players, rosters, and later teams,
  playbooks, league config). Carries the complete data contract so no C++ files need to be
  opened — the cheapest agent archetype; suitable for small/free-tier models.
---

# Data / Content Author

You work **only in `Data/`**. Do not open anything under `Source/` — this skill contains the
entire contract you need.

## Player data contract (`PSDataIngestion` ↔ `FPlayerAttributes`)

Top-level shape: a JSON object with a `Players` array. Each element maps 1:1 onto
`FPlayerAttributes` — field names must match **exactly** (they are matched by reflection):

| Field | Type | Notes |
|---|---|---|
| `PlayerId` | string | Unique row key, e.g. `"QB_001"`. Falls back to `DisplayName` if absent — always provide it. |
| `DisplayName` | string | Human-readable name. |
| `Role` | string | Exactly one of: `Quarterback`, `RunningBack`, `WideReceiver`, `TightEnd`, `OffensiveLineman`, `DefensiveLineman`, `Linebacker`, `DefensiveBack`. |
| `WeightKg` | number | Kilograms. |
| `HeightCm` | number | Centimeters. |
| `Speed`, `Agility`, `Strength`, `Acceleration`, `Awareness` | number | Ratings, 0–100 scale by convention. |

Reference example: `Data/sample_players.json` (2 rows). A full offense/defense set is 22 players
covering all 8 roles (see ROADMAP Epic 1, story 1).

## Rules

- Keep ratings plausible for the role (a lineman's `Speed` is not 95; a QB's `Awareness` is
  their headline stat).
- Validate the JSON parses before finishing (`python -m json.tool <file>`).
- If the story adds a *new* content type (teams, playbooks), the schema comes from the Epic's
  story text — in `ROADMAP.md` (Epics 1–25) or its `roadmap/` track file (26–125); read only
  that Epic's section.
- Tick the story's checkbox in `ROADMAP.md` in the same change.
