# Track H — Audio Depth (Epics 96–100)

Deepens core Epic 23's audio pipeline into full broadcast sound: commentary, reactive crowd
audio, on-field detail, and the mix that binds them. Sizing/mode legend: see `ROADMAP.md`.

### Epic 96: Commentary Engine

**Size/Mode:** XL / mixed
**Goal:** Play-by-play and color commentary that tracks the actual game — the hardest audio system in sports games.
**Depends on:** Core 23, 26, 92, 93

- [ ] Commentary event model: what happened, who, stakes, novelty — derived from telemetry + stats
- [ ] Line-selection engine: priority, cooldowns, interruption rules (big play cuts off filler)
- [ ] Two-voice structure: play-by-play cadence + color-analyst windows between plays
- [ ] Content pipeline: template library first; TTS/LLM generation path bridge-gated via Epic 82
- [ ] Storyline integration: Track G narratives (93) surface as talking points
- [ ] Repetition telemetry: measure and cap line reuse per game/season

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
