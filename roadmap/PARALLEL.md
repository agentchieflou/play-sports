# Parallel Dispatch Matrix

This matrix defines the scheduling relationships and disjoint file scopes across the roadmap. The supervisor program (`tools/orchestrator/`) parses the JSON block below to dispatch stories to workers concurrently.

## Parallel Groups Summary

- **G1: Phase 0/1.5 Cleanup**
  - Epics: 1, 2, 5, 12, C3, C4
  - Scope: C++ gameplay and specification files.
  - Serialization: Must serialize internally because they touch core pawn, simulation, and game mode systems.
- **G2: Agentic Orchestrator Infrastructure**
  - Epics: 135, 136, 137, 138
  - Scope: `tools/orchestrator/`
  - Parallel Safety: 100% disjoint from all gameplay and data files. Safe to run concurrently with any gameplay track.
- **G3: Playbook Scraper Extraction**
  - Epics: 132, 133, 134
  - Scope: `tools/playbook_scraper/`, `Data/playbooks/`
  - Parallel Safety: Disjoint from core runtime code. Safe to run in parallel with gameplay/UI work.
- **G4: Platform Ports & Audits**
  - Epics: 129, 130, 131
  - Scope: `Specs/Platform_Audit.md`, `Config/DefaultDeviceProfiles.ini`
  - Parallel Safety: Config/documentation changes. G4 stories do not touch gameplay files directly.

## JSON Matrix Specification

```json
{
  "matrix_version": "1.0",
  "groups": {
    "G1_cleanup": {
      "epics": [1, 2, 5, 12, "C3", "C4"],
      "scopes": ["Source/PlaySports/*", "Specs/*"],
      "serialize_within": true,
      "conflicts": []
    },
    "G2_orchestrator": {
      "epics": [135, 136, 137, 138],
      "scopes": ["tools/orchestrator/*", "tools/score_lib.py"],
      "serialize_within": true,
      "conflicts": []
    },
    "G3_scraper": {
      "epics": [132, 133, 134],
      "scopes": ["tools/playbook_scraper/*", "Data/playbooks/*"],
      "serialize_within": true,
      "conflicts": []
    },
    "G4_platform": {
      "epics": [129, 130, 131],
      "scopes": ["Specs/Platform_Audit.md", "Config/DefaultDeviceProfiles.ini", "Specs/Touch_Controls_Spec.md"],
      "serialize_within": false,
      "conflicts": []
    }
  },
  "overrides": [
    {
      "comment": "Epic 127 Xbox gamepad possession touches APSPlayerPawn and must not run concurrently with C3 fast-follows.",
      "epic_a": 127,
      "epic_b": "C3",
      "action": "serialize"
    }
  ]
}
```
