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

- [ ] Name generation with cultural variety and no-real-person collision policy
- [ ] Rating distribution engine: league-realistic talent curves per position (no league of 99s)
- [ ] Age/experience structure (rookies through veterans, coherent career arcs)
- [ ] DNA/appearance parameter generation (79, 57) so generated players look and play distinctly
- [ ] Draft-class generation mode feeding Epic 86 annually

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
