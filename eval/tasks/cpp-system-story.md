---
id: cpp-system-story
archetype: gameplay-cpp-story
allowed_paths:
  - Source/PlaySports/**
  - Plugins/**
  - ROADMAP.md
  - roadmap/**
requires_tests: true
---

# Benchmark: C++ System Story

A representative gameplay C++ story: implement one small, self-contained system extending an
existing class (e.g. "add a cooldown field and tick-down logic to `UPSPlaySimulation`, exposed
`BlueprintCallable`, with an automation test").

## Rubric (scored by tools/score_agent_run.py)

- CI green on the PR head (compile + all automation tests) — dominant weight.
- Diff scope stays inside `allowed_paths` (plus the roadmap tick).
- New/changed logic carries an automation test (`Source/**/Tests/`).
- Zero linter errors; style warnings noted, not penalized.
- Roadmap checkbox ticked in the same PR.
- Efficiency: fewer fix-up iterations before green scores higher.
