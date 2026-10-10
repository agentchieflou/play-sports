// PSLockerRoomData.h - Epic 91: morale, chemistry and the locker room
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSLockerRoomData.generated.h"

/** A unit whose cohesion grows with a stable lineup: its starters play better together. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSChemistryUnit
{
    GENERATED_BODY()

    /** "OffensiveLine", "Secondary", ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName Unit;

    /** The role whose starters (the default personnel package's count, depth-chart order) make
     *  the unit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    EPlayerRole Role = EPlayerRole::OffensiveLineman;

    /** Games the same starters play together to reach full cohesion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    int32 FullCohesionGames = 6;

    /** The starters' ratings rise by up to this fraction at full cohesion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    float MaxBonus = 0.04f;
};

/** How players feel and what it does (Data/morale.json, Epic 91). Morale runs 0-1, 0.5 neutral:
 *  0.5 plus each factor, eased from last week's by MoraleInertia. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSMoraleTuning
{
    GENERATED_BODY()

    /** Playing time: a starter (by the depth chart) gains this; a backup loses BackupPenalty, and
     *  BetterThanStarterPenalty more when he is rated above the man starting ahead of him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float StarterBonus = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float BackupPenalty = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float BetterThanStarterPenalty = 0.1f;

    /** Team success: this times (win percentage - 0.5). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float TeamSuccessWeight = 0.2f;

    /** Contract: paid under UnderpaidRatio of his worth (his demand), he loses up to
     *  UnderpaidPenalty (all of it unpaid); at his worth or more he gains WellPaidBonus; in his
     *  deal's last year he loses ContractYearPenalty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float UnderpaidRatio = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float UnderpaidPenalty = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float WellPaidBonus = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float ContractYearPenalty = 0.05f;

    /** Each leader on the team (up to MaxLeaders) lifts his teammates this much. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float LeaderBoost = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    int32 MaxLeaders = 2;

    /** Last week's morale's share of this week's (0: none, morale follows its inputs at once). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Morale")
    float MoraleInertia = 0.5f;

    /** Effects: a player's ratings rise or fall by up to this fraction at morale 1 or 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Effects")
    float PerformanceSwing = 0.04f;

    /** Events: a trade request after TradeRequestWeeks weeks in a row under TradeRequestMorale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float TradeRequestMorale = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    int32 TradeRequestWeeks = 3;

    /** A holdout, at a new league year: a player rated StarRating or better, paid under
     *  HoldoutPayRatio of his worth, with morale under HoldoutMorale. He sits until paid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float StarRating = 85.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float HoldoutPayRatio = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float HoldoutMorale = 0.45f;

    /** A leader emerges: a starter with Awareness of LeaderAwareness or more, on a team winning
     *  LeaderWinPercentage of its games, with morale of LeaderMorale or more. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float LeaderAwareness = 85.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float LeaderWinPercentage = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Events")
    float LeaderMorale = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom|Chemistry")
    TArray<FPSChemistryUnit> Units;
};

/** One input to a player's morale, as the transparency surface shows it. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSMoraleFactor
{
    GENERATED_BODY()

    /** PlayingTime, TeamSuccess, Contract, Leadership. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName Factor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    float Delta = 0.f;

    /** "Starting at quarterback", "Paid 62% of his worth", ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FString Description;
};

/** A player in the locker room. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSPlayerMorale
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName PlayerId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    float Morale = 0.5f;

    /** This week's inputs, and what they add up to (before easing from last week). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    TArray<FPSMoraleFactor> Factors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    float TargetMorale = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    int32 LowMoraleWeeks = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    bool bTradeRequested = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    bool bHoldingOut = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    bool bLeader = false;
};

/** A unit's lineup and how long it has played together. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSUnitChemistry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName TeamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    FName Unit;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    TArray<FName> Starters;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    int32 GamesTogether = 0;
};

/** The locker room, as the franchise save keeps it (UPSFranchiseSaveGame). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLockerRoomState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    TArray<FPSPlayerMorale> Players;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockerRoom")
    TArray<FPSUnitChemistry> Units;
};

UENUM(BlueprintType)
enum class EPSLockerRoomEventKind : uint8
{
    TradeRequest,
    Holdout,
    HoldoutEnded,
    LeaderEmerged
};

/** Something that happened in a locker room. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSLockerRoomEvent
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "LockerRoom")
    EPSLockerRoomEventKind Kind = EPSLockerRoomEventKind::TradeRequest;

    UPROPERTY(BlueprintReadOnly, Category = "LockerRoom")
    FName PlayerId;

    UPROPERTY(BlueprintReadOnly, Category = "LockerRoom")
    FName TeamId;

    UPROPERTY(BlueprintReadOnly, Category = "LockerRoom")
    FString Description;
};
