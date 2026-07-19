---
id: review-task
archetype: review-verify
allowed_paths: []
requires_tests: false
---

# Benchmark: Review Task

A representative review: given a diff with 2-3 seeded defects (a scope-creep file, a missing
roadmap tick, an uninitialized UPROPERTY), produce a verdict + findings.

## Rubric

Review tasks change no files, so the scorer's diff checks don't apply; scoring is
verdict-quality against the seeded defect list:

- Recall: found N of the seeded defects (dominant weight).
- Precision: findings that aren't seeded defects must be real (spot-checked by a human or a
  stronger model).
- Format: verdict-first output per the `review-verify` skill.

Record these manually in the scorecard's `manual` block (`--manual-recall N/M`); the harness
stores and reports them alongside machine-scored runs.
