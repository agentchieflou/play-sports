# Milestones: One Game, Three Platforms

**North star (re-pointed 2026-10-10):** one American football game, playable start to finish on
**iOS**, **Xbox** and **PC**. It is the same game on all three: one code base, one data set, one
rule set, and saves that move between them. Only packaging, input defaults, the scalability tier
and platform services differ.

This file replaces `roadmap/MILESTONE_FIRST_GAME.md` as the launch-critical path. That milestone's
definition, an editor Play session on a pad, stopped at the editor. Every goal here is a packaged
build on real hardware. The Epics these ladders add are in Track S (`roadmap/platform-release.md`,
Epics 145–154). Each rung below names the Epic or story that climbs it, and is ticked in the PR that
completes that story.

## The three dream states

Each goal is done only when every line is true on a packaged build, on that platform's hardware.
PIE never counts.

**PC (Win64)**
- Installs from a private zip and launches into the front end, with no editor on the machine.
  (Steam comes later, as its own Epic.)
- A newcomer plays a full game (kickoff, four quarters, final score) against the CPU, on keyboard
  and mouse or on a pad, without help.
- Season and franchise modes are reachable from the front end. Saves persist across launches.
- 60 fps at the `DesktopHigh` tier in a Shipping build on the owner's PC. Settings for graphics,
  audio and controls are saved.

