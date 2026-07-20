#pragma once

#include "CoreMinimal.h"
#include "PSRosterData.h"
#include "PSRoster.generated.h"

/** A 53-player roster with a depth chart per position. One authority for "who's on
 *  this team and in what order" (Architecture rule 6); the GameMode/orchestrator
 *  consumes this rather than hardcoding a spawn list. */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSRoster : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Roster")
    void InitializeRoster(const TArray<FPlayerAttributes>& Players);

    /** Builds depth chart order per role from roster order (first-seen-first-starter);
     *  call SetDepthChartOrder afterward to override with explicit priority. */
    UFUNCTION(BlueprintCallable, Category = "Roster")
    void BuildDefaultDepthChart();

    UFUNCTION(BlueprintCallable, Category = "Roster")
    void SetDepthChartOrder(EPlayerRole Role, const TArray<FName>& PlayerIdsByPriority);

    UFUNCTION(BlueprintPure, Category = "Roster")
    FName GetStarterId(EPlayerRole Role) const;

    UFUNCTION(BlueprintPure, Category = "Roster")
    TArray<FName> GetDepthChartForRole(EPlayerRole Role) const;

    /** Next-in-line backup for PlayerId at its role, or NAME_None if PlayerId is
     *  not found or has no backup (used for fatigue-driven substitution). */
    UFUNCTION(BlueprintPure, Category = "Roster")
    FName GetNextBackup(FName PlayerId) const;

    UFUNCTION(BlueprintPure, Category = "Roster")
    bool FindPlayerById(FName PlayerId, FPlayerAttributes& OutAttributes) const;

    /** Substitutes any on-field pawn whose CurrentStamina/MaxStamina ratio has
     *  dropped below FatigueThreshold with its next depth-chart backup. Returns
     *  the role->incoming-player substitutions made, for the caller to actually
     *  respawn/reassign pawns (this class owns roster data, not live actors). */
    UFUNCTION(BlueprintCallable, Category = "Roster")
    TMap<FName, FName> EvaluateFatigueSubstitutions(const TMap<FName, float>& OnFieldStaminaRatioByPlayerId, float FatigueThreshold = 0.3f) const;

    UFUNCTION(BlueprintPure, Category = "Roster")
    const TArray<FPlayerAttributes>& GetFullRoster() const { return FullRoster; }

private:
    UPROPERTY(Transient)
    TArray<FPlayerAttributes> FullRoster;

    UPROPERTY(Transient)
    TArray<FPSDepthChartEntry> DepthChart;

    FPSDepthChartEntry* FindOrAddEntry(EPlayerRole Role);
    const FPSDepthChartEntry* FindEntry(EPlayerRole Role) const;
};
