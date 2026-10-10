# Runbook: Signing and Installing the iOS Build

**Epic:** 131 (`roadmap/platform-ports.md`, Track N). **Audience:** the owner, or whoever
holds the Mac and the Apple account. **Agents cannot do any step here:** they have no Mac, no
Apple ID and no keychain.

This is how a build of this repo gets onto your iPhone. It assumes a Mac exists; which Mac, and
whether it becomes a CI runner, is `Specs/ADR_iOS_Build.md`. Unreal and Xcode screens move
between versions. Where this runbook names a setting, confirm the exact label in your 5.8
editor, and fix this file if it differs.

There are two paths. Pick one:

| | Free Apple ID ("personal team") | Apple Developer Program ($99 a year) |
|---|---|---|
| Installs on | Phones plugged into, or paired with, your Mac | Any phone of a tester you invite, over the air |
| A build lasts | **7 days**, then it won't launch until you reinstall it | 90 days in TestFlight |
| Delivered by | Xcode, from the Mac | The TestFlight app |
| Needs a Mac | Every week, to reinstall | Only to build and upload |
| Good for | Trying it out this week | Playing regularly; a cloud Mac (ADR Option C) |

## 1. One-time setup on the Mac (both paths)

1. **Install Xcode** from the Mac App Store. Use the version the Unreal Engine 5.8 release notes
   require (the "Platform SDK Upgrades" section), then open it once to finish installing
   components. Include the iOS platform when it asks.
2. **Install Unreal Engine 5.8** with the Epic Games Launcher. Under the engine's **Options**,
   tick **iOS** under target platforms.
3. **Clone this repo**, and open `play-sports.uproject` once in the editor, so it builds the
   project's C++ for the Mac.
4. **Prepare the iPhone:**
   1. Connect the iPhone by USB-C, unlock it, and tap **Trust** when it asks about this
      computer.
   2. Open Xcode's **Window → Devices and Simulators** and wait until the phone shows as ready.
   3. On the iPhone, open **Settings → Privacy & Security → Developer Mode** and turn it on. The
      phone restarts. The switch appears only after the phone has been connected to Xcode once.

## 2. Choose the bundle ID (both paths, once)

The repo ships the placeholder `com.example.playsports` (`Config/DefaultEngine.ini`, section
`[/Script/IOSRuntimeSettings.IOSRuntimeSettings]`, key `BundleIdentifier`). Apple needs a
bundle ID that no other team has registered, so signing with the placeholder will almost
certainly fail.

1. **Choose your ID:** a reverse domain name you control, for example
   `com.<yourname>.playsports`.
2. **Set it** in **Project Settings → Platforms → iOS → Bundle Identifier**. The editor writes
   it to `Config/DefaultEngine.ini`.
3. **Check Xcode Projects too.** If your 5.8 editor also has **Project Settings → Platforms →
   Xcode Projects** with a bundle identifier or bundle ID prefix, make it match.
