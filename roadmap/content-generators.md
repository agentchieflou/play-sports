# Track L — Procedural Content Generators (Epics 121–125)

The fictional league needs content at scale — rosters, playbooks, identities, venues — far
beyond hand-authoring. Pure code + data, highly parallelizable, and the natural home for
free-tier model delegation (cheap models generating content validated by contracts).
Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** `tools/validate_data.py` exists and CI-gates all
`Data/` JSON — Epic 125 extends it (players contract is done; teams/playbooks/league configs
are the open half) rather than starting fresh. Generators route through `UPSDataIngestion`
(AGENTS.md rule 4 — the review caught an ad-hoc second JSON parser; don't add a third).

### Epic 121: Procedural Playbook Generator

**Size/Mode:** M / code
**Goal:** Coherent, scheme-flavored playbooks generated from concept grammars — hundreds of plays without hand-authoring each.
**Depends on:** Core 16, 89

- [ ] Concept grammar: route-combination rules per formation (flood, mesh, dagger families)
- [ ] Scheme flavoring: generator parameters per coaching identity (89) — air-raid vs. ground-and-pound output
- [ ] Validity guarantees: every generated play passes Epic 35's art/AI consistency validation
- [ ] Defensive call-sheet generation (fronts × coverages × pressures with scheme weighting)

### Epic 122: Roster & League Generator

**Size/Mode:** L / code
**Goal:** Full fictional leagues — 32 teams × 53 players with plausible names, ratings, ages, DNA, and league-wide talent distribution.
**Depends on:** Core 19, 79

- [x] Name generation with cultural variety and no-real-person collision policy *(as built: `UPSLeagueGenerator` (C++, deterministic from a seed, so the game can make a league at runtime for a new franchise) draws a first and last name from one of 12 weighted cultures in `Data/league_generator.json`, drawing again when the name is already in the league or matches the `NameBlocklist` of real people in full or initial form, ignoring case and punctuation. `validate_data.py` holds every roster to the same blocklist. Nothing is scraped: the pools are hand-written and the blocklist is a hand-kept list)*
- [x] Rating distribution engine: league-realistic talent curves per position (no league of 99s) *(as built: each role's `RoleProfiles` curve per attribute at his prime, a talent shared across his ratings plus his team's and a veteran's experience, each role ranked best first so the default depth chart starts the best. The automation test holds every role's league mean under 90 with a spread of 2 or more, and CI runs `content.py check --strict` on a generated league)*
- [x] Age/experience structure (rookies through veterans, coherent career arcs) *(as built: `FPlayerAttributes::Age` (0 = unknown, so existing rosters keep working; the contract manager uses it through `UPSContractManager::GetPlayerAge`, falling back to `DefaultPlayerAge`). Years in the league are drawn from each role's attrition, so rookies are the biggest group and veterans thin out. Ratings are walked along the progression curve, now data in `Data/player_progression.json`: a young player is below his prime by what his offseasons will add, a veteran has lost what they took. Experience is the years since his entry age; it is not stored)*
- [ ] DNA/appearance parameter generation (79, 57) so generated players look and play distinctly *(DNA half built: every generated player gets Epic 79's DNA through `PSPlayerDNA::GenerateProfile`, the same rule as `tools/player_dna.py`, centered on the generated league, and his body (`WeightKg`, `HeightCm`) from his role's curves. Appearance parameters wait for Epic 57/143's look record (`FPSLookRow`), which doesn't exist yet; this story is ticked when the generator fills it)*
- [x] Draft-class generation mode feeding Epic 86 annually *(as built: `UPSLeagueGenerator::GenerateDraftClass(Seed, DraftYear, NumTeams, LeaguePlayers)`: `NumTeams` x `DraftClass.ProspectsPerTeam` rookies at entry ages, roles in a roster's shares, names new to the league, DNA, PlayerIds `DC<Year>_<NNN>`. A class depends only on the seed and year. Epic 86's draft consumes it)*

### Epic 123: Team Identity Generator

**Size/Mode:** M / mixed
**Goal:** Fictional teams with distinct, professional-feeling identities — names, palettes, logos, uniforms, lore.
**Depends on:** 37, 56

- [ ] Name/city generator with collision and trademark-adjacent avoidance rules
- [ ] Palette generation with contrast/colorblind constraints (feeds 37's theming)
- [ ] Logo construction system (parameterized marks; editor pass for polish)
- [ ] Uniform set derivation (56's schema populated from identity)
- [ ] Rivalry/history seed data for Track G narratives

### Epic 124: Venue Content Generator

**Size/Mode:** S / mixed
**Goal:** Stadium variety generated from the Epic 52 kit — each fictional team gets a home that fits its identity.
**Depends on:** 52, 123

- [ ] Venue parameter generation (capacity, bowl style, surface, roof) weighted by team identity/market
- [ ] Kit-assembly automation via Autonomix batch jobs (118) — generated params → editor-buildable venue
- [ ] Branding application: 53's end-zone/midfield pipeline fed from team identity

### Epic 125: Content Validation & Import CLI

**Size/Mode:** S / code
**Goal:** One command validates and imports everything Track L produces — the quality gate between generators and the game.
**Depends on:** Core 21, 113

- [x] Unified CLI: validate/import all content types (players, teams, playbooks, venues) with actionable errors *(as built: `python tools/content.py validate|report|import`. Validate is `validate_data.py` plus the new `tools/content_contracts.py`, which holds the teams, league config, playbook, route library and rating-range contracts. Import runs `PSContentReimportCommandlet`, now following league → teams → rosters. Venues have no content type until Epic 124; their contract goes in `content_contracts.py`)*
- [x] Cross-content referential integrity (every roster's team exists, every play's routes resolve) *(as built: in `content_contracts.check_references`, which checks the league's teams file and playoff size, each team's roster, rosters with no team, PlayerIds unique across the league, and plays' routes. The commandlet re-checks the last three on what it loaded)*
- [x] Statistical sanity reports on generated content (rating distributions, name duplication) *(as built: `content.py report [--json] [--strict]` covers per-role rating distributions with inflated and flat warnings, duplicate names, roles missing from a team, team overall outliers, body plausibility and playbook category gaps)*
- [x] CI integration (112): generated-content PRs are auto-validated *(as built: CI's "Validate data contracts" step now includes every content contract and reference, and a "Content report" step prints the report. The automation test `PlaySports.Content.ImportShippedContent` imports all of `Data/` through the game's loaders on every build)*
