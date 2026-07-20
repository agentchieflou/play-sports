# Track Q — Character Archetypes & Combat Rules (Epics 139–141)

The proprietary combat layer that differentiates this game from a standard football sim: every
on-field player has hitpoints, a snap isn't over until the ball carrier is downed, downed
carriers sit out exactly one play, non-carriers who die respawn at full health the next play,
interceptions punish the offense by auto-killing the intended receiver, punting is banned, and
the defense gets an extra player on 4th down. Also lays the archetype/leveling groundwork a
future multiplayer microtransaction layer would build on. Pure code — no Unreal Editor access
needed. Sizing/mode legend: see `ROADMAP.md`.

**Status (2026-07-20):** all three epics implemented in this track's first pass. `EPlayerRole`
already existed as the fine-grained position enum (QB/RB/WR/TE/OL/DL/LB/DB); this track adds
`EPlayerArchetypeClass` (OffenseSkill/DefenseSkill/Lineman) as the broad combat-tuning grouping
layered on top of it, per the user's framing of "offensive skill players, defensive skill
players, and linemen" as the three character archetype classes.

### Epic 139: Character Archetypes & Hitpoint Core

**Size/Mode:** L / code
**Goal:** Every player has hitpoints; a tackle deals damage instead of unconditionally ending the
play — the snap isn't over until the ball carrier is downed.
**Builds on:** `EPlayerRole`, `APSPlayerPawn`'s stamina-component pattern, `UPSInjuryModel`'s
deterministic-roll shape, `UPSRoster` (Architecture rule 6: single authority for player
availability), `UPSTelemetryBus`.
**Depends on:** Core 8 (Tackling & Collision Resolution), Core 19 (Roster/Progression/Injury), C1
(Telemetry Bus)

- [x] `EPlayerArchetypeClass` enum + `GetArchetypeClassForRole` mapping (`PSPlayerAttributes.h`,
  `PSArchetypeTuning.h/.cpp`)
- [x] `FPSArchetypeTuning` DataTable row (max hitpoints + damage multiplier per archetype class),
  loaded through `UPSDataIngestion::LoadArchetypeTuningFromJson` — no ad-hoc parser
- [x] `UPSHealthComponent`: per-pawn live hitpoint pool, attached to `APSPlayerPawn` alongside the
  existing `UPSPossessionComponent`/`UPSBallActionComponent` extraction pattern
- [x] `UPSCombatRulesModel`: deterministic, seedable tackle-damage roll (replay-safe, mirrors
  `UPSInjuryModel`)
- [x] Tackle resolution reworked in `UPSBallActionComponent::ResolveTackle`: a successful physical
  tackle now rolls damage against the carrier's `UPSHealthComponent`; the play only ends
  (`UPSPlaySimulation::RecordTackle`) if that brings hitpoints to 0 — otherwise it's a broken
  tackle and the play continues
- [x] `FPSTelemetryDamageEvent`/`FPSTelemetryDeathEvent`/`FPSTelemetryRespawnEvent` added to
  `UPSTelemetryBus` following the existing Publish/OnX/OnXMC triple pattern
- [x] `FPSPlayerLiveState` (hitpoints, downed flag, sit-out play index, level/XP) added as a new
  authoritative live-state map on `UPSRoster` — extends the existing "who's available" authority
  rather than a new parallel tracker
- [x] Downed-ball-carrier rule: `UPSRoster::MarkDownedForNextPlay` sits the player out exactly one
  play (`UnavailableUntilPlayIndex`), consumed via `IsAvailableForPlay`
- [x] Non-carrier death rule: `UPSRoster::MarkDownedForCurrentPlayOnly` + `RespawnForNewPlay` —
  full-HP respawn at the very next play, no sit-out (distinct authority path from the carrier rule)
- [x] `APSGameMode` wiring: `ActiveRoster` (built from the same roster split used for
  `OffenseRoster`/`DefenseRoster`), subscribes to `OnDeath` to route kills into roster live-state,
  and resets every on-field pawn's HP + roster live-state once per play in `ResetPawnPositions`
- [x] Tests: `PSHealthComponentTests.cpp`, `PSCombatRulesModelTests.cpp`, plus roster live-state
  coverage added to `PSRosterTests.cpp`

