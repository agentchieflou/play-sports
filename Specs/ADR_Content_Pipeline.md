# ADR: Content Pipeline (Content as Code, Git LFS)

**Status:** Accepted (2026-10-10). The owner chose Git LFS (decision D1 in
`roadmap/MILESTONES_PLATFORMS.md`); this record writes that decision down and says how it works.
**Epic:** 146 (`roadmap/platform-release.md`), stories 146.1 and 146.2.

## Context

`Content/` has been empty all project long. Every story that needs a level, a widget asset or an
imported mesh was an "editor" story, and agent sessions have no editor. Yet the CI runner already
runs the editor headlessly on every pull request (the automation tests), and the editor runs
Python. So most content can be built by scripts that drive the editor on the runner, reviewed as
code like everything else.

Two questions follow: where the binary assets live in Git, and how a script's output gets from
the runner onto a branch.

## Decision

1. **Every `.uasset` and `.umap` is committed through Git LFS**, generated or hand-made alike.
   So are the source binaries that are imported into `Content/` or staged beside it (textures,
   models, audio, video, fonts). `.gitattributes` lists the types.
2. **Content is built by the content pipeline**: Python steps in `tools/content_pipeline/steps/`,
   run inside the editor by `.github/workflows/content.yml` on the self-hosted runner. Each step is
   idempotent: it compares the asset with what it describes, changes only what differs, and saves
   only when something changed.
3. **The pipeline never commits.** Its output is uploaded as a workflow artifact. The branch's
   author (an agent or the owner) commits it through LFS and pushes, and that push gets normal CI.
4. **Build from code wherever that's honest, and generate a binary asset only where the engine
   needs one.** See "What is built from code instead" below.

## `.gitattributes`

`*.uasset` and `*.umap` everywhere, plus `png jpg jpeg tga exr psd fbx wav ogg mp3 mp4 bk2 ttf otf`,
all `filter=lfs diff=lfs merge=lfs -text`. None of those types was committed outside LFS when the
file was added, so no history needed migrating.

`RawAssets/world/`'s `.glb`, `.webp` and `.hdr` files are deliberately not listed. They are
11 MB of browser-sized CC0 sources, already committed as plain Git objects with per-file
provenance. Listing their types would leave every clone with files that "should have been
pointers". The first change that adds a large source binary of one of those types (the 2k Poly
Haven originals, say) adds the type and runs `git add --renormalize RawAssets` in the same
commit.

`*_BuiltData.uasset` stays in `.gitignore`. The generated levels use movable lights only, so they
need no lighting build and have no built data to commit.

LFS file locking (`lockable`) isn't used. Each asset has one writer by construction (see "Hand
edits"), and locking would make generated files read-only in every checkout.

## LFS on the runner and in agent sessions

- **Checkout:** every workflow that loads `Content/` checks out with `actions/checkout@v4` and
  `lfs: true`: `ci.yml` (the editor loads `GameMap` at startup; the tests load it), `content.yml`,
  and the packaging workflow (Epic 145), which cooks it. Without it the editor finds pointer files
  where the levels are.
- **Caching:** the self-hosted runner keeps its workspace, `.git/lfs/objects` included, between
  runs, so a checkout downloads only objects it hasn't seen. That local disk is the cache;
  `actions/cache` would only add an upload and a download through GitHub's cache service. Git for
  Windows bundles Git LFS, so the runner needs nothing installed. `git lfs prune` on the runner is
  safe at any time if the disk fills.
- **Agent sessions** have Git LFS (3.4.1) with smudge skipped, so a clone holds pointer files and
  downloads nothing. That's what code work wants. An agent that commits pipeline output has the
  real files from the artifact; `git add` turns them into pointers. These clones have no
  `pre-push` hook, so push the objects first: `git lfs push origin <branch>`, then `git push`.
- **The owner's machine:** a normal clone with Git LFS installed (`git lfs install` once) gets
  the real files on checkout.

## Storage and bandwidth quota

The repository belongs to a personal account on GitHub Free. GitHub's billing docs (checked
2026-10-10, "Git LFS" under Billing → product billing) give GitHub Free and Pro **10 GiB of LFS
storage and 10 GiB of LFS bandwidth a month**, reset each billing cycle.

