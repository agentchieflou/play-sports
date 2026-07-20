# Track O — Playbook Database Extraction (Epics 132–134)

One-time, polite, resumable extraction of playbook formations and plays from public madden-school.com playbook resources. Feeds play data schema contracts and seeds the procedural playbook generation algorithms (Track L). Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** Plays are team-agnostic by design. We only care about extracting the geometric and conceptual play designs (routes, concepts, formations) to build a library. The scraper must run slowly (5s-10s delay) with persistent cache/checkpointing to prevent network overload. CI tests must be offline (fixtures only).

### Epic 132: Compliance Gate & Extraction Design

**Size/Mode:** S / code
**Goal:** robots.txt compliance is verified and documented, and extraction architecture is finalized with fixtures.
**Depends on:** —

- [ ] Write `tools/playbook_scraper/COMPLIANCE.md` auditing site policies (robots.txt, terms of service) to verify crawler permissions. If blocked, document alternate targets or manual data collection plans
- [ ] Save a small set of manually fetched HTML pages from the target site into `tools/playbook_scraper/tests/fixtures/` to serve as offline test sources
- [ ] Draft play data target schema in `Data/playbooks/SCHEMA.md` specifying output fields (PlaybookName, FormationName, PlayName, ConceptFamily, RouteTree, PassProtection/RunBlockingScheme)
- [ ] Design extraction state checkpoint format (`state.json`) and Raw HTML caching strategy (files cached locally, never refetched on duplicate runs)

### Epic 133: Rate-Limited Resumable Scraper

**Size/Mode:** M / code
**Goal:** A command-line crawler downloads playbook pages under polite rate limits and updates checkpoint state.
**Depends on:** 132

- [ ] Implement `tools/playbook_scraper/crawler.py` managing rate limits (default 8s delay, minimum 5s, backoff on errors, custom User-Agent)
- [ ] Implement raw HTML caching (checking local gitignored cache before fetching, saving responses to disk)
- [ ] Implement `state.json` tracker (frontier URLs, completed, errors) allowing safe resume after interruption
- [ ] Build parsing logic converting raw playbook HTML into a unified JSONLines format
- [ ] Expose crawler controls via CLI: `python -m tools.playbook_scraper crawl|status|parse [--max-pages N]`
- [ ] Offline unit tests checking parser extraction accuracy against the HTML fixtures from Epic 132
- [ ] **First execution execution:** Compliance check runs. If green, crawler executes one validation batch of 20-30 pages with 8s delays to confirm parsers work

### Epic 134: Normalization, Dedup & Validation

**Size/Mode:** M / code
**Goal:** Scraped plays are normalized, deduplicated, and validated against the playbook schema.
**Depends on:** 133

- [ ] Write normalizer script converting raw JSONLines records into `Data/playbooks/extracted_plays.json`
- [ ] Implement play deduplication (collapsing identical play concept trees, counting occurrences, identifying common names)
- [ ] Extend `tools/validate_data.py` to validate `Data/playbooks/extracted_plays.json` against the playbook schemas, checking for structural integrity
- [ ] Write a script compiling validation and summary statistics in `Data/playbooks/REPORT.md` (plays count, formation counts, conceptual breakdown)
