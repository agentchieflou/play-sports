#include "PSDataIngestion.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"

bool UPSDataIngestion::LoadPlayerAttributesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable)
{
    if (!TargetDataTable)
    {
        return false;
    }

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!ParsedJson->TryGetArrayField(TEXT("Players"), Rows))
    {
        return false;
    }

    TargetDataTable->EmptyTable();
    for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
    {
        FPlayerAttributes Player;
        if (FJsonObjectConverter::JsonObjectToUStruct(RowValue->AsObject().ToSharedRef(), &Player, 0, 0))
        {
            FName RowName = Player.PlayerId.IsNone() ? FName(*Player.DisplayName) : Player.PlayerId;
            TargetDataTable->AddRow(RowName, Player);
        }
    }

    return true;
}

bool UPSDataIngestion::LoadTeamsFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable)
{
    if (!TargetDataTable)
    {
        return false;
    }

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!ParsedJson->TryGetArrayField(TEXT("Teams"), Rows))
    {
        return false;
    }

    TargetDataTable->EmptyTable();
    for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
    {
        FPSTeamInfo Team;
        if (FJsonObjectConverter::JsonObjectToUStruct(RowValue->AsObject().ToSharedRef(), &Team, 0, 0))
        {
            FName RowName = Team.TeamId.IsNone() ? FName(*Team.DisplayName) : Team.TeamId;
            TargetDataTable->AddRow(RowName, Team);
        }
    }

    return true;
}

bool UPSDataIngestion::LoadLeagueConfigFromJson(const FString& JsonFilePath, FPSLeagueConfig& OutConfig)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutConfig, 0, 0);
}

