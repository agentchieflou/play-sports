# ADR: Xbox Access (ID@Xbox, Partner Center, Dev Kits) and the Console Compile

**Status:** Accepted as the plan (2026-10-10). The owner applies to ID@Xbox when they choose
(owner decision D3 in `roadmap/MILESTONES_PLATFORMS.md`). Nothing here has been signed up for.
**Epic:** 150.5 (`roadmap/platform-release.md`, Track S). Epic 151 (console bring-up) follows it.
**Related:** `Specs/ADR_CI_Environment.md` (the Windows runner), `Specs/ADR_iOS_Build.md` (the
other platform's access ADR), `Specs/Platform_Audit.md` (the tiers).

Every fact below comes from public pages, listed under "Sources" with the date they were read. A
statement marked **unverified** could not be confirmed without gated material.

## The question 150.5 asks

> Can UE 5.8's public GDK plug-ins compile the Xbox Series X|S target without onboarding? If they
> can, CI compiles it on every PR from here; if not, the compile moves to 151.1.

**Answer: no. The console compile moves to 151.1.**

- **What became public in UE 5.8:** the Microsoft GDK plug-ins (Microsoft GDK Store, Microsoft
  GDK Runtime, Online Subsystem GDK, Online Subsystem Selector). Microsoft's article says the
  plug-ins "that you use to build for XBOX on PC are public". They ship with the engine from the
  Epic Games Launcher and in the GitHub source. They target **Xbox on PC**, which means Win64 with
  the GDK and the Microsoft Store and Game Pass on Windows. They do not target the console.
- **What stays gated:** Microsoft's Unreal page says that in 5.8 "XBOX console development (XBOX
  One, XBOX Series X|S) still requires the GDK Platform Extensions". Epic maintains and
  distributes those extensions. Access requires membership in an Xbox developer program such as
  ID@Xbox, plus an access request to Epic.
- **The SDK side is gated too.** The Microsoft GDK has two halves:
  - the GRDK, which covers PC and is public;
  - the GXDK, "the GDK version of the Xbox Development Kit", with the console headers, libraries
    and D3D12.x.

  Microsoft's documentation says building for Xbox Series X|S "requires the Microsoft GDK with the
  XBOX extensions, an authorized devkit, and Visual Studio".

So without onboarding, the runner cannot even generate the console target (in the editor the
platform is named `XSX`). No CI job for it is added now. 151.1 adds it once the owner has access.

**What CI could do today, if the owner wanted it:** compile a Win64 build with the public GDK
plug-ins enabled (Xbox on PC). That is a separate distribution channel, the Microsoft Store and PC
Game Pass. It needs the public GDK installed on the runner and a Partner Center association before
it ships. The PC plan (P1–P3: a private zip, then Steam) doesn't need it, so this ADR doesn't
propose it. Revisit it if the owner wants the Microsoft Store on PC.

## What applying involves (for the owner)

Agents cannot do any of these steps. Each is a human decision, an account or a signature.

1. **ID@Xbox registration**, from Microsoft's "Register for the ID@Xbox program":
   - Create a fresh personal Microsoft account (MSA), not tied to any console, Windows device or
     Store purchases. Turn on two-factor authentication.
   - On the Xbox Developer Programs site (developer.microsoft.com/games/publish), choose
     **ID@Xbox → Apply Now**, and fill in contact details and studio information.
   - **Mutual NDA:** Microsoft emails it, "typically within 20 minutes but could take up to 3
     business days". From here on, NDA material must stay out of this public repository (see
     "Risks").
   - **Game concept:** one application per game, reviewed by Trust & Safety, which "typically
     takes between 10 to 15 business days". Give as much as possible. For this game: a
     physics-based American football game, single player and local head-to-head, on Xbox Series
     X|S, PC and iOS.
   - **On approval:** sign the Title License Agreement (publishing rights) and the GDK Agreement
     (the developer tools, including the console extensions).
   - The pages read name no program fee. Check at application time (**unverified**).
2. **Partner Center:** the game's product is created and configured there. Store listing, age
   rating (IARC) and certification submissions all go through it. Microsoft's PC GDK setup also
   associates the Unreal project with the Partner Center product in Project Settings (MS Gaming).
3. **Dev hardware:**
   - Console builds run on an **authorized dev kit**, provisioned through the program.
   - The retail-console "Developer Mode" (the Xbox Dev Mode app, activated from a Partner Center
     app developer account) is for UWP apps. Microsoft's page for it is archived, and it tells
     managed-program partners not to use it on their dev hardware. It is not the route for a GDK
     game (**unverified** for UE 5.8 GDK titles specifically, but every current GDK page points to
     dev kits).
   - The certification test bench uses both Series S and Series X. The tier here budgets for
     Series S, the floor.
