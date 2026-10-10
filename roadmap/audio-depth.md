# Track H — Audio Depth (Epics 96–100)

Deepens core Epic 23's audio pipeline into full broadcast sound: commentary, reactive crowd
audio, on-field detail, and the mix that binds them. Sizing/mode legend: see `ROADMAP.md`.

**Reality note (2026-07-19 review):** every audio trigger in this track is a C1
`UPSTelemetryBus` subscription — the event bus is the audio event bus; Epic 96's commentary
event model reads C1 history + Epic 92 stats. No audio system polls game state or casts to
GameMode. Tests per epic where headless-checkable (event→cue mapping logic).

### Epic 96: Commentary Engine

**Size/Mode:** XL / mixed
**Goal:** Play-by-play and color commentary that tracks the actual game — the hardest audio system in sports games.
**Depends on:** Core 23, 26, 92, 93

- [x] Commentary event model: what happened, who, stakes, novelty — derived from telemetry + stats
  *As built: Core 23.5's `UPSCommentaryEventModel` is the event model: its `Commentary` bus events
  (what happened, who, the situation) now carry `Stakes` (late, close, third or fourth down, the red
  zone, points, a turnover) and `Novelty` (the first of its kind this game from the moments told so
  far, a big play, a record), weighted in `Data/commentary_hooks.json`, and for a play's result the
  primary player's statistic and game total from Epic 92's box score (`SetStats`, from the game
  mode; a play the box score hasn't heard yet is added). Tested:
  `PlaySports.Commentary.EventModelWeighs`.*
- [x] Line-selection engine: priority, cooldowns, interruption rules (big play cuts off filler)
  *As built: `UPSCommentaryEngine` (the booth) hears the `Commentary` events and speaks through the
  bus's `Speech` event. The lines that fit a moment (detail, down, yards, stakes, a first down, a
  turnover, the names and totals their text needs) score priority + stakes + novelty - repeats; one
  is picked within `VarietyBand` of the best, seeded per game. Cooldowns and caps rule lines out. A
  line `InterruptMargin` above the one being said cuts it off; others queue, and a play-by-play line
  waiting past `MaxDelaySeconds` is dropped. Per-tier `AudioUpdateHz` paces it. Tested:
  `PlaySports.Commentary.LineSelection`.*
- [x] Two-voice structure: play-by-play cadence + color-analyst windows between plays
  *As built: the play-by-play calls the action as it happens; the analyst speaks from
  `ColorWindowDelaySeconds` after the play is over until the snap, never over the play-by-play (who
  cuts him off), and the snap clears his queue. Tested: `PlaySports.Commentary.TwoVoices`.*
- [x] Content pipeline: template library first; TTS/LLM generation path bridge-gated via Epic 82
  *As built: `Data/commentary_lines.json` (58 lines, a play-by-play line for every moment) with each
  line's text in the string table (`Commentary.Line.<LineId>` in `Data/ui_text.csv`, through
  `UPSLocalization`; `tools/validate_data.py` checks every row and its placeholders, and
  `tools/ui_text.py` now gates `PSCommentary*` code against raw text). With Epic 82's bridge online
  the hooks offer moments to outside models (`narration` routing), and a model's line for the play
  replaces the analyst's template line (verbatim, not ours to translate); a late one is dropped. Each
  `Speech` carries its `LineId`, which the audio maps to a recorded voice-over (`Speech` trigger in
  `Data/audio_cues.json`; none recorded yet, editor work). Tested:
  `PlaySports.Commentary.ModelLinesAndStorylines`.*
- [x] Storyline integration: Track G narratives (93) surface as talking points
  *As built: `FeedFromNarrative` takes `UPSLeagueNarrative::GetTalkingPoints` for the game's two
  teams; the analyst brings one up in a quiet window (`Commentary.Storyline`), each once, at most
  `MaxTalkingPointsPerGame`, `TalkingPointGapSeconds` apart. Like Epic 93's `FeedBroadcast`, nothing
  calls it at a played franchise game's kickoff yet: the franchise flow isn't carried into the match
  world. Tested: `PlaySports.Commentary.ModelLinesAndStorylines`.*
- [x] Repetition telemetry: measure and cap line reuse per game/season
  *As built: every line's uses this game and season are counted; `MaxPerGame` and `MaxPerSeason` cap
  them; `BuildRepetitionReport` (lines said, distinct, repeat share, most used, cut off, stale, held by
  caps, lines at cap) is logged at the final whistle; the season's counts persist in
  `UPSFranchiseSaveGame::CommentaryUsage` (`SaveTo`/`LoadFrom`). Tested:
  `PlaySports.Commentary.RepetitionCaps`.*

### Epic 97: Reactive Crowd Audio

**Size/Mode:** M / mixed
**Goal:** The crowd's sound is the emotional score of the game, driven by the Epic 49 behavior model.
**Depends on:** Core 23, 49

- [ ] Layered crowd bed (murmur → buzz → roar → eruption) crossfaded by excitement state
- [ ] Event stingers: gasp on deep balls, groan on drops, eruption on scores, silence after visiting scores
- [ ] Anticipation swells (third-down rise, goal-line builds)
- [ ] Venue acoustics profile from Epic 52 (dome slap vs. open-air dissipation)

### Epic 98: On-Field & Mic'd-Up Audio

**Size/Mode:** S / editor
**Goal:** The field-level sound layer — pads, cadence, line calls, whistle — that sells contact and proximity.
**Depends on:** Core 23

- [ ] Contact SFX matrix scaled by collision physics data (glancing vs. big hit)
- [ ] QB cadence and line-call chatter tied to pre-snap behaviors (60)
- [ ] Footsteps/surface coupling (turf vs. grass vs. wet from 47/51)
- [ ] Whistle/officiating audio synced to Epic 65

### Epic 99: Stadium Identity & Traditions Audio

**Size/Mode:** S / code
**Goal:** Venues sound like themselves — chants, songs, horns, and tradition moments as per-team data.
**Depends on:** 97, 52

- [ ] Per-team audio identity data (chants, TD songs, tradition triggers)
- [ ] Tradition event scheduling (first-down chants, defense-stand horns)
- [ ] PA announcer layer (starting lineups, penalty announcements, two-minute warning)

### Epic 100: Broadcast Audio Mix

**Size/Mode:** M / mixed
**Goal:** All layers sit in a coherent broadcast-style mix with ducking, perspective, and replay treatment.
**Depends on:** 96, 97, 98

- [ ] Mix bus architecture: commentary ducks crowd, crowd ducks field, master broadcast EQ
- [ ] Camera-perspective audio (skycam hears more field; high wide hears more crowd)
- [ ] Replay/slow-mo treatment (wooshes, isolated contact audio, commentary handoff)
- [ ] User mix controls (commentary off, crowd up, etc.) feeding Track I settings
