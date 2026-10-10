#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Async/Future.h"
#include "PSSaveGame.h"
#include "PSSaveSubsystem.generated.h"

class UPSPlatformServices;

DECLARE_DYNAMIC_DELEGATE_TwoParams(FPSSaveOpComplete, const FString&, SlotName, bool, bSuccess);

/**
 * UPSSaveSubsystem writes and reads save slots (Epic 115): a versioned, CRC-checked file per slot
 * with a backup of the previous write, migrated on load.
 *
 * Where the files go is the platform's business (Epic 152): slots live under the save storage
 * root of UPSPlatformServices, which it reaches through the game instance. The file format is the
 * same everywhere, so a save written on one platform loads on another. When the platform
 * suspends or constrains the game, every write still in flight is finished before the handler
 * returns (FlushPendingWrites).
 *
 * Headless tests create it with NewObject, which skips Initialize: it then uses the configured
 * platform's default root (GetSlotPath), or the services handed to SetPlatformServices.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSaveSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    /** Stores through InServices from now on, and flushes on its lifecycle events (tests; the
     *  game's own are bound in Initialize). */
    void SetPlatformServices(UPSPlatformServices* InServices);

    /** The folder this subsystem keeps its slots in: its platform services' save storage root,
     *  or the configured platform's default without any. */
    UFUNCTION(BlueprintPure, Category = "Save")
    FString GetStorageRoot() const;

    /** SlotName's file under GetStorageRoot(). */
    FString GetSlotFilePath(const FString& SlotName) const;

    /** Waits for every SaveToSlotAsync write still in flight to reach the disk. A platform
     *  suspend or constrain calls it. */
    UFUNCTION(BlueprintCallable, Category = "Save")
    void FlushPendingWrites();

    /** SaveToSlotAsync writes not yet finished. */
    UFUNCTION(BlueprintPure, Category = "Save")
    int32 GetPendingWriteCount() const;

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

    // Deletes the slot and its backup, so nothing it held survives (Epic 117: opting out of
    // telemetry erases it). True when neither file remains.
    UFUNCTION(BlueprintCallable, Category = "Save")
    bool DeleteSlot(const FString& SlotName);

    UFUNCTION(BlueprintPure, Category = "Save")
    static FString MakeSlotName(EPSSaveCategory Category, const FString& Id);

    // Fetches and consumes the result for RequestId (returned by LoadFromSlotAsync).
    // Returns nullptr if the request is unknown, still pending, or failed.
    UFUNCTION(BlueprintCallable, Category = "Save")
    UPSSaveGame* GetAsyncLoadResult(int32 RequestId);

    /** SlotName's file under the configured platform's default storage root
     *  (UPSPlatformServices::GetDefaultSaveStorageRoot): where every subsystem without other
     *  platform services keeps it. */
    static FString GetSlotPath(const FString& SlotName);

private:
    static FString MakeSlotPath(const FString& Root, const FString& SlotName);
    void BindLifecycle();
    void UnbindLifecycle();

    /** Where slots go (Epic 152); null in headless tests that don't hand one over. */
    UPROPERTY(Transient)
    UPSPlatformServices* PlatformServices = nullptr;

    FDelegateHandle LifecycleHandle;

    /** SaveToSlotAsync writes in flight; finished ones are dropped as new ones start. */
    TArray<TFuture<void>> PendingWrites;

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
