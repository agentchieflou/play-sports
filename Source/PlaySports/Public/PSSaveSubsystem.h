#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PSSaveGame.h"
#include "PSSaveSubsystem.generated.h"

DECLARE_DYNAMIC_DELEGATE_TwoParams(FPSSaveOpComplete, const FString&, SlotName, bool, bSuccess);

UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Save")
    bool SaveToSlot(UPSSaveGame* SaveObject, const FString& SlotName);

    UFUNCTION(BlueprintCallable, Category = "Save")
    UPSSaveGame* LoadFromSlot(const FString& SlotName);

    UFUNCTION(BlueprintCallable, Category = "Save")
    void SaveToSlotAsync(UPSSaveGame* SaveObject, const FString& SlotName, FPSSaveOpComplete OnComplete);

    UFUNCTION(BlueprintCallable, Category = "Save")
    void LoadFromSlotAsync(const FString& SlotName, FPSSaveOpComplete OnComplete);

    UFUNCTION(BlueprintCallable, Category = "Save")
    bool DoesSlotExist(const FString& SlotName) const;

    UFUNCTION(BlueprintPure, Category = "Save")
    static FString MakeSlotName(EPSSaveCategory Category, const FString& Id);

    // Loaded object for async callers to fetch after a successful LoadFromSlotAsync.
    UFUNCTION(BlueprintCallable, Category = "Save")
    UPSSaveGame* GetLastAsyncLoadResult() const
    {
        return LastAsyncLoadResult;
    }

    static FString GetSlotPath(const FString& SlotName);

private:
    UPROPERTY(Transient)
    UPSSaveGame* LastAsyncLoadResult = nullptr;

    bool SerializeToFileData(UPSSaveGame* SaveObject, TArray<uint8>& OutFileData) const;
    UPSSaveGame* DeserializeFileData(const TArray<uint8>& FileData);
    UPSSaveGame* LoadAndMigrate(const TArray<uint8>& FileData);

    static bool ValidateAndExtractPayload(const TArray<uint8>& FileData, TArray<uint8>& OutPayload);
    static bool WriteFileWithBackup(const FString& Path, const TArray<uint8>& FileData);
    static bool ReadFileWithFallback(const FString& Path, TArray<uint8>& OutFileData, TArray<uint8>& OutPayload);
};