- **Bandwidth** is every LFS download: clones and checkouts that fetch objects, including those
  by GitHub Actions, forks pulling from this repository, and source archives that include LFS
  objects. Uploads don't count. Workflow artifacts are not LFS and don't count.
- **Storage** is every object version ever pushed, not just the current one. Regenerating an
  asset whose bytes didn't need to change still stores a new version, which is why the steps save
  only when something changed.
- **At the limit**, with no payment method on file: over storage, clones get pointers and new
  LFS files can't be pushed; over bandwidth, LFS is disabled until the next cycle. With a payment
  method, a $0 budget blocks LFS for the rest of the month instead of billing overage.
- **Expected use:** a generated level is tens of kilobytes. The world kit's import (146.5) is the
  first large addition: a few tens of MB of assets from 11 MB of sources. The runner's cache means
  CI downloads each object about once.
- **How to watch it:** on github.com, Settings → Billing and licensing → Usage, filtered to Git
  LFS, shows storage and bandwidth used this cycle. GitHub emails at 90% and 100% of the included
  usage. The REST endpoint `GET /users/agentchieflou/settings/billing/usage/summary` (public
  preview; agent sessions can't call it, their tokens are scoped to the repository) returns the
  same numbers. `git lfs ls-files --size` lists what the current commit holds.

## The pipeline

```
tools/content_pipeline/
  pipeline.json      the steps: each one's Script, its Inputs (files it reads), its Outputs (assets, or folders it owns whole)
  content.lock.json  written by the pipeline: digests of each step's sources and outputs when last built
  pipeline.py        no editor: the fast drift check, staging the artifact, applying it to a branch
  run_in_editor.py   the editor-side driver
  ue_content.py      helpers the steps share (open a level, make its managed actors match, save)
  steps/*.py         one script per step, each with build(ctx)
```

**The runner.** `content.yml` builds `PlaySportsEditor` (the steps load the project's classes),
then runs:

```
UnrealEditor-Cmd.exe play-sports.uproject -run=pythonscript -script=<repo>/tools/content_pipeline/run_in_editor.py
    -unattended -nullrhi -nosplash -nop4 -log
```

`-run=pythonscript` is the Python Editor Script Plugin's commandlet. It runs the file through
`IPythonScriptPlugin::ExecPythonCommandEx` in file mode, the same call Autonomix's `run_python`
makes (`FAutonomixPython::Run`, Core 25.2). The plugin is enabled by default in the engine; CI run
38070494245 (main, 2026-10-10) shows `PlaySports.Autonomix.McpTools` executing Python in the
headless editor. If it were ever off, the commandlet wouldn't exist and the run would fail at once.
The commandlet loads no level by itself; a step opens the level it owns. Settings travel in
environment variables (`PS_CONTENT_MODE`, `PS_CONTENT_STEPS`, `PS_CONTENT_REPORT`), not
arguments, so nothing depends on the editor's command-line quoting.

**Triggers.** Manual dispatch (either mode, optionally chosen steps); pull requests and pushes
to `main` that change `content.yml`, `tools/content_pipeline/**` or `Content/**`. A step whose
inputs live elsewhere adds them to both path filters (the world kit's `RawAssets/world/**` and `tools/assets/world/**` are there). A code-only
pull request never pays for the extra editor run on the shared runner.

**Modes.**

- **build** (pull requests; manual by default): run the steps, save what changed, update the
  lock, and upload what changed under `Content/` plus the lock as the `generated-content`
  artifact. The run fails while the branch's content is behind its sources ("Content committed?"),
  so a pull request can't look green without the content it generates.
- **check** (pushes to `main`; manual on request): the drift check below.

The editor log and the run's JSON report are uploaded as `content-logs` on every run.

## How pipeline output reaches a branch

A push made with the workflow's `GITHUB_TOKEN` doesn't trigger workflows, so a commit made by the
pipeline would never get CI and the required check would never report. A token that could push
and trigger, on a self-hosted runner serving a public repository, would widen what a workflow
can do on the owner's machine. So the loop is:

1. Change a step's script or inputs; push. `content.yml` runs in build mode and fails with
   "behind its sources", uploading `generated-content`.
2. `gh run download <run-id> -n generated-content -D <empty dir>`, then
   `python tools/content_pipeline/pipeline.py apply <dir>`, which copies the listed files into
   place and deletes what the artifact lists as deleted.
3. `git add Content tools/content_pipeline/content.lock.json` (`.gitattributes` makes the assets
   LFS pointers), commit, `git lfs push origin <branch>`, `git push`.
4. That push runs CI, and `content.yml` again, which must now find nothing to change.

## The drift check

Content drifts when it no longer matches its sources. Two checks catch it:

- **Fast, no editor** (`pipeline.py check`): for each step, the digest of every source (the
  shared helpers, the step's script, its inputs) must equal the lock's, and every output must
  exist with the digest the pipeline recorded when it wrote it. A digest is the file's SHA-256,
  which for an LFS file is its object id, so the check reads pointer files without downloading
  anything. Text is digested with CRLF read as LF, so the Windows runner and a Linux clone agree.
  It names what changed: "sources changed since its content was generated: <files>", "output ...
  is not what the pipeline wrote", "never generated", "missing".
- **Deep, in the editor** (check mode): every step runs without saving and lists what it would
  change. Any difference fails. This catches what digests can't: a class a level references being
  renamed, an engine upgrade changing how an asset loads, or a step that is no longer idempotent.

Both run on every push to `main` that touches content or the pipeline, and on demand.

## Hand edits

Every asset under `Content/` has exactly one writer:

- **Generated** assets are the Outputs of a step. Their truth is the step's script and inputs; to
  change one, change those. Saving a generated asset in the editor is drift (the fast check names
  it), and the next build would put the step's parts back anyway.
- **Hand-made** assets are everything else. The pipeline never writes them.

Within a level, a step owns only the actors it made (tagged `PSContent:<step>:<id>`) and the
settings it names. It leaves every other actor alone, so a level can be handed over without
losing anything. The ways to put hand work into the game:

1. **Change the source.** Most "hand edits" to generated content are tuning (the sun's angle, a
   material colour) and belong in the step or its data.
2. **A hand-made asset beside the generated one.** For example, set dressing in its own level
   that the generated level streams in. The step adds the streaming entry the first time it's
   needed.
3. **Take it over.** Remove the asset from its step's Outputs (or the step itself) in
   `pipeline.json`, in the commit that first saves the hand edit. From then on it's hand-made and
   the pipeline leaves it alone.

Binary files are never merged. A conflict on a generated asset is resolved by taking either side
and rerunning the pipeline on the merged sources. A conflict on a hand-made asset is resolved by
choosing a side.

## What is built from code instead

A binary asset is generated only where the engine needs one:

| Content | How | Why |
|---|---|---|
| The default level (`/Game/Maps/GameMap`) | Generated `.umap` (step `game_map`) | The engine boots into a level asset. It holds only lighting, a player start and its game mode. |
| The field: surface, yard lines, hashes, end zones | Built at runtime from `Data/field_dimensions.json` with engine basic shapes and dynamic material instances (146.3) | It already has one authority (`PSField`); a baked mesh would be a second copy to keep in step. |
| HUD, menus, play-call screen, touch controls | C++ `UUserWidget` classes that build their own widget trees (`RebuildWidget`), as the score bug and the menu screens already do (146.4) | UMG Widget Blueprints are poorly exposed to Python, and their diffs can't be reviewed; a C++ tree is reviewable and testable headlessly. |
| Imported meshes, textures, materials (the world kit) | Generated `.uasset`s imported from `RawAssets/` through Interchange (146.5) | Imported art is inherently binary. |

## Consequences

- An "editor" story that is really content becomes a pipeline step: reviewable code, run on the
  runner, with its output committed through LFS. Human editor sessions remain for what can't be
  scripted: judging how things look, playtests, device work.
- Every workflow that loads content checks out with `lfs: true`. The packaging workflow (Epic
  145) needs it too, or it cooks pointer files.
- A pull request that changes the pipeline takes two pushes (sources, then the generated content)
  and one extra editor run on the shared runner for each. Code-only pull requests are unaffected.
- LFS storage grows with every committed version of every asset. The steps' save-only-on-change
  rule keeps that growth to real changes.
