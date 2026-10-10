// PSRushMoveComponent.h - Epic 70: rushers win with technique, not just stats
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSPlayerAttributes.h"
#include "PSRushMoveComponent.generated.h"

class APSDefenseController;
class APSPlayerPawn;

/** A pass rusher's move against the man blocking him (Epic 70). */
UENUM(BlueprintType)
enum class EPSRushMove : uint8
{
    None,
    /** Power straight through the blocker's chest. */
    Bull,
    /** An arm over the blocker's shoulder. */
    Swim,
    /** An arm under the blocker's arm, dipping past his outside shoulder. */
    Rip,
    /** A spin off the blocker's contact. */
    Spin,
    /** A slap that knocks the blocker's hands away. */
    Club,
    /** Squeezing between two blockers: the answer to a double team. */
    Split
};

/** How a blocker stops a rush move (Epic 70). A failed move draws its stopping response; the
 *  move that counters that response is then easier. */
UENUM(BlueprintType)
enum class EPSBlockResponse : uint8
{
    None,
    /** Sitting down on his hips against power. */
    Anchor,
    /** Striking with his hands inside before the rusher's arms get there. */
    Punch,
    /** Sliding with the rusher, staying square to him. */
    Mirror
};

/** One rush move as data (Data/pass_rush_moves.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSRushMoveDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    EPSRushMove Move = EPSRushMove::None;

    /** The rusher's rating the move runs on: Strength, Agility, Speed or Awareness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    FName Attribute;

    /** The blocker's rating that resists it (same choices). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    FName BlockerAttribute;

    /** Below this rating the rusher doesn't have the move. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float MinAttribute = 0.f;

    /** The success curve: BaseWinChance + (rusher rating - blocker rating) * RatingScalar,
     *  clamped to the catalog's WinChanceMin..WinChanceMax. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float BaseWinChance = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float RatingScalar = 0.005f;

    /** How long the move takes before it wins or is stopped. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float MoveSeconds = 0.5f;

    /** Stamina it costs; a rusher without that much can't do it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float StaminaCost = 5.f;

    /** Speed (cm/s) the rusher gains toward the passer when the move wins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float WinBurstSpeed = 150.f;

    /** What the win chance is multiplied by against a double team. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float DoubleTeamWinScale = 0.3f;

    /** Only used against a double team (the split). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    bool bDoubleTeamOnly = false;

    /** The blocker response that stops this move. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    EPSBlockResponse Response = EPSBlockResponse::None;

    /** The blocker response this move beats: right after it, the move gets the catalog's
     *  CounterBonus (a blocker who anchored on a bull is spun off). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    EPSBlockResponse Counters = EPSBlockResponse::None;
};

/** The move library and the rush plan's tuning (Data/pass_rush_moves.json). */
USTRUCT(BlueprintType)
struct FPSRushMoveCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    TArray<FPSRushMoveDef> RushMoves;

    /** Seconds from the engagement to the first move. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float FirstMoveSeconds = 0.3f;

    /** Seconds after a stopped move before the next. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float RecoverySeconds = 0.4f;

    /** Added to a move's chance right after the blocker used the response it counters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float CounterBonus = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float WinChanceMin = 0.02f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float WinChanceMax = 0.85f;

    /** How many tries the rush plan's estimate of a move is worth before this blocker has
     *  seen it: the plan scores a move (wins + PriorWeight * chance) / (tries + PriorWeight). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float HistoryPriorWeight = 3.f;

    /** A free offensive lineman this close to the rusher (cm) is the second man of a double
     *  team. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PassRush")
    float DoubleTeamRadius = 180.f;
};

/** How one move has done against one blocker this game. */
USTRUCT(BlueprintType)
struct FPSRushMoveRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PassRush")
    int32 Attempts = 0;

    UPROPERTY(BlueprintReadOnly, Category = "PassRush")
    int32 Wins = 0;
};

/** The pure rules of the move library: the success curve and the rush plan's score. */
namespace PSRushMoves
{
    /** A rating by name (Strength, Agility, Speed, Awareness); 0 for an unknown name. */
    PLAYSPORTS_API float GetRating(const FPlayerAttributes& Attributes, FName Attribute);

    /** Def's chance to beat Blocker: 0 when Rusher is rated below MinAttribute, or when the
     *  move is for double teams only and this isn't one. Otherwise the curve, plus CounterBonus
     *  when LastResponse is what the move counters, clamped, then scaled by DoubleTeamWinScale
     *  against a double team. */
    PLAYSPORTS_API float ComputeWinChance(const FPSRushMoveDef& Def, const FPlayerAttributes& Rusher, const FPlayerAttributes& Blocker,
        EPSBlockResponse LastResponse, bool bDoubleTeamed, const FPSRushMoveCatalog& Catalog);