**Known scope boundary:** full personnel substitution (benching a sat-out player's pawn with a
bench player's pawn) is Epic 19's still-open "personnel packages tied into formations" work —
this epic tracks the authoritative sit-out/availability state correctly, it does not yet swap
pawns on the field. Non-carrier damage during blocking/pass-rush contact (as opposed to the ball
carrier's tackle) is left for a follow-up story once Epic 9's line-play contact events are
extracted the same way Epic 8's tackle contact was.

### Epic 140: Combat Rule Variants — No Punting, INT Punishment, 4th-Down Overload

**Size/Mode:** M / code
**Goal:** The rule variants that make 4th down meaningfully different: no punting, and defense
gets reinforcements instead.
**Depends on:** Epic 139, Core 18 (Coaching & Play-Selection AI), Core 17 (Play Orchestration)

- [x] `UPSRulesConfig` gains `bAllowPunting` (default false), `bFourthDownDefensiveOverload`
  (default true), `NumExtraDefendersOnFourthDown` (default 1) — tuning in the existing rules data
  asset, not a hardcoded branch
- [x] No punting: `UPSPlaySimulation::EndPlayAndPrepareNext`'s 4th-down special-teams branch skips
  the Punt phase when disallowed and out of field-goal range — the drive just continues as a
  normal down (go for it), since `NextPhase` defaults to `PreSnap`
- [x] INT auto-kill: `UPSBallActionComponent::ThrowPass` gained an optional `IntendedTarget`
  parameter that publishes a `Throw` bus event (previously declared but never published in
  production code); `APSBall` tracks the most recent intended target and, on an interception,
  kills that pawn's `UPSHealthComponent` and publishes a `Death` event with
  `Cause=InterceptionPunishment` — punishing the QB's read, not whoever the ball happens to hit
- [x] 4th-down defensive overload: `GetDefensivePersonnelCount(Down, RulesConfig)` pure helper
  (`PSRulesConfig.h/.cpp`); `APSGameMode::ResetPawnPositions` spawns/despawns one extra defender
  from the bench (via `UPSRoster`'s depth chart) when the down is 4 and the rule is enabled
- [x] Tests: `PSCombatRuleVariantTests.cpp` (personnel count by down, no-punting phase behavior,
  archetype role-mapping regression)

**Known scope boundary:** `UPSCoachingAI`'s `ShouldGoForItOnFourthDown` play-selection heuristic
exists but was already unwired into the live 4th-down decision before this epic (confirmed by
code inspection — `UPSPlaySimulation` hardcodes the Punt/FieldGoal choice directly); this epic
fixes the punting rule at that actual decision point rather than wiring a second, parallel path
through `UPSCoachingAI`.

### Epic 141: Player Leveling & XP Progression

**Size/Mode:** M / code
**Goal:** Players earn XP and level up in a way that's visibly different per archetype — the seam
a future multiplayer microtransaction layer (XP boosts, cosmetic unlocks) would build on, with no
store integration in this epic.
**Depends on:** Epic 139, Core 19 (Player Progression)

- [x] `FPSLevelingTuning` DataTable row: XP-per-level threshold, per-play XP awards, touchdown
  bonus, per-level attribute growth amount — mirrors `FPSProgressionTuning`'s shape
- [x] `FPSPlayerLiveState` extended with `Level`/`CurrentXp` (added alongside the hitpoint fields
  in Epic 139)
- [x] `UPSPlayerLeveling`: `ComputeXpForPlay` (participation + touchdown bonus) and
  `AwardXpForPlay` (accrues XP via `UPSRoster`, applies level-ups and grows the player's stored
  attributes in place — Speed/Agility for skill players, Strength for linemen, split by
  `EPlayerArchetypeClass`)
- [x] `APSGameMode` calls `AwardXpForPlay` for every on-field pawn once per play in
  `ResetPawnPositions`, modeled on how the existing `UPSInjuryModel`/`UPSPlayerProgression` are
  invoked as standalone objects rather than being buried in the sim's state machine
- [x] Tests: `PSPlayerLevelingTests.cpp` (XP computation, level-up threshold crossing, per-
  archetype attribute growth)

**Explicitly out of scope:** any store/purchase integration, XP-boost items, or cosmetic unlock
UI — this epic is the gameplay-only foundation the user asked to have "prepared for now."
