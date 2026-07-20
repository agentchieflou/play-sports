#include "PSPlaybookIngestion.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool UPSPlaybookIngestion::LoadPlaysFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable)
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
    if (!ParsedJson->TryGetArrayField(TEXT("Plays"), Rows))
    {
        return false;
    }

    TargetDataTable->EmptyTable();
    for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
    {
        FPSPlayDefinition Play;
        if (FJsonObjectConverter::JsonObjectToUStruct(RowValue->AsObject().ToSharedRef(), &Play, 0, 0))
        {
            FName RowName = Play.PlayId.IsNone() ? FName(*Play.DisplayName) : Play.PlayId;
            TargetDataTable->AddRow(RowName, Play);
        }
    }

    return true;
}

bool UPSPlaybookIngestion::LoadRoutesFromJson(const FString& JsonFilePath, UDataTable* TargetDataTable)
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
    if (!ParsedJson->TryGetArrayField(TEXT("Routes"), Rows))
    {
        return false;
    }

    TargetDataTable->EmptyTable();
    for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
    {
        FPSRoute Route;
        if (FJsonObjectConverter::JsonObjectToUStruct(RowValue->AsObject().ToSharedRef(), &Route, 0, 0))
        {
            FName RowName = Route.RouteId;
            TargetDataTable->AddRow(RowName, Route);
        }
    }

    return true;
}