**iOS (the owner's iPhone 17 Pro): playable on the phone, not App Store ready**
- A Development build from the Mac CI runner is installed on the phone with the owner's free Apple
  ID (it is reinstalled every 7 days).
- A full game is played with touch: the touch HUD, safe areas and landscape lock work. A paired
  Xbox pad works too.
- Smooth enough to play: targeting 60 fps at `MobileBaseline`, never below 30 fps in play, through
  a full game without thermal throttling below 30 fps.
- Backgrounding mid-play pauses and saves; returning resumes the game.
- Not part of this goal (later, Epic 154): the Developer Program, TestFlight, the App Store's
  requirements and submission.

**Xbox Series X|S** (the owner applies to ID@Xbox later; until then the work is the parts that
build and test on PC, Epic 150)
- A packaged build runs on console hardware enabled for development.
- A full game is played with an Xbox controller at 60 fps on Series S (the floor) and Series X.
- Sign-in, controller disconnect and reassign, suspend/resume and Quick Resume all behave.
- The build passes a self-run pre-certification pass against the current Xbox Requirements. Store
  submission is a separate owner decision.

**Shared, all three:** the same data and rules everywhere, proven by comparing the determinism
fingerprint (Epic 108's audit) across the three builds. Saves move between platforms. One CI build
matrix proves every change on all three.

## Where we are (2026-10-10)

The simulation is far ahead of the game you can hold. About 500 headless tests pass on every PR.
The AI plays four full quarters, and the human input paths (pad, keyboard, touch) are built. The
front end, play calling, season and franchise systems exist in C++.

But `Content/` is empty: there is no level, no character mesh, no widget Blueprint and no sound. So
nothing runs outside the editor, and nobody has played a full game on any device.

The reason is structural. Every content story is in "editor" mode, and agent sessions have no
editor. Epic 146 fixes that: content is built by Python scripts that drive the editor headlessly on
the CI runner, which already runs the editor headlessly for tests.

## Shared ladder: S0, out of the editor (all three platforms wait on it)

- [ ] **S0.1:** CI packages a cooked Win64 build on every push to main (145.1, 145.2)
- [ ] **S0.2:** the packaged build plays a scripted full game headlessly and exits with a score (145.3)
- [x] **S0.3:** the content pipeline ADR is written for the owner's Git LFS decision, and the headless editor pipeline runs in CI (146.1, 146.2)
- [ ] **S0.4:** the field level is the default map (146.3, closes 2.1–2.2), and the widget Blueprints are generated (146.4)
- [ ] **S0.5:** the world kit is imported (146.5, closes 142's editor stories)
- [ ] **S0.6:** the minimum playable content set: field, 22 players in team colors, core animation, ball, placeholder audio, and the assembled HUD and menus (147.1–147.4, closes 22's minimum and 23.3's sounds)
- [ ] **S0.7:** the packaged build boots into the front end and Play Now reaches the field (145.4)

## PC ladder

- [ ] **P1, PC playable:** a newcomer finishes a full game on the packaged PC build without help (147.5). This is the old first-game milestone's human playtest, now on a packaged build.
- [ ] **P2, PC complete:** settings, input parity on every screen, 60 fps at `DesktopHigh` in Shipping, and crash reporting and a version stamp (148.1–148.4, closes 17.5 for PC)
- [ ] **P3, PC distributed:** a private zip for the owner and testers (148.5); Steam later

## iOS ladder

- [x] **I0a, decisions:** `Specs/ADR_iOS_Build.md` accepted: a Mac as a second CI runner, with a free Apple ID (131.1)
- [ ] **I0b, the Mac runner:** the owner provides the Mac, registered as the `mac` runner, with the iPhone paired once (149.1)
- [ ] **I1, on the phone:** a signed Development build installed on the iPhone (149.2)
- [ ] **I2, iOS playable:** a full game on the phone with touch, smooth enough to play (60 fps target, 30 floor) through a full game, and backgrounding behaves (149.3–149.5, closes 17.5 for iOS). **This is the iOS goal.**
- [ ] **Later, App Store (154):** the Developer Program, TestFlight, the App Store's requirements and submission, when the owner chooses. Not part of the iOS goal.

## Xbox ladder

- [ ] **X0, the guts on PC:** the `XboxSeries` tier, users and controllers, lifecycle, the title-safe area and glyphs, all through 152 and tested on Win64; and `Specs/ADR_Xbox_Access.md`, ready for the owner to apply (150)
- [ ] **X1, access (when the owner applies):** ID@Xbox onboarding and console hardware enabled for development; the console target compiles in CI (151.1, or 150 if the public plug-ins allow it)
- [ ] **X2, Xbox playable:** 152's Xbox implementation and a full game on Series S and Series X with a controller (151.2, 151.3)
- [ ] **X3, certification ready:** on-console checks of the guts, boot time, and a pre-certification pass (151.4, 151.5)

## Cross-cutting rungs (climbed alongside the ladders)

- [ ] **C1, one platform interface:** `UPSPlatformServices` with null, iOS and Xbox implementations; saves through it (152). Needed by I2 and X2.
- [ ] **C2, parity:** the CI build matrix, `Specs/Release_Checklist.md`, the cross-platform determinism fingerprint, and per-tier performance budgets (153). It grows with each platform.

## Order (owner decided 2026-10-10)

1. **S0 first.** It blocks every platform, and the existing Windows runner can do it with no new
   hardware or accounts.
2. **PC next (P1–P3).** It is the first full game anyone can hold. It falls out of S0 almost for
   free, and every later platform reuses its content and fixes.
3. **iOS (I0b–I2)** once the Mac runner exists. Touch input and the mobile tiers are already built
   (Track N), so the work is packaging, signing and mobile performance.
4. **Xbox last.** Its guts (X0) are pure code tested on PC, so a spare lane may build them before
   iOS finishes without touching the S0 chain. The console rungs (X1–X3) wait for the owner's
   ID@Xbox application.
5. **152 and 153 alongside.** 152 lands before X0 and I2; 153 grows with each platform.

## What this means for the other tracks

- **Frozen until S0 lands:** new depth work in Tracks A–H, J and L–Q, and the open stories of
  partial Epics. The simulation is already deeper than the game a player can see. Two exceptions:
  bug fixes in shipped systems, and the stories a rung names (2.1–2.2, 22's minimum, 23.3–23.4's
  sounds, 24.1, 142's editor stories and 17.5's device measurements).
- **Dream depth, after all three are playable**, in this order: C (stadium, lighting, weather,
  crowd), D (uniforms, animation depth), H (crowd and commentary audio), B (broadcast cameras),
  A (overlays). Then O's playbook (after its compliance decision) and the remaining E, F and G
  depth. **Online play is parked** by the owner (2026-10-10): 108.3–108.4 and the online ADR stay
  as they are until the owner reopens them.
- **Unchanged:** `roadmap/PARALLEL.md` stays the source of truth for what is unblocked. Track S
  is its group G10.

## Owner decisions

Answered 2026-10-10:

- **D1, content pipeline (146.1):** Git LFS. Every `.uasset`/`.umap` is committed through LFS;
  scripts regenerate the generated ones, and CI's drift check keeps them in step.
- **D2, iOS (131.1, 149.1):** a Mac as a second CI runner (ADR Option B), with a free Apple ID.
  The Mac itself is still to come.
- **D3, Xbox (150, 151):** don't apply to ID@Xbox yet. Build the guts now; the owner applies later.
- **D4, PC distribution (148.5):** a private zip first; Steam later.
- **D5, online play:** left alone for now (parked, not in the platform dream states).
- **D6, order:** PC, then iOS, then Xbox.
- **D7, the iOS goal:** playable on the owner's iPhone, not App Store ready. App Store work
  (Epic 154) comes later.

Still open from earlier work:

- the playbook source: manual authoring or the site's permission (Track O, 132);
- MCP server registration (25.4, then 118/119) and an AgenticLink auth token;
- play-art defaults (102.1) and the telestrator's on-field entry (Epic 41);
- Track L tuning and the generated-name blocklist policy;
- a non-unity compile in CI to catch missing includes (more load on the runner). 145.1's first
  game-target build covers part of it.
