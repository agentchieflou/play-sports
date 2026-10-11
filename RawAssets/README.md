# RawAssets/

Source assets that are **not** Unreal `.uasset` files: glTF/GLB models, WebP/HDR textures, JSON
manifests and licences, kept in git so an editor session (or the Interchange importer, or a
commandlet) can turn them into `Content/` assets. `Content/` stays the engine's; this folder is the
provenance-tracked input to it.

| Folder | What | Licence |
|---|---|---|
| `stadium/` | the stadium bowl's CC0 concrete (Poly Haven, 2K), fetched by `tools/assets/stadium/fetch_stadium_assets.py` and imported by the content pipeline's `stadium_look` step | CC0, `stadium/concrete/LICENSE` |
| `world/` | the "world kit" brought over from `agentchieflou/this-next-please` on 2026-10-08: CC0 Poly Haven materials, skies and props; a CC0 MakeHuman player character and crowd on the Unreal body skeleton; procedurally grown trees, cars and office furniture; and the browser reference implementation | per subfolder `LICENSE` (CC0, or this repository's MIT) |

Rules:

1. Every subfolder carries a `LICENSE` naming each file's source and author. A file with no line in
   a `LICENSE` is not allowed here.
2. Nothing under `RawAssets/` is loaded at runtime. CI does not read it (`tools/lint_conventions.py`
   scans `Source/` and `Plugins/`; `tools/validate_data.py` scans `Data/`).
3. Keep raw inputs small. The browser pipeline that made these (`tools/assets/world/`) decimated and
   re-encoded everything; full-resolution sources stay outside git and are remade by the scripts.
4. Content that Epic's EULA binds to Unreal Engine (Twinmotion, UE-only Marketplace/Fab content,
   MetaHuman exports) does not go here: it lives in `Content/` after an editor import, or outside git.
