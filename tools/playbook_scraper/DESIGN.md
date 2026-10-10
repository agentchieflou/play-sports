# Playbook extraction: design (Epic 132)

How Epics 133 and 134 extract a play corpus, *if* `COMPLIANCE.md` clears a source. Nothing here
authorizes fetching. The design doesn't depend on any one site's markup, so the same pipeline
serves a cleared source or hand-authored input (`COMPLIANCE.md`, option A).

## Pipeline

```
compliance check ─▶ fetch (cache) ─▶ parse ─▶ raw records (JSONL) ─▶ normalize/validate ─▶ Data/playbooks/*.json
   (start of run)     Epic 133         133            133                  Epic 134                SCHEMA.md
```

Each stage reads only the previous stage's files, so a stage can be rerun without refetching.

## Politeness and rate limit

| Rule | Value |
| --- | --- |
| Concurrency | One request at a time, one process. |
| Delay between requests | Default 8 s, floor 5 s (`--delay` can't go lower), or the site's `Crawl-delay` when larger. |
| User-Agent | `play-sports-playbook-extractor/<version> (+https://github.com/agentchieflou/play-sports)`. Never a browser string. |
| robots.txt | Fetched and parsed (Python `urllib.robotparser`) at the start of every run. Its SHA-256 is stored in the state file; a change stops the run for a person to re-check. Every URL is checked against it before fetching. |
| 429 / 503 | Honour `Retry-After`, otherwise back off exponentially (30 s, 60 s, 120 s, ... capped at 30 min). After 5 attempts the URL goes to the error list. |
| Other 5xx, timeouts | Same backoff, max 3 attempts. |
| 401 / 403 / challenge page | **Stop the run.** Record it, and never retry around it (`COMPLIANCE.md`). |
| 404 / 410 | Record it once and never retry. |
| Redirects | Followed only within the cleared host, and each target is checked against robots.txt. |

## Raw-HTML cache

- Location: `tools/playbook_scraper/.cache/`, gitignored and never committed.
- Key: `sha256(normalized URL)`. Normalizing lowercases the scheme and host, drops the fragment
  and default port, and sorts the query.
- Layout: `.cache/<key[0:2]>/<key>.html` holds the body exactly as received, and
  `.cache/<key[0:2]>/<key>.json` holds `{url, final_url, fetched_at (UTC ISO 8601), status,
  content_type, etag, last_modified, sha256}`.
- **A cache hit never refetches.** Only an explicit `--revalidate URL` sends a conditional
  request (`If-None-Match` / `If-Modified-Since`), and it is still rate-limited.
- Parsers read only from the cache. Tests read only from committed fixtures.

## Checkpoint and resume: `state.json`

```json
{
  "schema_version": 1,
  "source": "https://host/path/",
  "robots_sha256": "…",
  "runs": [{"run_id": "2026-10-11T09:00:00Z", "pages_fetched": 25, "stopped_because": "max_pages"}],
  "frontier": [{"url": "…", "kind": "index|team|formation|play", "depth": 2, "discovered_from": "…"}],
  "completed": {"<normalized url>": {"cache_key": "…", "kind": "formation", "fetched_at": "…"}},
  "errors": {"<normalized url>": {"status": 503, "attempts": 2, "last_error": "…", "next_retry_at": "…"}},
  "totals": {"pages_fetched": 125, "plays_parsed": 410}
}
```

- Location: `tools/playbook_scraper/.state/state.json`, gitignored.
- Crash-safe writes: write `state.json.tmp`, `fsync` it, then `os.replace`. The state is saved
  after every page, so killing the process loses at most the page in flight.
- Resume: load the state, re-check robots.txt, then take the frontier in order. Completed URLs
  and URLs in the cache are skipped without a request.
- The frontier is breadth-first by `kind` (index, then teams, then formations, then plays), so
  a partial run still yields whole formations.

## Batch-size policy

- `--max-pages N` per invocation: default 25, hard cap 100. The run ends cleanly at N.
- A daily ceiling of 200 fetched pages, counted from `runs`. A run that would cross it stops
  first.
- **The first live batch is a person's call:** about 20 to 30 pages. Then a person reviews
  the parsed records against the pages before any further batch. Re-run the compliance check
  just before it.
- A run never starts without a clean `COMPLIANCE.md` status and an unchanged `robots_sha256`.

## CLI (Epic 133)

`python -m tools.playbook_scraper crawl [--max-pages N] [--delay S]`, `status`, `parse`,
`normalize`. `status` prints the totals, the frontier size, the errors and the last run's stop
reason. Every command works offline except `crawl`.

## Records

- **Raw** (`.state/raw_plays.jsonl`, one JSON object per line): `{source_url, retrieved_at,
  formation, play_name, concept_text, description, diagram_url, parser_version}`. This is
  site-shaped and kept only locally.
- **Normalized** (Epic 134): `Data/playbooks/*.json` per `Data/playbooks/SCHEMA.md`. These are
  the shipped `FPSPlayDefinition` shape plus provenance, deduplicated by (formation, concept
  family, assignments), and carry **no team field**.

## Tests

- Unit tests run over committed fixture pages and never touch the network. CI stays
  offline-safe, and a test that opens a socket fails.
- Fixtures are committed only if the source's terms allow it. Otherwise they are synthetic
  pages written for this project that mirror the structure, so parsers can be tested without
  copying anyone's content.
