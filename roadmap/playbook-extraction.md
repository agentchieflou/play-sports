# Track O — Playbook Extraction (Epics 132–134)

A **one-time, polite, resumable** extraction of a real play corpus from
`https://www.madden-school.com/playbooks/` into team-agnostic canonical play data — feeding
Core 16's play schema and Epic 121's concept grammar. This is not a live scraper: it runs in
small rate-limited batches across sessions until the catalog is covered, then stops forever.
Output carries **no team assignment** by design. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** The compliance gate (Epic 132) can stop this track —
no fetch code may be written before its go/no-go merges. Python tooling lives under
`tools/`, following the repo's existing script conventions (`lint_conventions.py`,
`validate_data.py`); tests run against committed fixture pages and never hit the network,
so CI stays offline-safe. Core 16's play-definition schema does not exist yet: Epic 132
drafts a provisional schema, and whichever of (Epic 132, Core 16 story 1) lands first is
authoritative — the other conforms. Do not hard-block extraction behind Phase 2.

### Epic 132: Compliance Gate & Extraction Design

**Size/Mode:** S / code
**Goal:** A written go/no-go on scraping the source plus the full extraction design — no fetch code before this lands.
**Depends on:** —

- [ ] Compliance review: fetch and archive `robots.txt` + terms of use of madden-school.com; written go/no-go finding with the constraints to honor (crawl-delay, disallowed paths) in `tools/playbook_scraper/COMPLIANCE.md` — on a no-go, Epics 133/134 re-target an allowed source or manual authoring and this track's scope note is updated
- [ ] Site recon: URL structure map (playbook index → team playbook → formation → play pages) and extractable fields, derived from a handful of manually fetched sample pages committed as test fixtures
- [ ] Extraction design doc: rate limit (≥5–10 s/request, single-threaded, identifying User-Agent), raw-HTML cache layout, checkpoint/resume state format, batch-size policy
- [ ] Provisional play schema draft `Data/playbooks/SCHEMA.md` (formation, play name, concept family, personnel, per-position assignments; **no team field**) — co-designed with Core 16's play-definition story: whichever lands first is authoritative

### Epic 133: Rate-Limited Resumable Scraper

**Size/Mode:** M / code
**Goal:** A polite, checkpointed crawler that can be run in small batches over days without ever re-fetching a page.
**Depends on:** 132

- [ ] `tools/playbook_scraper/` package: fetcher with configurable delay (default 8 s, floor 5 s), exponential backoff on 4xx/5xx, robots.txt re-check at startup, identifying User-Agent; raw HTML cached to a gitignored local dir keyed by URL hash — cache hits never refetch
- [ ] Checkpoint state (`state.json`): URL frontier, completed set, error list, batch counters; `--max-pages N` per invocation; safe to interrupt and resume across sessions
- [ ] Page parsers (index/team/formation/play) → raw play records (name, formation, concept, description, diagram URL, source URL) as JSONL
- [ ] CLI: `python -m tools.playbook_scraper crawl|status|parse` with progress reporting
- [ ] Unit tests over the committed fixture pages — zero network access in tests or CI; first live run = compliance go/no-go re-check, then one ~20–30 page batch to validate parsers and checkpointing

### Epic 134: Normalization, Dedup & Validation

**Size/Mode:** M / code
**Goal:** Raw scrape records become one validated, deduplicated, team-agnostic play dataset under `Data/`.
**Depends on:** 133

- [ ] Normalizer: raw JSONL → `Data/playbooks/extracted_plays.json` per the 132 schema; source-team names dropped, formation/concept retained
- [ ] Dedup/canonicalization: the same play appearing across many team playbooks collapses to one canonical entry with an occurrence count and name variants
- [ ] Extend `tools/validate_data.py` with the playbooks contract (required fields, formation enum, assignment-vocabulary integrity) — CI-gated exactly like the players contract
- [ ] Statistical sanity report (`Data/playbooks/REPORT.md`): counts by formation/concept, duplicate rate, coverage vs the site catalog
- [ ] Cross-link: note added to Track L that Epic 121's concept grammar seeds from this corpus