    /** The rush plan's score for a move: its chance, pulled toward how it has actually done
     *  against this blocker. */
    PLAYSPORTS_API float ScoreMove(float WinChance, const FPSRushMoveRecord& Record, float PriorWeight);

    /** Problems with Catalog (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateCatalog(const FPSRushMoveCatalog& Catalog);
}

/**
 * UPSRushMoveComponent is a CPU pass rusher's technique (Epic 70). While its defender is
 * rushing (UPSDefenderAIComponent's Rush or Contain) or fitting a gap on a run (Fit, Epic 81)
 * and held by a blocker (the engagement APSGameMode::PairLinemen starts at the snap), it works
 * moves on him: swim, rip, bull, spin and club from Data/pass_rush_moves.json, each gated and
 * scaled by the ratings it pits against each other.
 *
 * The rush plan picks the move with the best score: its chance (higher right after the blocker
 * used the response the move counters) pulled toward how it has done against this blocker in
 * this game, and weighted by the rusher's style -- a power rusher favours power moves, a finesse
 * rusher finesse ones (FPSPlayerDNA::RushPower, Data/player_dna.json; Epic 79). A stopped move records the blocker's response; a winning one ends the engagement
 * and bursts the rusher toward the ball carrier (the passer, until he hands off or throws). A free offensive lineman beside the rusher makes a
 * double team: every move gets harder and the split becomes available. Each resolved move is
 * published on UPSTelemetryBus (PassRushMove).
 *
 * APSDefenseController owns one beside its UPSDefenderAIComponent. It ticks with the
 * controller; headless tests call TickRush and ResolveActiveMove.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSRushMoveComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSRushMoveComponent();

    static FString GetDefaultCatalogPath();

    /** The move library in use, loaded from the default path on first use. */
    const FPSRushMoveCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Replaces the move library (tests, or a mode with its own). */
    void SetCatalog(const FPSRushMoveCatalog& InCatalog);

    /** One step: starts a move when the last is done and the rusher has recovered, and
     *  resolves the active move when its time is up. */
    void TickRush(float DeltaSeconds);

    /** Resolves the active move now: it wins when Roll (0..1) is below its chance. Returns
     *  whether it won; false with no active move. */
    bool ResolveActiveMove(float Roll);

    UFUNCTION(BlueprintPure, Category = "PassRush")
    EPSRushMove GetActiveMove() const { return ActiveMove; }

    /** The chance the active move was started with. */
    UFUNCTION(BlueprintPure, Category = "PassRush")
    float GetActiveWinChance() const { return ActiveWinChance; }

    /** The current blocker's last response, None after a win or a new blocker. */
    UFUNCTION(BlueprintPure, Category = "PassRush")
    EPSBlockResponse GetLastResponse() const { return LastResponse; }

    /** True when the last move was picked against two blockers. */
    UFUNCTION(BlueprintPure, Category = "PassRush")
    bool IsDoubleTeamed() const { return bDoubleTeamed; }

    /** The move the rush plan would pick now against the current blocker (None if there is
     *  no blocker or no move available). */
    EPSRushMove ChooseMove();

    /** How Move has done against the blocker with BlockerId this game. */
    FPSRushMoveRecord GetMatchupRecord(FName BlockerId, EPSRushMove Move) const;

    /** Forgets every matchup (a new game). */
    UFUNCTION(BlueprintCallable, Category = "PassRush")
    void ResetMatchupHistory();

    /** Seeds the rolls TickRush makes, so a rush can be replayed. */
    UFUNCTION(BlueprintCallable, Category = "PassRush")
    void SeedRolls(int32 Seed) { RollStream.Initialize(Seed); }

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    struct FMatchup
    {
        TMap<EPSRushMove, FPSRushMoveRecord> Records;
    };

    APSDefenseController* GetDefenseController() const;
    APSPlayerPawn* GetSelf() const;
    bool IsRushing() const;
    APSPlayerPawn* GetBlocker() const;
    bool ReadDoubleTeam(const APSPlayerPawn* Self) const;
    const FPSRushMoveDef* FindDef(EPSRushMove Move);
    void StartMove(EPSRushMove Move);
    void EndEngagementState();

    UPROPERTY(Transient)
    FPSRushMoveCatalog Catalog;

    /** Matchup history this game, by blocker PlayerId. */
    TMap<FName, FMatchup> Matchups;

    TWeakObjectPtr<APSPlayerPawn> CurrentBlocker;
    FRandomStream RollStream;
    EPSRushMove ActiveMove = EPSRushMove::None;
    EPSBlockResponse LastResponse = EPSBlockResponse::None;
    float ActiveWinChance = 0.f;
    float Clock = 0.f;
    float ResolveAt = -1.f;
    float NextMoveAt = -1.f;
    bool bDoubleTeamed = false;
    bool bCatalogLoaded = false;
};
