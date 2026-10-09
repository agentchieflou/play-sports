# Specification: Character Customization (looks, not categories)

Imported 2026-10-08 from the browser world's character picker ("Who are you in the world?") in
`agentchieflou/this-next-please`. It satisfies the operator's standing requirement, given with a
picture of a friendly cartoon figure in a light-grey shirt and navy trousers, one arm out, presenting:
*"Use some one like this as the main character. Create diverse character options so everyone feels
included."* This spec is the contract for Track R Epic 143 and the input to Track D (Epics 57, 58)
and Epic 22. Mode: mixed (the data and the C++ side are code; the morphs and grooms are editor).

## 1. Principle

**No option or look is named for a gender.** Every row is a choice a person makes about how they
appear, never a box they are put in. A colour is a swatch named by its place in the row ("skin 3 of
8"), never by a word for a person's colour. A body's figure is offered as a *shape*, never as a sex.

## 2. The rows (in the order a person thinks about themselves)

| Row | Values |
|---|---|
| skin | 8 tones, deep to light, warm and neutral (a creator that offered three would tell most people it was not made for them) |
| hair | side part, curls, coils (their own shapes, never one "afro"), long, bun, locs, buzz cut, none, headscarf (a hijab-like scarf with a drape over the neck), wrap (a gele or turban, tied high) |
| hair colour | black, dark brown, brown, auburn, blond, silver (age is part of who people are), blue |
| scarf or wrap colour | its own colour, when the hair is covered |
| face | none, beard, moustache |
| glasses | none, round, square |
| shirt, trousers | 8 and 5 colours (for play-sports: team uniform replaces this on the field; the row survives for the lobby, sidelines and franchise screens) |
| figure | angular, between, curved (a morph of the realistic body) |
| build | slim, regular, broad (a morph; for play-sports this row is *derived* from `WeightKg`/`HeightCm` and role per Epic 58, and editable only off-field) |
| age | young, middle, older (a morph) |
| moves by | walking, or a wheelchair whose wheels turn as it rolls and whose rims the arms push (off-field: lobby, sidelines, coaching staff, fans, broadcast crew) |

## 3. The ten starting looks

Ten complete looks to start from, named by what they look like, never by a name that implies a
gender: *side part* (the operator's picture), *curls*, *headscarf*, *locs*, *silver bun*, *beard*,
*wheelchair*, *long hair*, *wrap*, *bald*. Between them they span every skin tone, every hair
texture, both coverings, both kinds of glasses, a beard and a moustache, all three builds and a
wheelchair user. Each is only a starting point: every option can be changed after.

## 4. Persistence and normalisation

- A look is a small record (one value per row). Persist it per player profile (Epic 117's save
  system; in the browser it was `localStorage`, never the server). `?who=0..9` chose a starting look.
- **Normalise on read**: a look from anywhere (storage, a preset, a click, an older version) is
  reduced to known values so a stored look can never build something broken. In UE this is a
  `USTRUCT(BlueprintType) FPSLookRow`-style record validated by a `Normalize()` that clamps every
  field to its enum/range, and a DataTable of looks (rule 4: tuning lives in DataTables).

## 5. Assets that exist for this

`RawAssets/world/people/standin.glb` (CC0, MakeHuman base mesh with six hair styles, beards, glasses
and five morph targets: angular, curved, older, slim, broad, on the Unreal body bone names) and
`people.json`, which maps each row value to a style or a set of morph weights
(`"figure": {"angular": {"angular": 1}, "between": {}, "curved": {"curved": 1}}` …). The hair styles
no file had (locs, headscarf, wrap) were drawn procedurally and skinned to the head. The tints came
from one normalised detail map per part, dyed at runtime.

In Unreal the intended source is **MetaHuman** (operator decision 2026-10-05: MetaHumans for the
player and for the agents; the CC0 stand-in stays as the fallback when no MetaHuman is present):
create a diverse set, none based on a real person, with hair as cards at the crowd LOD, and the
stand-in's `people.json` → DataTable mapping becomes the MetaHuman preset mapping.

## 6. Acceptance (Epic 143)

- A `UPSCharacterLookComponent` (new class, rule 1) holds the look record, applies skin tint, hair
  style/colour, face, glasses, figure/build/age morphs and the wheelchair flag to a skeletal mesh,
  and publishes `LookChanged` on `UPSTelemetryBus` (rule 5).
- Looks and rows come from DataTables (`FPSLookRow`, `FPSLookPresetRow`), none hardcoded (rule 4).
- Headless automation test: every one of the ten presets normalises to itself; a corrupt record
  normalises to a valid look; the ten presets between them cover all 8 skin tones, every hair style,
  both coverings, all three builds and the wheelchair (rule 2).
- No string in the rows, presets or UI names a gender (a test greps the DataTable export).
