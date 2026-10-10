# Specification: Field Markings and Materials

The field's markings are drawn by `APSFieldSurface` (Epic 146.3). It builds them at runtime from
engine basic shapes and dynamic material instances, with no level, texture or mesh asset of its own.

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

## Not yet drawn

- Yard-line numbers, directional arrows, the midfield logo and end-zone lettering. They need text
  or decals, which arrive with the world kit's materials (Epic 147.1).
- Grass texture and lighting response. The flat colours are placeholder art. A textured material
  replaces `MaterialPath` once the world kit's grass is imported (146.5). It needs a vector
  parameter named `ColorParameter` to be tinted.

## Verification

- `PlaySports.Field.MarkingsData`: the data file loads, equals the struct's defaults and
  validates.
- `PlaySports.Field.MarkingsLayout`: every mark above is where the frame says, at both scales.
- `PlaySports.Field.SurfaceBuilds`: the grid spawns the surface once, and it builds in a world
  (one instance per line, the ground at Z = 0 with collision).
- Still a human check: how it looks in PIE or a packaged build.
