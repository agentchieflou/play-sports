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
    if (!FJsonObjectConverter::UStructToJsonObjectString(Recording, JsonString))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSReplayFormat: Failed to serialize recording to JSON."));
        return FString();
    }
    return JsonString;
}

bool UPSReplayFormat::DeserializeFromJson(const FString& Json, FPSReplayRecording& OutRecording)
{
    FPSReplayRecording Parsed;
    if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Parsed, 0, 0))
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
    switch (FromVersion)
    {
    // Future breaking changes add one case per step, e.g.:
    // case 1: /* transform v1 -> v2 */ Recording.Header.FormatVersion = 2; return true;
    default:
        return false;
    }
}
