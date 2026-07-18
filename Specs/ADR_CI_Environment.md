# ADR: CI Environment for Unreal Engine Builds

**Status:** Accepted (2026-07-18)
**Epic:** 112 (`roadmap/engineering-infra.md`)

## Context

Agent-written C++ was merging with zero compilation — no UE toolchain existed in any agent
environment, and static review missed real defects (this epic's prerequisite commit fixes three
scaffold-breaking ones: missing `*.Target.cs` files, missing `IMPLEMENT_MODULE` macros, missing
`FunctionalTesting` dependency). Every PR needs a real compile gate.

## Options considered

1. **GitHub-hosted runners + Epic's official UE container images** (`ghcr.io/epicgames/…`).
   Free minutes (public repo), always-on, no local footprint. Costs: one-time Epic↔GitHub
   account linking + PAT secret; multi-GB image pulls per run unless cached; hosted-runner disk
   is tight for UE images (mitigable with cleanup actions but fragile); Linux toolchain, while
   the project's dev environment is Windows.
2. **Self-hosted runner on the dev machine with a locally installed engine.** Fast incremental
   builds (persistent workspace), Windows toolchain matching dev reality, zero image pulls, and
   the same install powers the human editor sessions the `Specs/` backlog needs anyway. Costs:
   CI is down when the PC is off; public-repo security posture must be handled (below).
3. **Cloud VM with UE preinstalled.** Always-on and fast, but continuous cost with no offsetting
   benefit at this project's scale.

## Decision

**Option 2 — self-hosted runner on the dev machine, Unreal Engine 5.8** (installed via the Epic
Games Launcher; the same decision moved the project's `EngineAssociation` from 5.3 to 5.8).
Runner labels: `self-hosted, windows, unreal`. Engine path exposed to workflows as the runner
environment variable `UE_ROOT` — workflows must not hardcode engine paths.

**Runner machine prerequisites** (discovered empirically — a fresh machine needs all of these):
Unreal Engine 5.8 via Epic Launcher; Visual Studio 2022 with the MSVC v143 C++ toolset, a
Windows 10/11 SDK, **and the ".NET Framework 4.8 SDK" component** (`Microsoft.Net.Component.4.8.SDK`
— its absence fails UBT rules compilation with `Could not find NetFxSDK install dir` via
SwarmInterface).

Option 1 remains the documented upgrade path if CI availability (PC-off gaps) becomes a real
constraint; nothing in the workflow design precludes adding a container lane later.

## Public-repo security posture (required reading)

A self-hosted runner on a public repo executes workflow code on this PC. Mitigations in force:

- Repo Actions settings require **approval for all outside-collaborator workflow runs** — fork
  PRs never auto-run. Agent PRs come from same-repo branches and run without approval.
- The runner runs as a normal user account, in `C:\actions-runner`, outside any repo checkout.
- Secrets are not exposed to fork-triggered runs (GitHub default).

## Consequences

- `AGENTS.md`'s verification rules change: agents may (and should) cite CI results
  (`gh pr checks`, `gh run view --log-failed`) as build evidence. The no-unverified-claims rule
  still applies to anything CI didn't run (PIE behavior, editor work, perf).
- `git-steward` merges require CI green on the current head; `main` branch protection enforces
  the compile check server-side.
- The test job runs the automation harness even while zero project tests match — Epic 24 fills
  the suite; the harness existing first is deliberate.
