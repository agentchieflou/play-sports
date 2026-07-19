# Save System Architecture (Epic 116)

One versioned save architecture for everything the game persists — designed before Track G
(franchise), Track B (replays), and Track I (settings/profiles) each invent their own.

## Categories & slot naming

`EPSSaveCategory` partitions saves; slot names are `<Category>_<Id>`:

| Category | Slot examples | Contents (owning track) |
|---|---|---|
| `Profile` | `Profile_Default` | Player settings, control prefs (Track I) |
| `Franchise` | `Franchise_1` | Season/league state (Core 20, Track G) |
| `Replay` | `Replay_<timestamp>` | Recorded plays (Epic 41/115) |

Files live at `Saved/SaveGames/<Slot>.psav`. `UPSSaveSubsystem::MakeSlotName` builds names;
never hand-format them.

## File format

```
uint32 Magic        'PSAV' (0x50534156)
int32  FormatVersion  container format, currently 1 (headers, not payload schema)
uint32 PayloadCrc     CRC32 of the payload bytes
int32  PayloadSize
uint8  Payload[]      UE USaveGame serialization (SaveGameToMemory)
```

Corruption detection = magic + size + CRC check on load. **Backup-on-write**: before
overwriting a slot, the existing file is copied to `<Slot>.psav.bak`; a corrupt primary falls
back to the backup automatically.

## Versioning & migration

`UPSSaveGame` (base for all save objects) carries `SaveVersion`. On load, if the stored
version is older than `UPSSaveGame::CurrentSaveVersion`, the subsystem runs
`MigrateFrom(FromVersion)` once per step until current. Subclasses override it, migrating one
version at a time; returning false marks the save unloadable (surfaced as a failed load, never
a crash). Old saves must load forever — the migration test harness in
`PSSaveSystemTests.cpp` guards the chain.

**Policy:** bump `CurrentSaveVersion` in the same PR that changes any persisted field, with a
matching `MigrateFrom` step and a test.

## Async model

Threading: UObject (de)serialization happens **only on the game thread**; file I/O happens on
the thread pool. `SaveToSlotAsync`/`LoadFromSlotAsync` split the work accordingly and invoke
their completion delegate (`FPSSaveOpComplete(SlotName, bSuccess)`) back on the game thread.
Track I binds UI states (spinner, error toast) to those delegates — the save side is complete;
widget wiring is Track I scope.

## Usage

```cpp
UPSSaveSubsystem* Saves = GameInstance->GetSubsystem<UPSSaveSubsystem>();
FString Slot = UPSSaveSubsystem::MakeSlotName(EPSSaveCategory::Franchise, TEXT("1"));
Saves->SaveToSlotAsync(MySave, Slot, OnSaved);   // or SaveToSlot for sync
```
