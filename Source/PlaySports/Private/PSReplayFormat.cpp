#include "PSReplayFormat.h"
#include "JsonObjectConverter.h"

FPSReplayRecording UPSReplayFormat::MakeRecording(const FPlayState& InitialPlayState, const TArray<FPlayerAttributes>& OffenseRoster, const TArray<FPlayerAttributes>& DefenseRoster)
{
    FPSReplayRecording Recording;
    Recording.Header.FormatVersion = CurrentFormatVersion;
    Recording.Header.RecordedAtUtc = FDateTime::UtcNow();
    Recording.InitialState.PlayState = InitialPlayState;
    Recording.InitialState.OffenseRoster = OffenseRoster;
    Recording.InitialState.DefenseRoster = DefenseRoster;
    return Recording;
}

FString UPSReplayFormat::SerializeToJson(const FPSReplayRecording& Recording)
{
    FString JsonString;
    // A snapshot's live pawn is Transient: it means nothing outside the run that captured it.
    if (!FJsonObjectConverter::UStructToJsonObjectString(Recording, JsonString, 0, CPF_Transient))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Failed to serialize recording to JSON."));
        return FString();
    }
    return JsonString;
}

bool UPSReplayFormat::DeserializeFromJson(const FString& Json, FPSReplayRecording& OutRecording)
{
    FPSReplayRecording Parsed;
    if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Parsed, 0, CPF_Transient))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Failed to parse replay JSON."));
        return false;
    }

    if (Parsed.Header.FormatVersion <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Rejected unversioned replay document (FormatVersion %d)."), Parsed.Header.FormatVersion);
        return false;
    }

    if (Parsed.Header.FormatVersion > CurrentFormatVersion)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Replay format version %d is newer than supported version %d."), Parsed.Header.FormatVersion, CurrentFormatVersion);
        return false;
    }

    while (Parsed.Header.FormatVersion < CurrentFormatVersion)
    {
        const int32 FromVersion = Parsed.Header.FormatVersion;
        if (!MigrateStep(Parsed, FromVersion) || Parsed.Header.FormatVersion <= FromVersion)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Migration from format version %d failed."), FromVersion);
            return false;
        }
    }

    OutRecording = Parsed;
    return true;
}

bool UPSReplayFormat::MigrateStep(FPSReplayRecording& Recording, int32 FromVersion)
{
    if (FromVersion == 1)
    {
        // A version 1 seed seeded the engine's global stream (the C runtime's rand(), different
        // on every platform). Version 2 seeds the simulation's own stream, so the old seed would
        // re-simulate another game: the recording keeps its events for playback, without a seed.
        Recording.Header.RandomSeed = 0;
        Recording.Header.FormatVersion = 2;
        return true;
    }
    return false;
}
