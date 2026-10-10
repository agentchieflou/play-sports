# Playbook extraction: compliance gate (Epic 132)

**Status: NOT CLEARED. No fetch code may be written (Epics 133 and 134 stay blocked) until a
person makes the decision in "Decision needed" below.**

| | |
| --- | --- |
| Source under review | `https://www.madden-school.com/playbooks/` |
| Checked | 2026-10-10, from the project's agent environment (cloud container behind an egress proxy) |
| Client | `curl` with the identifying User-Agent `play-sports-compliance-check/0.1 (+https://github.com/agentchieflou/play-sports)`, plus the agent's own web-fetch tool |

## What the check found

| Request | Result |
| --- | --- |
| `GET https://www.madden-school.com/robots.txt` | **HTTP 403**, `server: cloudflare`, 103-byte "403 Forbidden" page |
| `GET https://madden-school.com/robots.txt` | **HTTP 403**, same Cloudflare page |
| `GET https://www.madden-school.com/` | **HTTP 403**, same Cloudflare page |
| The agent's web-fetch tool, `robots.txt` | HTTP 403 |
| Internet Archive (`archive.org/wayback/available`, the CDX API, `web.archive.org/web/...`) for archived copies of `robots.txt` and the terms | 429 Too Many Requests, then connection resets, then refused by the fetch tool. Nothing came back. |

The 403s come from the site's own Cloudflare edge (the CONNECT through the egress proxy
succeeded, and the response carries Cloudflare headers and a `cf-ray` id). They are not an
egress-policy block on our side. The edge refused this environment's identified, automated
requests before serving any page, `robots.txt` included. A person's browser on a home
connection may well be served, but that tells us nothing about permission to extract.

**Nothing was archived.** Neither `robots.txt` nor the terms of use could be retrieved. No page
content from the site was fetched, stored or committed.

## Finding

1. **The site refused our automated access.** An edge that answers `robots.txt` itself with
   403 is an access control, not a crawl-rate hint. The only ways past it are a browser-like
   User-Agent, a challenge solver, a headless browser or rotating addresses. Each one is
   circumvention, and this project does not do it.
2. **Permission can't be established.** The terms of use couldn't be read. With no terms and
   no `robots.txt`, there is nothing that allows extraction, so the default is no.
3. **Content rights are an open question in any case.** The site catalogues the playbooks of
   EA SPORTS Madden NFL: formation and play names, diagrams and descriptions. That
   compilation and the diagrams belong to EA and to the site, not to this project. Generic
   football vocabulary (Slant-Flat, Four Verticals, Cover 2, 11 personnel) is common usage. A
   play corpus taken from this source would not be. Whether any of it may become this game's
   play data is a licensing and legal question for a person. It is not for an agent to settle.

**Outcome:** no go-ahead for automated extraction from this source. The track stays gated on
the decision below.

## Decision needed (a person: the repo owner)

Choose one:

- **A. Re-target (no outside permission needed).** Author the corpus by hand from general
  football knowledge into the shipped schema (`Data/playbooks/SCHEMA.md`). Epic 133's crawler
  becomes unnecessary or points at a source whose licence allows reuse. Epic 134's
  normalization and validation still apply. The design in `DESIGN.md` is written so either
  path can use it.
- **B. Seek permission.** Get written permission from madden-school.com (and clarity on the EA
  material it depicts) to extract and reuse. Then re-run this check from an environment the
  site serves. Archive `robots.txt` and the terms under `tools/playbook_scraper/compliance/`
  with retrieval dates and SHA-256 hashes, and record the permission here before Epic 133
  starts.

Until A or B is recorded in this file, Epics 133 and 134 must not fetch from the source.

## Constraints that bind any future run (if B ever clears it)

- Honour `robots.txt` exactly: Disallow rules and Crawl-delay. Re-read it at the start of every
  run, and stop if it changed in a way that disallows the run.
- The terms of use win over everything below. If they forbid automated access or reuse, there
  is no run.
- Use one identifying User-Agent with a contact URL. Never imitate a browser. Never solve
  challenges or run a headless browser. Never use proxies or rotate addresses.
- **Stop on 401, 403 or a challenge page.** Treat it as access withdrawn. Never retry around
  it.
- Rate and batching follow `DESIGN.md`: at least 5 s between requests (default 8 s, or the
  site's Crawl-delay if larger), single-threaded, small batches over days.
- Output carries no team assignment, and every record keeps its source URL and retrieval date.
