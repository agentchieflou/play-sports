# Prompt: Phase 0 - 1.5 Consolidation and Cleanup

Run the `phase-runner` loop to complete the outstanding stories from Phase 0 through Phase 1.5 sequentially.

## Assigned Stories to Resolve:

1. **Epic 12, Story 5 (Timeout budget per team)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Add timeout data properties to `FPlayState`, track timeouts remaining per team, implement rules to subtract timeouts, and stop clock.

2. **Epic C3 Fast-Follow A (Ball-Action Component)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Extract ball-action logic (`ThrowPass`, `ExecuteHandoff`, `ExecutePitch`, `ExecuteKick`, `FumbleBall`, `ResolveTackle`) out of `APSPlayerPawn` into a dedicated `UPSBallActionComponent`. Work carefully; verify changes in CI.

3. **Epic C3 Fast-Follow B (Single Roster Source of Truth)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Refactor pawns and play simulation to reference a single centralized roster source of truth, removing duplicate, independent copies of `FPlayerAttributes` per pawn.

4. **Epic 1, Story 5 (End-to-End PIE test)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Implement a headless automation test satisfying `Specs/PIE_Test_Spec.md` that asserts loading a roster, spawning, advancing simulation phases, and logging play results.

5. **Epic 2, Story 4 (Trigger Volumes)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Reconcile with checked touchdown detection (Epic 11) and wire the out-of-bounds/end-zone trigger volumes per `Specs/Trigger_Volumes_Spec.md`. Editor placement itself is out of scope and escalates to a human editor task.

6. **Epic 5, Stories 1-4 (Core HUD scoreboard)**
   - **Coder Specialization:** `gameplay-cpp-story`
   - **Implementation:** Build the C++ scoreboard widget hosting logic and C1 bus event subscriptions described in `Specs/HUD_Spec.md`. UMG asset compilation and visual styling elements escalates to a human editor task.

## Rules & Constraints:
- Follow the six-role pipeline behavior. Do not skip testing or review roles.
- Run locally and verify builds via self-hosted CI runs (`gh pr checks <PR> --watch`).
- If any manual visual adjustments or level placements are required, document them as specification items and add them to your final phase report escalation list.
- Tick checkboxes in `ROADMAP.md` only in the same commit/PR that completes the story.
