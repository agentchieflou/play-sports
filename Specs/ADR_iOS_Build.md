# ADR: iOS Build Pipeline (Mac in the Loop)

**Status:** Accepted (2026-10-10): **Option B** (a Mac as a second self-hosted runner), with a
**free Apple ID**. See "Decision" below.
**Epic:** 131 (`roadmap/platform-ports.md`, Track N)
**Related:** `Specs/ADR_CI_Environment.md` (the Windows runner), `Specs/Platform_Audit.md`
(Epic 129: the iPhone budget), `Specs/iOS_Signing_Runbook.md` (the human signing steps),
`.github/workflows/ios-package.yml` (the packaging job stub).

## Context

The owner wants to play the game on their iPhone 17 Pro. Three facts constrain how:

1. **Building for iOS needs a Mac.** Unreal compiles iOS code with Xcode's toolchain and signs
   the app with Apple's tools. Both run only on macOS. The project has C++ code
   (`Source/PlaySports`), so there is no Windows-only route to an iOS build.
2. **The only CI runner is Windows.** `Specs/ADR_CI_Environment.md` put a self-hosted runner on
   the owner's Windows PC (labels `self-hosted, windows, unreal`). It builds the Win64 editor
   target and runs the headless tests. It cannot produce an iOS build alone.
3. **Installing on a phone needs an Apple account.** A free Apple ID can sign builds for the
   owner's own phone, and those builds stop launching after 7 days. The Apple Developer Program
   ($99 a year) adds TestFlight. `Specs/iOS_Signing_Runbook.md` covers both paths.

Agents cannot do any of the Mac or Apple steps: they have no Mac, no Apple credentials and no
keychain. Everything in this ADR that touches signing is a human step.

## Options considered

### A. Remote build from the Windows runner to a Mac (SSH)

The Windows runner runs `RunUAT BuildCookRun -platform=IOS`. Unreal's remote build copies the
sources to a Mac over SSH (rsync), compiles there with Xcode, and brings the result back.
Cooking stays on Windows.

- **For:** one workflow on the existing runner. The Mac needs only Xcode, not an Unreal install.
- **Against:**
  - Two machines must be on and reachable, and the network path (SSH keys, rsync, firewall)
    adds failure modes.
  - The first sync moves gigabytes.
  - Signing ends up split between the two machines.
  - **Unverified for 5.8:** Epic's newer Xcode-project workflow changed how iOS builds are
    driven. Before choosing this option, check the 5.8 iOS documentation to confirm that
    remote building from Windows is still supported, and how it signs.
- **Needs:** a Mac on the same network with Remote Login (SSH) on. The remote-build settings
  (`RemoteServerName`, `RSyncUsername` and the SSH key) go in the iOS section of the project
  settings. They are machine-specific, so keep them in a user-level config, not in this repo.

### B. A Mac as a second self-hosted runner

Register a Mac as a second GitHub Actions runner with the labels `self-hosted, macOS, mac`, with
Unreal Engine 5.8 (with iOS support) and Xcode installed. The `iOS Package` workflow
(`.github/workflows/ios-package.yml`) runs `RunUAT.sh BuildCookRun` natively on it.

- **For:**
  - The same pattern as the accepted Windows ADR.
  - The Mac-native build path, the one Epic tests most.
  - Signing stays on one machine: the runner's login keychain holds the certificates.
  - The same Mac does the first build by hand (Option D) to shake out signing, and the owner
    can install development builds on the phone from it over USB or Wi-Fi.
- **Against:**
  - Needs a Mac that can stay on.
  - Needs about 200 GB of free disk: Unreal for Mac with iOS support, Xcode and its iOS
    platform, and build intermediates.
  - Unreal's install via the Epic Games Launcher is interactive (one-time).
  - The public-repo runner posture from the Windows ADR applies to this machine too.
- **Needs:**
  - An Apple-silicon Mac is strongly preferred; check the 5.8 release notes for Intel support.
  - The Xcode version the Unreal 5.8 release notes require ("Platform SDK Upgrades").
  - A runner running as the logged-in user, so it can reach that user's keychain.
  - The repository variable `IOS_MAC_RUNNER=true`, which un-gates the workflow.

### C. A cloud Mac

- **GitHub-hosted macOS runners:** not viable.
  - Unreal is not preinstalled, and the Launcher install can't be scripted.
  - A source build of the engine takes hours.
  - The standard images don't have the disk for either.
- **A rented dedicated Mac** (AWS EC2 Mac, MacStadium, Scaleway, MacinCloud and others): install
  Unreal once over remote desktop, then register it as the `mac` runner. From then on it works
  exactly like Option B.
  - **For:** no hardware to own or keep powered.
  - **Against:**
    - A recurring cost, roughly tens to hundreds of dollars a month depending on provider and
      model; check current pricing. Apple's macOS licence imposes a 24-hour minimum lease.
    - The phone is never plugged into a cloud Mac. With a free Apple ID, Xcode registers the
      phone only when it is connected, so in practice a cloud Mac **requires the paid Developer
      Program** and delivers through TestFlight.

### D. Manual packaging on a borrowed (or owned) Mac

A person clones the repo on a Mac, installs Unreal 5.8 and Xcode, sets signing, and packages or
launches straight to the connected iPhone from the editor or `RunUAT.sh`.

- **For:** cheapest way to a first build; no CI work; works with a free Apple ID.
- **Against:**
  - Not reproducible.
  - Every build costs a person's time.
  - Free-account builds expire weekly, so a borrowed Mac is needed again every 7 days, unless
    the owner moves to TestFlight (90-day builds).

