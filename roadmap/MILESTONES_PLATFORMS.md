# Milestones: One Game, Three Platforms

**North star (re-pointed 2026-10-10):** one American football game, playable start to finish on
**iOS**, **Xbox** and **PC**. It is the same game on all three: one code base, one data set, one
rule set, and saves that move between them. Only packaging, input defaults, the scalability tier
and platform services differ.

This file replaces `roadmap/MILESTONE_FIRST_GAME.md` as the launch-critical path. That milestone's
definition, an editor Play session on a pad, stopped at the editor. Every goal here is a packaged
build on real hardware. The Epics these ladders add are in Track S (`roadmap/platform-release.md`,
Epics 145–153). Each rung below names the Epic or story that climbs it, and is ticked in the PR that
completes that story.

## The three dream states

Each goal is done only when every line is true on a packaged build, on that platform's hardware.
PIE never counts.

**PC (Win64)**
- Installs from a zip or installer and launches into the front end, with no editor on the machine.
- A newcomer plays a full game (kickoff, four quarters, final score) against the CPU, on keyboard
  and mouse or on a pad, without help.
- Season and franchise modes are reachable from the front end. Saves persist across launches.
- 60 fps at the `DesktopHigh` tier in a Shipping build on the owner's PC. Settings for graphics,
  audio and controls are saved.

