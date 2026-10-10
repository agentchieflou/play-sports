# Specification: Player emphasis rendering (editor handoff)

**Epic:** 36 (`roadmap/broadcast-overlays.md`, Track A). **Status:** code done. The post-process
material that draws the looks, and tuning it against night lighting, are editor work (the epic's
last story).

## What exists in code

- **`UPSOverlayEmphasisSubsystem`** is a world subsystem. Commentary, replays and coaching tips
  ask it to emphasize a player:
  - `Emphasize(Pawn, Kind, Source, Seconds)` with Kind `Highlight`, `Mismatch` or `Focus`.
  - `Spotlight(Pawn, Source, Seconds)`: a Focus that dims everyone else, for isolation replays.
  - `ClearEmphasis(Handle)`, `ClearSource(Source)` and `ClearAll()`.
- **Weighing requests.**
  - Of several requests on one player, the highest priority wins.
  - Timed requests run out.
  - At most `MaxEmphasized` players are drawn at once.
- **How a look is drawn.** For each player's look, the subsystem marks his meshes for the
  custom-depth pass (`SetRenderCustomDepth`) with a stencil value
  (`SetCustomDepthStencilValue`). It unmarks them when the look ends.
- **Default stencils** (`Data/player_emphasis.json`):

  | Look | Stencil |
  |---|---|
  | Highlight | 1 |
  | Mismatch | 2 |
  | Focus | 3 |
  | Dimmed (another player in the spotlight) | 4 |

- **Config.** `Config/DefaultEngine.ini` sets `r.CustomDepth=3`, the custom depth-stencil pass
  with stencil.
- **Tiers.** A tier whose `OverlayDetail` is `Minimal` draws nothing. The requests are kept.

`PlaySports.Overlay.EmphasisStyleValidates` and `PlaySports.Overlay.EmphasisMarksPlayers` cover
the logic headlessly:
- the stencils marked and unmarked;
- priority, expiry and the budget;
- the spotlight and its dimming;
- the tiers.

Nothing draws the looks yet.

## What the editor session does

1. **Material.** Author `M_PSEmphasis`, a post-process material at Before Tonemapping, and an
   instance, `MI_PSEmphasis`. It reads `CustomStencil` and `CustomDepth`:
   - **Stencils 1-3:** an outline (edge-detect the stencil against its neighbors) and a soft
     glow in a color per look.
     - Highlight: team-neutral white.
     - Mismatch: amber.
     - Focus: bright.
   - **Stencil 4:** darken and desaturate the player's pixels.
   - The outline shows through occluders (custom depth versus scene depth), so a focused player
     behind a pile still reads.
2. **Where it goes.** Add it to the game's post-process: the broadcast camera's settings, or an
   unbound post-process volume in the game map. It costs nothing while no mesh is marked.
3. **Night lighting.** Tune the glow and the outline in day and night lighting (Epic 144), so
   emphasis reads without blowing out. Keep the colors in the material instance, not in code.
4. **Check in PIE.** Call the subsystem from the console or a test level:
   - `Highlight` on a receiver;
   - `Mismatch` on him and his defender;
   - a `Spotlight` on the ball carrier.

   Confirm each look and that the dimming leaves the focused player bright. If the stencil
   values must change, change them in `Data/player_emphasis.json` and the material together.

## Done when

- The three looks and the dimming read clearly in day and night lighting from the broadcast
  camera.
- The material is in the game's post-process.
- The epic's last story is ticked.