## Recommendation

**Option B, starting with a one-time Option D on the same Mac.**

1. On the Mac, do the first build by hand, following `Specs/iOS_Signing_Runbook.md`. That proves
   signing, the bundle ID and the iOS settings in `Config/DefaultEngine.ini`.
2. Then register the Mac as the `mac` runner and set `IOS_MAC_RUNNER=true`. The
   `iOS Package` workflow then produces builds on demand, the same way the Windows runner
   produces the Win64 build.

**Fallbacks:**

- **No Mac, and the owner won't buy one:** Option D on a borrowed Mac for a first playable build.
- **Regular builds without owning a Mac:** Option C (rented Mac plus the Developer Program).
- **Option A:** not recommended as the primary path. It has the most moving parts, and its 5.8
  support is unverified. It is only worth it if a Mac is on the network but can't host a runner.


## Decision (2026-10-10)

The owner chose **Option B** with a **free Apple ID**:

- iOS builds run on a Mac registered as a second self-hosted runner (`self-hosted, macOS, mac`),
  through `.github/workflows/ios-package.yml`, once `IOS_MAC_RUNNER` is `true`. The first build on
  that Mac is done by hand (Option D) to prove signing, as recommended.
- Signing uses the owner's free Apple ID (a Personal Team in Xcode on the runner Mac). Builds stop
  launching after 7 days and are reinstalled from that Mac; the iPhone is paired with it once.
- The iOS goal is playing on the owner's iPhone, not an App Store build. TestFlight and the App
  Store are deferred to `roadmap/platform-release.md` Epic 154, after the owner joins the
  Developer Program.
- Still to come from the owner, when iOS work starts (`roadmap/platform-release.md`, 149.1): the
  Mac itself (model and chip, macOS version, free disk, able to stay on) and the runner
  registration.

Options A and C are not pursued. Option C needed the paid Developer Program in any case.

## Prerequisites the first iOS build will hit (found while writing this ADR)

None of these are verified; CI has never built anything but the Win64 editor target. Check them
in order on the first Mac build.

1. **The game target has never been compiled.** CI builds `PlaySportsEditor` only. The first iOS
   build will also be the first build of the `PlaySports` game target, on any platform. Any
   editor-only code outside `WITH_EDITOR` will fail there. A cheap early warning, which needs no
   Mac, is a Win64 `PlaySports` (game) compile in CI. That is a Track K (`.github/workflows/ci.yml`)
   decision, not this ADR's. *Epic 145 does it: `.github/workflows/package.yml` builds, cooks and
   packages the Win64 game target and smoke-tests it.*
2. **`Plugins/Autonomix` depends on editor-only modules.** Its module is `Developer` type, and its
   `Build.cs` depends on `EditorScriptingUtilities` and `PythonScriptPlugin`. If the build pulls
   it into a game target, the build fails. The fix is to make its module `Editor` type, or to
   restrict its platforms in `Autonomix.uplugin`. That plugin is Track K's. *Epic 145 made both
   plugins' modules `Editor` type, so no game target builds them.*
3. **There is no map to cook.** `Content/` is empty. `GameDefaultMap=/Game/Maps/GameMap` does not
   exist until an editor session creates it (`Specs/Default_Map_Spec.md`). Until then, an iOS
   build proves the toolchain and signing, not the game.
4. **Input on the phone.** Until Epic 130's touch layer exists, play needs a paired Bluetooth
   controller (Xbox or PlayStation), which goes through the existing gamepad path
   (`Specs/Platform_Audit.md`).

## Consequences

- **The Mac runner:** `.github/workflows/ios-package.yml` exists now, gated three ways so it can
  never touch the Windows runner or block a PR:
  - it runs only on `workflow_dispatch`, never on push or pull request;
  - it requires the labels `self-hosted, macOS, mac`;
  - its job is skipped unless the repository variable `IOS_MAC_RUNNER` is `true`.

  `ci.yml` and the Windows runner are untouched.
- **iOS settings:** the iOS target settings live in `Config/DefaultEngine.ini`, in
  `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]`. They are compile-safe on Win64, because CI
  never reads them. Their effect is **unverified until a Mac build**.
- **The bundle ID:** `com.example.playsports` is a placeholder. Apple requires a bundle ID unique
  to the owner, so the owner replaces it before the first signed build (runbook, step 2).
- **Agents and Apple:** agents can edit the iOS config and the workflow, and read the packaging
  job's logs once a Mac runner exists. Apple accounts, certificates, devices, App Store Connect
  and TestFlight stay human-only.
- **Platform tiers:** frame-rate caps and rendering quality for iOS are the device profiles'
  job (`Config/DefaultDeviceProfiles.ini`, Epic 129). The iOS section doesn't repeat them, so
  each setting has one authority.

## Question for the owner (answered 2026-10-10; see "Decision")

> **Do you have a Mac you can use for iOS builds?**
>
> - **If yes:**
>   - Which model and chip (Apple silicon or Intel)?
>   - Which macOS version, and how much free disk space?
>   - Can it stay on, plugged in and awake, as a second CI runner?
> - **If no:**
>   - Would you rather buy a Mac mini (Option B)?
>   - Borrow a Mac for occasional manual builds (Option D)?
>   - Or rent a cloud Mac (Option C, which needs the paid Developer Program)?
>
> **Also: free Apple ID or the Apple Developer Program?**
>
> - A free Apple ID costs nothing, but builds stop launching after 7 days and must be reinstalled
>   from the Mac.
> - The Developer Program costs $99 a year and adds TestFlight (90-day builds, installed over
>   the air).
