// PSSessionTelemetrySave.h - Epic 117: the local, opt-in store of session summaries
#pragma once

#include "CoreMinimal.h"
#include "PSSaveGame.h"
#include "PSSessionTelemetryTypes.h"
#include "PSSessionTelemetrySave.generated.h"

class UPSSaveSubsystem;

/**
 * The player's telemetry consent and their recent session summaries, saved through
 * UPSSaveSubsystem in the Telemetry category. It is the one authority on consent: no slot means
 * the player was never asked; a slot with bOptedIn false means they declined and holds no
 * sessions. Opting out deletes the slot and its backup before recording the answer, so nothing
 * collected earlier survives (Specs/Privacy_Telemetry.md).
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSessionTelemetrySave : public UPSSaveGame
{
    GENERATED_BODY()

public:
    UPSSessionTelemetrySave()
    {
        Category = EPSSaveCategory::Telemetry;
    }

    /** The slot the game's store lives in. */
    static FString GetDefaultSlotName();

    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    bool bOptedIn = false;

    /** Oldest first, at most the tuning's MaxStoredSessions. */
    UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
    TArray<FPSSessionSummary> Sessions;

    /** Replaces the stored session with Summary's SessionId, or adds Summary; then drops the
     *  oldest sessions beyond MaxSessions. */
    void UpsertSession(const FPSSessionSummary& Summary, int32 MaxSessions);

    void RemoveSession(const FGuid& SessionId);

    /** Session health over the stored sessions. */
    UFUNCTION(BlueprintPure, Category = "Telemetry")
    FPSSessionTelemetryReport BuildReport() const;

    /** Report as JSON (FJsonObjectConverter): the payload a future uploader sends. */
    static FString ReportToJson(const FPSSessionTelemetryReport& Report);

    /** The store in SlotName, or null when there is none (the player was never asked). */
    static UPSSessionTelemetrySave* LoadStore(UPSSaveSubsystem* Saves, const FString& SlotName);

    static EPSTelemetryConsent GetConsentInSlot(UPSSaveSubsystem* Saves, const FString& SlotName);

    /** Records the player's answer in SlotName. Opting in keeps stored sessions; opting out
     *  erases the slot and its backup first. False when the slot could not be written. */
    static bool SetConsentInSlot(UPSSaveSubsystem* Saves, const FString& SlotName, bool bOptIn);

    /** The player's answer, from the game's store. */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    static EPSTelemetryConsent GetConsent(UPSSaveSubsystem* Saves);

    /** Records the player's answer in the game's store (a settings screen or first-run prompt
     *  calls this). */
    UFUNCTION(BlueprintCallable, Category = "Telemetry")
    static bool SetConsent(UPSSaveSubsystem* Saves, bool bOptIn);
};