4. **Commit the change.** A bundle ID is public (it's inside every copy of the app), so it
   belongs in the repo.

Choose carefully: a free account can create only a few new App IDs a week, and TestFlight ties
the app to this ID for good.

## 3. Path A: free Apple ID (7-day builds)

### Set up signing (once)

1. **Add your Apple ID to Xcode:** **Xcode → Settings → Accounts → +**. Xcode creates a
   "Personal Team" for it.
2. **Tell Unreal to sign automatically with that team.**
   1. Under **Project Settings → Platforms → iOS**, turn on automatic signing.
   2. Set the team: in 5.8 this may be **Platforms → Xcode Projects → Code Signing Team**, or the
      **IOS Team ID** field on the iOS page.
   3. To find the team ID, look up the 10-character team ID in your Apple Development
      certificate: Keychain Access, under My Certificates, "Organizational Unit".
3. **Check the entitlements are off.** The repo already turns off Game Center and push
   notifications, which a personal team can't always sign. Leave them off.

### Build and install (each time)

1. Build and install, either way:
   - **From the editor:** with the phone plugged in, use the toolbar's platform or launch menu,
     choose the iPhone, and launch. Unreal builds, cooks, signs and installs.
   - **Or package, then install:** **Platforms → iOS → Package Project**, then in Xcode's
     **Devices and Simulators** window select the phone and add the `.ipa` under
     **Installed Apps** (the **+** button).
2. **First launch only:** iOS blocks the app as an "Untrusted Developer". Open **Settings →
   General → VPN & Device Management**, select your Apple ID under Developer App, and tap
   **Trust**.

### Every 7 days

The provisioning profile expires and the app stops launching. Rebuild or re-package, and install
**over** the existing app. Don't delete the app first: deleting it deletes its saves.

## 4. Path B: Apple Developer Program (TestFlight)

### Enrol and register the app (once)

1. **Enrol** at developer.apple.com/programs as an individual. Approval can take a day or two.
2. **Add the paid team to Xcode:** **Xcode → Settings → Accounts**. Your Developer Program team
   appears next to the personal one. Choose the paid team in Unreal's signing settings
   (path A, "Set up signing", step 2).
3. **Let Xcode handle certificates and profiles.** With automatic signing, Xcode creates and
   renews them:
   - **Certificates.**
     - Apple Development: development builds that install from the Mac.
     - Apple Distribution: TestFlight and App Store builds.
   - **App ID.** Your bundle ID from step 2, registered to the team.
   - **Devices.** Your iPhone's identifier, registered when the phone is connected. Development
     builds need this; TestFlight builds don't.
   - **Provisioning profiles.**
     - A development profile: the development certificate, the App ID and your devices.
     - An App Store profile: the distribution certificate and the App ID. TestFlight uses it.

   You can see all of these at developer.apple.com → Certificates, Identifiers & Profiles.
4. **Back up the certificates.**
   1. In Keychain Access, under My Certificates, export each Apple certificate *with its private
      key* as a password-protected `.p12`.
   2. Store the files and their passwords in a password manager.
   3. **Never commit them,** and never paste them into an issue, a PR or an agent prompt.
5. **Create the app record.** In App Store Connect, go to Apps → **+** → New App. Choose iOS, a
   name, your bundle ID and any SKU.

### Build and upload (each time)

1. **Bump the build number.** Raise the version in **Project Settings → Platforms → iOS**
   (Version Info) before every upload. App Store Connect rejects a build number it has seen.
2. **Package a Shipping distribution build**, either:
   - from the editor: Shipping configuration, distribution on (packaging settings, "For
     Distribution"); or
   - on the Mac runner: the `iOS Package` workflow with `config: Shipping`, which adds
     `-distribution`.
3. **Upload the `.ipa`** with Apple's **Transporter** app (free on the Mac App Store), or from
   Xcode's Organizer.
4. **Answer the export-compliance question** about encryption. App Store Connect asks it for
   each build until the app answers it in its Info.plist (`ITSAppUsesNonExemptEncryption`,
   through the iOS settings' Additional Plist Data). That answer is a legal declaration: **the
   owner makes it,** not an agent.
5. **Install through TestFlight.**
   1. In TestFlight, add yourself as an internal tester. Internal builds need no App Review.
   2. Install the **TestFlight** app on the iPhone, accept the invite, and install.
   3. Builds expire after 90 days.

## 5. When the Mac becomes the CI runner (ADR Option B)

1. **Register the runner.** In GitHub, open the repo's **Settings → Actions → Runners → New
   self-hosted runner → macOS**. Follow the steps, and add the label **`mac`** (GitHub adds
   `self-hosted` and `macOS` itself).
2. **Run it as your logged-in user**, not as a system daemon. It needs your login keychain,
   which holds the signing certificates, and your Xcode account.
3. **Set the engine path.** Set the runner's environment variable `UE_ROOT` to the engine folder,
   usually `/Users/Shared/Epic Games/UE_5.8`. Put it in the runner directory's `.env` file, the
   same convention as the Windows runner.
4. **Un-gate the workflow.** Set the repository variable **`IOS_MAC_RUNNER`** to `true`
   (**Settings → Secrets and variables → Actions → Variables**). Until it is set, the
   `iOS Package` workflow's job is skipped.
5. **Run it.** **Actions → iOS Package → Run workflow**, choosing Development or Shipping.
6. **Get the build.** Download the `ios-package-<config>` artifact. For a development build,
   install it from the Mac (path A). For a Shipping build, upload it to TestFlight (path B).

The runner security rules in `Specs/ADR_CI_Environment.md` apply to this Mac too:
outside-collaborator runs need approval, and the runner lives outside any repo checkout. The
iOS workflow runs only when someone with write access starts it by hand.

## 6. When it fails

| Symptom | Likely cause | Fix |
|---|---|---|
| "Failed to register bundle identifier" or "not available" | The bundle ID is taken (the `com.example` placeholder always is) | Step 2 |
| "No profiles for '…' were found" or "No signing certificate" | Automatic signing is off, or no team is set | Path A, "Set up signing", step 2; for Path B, choose the paid team |
| "Personal development teams do not support the … capability" | An entitlement a free account can't sign | Turn that capability off in the iOS settings, or use Path B |
| The phone says "Untrusted Developer" | First launch of a free-account build | Path A, "Build and install", step 2 |
| The app opened last week and now won't | The free profile expired after 7 days | Reinstall (path A, "Every 7 days") |
| The phone asks for Developer Mode | Developer Mode is off | Step 1.4.3 |
| App Store Connect rejects the upload as a duplicate | The build number didn't change | Path B, "Build and upload", step 1 |
| The app launches to a black screen | There is no cooked map yet: `Content/` is empty until an editor session makes `/Game/Maps/GameMap` | `Specs/Default_Map_Spec.md` (ADR, "Prerequisites") |
| The build fails compiling `Autonomix` | An editor-only plugin was pulled into the game target | ADR, "Prerequisites", item 2 |