**iOS (the owner's iPhone 17 Pro)**
- A signed build is installed on the phone (a 7-day sideload, or TestFlight with the Developer
  Program).
- A full game is played with touch: the touch HUD, safe areas and landscape lock work. A
  connected Xbox or MFi pad works too.
- 60 fps at `MobileBaseline`, and a 30-minute session without thermal throttling below 30 fps.
- Backgrounding mid-play pauses and saves; resuming continues the game.

**Xbox Series X|S**
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
- [ ] **S0.3:** a content pipeline decision is made and the headless editor pipeline runs in CI (146.1, 146.2)
- [ ] **S0.4:** the field level is the default map (146.3, closes 2.1–2.2), and the widget Blueprints are generated (146.4)
- [ ] **S0.5:** the world kit is imported (146.5, closes 142's editor stories)
- [ ] **S0.6:** the minimum playable content set: field, 22 players in team colors, core animation, ball, placeholder audio, and the assembled HUD and menus (147.1–147.4, closes 22's minimum and 23.3's sounds)
- [ ] **S0.7:** the packaged build boots into the front end and Play Now reaches the field (145.4)

## PC ladder

- [ ] **P1, PC playable:** a newcomer finishes a full game on the packaged PC build without help (147.5). This is the old first-game milestone's human playtest, now on a packaged build.
- [ ] **P2, PC complete:** settings, input parity on every screen, 60 fps at `DesktopHigh` in Shipping, and crash reporting and a version stamp (148.1–148.4, closes 17.5 for PC)
- [ ] **P3, PC distributed:** the owner's chosen distribution route (148.5)

## iOS ladder

- [ ] **I0, owner gates:** a Mac strategy accepted in `Specs/ADR_iOS_Build.md`, and an Apple account chosen (131.1, 149.1)
- [ ] **I1, on the phone:** a signed Development build installed on the iPhone (149.2)
- [ ] **I2, iOS playable:** a full game with touch at 60 fps, the thermal session passes, and lifecycle behaves (149.3–149.5, closes 17.5 for iOS)
- [ ] **I3, TestFlight:** testers install it (149.6, needs the Developer Program)

## Xbox ladder

- [ ] **X0, owner gates:** ID@Xbox onboarding and console hardware enabled for development (150.1). These take the longest, so apply early, in parallel with S0.
- [ ] **X1, Xbox compiles:** the GDK and UE 5.8's public Xbox plug-ins are enabled, there is an `XboxSeries` tier, and CI compiles the console target on every PR (150.2–150.4)
- [ ] **X2, Xbox playable:** a full game on Series S and Series X with a controller, plus user, controller and lifecycle handling (151.1–151.3)
- [ ] **X3, certification ready:** title-safe UI, platform glyphs, boot time, and a pre-certification pass (151.4–151.5)

## Cross-cutting rungs (climbed alongside the ladders)

- [ ] **C1, one platform interface:** `UPSPlatformServices` with null, iOS and Xbox implementations; saves through it (152). Needed by I2 and X2.
- [ ] **C2, parity:** the CI build matrix, `Specs/Release_Checklist.md`, the cross-platform determinism fingerprint, and per-tier performance budgets (153). It grows with each platform.

## Recommended order

1. **Now: S0 first.** It blocks every platform, and the existing Windows runner can do it with no
   new hardware or accounts. **At the same time, start the owner paperwork:** the ID@Xbox
   application and the Apple account. Both have lead times measured in weeks, and neither costs
   engineering time.
2. **PC next (P1–P2).** It is the first full game anyone can hold. It falls out of S0 almost for
   free, and every later platform reuses its content and fixes.
3. **iOS (I1–I2), as soon as a Mac path exists.** It is the owner's stated device. Touch input and
   the mobile tiers are already built (Track N), so the work is packaging, signing and mobile
   performance.
4. **Xbox (X1–X3) as access arrives.** The compile rung (X1) can run in CI as soon as the GDK is
   installed. Console rungs wait on hardware. Pad input is already built (Track M).
5. **152 and 153 alongside**, landing before I2 and X2 respectively.

## What this means for the other tracks

- **Frozen until S0 lands:** new depth work in Tracks A–H, J and L–Q, and the open stories of
  partial Epics. The simulation is already deeper than the game a player can see. Two exceptions:
  bug fixes in shipped systems, and the stories a rung names (2.1–2.2, 22's minimum, 23.3–23.4's
  sounds, 24.1, 142's editor stories and 17.5's device measurements).
- **Dream depth, after all three are playable**, in this order: C (stadium, lighting, weather,
  crowd), D (uniforms, animation depth), H (crowd and commentary audio), B (broadcast cameras),
  A (overlays). Then J's online play (108.3–108.4, after the online ADR), O's playbook (after its
  compliance decision), and the remaining E, F and G depth.
- **Unchanged:** `roadmap/PARALLEL.md` stays the source of truth for what is unblocked. Track S
  is its group G10.

## Owner decisions this plan needs

New with this plan:

- **D1, content pipeline (146.1):** commit generated `.uasset` content through Git LFS, or
  regenerate it in CI on every run? *Recommendation:* LFS for imported art (meshes, textures,
  sounds), and regenerate levels and widgets from scripts. That keeps PR diffs reviewable and CI
  fast.
- **D2, Mac for iOS (131.1, 149.1):** own a Mac, rent a cloud Mac, or use a Mac runner? And which
  account: a free Apple ID (7-day sideloads) or the $99/year Developer Program (TestFlight)?
- **D3, Xbox access (150.1):** apply to ID@Xbox now? It means an NDA, concept approval and
  Partner Center registration, then console hardware for development. Whether the console target
  compiles without onboarding is checked in 150.1.
- **D4, PC distribution (148.5):** a private zip for you and testers, itch.io, or Steam (the
  Steamworks fee and integration would be their own Epic)?
- **D5, online play in the dream state?** *Recommendation:* no. Ship v1 as single player plus
  local versus on all three, and treat online as the first dream-depth item after the online ADR
  is accepted. Cross-platform online play adds platform network policy and account linking on top
  of the netcode.
- **D6, the order:** PC → iOS → Xbox as above, or iOS before PC once the Mac exists? PC first
  costs iOS little, because S0 is most of the work for both.

Still open from earlier work:

- the playbook source: manual authoring or the site's permission (Track O, 132);
- the online ADR (`Specs/ADR_Online_Architecture.md`): accept Option C and its four questions;
- MCP server registration (25.4, then 118/119) and an AgenticLink auth token;
- play-art defaults (102.1) and the telestrator's on-field entry (Epic 41);
- Track L tuning and the generated-name blocklist policy;
- a non-unity compile in CI to catch missing includes (more load on the runner).