4. **Epic's side:** request the GDK Platform Extensions through Epic's access form (linked from
   Microsoft's Unreal pages) once ID@Xbox approves. They arrive as engine source with the
   `Engine/Platforms/...` console code.
5. **The runner (151.1):**
   - Install the GDK with the GXDK on the Windows runner (or on a second, private runner).
   - Build the engine from source with the platform extensions.
   - Add a console compile job that runs only where that engine exists.

## What Epic 150 already built (tested on Win64)

These parts need no console, so bring-up is packaging rather than new systems:

- **Tier:** `XboxSeries` in `Data/platform_tiers.json`. The platform `XSX` maps to it. It is
  budgeted for Series S: 60 fps, AI every frame, a title-safe area of 0.9. Its rendering half is
  the engine's `XSX` device profile, set in `Config/DefaultDeviceProfiles.ini`. `-PSTier=XboxSeries`
  runs a PC build on the console budgets.
- **Platform services (Epic 152):**
  - `UPSPlatformServices` with a backend chosen by config. 151.2 adds the GDK backend: XUser for
    users, XGameSave or the platform's save system for storage, and PLM for lifecycle.
  - Gameplay never names a backend or uses `#if PLATFORM_*`. `tools/lint_conventions.py` enforces
    both.
- **Users and controllers** (`UPSControllerPairingSubsystem`):
  - Each local player is paired with a platform user and a controller.
  - A lost controller pauses the game, and A on any controller takes over (XR-115).
  - In a head-to-head game the seat is reassigned (`UPSVersusSubsystem::ReassignSeat`).
  - On resume or unconstrain, each user is checked again. A user who signed out is asked for on
    the pause screen (XR-112).
- **Lifecycle:**
  - Suspend and constrain pause a live game. Resume leaves it paused for the player.
  - The save writes in flight are flushed. Quick Resume needs nothing more of gameplay.
  - XR-001 tests stability across suspends.
- **Presentation:**
  - Every HUD and menu widget keeps to the tier's title-safe area (`PSTitleSafeArea`; the linter
    forbids a direct `AddToViewport`). The 0.9 follows Microsoft's guidance that the title-safe
    region of an HDTV is the inner 90%.
  - Every context's gamepad actions draw Xbox glyphs.
  - Player-facing text uses the Xbox naming standard (XR-022): "vibration", not "rumble", and the
    stick buttons are LSB and RSB, not L3 and R3. `tools/ui_text.py` checks it.

## What stays unverified until there is access

- **The runtime platform name.** The tier mapping assumes `XSX`, which is how Microsoft's Unreal
  page says the editor names the platform. The name `UGameplayStatics::GetPlatformName` returns
  on the console is unverified. If it differs, change one line of `platform_tiers.json`.
- **The device profiles.** `XSX` as the base profile, and the names of its Series X and Series S
  children, come from the gated extension and are unverified. On Win64 the `[XSX DeviceProfile]`
  section changes nothing.
- **Where the Xbox platform's config lives** (`Config/XSX/` or `Platforms/XSX/Config/`), and so
  where its `BackendClass` override goes. Unverified.
- **Platform events and the engine's delegates.** Whether the GDK platform layer raises the
  engine's generic application delegates for suspend, resume and constrain, or the backend must
  register for PLM itself, is unverified. 151.2 decides; either way it calls
  `UPSPlatformServices::HandleLifecycle`.
- **The current Xbox Requirements.** They are public and cited below. The forbidden-terms list is
  an NDA document and was not read.

## Risks and decisions for the owner

- **This repository is public.** Once the NDA is signed, the console platform extension, the
  GXDK, Microsoft's NDA documentation and anything derived from them must never be committed
  here. Before 151, either make the repository private, or keep console-only files (the platform
  config, the GDK backend if it uses NDA APIs, the console CI job's details) in a private
  overlay. Decide this before applying.
- **Runner load.** A console compile is another full engine target on the one Windows runner.
  151.1 should run it only for pushes to main and for PRs that touch the platform layer, like the
  packaging workflow (lane S1).
- **Timing.** Expect up to 3 business days for the NDA and 10–15 for concept review before any
  console work can start. Applying early costs nothing in code: 150's work doesn't depend on it.

## Sources (read 2026-10-10)

- Microsoft Game Dev: "Building Xbox games with Unreal Engine's new GDK plug-ins" (June 2026),
  https://developer.microsoft.com/en-us/games/articles/2026/06/building-xbox-games-with-unreal-engines-new-gdk-plug-ins/
- Xbox devdocs: "Unreal on Xbox", https://devdocs.xbox.com/build/gdk-and-engines/unreal/unreal
- Microsoft Learn (GDK): "Get started with Unreal Engine for Xbox PC: ways to acquire the
  plug-ins", https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/get-started-with-unreal-pc/gc-get-started-with-unreal-xbox-pc
- Microsoft Learn (GDK): "Step 1 - Register for the ID@Xbox program",
  https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/pc-e2e-guide/e2e-register-id-at-xbox
- Microsoft Learn (GDK): "What is the Microsoft Game Development Kit?" (GRDK and GXDK),
  https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/intro/introduction
- Microsoft Learn (GDK): "Getting started with MonoGame: get access for Xbox consoles" (console
  builds need the GDK's Xbox extensions and an authorized dev kit),
  https://learn.microsoft.com/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/getting-started-with-monogame/gc-get-started-monogame
- Microsoft Learn (archived UWP): "Xbox Developer Mode activation",
  https://learn.microsoft.com/windows/uwp/xbox-apps/devkit-activation
- Microsoft Learn (GDK): "Xbox Requirements for Xbox console games" (XR-112, XR-115) and
  "Certification tested Xbox Requirements" (XR-001, test bench),
  https://learn.microsoft.com/gaming/gdk/docs/store/policies/console/certification-requirements and
  https://learn.microsoft.com/gaming/gdk/docs/store/policies/console/console-certification-requirements-and-tests
- Microsoft Learn (GDK): "Terminology" (required controller terms) and "XR-022: Official Naming
  Standards",
  https://learn.microsoft.com/gaming/gdk/docs/store/policies/console/console-certification-terminology and
  https://learn.microsoft.com/gaming/gdk/docs/store/policies/fma/xr022-official-naming-standards
- Microsoft Learn (Win32): "Introduction to the 10-Foot Experience for Windows Game Developers"
  (title-safe region: the inner 90% on HDTV),
  https://learn.microsoft.com/windows/win32/dxtecharts/introduction-to-the-10-foot-experience-for-windows-game-developers
