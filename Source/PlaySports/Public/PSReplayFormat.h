#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PSPlayerAttributes.h"
#include "PSPlaySimulation.h"
#include "PSTelemetrySamplingTypes.h"
#include "PSReplayFormat.generated.h"

USTRUCT(BlueprintType)
struct FPSReplayHeader
{
    GENERATED_BODY()

    // 0 means "unversioned/invalid": only MakeRecording (and future recorders) stamp the
    // real version, so an empty or truncated document can never pass the read gate.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    int32 FormatVersion = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString GameBuildVersion;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FDateTime RecordedAtUtc;

    // Master seed for deterministic re-simulation (Mode 2 in Specs/Determinism_Audit.md): from
    // version 2 (Epic 108), the seed of the simulation's own stream (UPSPlaySimulation::
    // SeedRolls), which every platform draws alike. 0 = no seed: event playback only. Version 1
    // seeds seeded the engine's global stream, the C runtime's rand(), which differs between
    // platforms and isn't drawn any more; they migrate to 0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    int32 RandomSeed = 0;

    // Fixed decision-tick delta used while recording. 0 = variable frame delta
    // (playback-only recording).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float FixedDeltaSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct FPSReplayInitialState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FPlayState PlayState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPlayerAttributes> OffenseRoster;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPlayerAttributes> DefenseRoster;
};

USTRUCT(BlueprintType)
struct FPSReplayEventRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    int32 TickIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    float TimestampSeconds = 0.f;

    // Event type by name (e.g. "Snap", "Tackle"), matching EPSTelemetryEventType entries
    // once Epic C1 merges. Stored as a string so the format never depends on enum integer
    // values and unknown types can be skipped by older players.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString EventType;

    // Typed payload serialized as JSON, same convention as the telemetry bus history.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString PayloadJson;
};

/** A team in a recording, as a viewer labels and colours it (from Data/sample_teams.json). */
USTRUCT(BlueprintType)
struct FPSReplayTeam
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString Abbreviation;

    /** #RRGGBB. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString PrimaryColor;

    /** #RRGGBB. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString SecondaryColor;

    /** The home team; the other is away. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    bool bHome = false;
};

/** A player who appears in a recording's frames, by the PlayerId the frames name him with: who
 *  he is, so a viewer can label him. */
USTRUCT(BlueprintType)
struct FPSReplayParticipant
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FString DisplayName;

    /** His team, one of the recording's Teams. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FName TeamId;

    /** The side he played on in the recording. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    EPSTeamSide TeamSide = EPSTeamSide::Offense;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    EPlayerRole Role = EPlayerRole::Quarterback;

    /** 1-99; 0 when his roster gives him none (FPlayerAttributes::JerseyNumber). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    int32 JerseyNumber = 0;
};

USTRUCT(BlueprintType)
struct FPSReplayRecording
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FPSReplayHeader Header;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    FPSReplayInitialState InitialState;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPSReplayEventRecord> Events;

    /** For state playback (Epic 41): Epic 26's snapshots of the recorded span, in time order,
     *  so a replay shows what happened rather than re-simulating it. A replay clip's event
     *  times (TimestampSeconds) are on these frames' clock, and each event's TickIndex is the
     *  index of the last frame at or before it. Empty in a recording of events alone. Added
     *  without a version bump: an optional field (policy rule 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPSSnapshotFrame> Frames;

    /** For viewers: the teams on the field, home first. Optional, added without a version bump
     *  (policy rule 1); empty in recordings that don't name them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPSReplayTeam> Teams;

    /** For viewers: everyone the Frames name, once each, in the order they first appear.
     *  Optional, added without a version bump (policy rule 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Replay")
    TArray<FPSReplayParticipant> Participants;
};

UCLASS()
class PLAYSPORTS_API UPSReplayFormat : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** 2 (Epic 108): RandomSeed seeds the simulation's own stream, not the global one. */
    static const int32 CurrentFormatVersion = 2;

    UFUNCTION(BlueprintCallable, Category = "Replay")
    static FPSReplayRecording MakeRecording(const FPlayState& InitialPlayState, const TArray<FPlayerAttributes>& OffenseRoster, const TArray<FPlayerAttributes>& DefenseRoster);

    /** The recording as JSON. Transient fields (a snapshot's live pawn) are left out. */
    UFUNCTION(BlueprintCallable, Category = "Replay")
    static FString SerializeToJson(const FPSReplayRecording& Recording);

    // Parses, version-gates, and migrates a recording. Returns false for malformed JSON,
    // unversioned documents (FormatVersion <= 0), versions newer than this build, or a
    // failed migration step. See "Versioning & migration policy" in
    // Specs/Determinism_Audit.md.
    UFUNCTION(BlueprintCallable, Category = "Replay")
    static bool DeserializeFromJson(const FString& Json, FPSReplayRecording& OutRecording);

private:
    // Migrates one step forward from FromVersion, mirroring UPSSaveGame::MigrateFrom.
    // Each supported step must advance Header.FormatVersion; returns false for unknown steps.
    static bool MigrateStep(FPSReplayRecording& Recording, int32 FromVersion);
};
