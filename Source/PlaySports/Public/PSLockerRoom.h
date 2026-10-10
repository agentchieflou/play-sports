// PSLockerRoom.h - Epic 91: morale, chemistry and the locker room
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSLockerRoomData.h"
#include "PSPlayerAttributes.h"
#include "PSLockerRoom.generated.h"

class UPSContractManager;
class UPSFranchiseSaveGame;
class UPSRoster;

/**
 * UPSLockerRoom is the one authority on how players feel and how units gel (Epic 91): a human
 * layer over the roster, tuned in Data/morale.json. It reads the other authorities and keeps only
 * its own facts (morale, its inputs, flags, lineups played together):
 *
 *  - Morale (0-1, 0.5 neutral), re-evaluated each week (EvaluateTeam): playing time (a starter by
 *    the roster's depth chart, a backup, a backup rated above the man ahead of him), team success
 *    (the win percentage the caller reads from the standings), contract status (pay against his
 *    worth and a deal's last year, from UPSContractManager) and the team's leaders. It eases from
 *    last week's by MoraleInertia.
 *  - Effects: a player plays up to PerformanceSwing above or below his ratings (ApplyEffects, for
 *    the simulation's copies; the roster keeps his own), and his morale is what he brings to
 *    contract talks and free agency (FPSNegotiationContext::Morale, Epic 87).
 *  - Chemistry: each unit (Data/morale.json: the offensive line, the secondary) gels as the same
 *    starters play game after game (RecordLineup); at full cohesion they play MaxBonus better.
 *    How many start at a role comes from the default personnel packages (Epic 19.5).
 *  - Events: a trade request after weeks of misery; a holdout at a new league year by an
 *    underpaid, unhappy star, who sits until paid; a leader emerging on a winning team.
 *  - Transparency: each player's morale lists its factors with their effect (DescribePlayer), and
 *    each unit its games together and bonus (DescribeTeamChemistry).
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSLockerRoom : public UObject
{
    GENERATED_BODY()

public:
    /** Data/morale.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "LockerRoom")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** How many start at each role: its count in the catalog's default offense and defense
     *  packages, read through UPSPersonnelManager (Data/personnel_packages.json). */
    UFUNCTION(BlueprintCallable, Category = "LockerRoom")
    bool LoadStartersFromPersonnel(const FString& PersonnelJsonPath);

    /** Both, from their default paths. */
    UFUNCTION(BlueprintCallable, Category = "LockerRoom")
    bool LoadDefaults();

    void SetTuning(const FPSMoraleTuning& InTuning) { Tuning = InTuning; }

    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    const FPSMoraleTuning& GetTuning() const { return Tuning; }

    /** Problems with a tuning, one line each; empty when it is sound. */
    static TArray<FString> ValidateTuning(const FPSMoraleTuning& InTuning);

    /** The starters at Role (1 for a role no package lists). */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    int32 GetStarterCount(EPlayerRole Role) const;

    /** Role's starters on Roster: the first GetStarterCount on its depth chart. */
    TArray<FName> GetStarters(const UPSRoster* Roster, EPlayerRole Role) const;

    /** A week in TeamId's locker room: every rostered player's morale from his playing time, the
     *  team's WinPercentage (0-1), his contract (Contracts may be null) and its leaders; trade
     *  requests and leaders that emerge, and holdouts begun (at a new league year) or ended.
     *  Returns what happened. */
    TArray<FPSLockerRoomEvent> EvaluateTeam(FName TeamId, const UPSRoster* Roster, float WinPercentage, const UPSContractManager* Contracts, bool bNewLeagueYear);

    /** TeamId plays a game with Roster's starters: each unit's lineup, and how long it has played
     *  together. */
    void RecordLineup(FName TeamId, const UPSRoster* Roster);

    /** PlayerId's morale; 0.5 for a player never evaluated. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    float GetMorale(FName PlayerId) const;

    /** PlayerId's morale and what makes it up. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    bool GetPlayerMorale(FName PlayerId, FPSPlayerMorale& OutMorale) const;

    /** A unit's cohesion, 0-1: games together over its FullCohesionGames. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    float GetUnitCohesion(FName TeamId, FName Unit) const;

    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    bool IsHoldingOut(FName PlayerId) const;

    /** What Player's ratings are multiplied by on TeamId: his morale's swing, and his unit's
     *  cohesion when he starts in one. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    float GetEffectMultiplier(FName TeamId, const FPlayerAttributes& Player) const;

    /** Player as he plays on TeamId: Speed, Agility, Strength, Acceleration and Awareness times
     *  GetEffectMultiplier (size and stamina untouched). */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    FPlayerAttributes ApplyEffects(FName TeamId, const FPlayerAttributes& Player) const;

    /** PlayerId's morale, line by line: the total, each factor and its effect, his flags. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    TArray<FString> DescribePlayer(FName PlayerId) const;

    /** TeamId's units: games together, cohesion and bonus. */
    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    TArray<FString> DescribeTeamChemistry(FName TeamId) const;

    UFUNCTION(BlueprintPure, Category = "LockerRoom")
    const FPSLockerRoomState& GetState() const { return State; }

    /** Writes the locker room into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads it back; false (keeping the current one) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

private:
    FPSPlayerMorale& FindOrAddPlayer(FName PlayerId);
    const FPSPlayerMorale* FindPlayer(FName PlayerId) const;
    const FPSUnitChemistry* FindUnit(FName TeamId, FName Unit) const;
    const FPSChemistryUnit* FindUnitTuning(FName Unit) const;

    UPROPERTY(Transient)
    FPSMoraleTuning Tuning;

    UPROPERTY(Transient)
    FPSLockerRoomState State;

    /** Starters by role, from the default personnel packages. */
    TMap<EPlayerRole, int32> StarterCounts;
};
