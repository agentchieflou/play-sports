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

    // Returns a request ID; fetch the result via GetAsyncLoadResult(RequestId) once
    // OnComplete fires. Per-request (not single-slot) so two loads in flight at once
    // can't race and overwrite each other's result (Epic C4).
    UFUNCTION(BlueprintCallable, Category = "Save")
    int32 LoadFromSlotAsync(const FString& SlotName, FPSSaveOpComplete OnComplete);

    UFUNCTION(BlueprintCallable, Category = "Save")
    bool DoesSlotExist(const FString& SlotName) const;

    UFUNCTION(BlueprintPure, Category = "Save")
    static FString MakeSlotName(EPSSaveCategory Category, const FString& Id);

    // Fetches and consumes the result for RequestId (returned by LoadFromSlotAsync).
    // Returns nullptr if the request is unknown, still pending, or failed.
    UFUNCTION(BlueprintCallable, Category = "Save")
    UPSSaveGame* GetAsyncLoadResult(int32 RequestId);

    static FString GetSlotPath(const FString& SlotName);

private:
    UPROPERTY(Transient)
    TMap<int32, UPSSaveGame*> PendingLoadResults;

    int32 NextAsyncLoadRequestId = 1;

    bool SerializeToFileData(UPSSaveGame* SaveObject, TArray<uint8>& OutFileData) const;
    UPSSaveGame* DeserializeFileData(const TArray<uint8>& FileData);
    UPSSaveGame* LoadAndMigrate(const TArray<uint8>& FileData);

    static bool ValidateAndExtractPayload(const TArray<uint8>& FileData, TArray<uint8>& OutPayload);
    static bool WriteFileWithBackup(const FString& Path, const TArray<uint8>& FileData);
    static bool ReadFileWithFallback(const FString& Path, TArray<uint8>& OutFileData, TArray<uint8>& OutPayload);
};
