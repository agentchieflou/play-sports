---
id: data-authoring-story
archetype: data-content-author
allowed_paths:
  - Data/**
  - ROADMAP.md
  - roadmap/**
requires_tests: false
---

# Benchmark: Data Authoring Story

A representative content story: author or extend a `Data/` JSON payload against a documented
contract (e.g. "add a second 22-player roster with plausible role-appropriate ratings").

## Rubric

- `tools/validate_data.py` clean (contract exactness is the whole job) and CI green.
- Diff scope: `Data/` + roadmap tick only — any `Source/` read/write is scope failure.
- Content plausibility spot-check is the Reviewer's note, not machine-scored.
- Efficiency: content stories should land in one iteration; more indicates contract misses.
