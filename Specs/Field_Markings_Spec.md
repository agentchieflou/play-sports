# Specification: Field Markings and Materials

The field's markings are drawn by `APSFieldSurface` (Epic 146.3). It builds them at runtime from
engine basic shapes and dynamic material instances, with no level or mesh asset of its own. Its
grass and paint are two materials that the content pipeline's `field_look` step generates from CC0
textures (lane V2, below).

- **Where each mark goes** is the field's frame (`Specs/Field_Geometry_Spec.md`, `PSField`,
  `Data/field_dimensions.json`). So the lines are always where the game spots the ball.
- **How the marks look** is `Data/field_markings.json` (`FPSFieldMarkingsStyle`, schema in
  `Data/README.md`).

Lengths in the style are yards of the frame, so the markings keep their proportions at any scale.
Coordinates below are at the game's scale (100 cm/yd), with the near goal line at X = 0.

## The pieces, bottom to top

Each layer sits `LayerLiftCm` above the one below, so no two fight for the same depth.

1. **Ground** (`SurroundColor`): a box whose top is Z = 0, from the out-of-bounds depth behind one
   end line to the same beyond the other, and the same depth past each sideline. It is the only
   piece with collision: the ball lands on it.
2. **Field** (`FieldColor`): end line to end line, sideline to sideline.
3. **End zones**: the near one (`NearEndZoneColor`) from X = −1,000 to the near goal line, the far
   one (`FarEndZoneColor`) from the far goal line to X = 11,000. These are exactly where
   `APSFieldGrid`'s end-zone volumes score. The colours are by end, not by team: the frame turns
   with possession, so the near end zone is always the offense's own.
4. **Lines** (`LineColor`, all `LineWidthYards` wide, 4 in by default), drawn as one instanced mesh:
   - **Sidelines** on Y = ±2,666.67, along the whole field, end zones included.
   - **End lines** on X = −1,000 and X = 11,000, sideline to sideline.
   - **Yard lines** every `YardLineSpacingYards` (5) from the near goal line, sideline to sideline:
     X = 0, 500, …, 10,000, which includes both goal lines (21 lines).
   - **Hash marks**: a pair every `HashSpacingYards` (1) between the goal lines, except where a
     yard line already crosses (80 pairs). Each is `HashLengthYards` (2 ft) across the field and
     centred `HashOffsetYards` (3.0833 yd, so the rows are 18 ft 6 in apart, NFL) from the middle:
     Y = ±308.33.

## The paint around and on the lines (lane V2)

Drawn in the same frame, each kind as one instanced mesh:

- **The border** (`BorderWidthYards`, 6 ft): a white band outside each sideline, reaching past the
  corners, and one outside each end line. It is drawn under the lines.
- **The numerals**: 10, 20, 30, 40, 50, 40, 30, 20, 10, on both sides of the field.
  - Each counts to the nearer goal line, and its two digits sit either side of its yard line, 1 ft
    from it.
  - Digits are 4 ft × 6 ft, block strokes 1 ft wide (`APSFieldSurface::GetDigitStrokes`, a
    seven-segment face). Bottoms are 7 yards in from the sideline and tops 9 (NFL), pointing to
    the middle of the field.
  - So from the broadcast side (−Y) the near numerals read upright and the far ones upside down,
    as on television.
- **The arrows**: beside every numeral but the 50, pointing to the goal line it counts to, 36 in by
  18 in. The paint material cuts each triangle in world space from per-instance data: direction,
  centre, length and base.

## The look (lane V2)

- **Sources:** ambientCG's CC0 "Grass 005" at 2K (`RawAssets/field/turf/`), fetched with a pinned
  checksum by `tools/assets/field/fetch_field_assets.py`. The content step `field_look` imports it
  into `/Game/Field`.
- **The turf** (`M_Turf`) is world-aligned:
  - the grass is sampled twice, at `TileSizeCm` and 3.3 times larger, to hide tiling;
  - its colour is divided by its own mean, so `FieldColor` is the field's average colour;
  - a macro variation and drier patches come from a generated noise map;
  - mowing stripes change at every yard line, stronger as the camera looks along the field;
  - `MPC_Field`'s `Dew` makes it glossier (the night preset sets it).
- **`M_TurfLite`** is the turf with one sample, for the `MobileLow` tier (`TierLooks`).
- **The paint** (`M_FieldPaint`) is masked painted grass:
  - the paint's colour carries the grass's own light and dark (`PaintTextureStrength`);
  - a noise with an even histogram leaves `1 - Coverage` of the pixels unpainted, showing the grass
    plane below;
  - `ShapeTriangle` cuts the arrows.
- **The flat `MaterialPath`** stands in for either material in a checkout without the content.

## Not yet drawn

- The midfield logo and end-zone lettering (they need a team-identity source and decals).
- Wear between the hashes, divots and paint scuffs (Epic 51).

## Verification

- `PlaySports.Field.MarkingsData`: the data file loads, equals the struct's defaults and
  validates.
- `PlaySports.Field.MarkingsLayout`: every mark above is where the frame says, at both scales.
- `PlaySports.Field.SurfaceBuilds`: the grid spawns the surface once, and it builds in a world
  (one instance per line, the ground at Z = 0 with collision).
- `PlaySports.Field.PaintLayout`: the border, the digits' strokes, every numeral's place and
  reading direction, the arrows' directions, and the per-tier turf.
- `PlaySports.Content.FieldLook`: the generated materials, collection and skies are real assets
  with the parameters the code sets.
- How it looks is judged on lane V1's GPU renders (`.github/workflows/render.yml`), not by a test.