bool UPSDataIngestion::LoadArchetypeTuningFromJson(const FString& JsonFilePath, FPSArchetypeTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadInputCatalogFromJson(const FString& JsonFilePath, FPSInputCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadInputTuningFromJson(const FString& JsonFilePath, FInputTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadMenuCatalogFromJson(const FString& JsonFilePath, FPSMenuCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadLoadingTipsFromJson(const FString& JsonFilePath, FPSLoadingTipCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadForceFeedbackTuningFromJson(const FString& JsonFilePath, FPSForceFeedbackTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadInputGlyphsFromJson(const FString& JsonFilePath, FPSInputGlyphCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadPlayCallTuningFromJson(const FString& JsonFilePath, FPlayCallTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDefensiveAdjustmentsFromJson(const FString& JsonFilePath, FPSDefensiveAdjustmentCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadSkillPlayerAITuningFromJson(const FString& JsonFilePath, FSkillPlayerAITuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDefenderAITuningFromJson(const FString& JsonFilePath, FDefenderAITuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPassingInputTuningFromJson(const FString& JsonFilePath, FPassingInputTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPlatformTiersFromJson(const FString& JsonFilePath, FPSPlatformTierCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadPreSnapTuningFromJson(const FString& JsonFilePath, FPreSnapTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadRouteRunningTuningFromJson(const FString& JsonFilePath, FRouteRunningTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadBlownCoverageTuningFromJson(const FString& JsonFilePath, FBlownCoverageTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadCoverageMatchupTuningFromJson(const FString& JsonFilePath, FPSCoverageMatchupTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDeceptionTuningFromJson(const FString& JsonFilePath, FPSDeceptionTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPlayRecognitionTuningFromJson(const FString& JsonFilePath, FPSPlayRecognitionTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadLooseBallTuningFromJson(const FString& JsonFilePath, FPSLooseBallTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPocketTuningFromJson(const FString& JsonFilePath, FPocketTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadCarrierMovesFromJson(const FString& JsonFilePath, FPSCarrierMoveCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadInputBufferTuningFromJson(const FString& JsonFilePath, FInputBufferTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadRushMovesFromJson(const FString& JsonFilePath, FPSRushMoveCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadSituationalTuningFromJson(const FString& JsonFilePath, FPSSituationalTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadTelemetrySamplingTuningFromJson(const FString& JsonFilePath, FPSTelemetrySamplingTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadSessionTelemetryTuningFromJson(const FString& JsonFilePath, FPSSessionTelemetryTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDefensivePreSnapTuningFromJson(const FString& JsonFilePath, FPSDefensivePreSnapTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadRunFitsFromJson(const FString& JsonFilePath, FPSRunFitCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadAll22CameraTuningFromJson(const FString& JsonFilePath, FPSAll22CameraTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDefensiveTechniquesFromJson(const FString& JsonFilePath, FDefensiveTechniqueTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadKickMeterTuningFromJson(const FString& JsonFilePath, FKickMeterTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadSettingsCatalogFromJson(const FString& JsonFilePath, FPSSettingsCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadCameraDirectorTuningFromJson(const FString& JsonFilePath, FPSCameraDirectorTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadOverlayReticleStyleFromJson(const FString& JsonFilePath, FPSOverlayReticleStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadControlHandoffTuningFromJson(const FString& JsonFilePath, FControlHandoffTuningRow& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadVersusRulesFromJson(const FString& JsonFilePath, FPSVersusRules& OutRules)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutRules, 0, 0);
}

bool UPSDataIngestion::LoadPerfHarnessTuningFromJson(const FString& JsonFilePath, FPSPerfHarnessTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadBroadcastOverlayThemeFromJson(const FString& JsonFilePath, FPSBroadcastOverlayTheme& OutTheme)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTheme, 0, 0);
}

bool UPSDataIngestion::LoadSkycamTuningFromJson(const FString& JsonFilePath, FPSSkycamTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPersonnelCatalogFromJson(const FString& JsonFilePath, FPSPersonnelCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadPlayerDNACatalogFromJson(const FString& JsonFilePath, FPSPlayerDNACatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadPhotoModeTuningFromJson(const FString& JsonFilePath, FPSPhotoModeTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadContractTuningFromJson(const FString& JsonFilePath, FPSContractTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadEconomyTuningFromJson(const FString& JsonFilePath, FPSEconomyTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadMoraleTuningFromJson(const FString& JsonFilePath, FPSMoraleTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadHighlightTuningFromJson(const FString& JsonFilePath, FPSHighlightTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadTrainingTuningFromJson(const FString& JsonFilePath, FPSTrainingTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadGameIntelligenceTuningFromJson(const FString& JsonFilePath, FPSGameIntelligenceTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadFieldDimensionsFromJson(const FString& JsonFilePath, FPSFieldDimensions& OutDimensions)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutDimensions, 0, 0);
}

bool UPSDataIngestion::LoadPenaltyTuningFromJson(const FString& JsonFilePath, FPSPenaltyTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadFormationCatalogFromJson(const FString& JsonFilePath, FPSFormationCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadNarrativeTuningFromJson(const FString& JsonFilePath, FPSNarrativeTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    // The arrays replace the struct's defaults rather than add to them.
    OutTuning.StorylineKinds.Reset();
    OutTuning.OffenseScoring.Reset();
    OutTuning.DefenseScoring.Reset();
    OutTuning.BallotPoints.Reset();
    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

namespace PSDataIngestionAudio
{
    /** JsonFilePath's top-level object, or null. */
    TSharedPtr<FJsonObject> ReadObject(const FString& JsonFilePath)
    {
        FString JsonString;
        if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
        {
            return nullptr;
        }
        TSharedPtr<FJsonObject> ParsedJson;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
        if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
        {
            return nullptr;
        }
        return ParsedJson;
    }
}

bool UPSDataIngestion::LoadAudioTuningFromJson(const FString& JsonFilePath, FPSAudioTuning& OutTuning)
{
    const TSharedPtr<FJsonObject> ParsedJson = PSDataIngestionAudio::ReadObject(JsonFilePath);
    if (!ParsedJson.IsValid())
    {
        return false;
    }
    // The arrays replace the struct's rather than add to them.
    OutTuning.Cues.Reset();
    OutTuning.EventCues.Reset();
    OutTuning.LayerSettings.Reset();
    OutTuning.StartupLoops.Reset();
    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadCrowdTuningFromJson(const FString& JsonFilePath, FPSCrowdTuning& OutTuning)
{
    const TSharedPtr<FJsonObject> ParsedJson = PSDataIngestionAudio::ReadObject(JsonFilePath);
    if (!ParsedJson.IsValid())
    {
        return false;
    }
    OutTuning.Levels.Reset();
    OutTuning.CrowdReactions.Reset();
    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadCommentaryHookTuningFromJson(const FString& JsonFilePath, FPSCommentaryHookTuning& OutTuning)
{
    const TSharedPtr<FJsonObject> ParsedJson = PSDataIngestionAudio::ReadObject(JsonFilePath);
    if (!ParsedJson.IsValid())
    {
        return false;
    }
    OutTuning.ModelMoments.Reset();
    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadSessionMatchmakingFromJson(const FString& JsonFilePath, FPSSessionMatchmakingTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadCommentaryLibraryFromJson(const FString& JsonFilePath, FPSCommentaryLibrary& OutLibrary)
{
    const TSharedPtr<FJsonObject> ParsedJson = PSDataIngestionAudio::ReadObject(JsonFilePath);
    if (!ParsedJson.IsValid())
    {
        return false;
    }
    OutLibrary.Lines.Reset();
    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutLibrary, 0, 0);
}

bool UPSDataIngestion::LoadTelestratorTuningFromJson(const FString& JsonFilePath, FPSTelestratorTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadDraftTuningFromJson(const FString& JsonFilePath, FPSDraftTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadTradeTuningFromJson(const FString& JsonFilePath, FPSTradeTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadLegacyTuningFromJson(const FString& JsonFilePath, FPSLegacyTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadReplayTuningFromJson(const FString& JsonFilePath, FPSReplayTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadOpponentModelTuningFromJson(const FString& JsonFilePath, FPSOpponentModelTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadAIDebugTuningFromJson(const FString& JsonFilePath, FPSAIDebugTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadAIScenariosFromJson(const FString& JsonFilePath, FPSAIScenarioCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadDifficultyCatalogFromJson(const FString& JsonFilePath, FPSDifficultyCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadUIAccessibilityTuningFromJson(const FString& JsonFilePath, FPSUIAccessibilityTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadSpecialTeamsTuningFromJson(const FString& JsonFilePath, FPSSpecialTeamsTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadBallFlightStyleFromJson(const FString& JsonFilePath, FPSBallFlightStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadTouchLayoutFromJson(const FString& JsonFilePath, FPSTouchLayout& OutLayout)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutLayout, 0, 0);
}

bool UPSDataIngestion::LoadTouchHudStyleFromJson(const FString& JsonFilePath, FPSTouchHudStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadCoachingLeagueFromJson(const FString& JsonFilePath, FPSCoachingLeague& OutLeague)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutLeague, 0, 0);
}

bool UPSDataIngestion::LoadOverlayBadgeStyleFromJson(const FString& JsonFilePath, FPSOverlayBadgeStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadEmphasisStyleFromJson(const FString& JsonFilePath, FPSEmphasisStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadHintCatalogFromJson(const FString& JsonFilePath, FPSHintCatalog& OutCatalog)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutCatalog, 0, 0);
}

bool UPSDataIngestion::LoadPersonnelPanelStyleFromJson(const FString& JsonFilePath, FPSPersonnelPanelStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadGapOverlayStyleFromJson(const FString& JsonFilePath, FPSGapOverlayStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::LoadLeagueGeneratorTuningFromJson(const FString& JsonFilePath, FPSLeagueGeneratorTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPlaybookGeneratorTuningFromJson(const FString& JsonFilePath, FPSPlaybookGeneratorTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadProgressionTuningFromJson(const FString& JsonFilePath, FPSProgressionTuning& OutTuning)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutTuning, 0, 0);
}

bool UPSDataIngestion::LoadPlayArtStyleFromJson(const FString& JsonFilePath, FPSPlayArtStyle& OutStyle)
{
    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        return false;
    }

    return FJsonObjectConverter::JsonObjectToUStruct(ParsedJson.ToSharedRef(), &OutStyle, 0, 0);
}

bool UPSDataIngestion::IsValidPlayerRoleString(const FString& RoleString)
{
    const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
    return RoleEnum && RoleEnum->GetIndexByNameString(RoleString) != INDEX_NONE;
}

bool UPSDataIngestion::ValidatePlayersJson(const FString& JsonFilePath, TArray<FString>& OutErrors)
{
    OutErrors.Empty();

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        OutErrors.Add(FString::Printf(TEXT("Could not read file: %s"), *JsonFilePath));
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        OutErrors.Add(TEXT("File is not valid JSON."));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!ParsedJson->TryGetArrayField(TEXT("Players"), Rows))
    {
        OutErrors.Add(TEXT("Missing top-level \"Players\" array."));
        return false;
    }

    for (int32 RowIndex = 0; RowIndex < Rows->Num(); ++RowIndex)
    {
        const TSharedPtr<FJsonObject>* RowObject;
        if (!(*Rows)[RowIndex]->TryGetObject(RowObject))
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: is not a JSON object."), RowIndex));
            continue;
        }

        FString PlayerId;
        if (!(*RowObject)->TryGetStringField(TEXT("PlayerId"), PlayerId) || PlayerId.IsEmpty())
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: missing or empty \"PlayerId\"."), RowIndex));
        }

        FString RoleString;
        if (!(*RowObject)->TryGetStringField(TEXT("Role"), RoleString))
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: missing \"Role\" field."), RowIndex));
        }
        else if (!IsValidPlayerRoleString(RoleString))
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: \"Role\" value \"%s\" is not a recognized EPlayerRole."), RowIndex, *RoleString));
        }

        static const TArray<FString> NumericFields = { TEXT("WeightKg"), TEXT("HeightCm"), TEXT("Speed"), TEXT("Agility"), TEXT("Strength"), TEXT("Acceleration"), TEXT("Awareness"), TEXT("Stamina"), TEXT("Age") };
        for (const FString& Field : NumericFields)
        {
            double Value = 0.0;
            if ((*RowObject)->TryGetNumberField(Field, Value) && Value < 0.0)
            {
                OutErrors.Add(FString::Printf(TEXT("Row %d: \"%s\" is negative (%f)."), RowIndex, *Field, Value));
            }
        }
    }

    return OutErrors.Num() == 0;
}

bool UPSDataIngestion::ValidateTeamsJson(const FString& JsonFilePath, TArray<FString>& OutErrors)
{
    OutErrors.Empty();

    FString JsonString;
    if (!FFileHelper::LoadFileToString(JsonString, *JsonFilePath))
    {
        OutErrors.Add(FString::Printf(TEXT("Could not read file: %s"), *JsonFilePath));
        return false;
    }

    TSharedPtr<FJsonObject> ParsedJson;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, ParsedJson) || !ParsedJson.IsValid())
    {
        OutErrors.Add(TEXT("File is not valid JSON."));
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Rows;
    if (!ParsedJson->TryGetArrayField(TEXT("Teams"), Rows))
    {
        OutErrors.Add(TEXT("Missing top-level \"Teams\" array."));
        return false;
    }

    TSet<FString> SeenTeamIds;
    for (int32 RowIndex = 0; RowIndex < Rows->Num(); ++RowIndex)
    {
        const TSharedPtr<FJsonObject>* RowObject;
        if (!(*Rows)[RowIndex]->TryGetObject(RowObject))
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: is not a JSON object."), RowIndex));
            continue;
        }

        FString TeamId;
        if (!(*RowObject)->TryGetStringField(TEXT("TeamId"), TeamId) || TeamId.IsEmpty())
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: missing or empty \"TeamId\"."), RowIndex));
        }
        else if (SeenTeamIds.Contains(TeamId))
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: duplicate \"TeamId\" \"%s\"."), RowIndex, *TeamId));
        }
        else
        {
            SeenTeamIds.Add(TeamId);
        }

        FString RosterPath;
        if (!(*RowObject)->TryGetStringField(TEXT("RosterDataTablePath"), RosterPath) || RosterPath.IsEmpty())
        {
            OutErrors.Add(FString::Printf(TEXT("Row %d: missing or empty \"RosterDataTablePath\"."), RowIndex));
        }
    }

    return OutErrors.Num() == 0;
}
