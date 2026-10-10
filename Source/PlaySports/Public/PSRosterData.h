#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSRosterData.generated.h"

/** Ordered depth chart for one position: index 0 is the starter. */
USTRUCT(BlueprintType)
struct FPSDepthChartEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EPlayerRole Role = EPlayerRole::Quarterback;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FName> PlayerIdsByPriority;
};

/** Cross-play authoritative state for one rostered player (Epic 139/141): hitpoints,
 *  downed/availability, and level/XP. Owned by UPSRoster -- the existing single
 *  authority for "who's on this team and available" (Architecture rule 6) -- rather
 *  than a new parallel tracker. Distinct from UPSHealthComponent, which owns the
 *  live in-play HP pool for the current play only. */
USTRUCT(BlueprintType)
struct FPSPlayerLiveState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CurrentHitPoints = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bIsDowned = false;

    /** Ball carriers downed while carrying sit out exactly the next play: this is the
     *  play index (UPSRoster::CurrentPlayIndex-style counter) below which the player
     *  is unavailable. INDEX_NONE (-1) means no sit-out is active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 UnavailableUntilPlayIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 Level = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CurrentXp = 0.f;
};

/** Personnel package: how many of each role take the field for a formation grouping,
 *  e.g. "11 Personnel" (1 RB, 1 TE, 3 WR), "Nickel" (5 DB). Tuning lives in data,
 *  not code (Architecture rule 4): Data/personnel_packages.json, read by
 *  UPSPersonnelManager (Epic 19.5). */
USTRUCT(BlueprintType)
struct FPSPersonnelPackage : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName PackageId;

    /** What a broadcast or a play-call screen calls it, e.g. "12 Personnel". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DisplayName;

    /** True for an offensive grouping; its roles must all be on that side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bOffense = true;

    /** EPlayerRole name -> how many of that role take the field; 11 in all. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FString, int32> RoleCounts;

    /** Play formations (FPSPlayDefinition::Formation) that bring this package on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> Formations;
};

/** Every personnel package and how they rotate (Data/personnel_packages.json, Epic 19.5). */
USTRUCT(BlueprintType)
struct FPSPersonnelCatalog
{
    GENERATED_BODY()

    /** The offense's grouping at kickoff and for a formation no package lists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName DefaultOffensePackage = TEXT("P11");

    /** The defense's grouping at kickoff and for a formation no package lists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName DefaultDefensePackage = TEXT("Base43");

    /** A player whose stamina has fallen below this fraction of his maximum sits the next
     *  play when the depth chart has someone behind him (UPSRoster::EvaluateFatigueSubstitutions). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float FatigueSubstitutionThreshold = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FPSPersonnelPackage> Packages;
};
