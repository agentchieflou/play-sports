#include "PSDataIngestion.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
