// PSPersonnelManager.h - Epic 19.5: who is on the field, by personnel package
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlayerAttributes.h"
#include "PSRosterData.h"
#include "PSTelemetryBus.h"
#include "PSPersonnelManager.generated.h"

class APSPlayerPawn;
class UPSRoster;
class UWorld;

/**
 * UPSPersonnelManager decides which rostered players are on the field (Epic 19.5). UPSRoster
 * stays the one authority on who is on the team and in what depth order (Architecture rule
 * 6); this class picks from it by personnel package and puts the picks on the field.
 *
 *  - Packages (11/12/21/10 personnel; base 4-3 and 3-4, nickel, dime, goal line) are data:
 *    Data/personnel_packages.json, read through UPSDataIngestion. Each says how many of each
 *    role it fields (11 in all) and which play formations bring it on.
 *  - The game mode spawns each side's default package (GetStartingLineup) and hands the
 *    pawns over (BindPawns). Those pawns stay for the game: a substitution points a pawn at
 *    the incoming player's roster row, so every pawn is always a rostered player.
 *  - When a side calls a play (the bus's PlayCall), the package for the play's formation
 *    comes on: per role, the first players on the depth chart who can play. Players already
 *    on the field keep their pawns; only the changes move, and the side lines up again
 *    through APSFieldGrid::ComputeLineup.
 *  - At every new play (BeginNewPlay) both sides' packages are refilled: a ball carrier who
 *    must sit out (UPSRoster::IsAvailableForPlay) and a player whose stamina fell below the
 *    catalog's threshold (UPSRoster::EvaluateFatigueSubstitutions) give way to the next man
 *    on the depth chart for one play.
 *  - A player already on the field on a pawn this class doesn't own (the 4th-down extra
 *    defender, Epic 140) is never picked a second time.
 *  - Every change is announced on the bus (Personnel). A substituted pawn a human controls
 *    reports ControlChange for the player who left and for the one who came on.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSPersonnelManager : public UObject
{
    GENERATED_BODY()

public:
    /** Data/personnel_packages.json under the project directory. */
    static FString GetDefaultCatalogPath();

    /** The roster every selection comes from. Call before anything else. */
    UFUNCTION(BlueprintCallable, Category = "Personnel")
    void Initialize(UPSRoster* InRoster);

    /** Replaces the catalog with JsonFilePath's, read through UPSDataIngestion. False, and
     *  the catalog unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "Personnel")
    bool LoadCatalogFromJson(const FString& JsonFilePath);

    UFUNCTION(BlueprintCallable, Category = "Personnel")
    void SetCatalog(const FPSPersonnelCatalog& InCatalog) { Catalog = InCatalog; }

    UFUNCTION(BlueprintPure, Category = "Personnel")
    const FPSPersonnelCatalog& GetCatalog() const { return Catalog; }

    /** Problems with a catalog, one line each; empty when it is sound. A package needs a
     *  unique ID, a display name, known roles all on its own side, 11 players in all and, on
     *  offense, a quarterback and a lineman to snap to him. A formation belongs to at most
     *  one package per side, and both defaults must name a package of their side. */
    static TArray<FString> ValidateCatalog(const FPSPersonnelCatalog& InCatalog);

    /** The package with this ID, or null. */
    const FPSPersonnelPackage* FindPackage(FName PackageId) const;

    /** The side's package for Formation, or the side's default package when none lists it. */
    UFUNCTION(BlueprintPure, Category = "Personnel")
    FName GetPackageForFormation(const FString& Formation, bool bOffense) const;

    /** The players Package fields for the current play, role by role in depth-chart order:
     *  players who can play this play (not sitting out, not in Excluded) and aren't resting;
     *  a resting (tired) player only when nobody else can fill the role. False when the
     *  roster can't fill the package. */
    bool SelectPlayers(const FPSPersonnelPackage& Package, const TSet<FName>& Excluded, TArray<FName>& OutPlayerIds) const;

    /** The players to spawn at kickoff, as pointers to the roster's rows: each side's default
     *  package, in roster order, offense first. A side whose default package the roster can't
     *  fill fields every player it has on that side. */
    TArray<const FPlayerAttributes*> GetStartingLineup() const;

    /** Takes over the on-field pawns (spawned from GetStartingLineup); each side's default
     *  package is then the one on the field. */
    void BindPawns(const TArray<APSPlayerPawn*>& Pawns);

    /** Listens for play calls and announces personnel changes on Bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

    /** A new play lines up with the line of scrimmage at world X ScrimmageX: records the line
     *  and the play index, rests tired players, then refills both sides' current packages.
     *  Call once the pawns are reset for the play (after XP and healing for the last one). */
    UFUNCTION(BlueprintCallable, Category = "Personnel")
    void BeginNewPlay(float ScrimmageX, int32 PlayIndex);

    /** Puts PackageId on the field for its side, or the side's default package when the
     *  roster can't fill it. False, with the field unchanged, when neither can be filled. */
    UFUNCTION(BlueprintCallable, Category = "Personnel")
    bool ApplyPackage(FName PackageId);

    /** The package on the field for the side (NAME_None before BindPawns). */
    UFUNCTION(BlueprintPure, Category = "Personnel")
    FName GetCurrentPackage(bool bOffense) const { return bOffense ? OffensePackage : DefensePackage; }

    /** PlayerIds on the bound pawns of the side, in pawn order. */
    UFUNCTION(BlueprintPure, Category = "Personnel")
    TArray<FName> GetOnFieldPlayerIds(bool bOffense) const;

    /** Players resting this play because their stamina ran low (set by BeginNewPlay). */
    const TSet<FName>& GetRestingPlayerIds() const { return RestingPlayerIds; }

private:
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);

    /** Fills the side with PackageId (or the side's default), swapping only the players who
     *  change, then lines the side up again and announces it. */
    bool ApplyPackageToSide(bool bOffense, FName PackageId);

    /** Points Pawn at PlayerId's roster row, keeping the ball and any human on it. */
    bool PutPlayerOnPawn(APSPlayerPawn& Pawn, FName PlayerId);

    /** Lines up the side's pawns (bound ones first, then any other pawn of the side) at the
     *  recorded line of scrimmage. */
    void LineUpSide(bool bOffense);

    /** The bound pawns still alive, in spawn order. */
    TArray<APSPlayerPawn*> GetAllBoundPawns() const;
    TArray<APSPlayerPawn*> GetBoundPawns(bool bOffense) const;
    bool IsBound(const APSPlayerPawn* Pawn) const;
    UWorld* GetPawnWorld() const;

    /** PlayerIds on pawns in the world that aren't bound here. */
    TSet<FName> GetPlayersOnOtherPawns() const;

    void PublishControlChange(const FPlayerAttributes& Player, bool bHumanControlled) const;

    UPROPERTY(Transient)
    UPSRoster* Roster = nullptr;

    UPROPERTY(Transient)
    FPSPersonnelCatalog Catalog;

    /** The on-field pawns, in the order they were spawned. */
    TArray<TWeakObjectPtr<APSPlayerPawn>> BoundPawns;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    FDelegateHandle PlayCallHandle;

    TSet<FName> RestingPlayerIds;
    FName OffensePackage;
    FName DefensePackage;
    float LineOfScrimmageX = 0.f;
    int32 CurrentPlayIndex = 0;
};
