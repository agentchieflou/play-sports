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

        static const TArray<FString> NumericFields = { TEXT("WeightKg"), TEXT("HeightCm"), TEXT("Speed"), TEXT("Agility"), TEXT("Strength"), TEXT("Acceleration"), TEXT("Awareness"), TEXT("Stamina") };
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
