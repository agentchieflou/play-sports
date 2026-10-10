// PSSpecialTeamsData.h - Epic 75: special-teams calls, outcomes and tuning
#pragma once

#include "CoreMinimal.h"
#include "PSSpecialTeamsData.generated.h"

/** A special-teams call. The playbook marks one by its PlayCategory (the same name). */
UENUM(BlueprintType)
enum class EPSSpecialTeamsPlay : uint8
{
    None,
    /** The offense on a scrimmage down. */
    Punt,
    FieldGoal,
    /** Lines up as a punt or a field goal, then runs or throws for the first down. */
    FakePunt,
    FakeFieldGoal,
    /** The kicking team at a kickoff. */
    Kickoff,
    OnsideKick,
    /** The team facing a kick: set up a return (the play's formation is the return scheme) ... */
    KickReturn,
    /** ... or rush everybody to block it (scrimmage kicks only). */
    KickBlock,
    /** A kickoff: sure hands up front against an onside kick. */
    HandsTeam,
    /** A kickoff with the game on the line: return it and lateral until someone scores. */
    ReturnLaterals
};

/** How a kick, a block or a fake turned out. */
UENUM(BlueprintType)
enum class EPSSpecialTeamsResult : uint8
{
    None,
    Touchback,
    Returned,
    ReturnTouchdown,
    OnsideRecovered,
    OnsideLost,
    LateralTouchdown,
    LateralFumbleLost,
    Punted,
    Blocked,
    BlockedTouchdown,
    FieldGoalGood,
    FieldGoalMissed,
    FakeConverted,
    FakeStopped
};

/** Both sides' special-teams calls for a kick. None means the default: a plain kick, a plain return. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSpecialTeamsCall
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    EPSSpecialTeamsPlay Kicking = EPSSpecialTeamsPlay::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    EPSSpecialTeamsPlay Receiving = EPSSpecialTeamsPlay::None;

    /** The receiving call's formation, which names its return scheme. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    FString ReturnFormation;
};

/** What a special-teams unit brings, from its players' ratings (0-100). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSpecialTeamsUnitRatings
{
    GENERATED_BODY()

    /** The fastest player: edge rushers on a block, the gunners in coverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float EdgeSpeed = 50.f;

    /** The linemen's strength: the interior push on a block, the protection against one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float InteriorStrength = 50.f;

    /** The unit's awareness: lane discipline in coverage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float Awareness = 50.f;
};

/**
 * A kick's outcome, from the kicking team's line of scrimmage (or kickoff spot). The simulation
 * applies it (UPSSpecialTeamsModel::ApplyOutcome): who has the ball next, where, and the score.
 */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSpecialTeamsOutcome
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    EPSSpecialTeamsResult Result = EPSSpecialTeamsResult::None;

    /** True when the other team (the receivers, or the defense on a block) has the ball next. */
    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    bool bPossessionChanges = false;

    /** Where the team with the ball next starts, from its own goal line. */
    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    int32 NextYardLine = 25;

    /** The team with the ball next scored a touchdown on the play (a return, a block, laterals). */
    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    bool bTouchdown = false;

    /** Points for the kicking team (a field goal). */
    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    int32 KickingTeamPoints = 0;

    /** The kick's or the return's yards, for the log. */
    UPROPERTY(BlueprintReadOnly, Category = "SpecialTeams")
    int32 Yards = 0;
};

/** A return scheme: a receiving call's formation and what it does for the return. */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSReturnSchemeDef
{
    GENERATED_BODY()

    /** The formation of the receiving team's play, e.g. "Return Wall". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    FString Formation;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float ReturnYardsBonus = 0.f;

    /** The chance the return breaks for BigReturnYards more, before coverage discipline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float BigReturnChance = 0.05f;
};

/** A field goal's make chance out to MaxYards (the kick's distance: line of scrimmage to the posts). */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSFieldGoalRangeDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float MaxYards = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams")
    float MakeChance = 0.95f;
};

/**
 * Special-teams tuning (Data/special_teams.json; Architecture rule 4). The defaults equal the
 * file. Yard lines count from the team's own goal line; chances are 0-1.
 */
USTRUCT(BlueprintType)
struct PLAYSPORTS_API FPSSpecialTeamsTuning
{
    GENERATED_BODY()

    FPSSpecialTeamsTuning();

    // --- Kickoffs ---

    /** The kicking team's spot for a kickoff after a touchdown or a field goal ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 KickoffYardLine = 35;

    /** ... and for the free kick after a safety. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 SafetyKickYardLine = 20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float KickoffTouchbackChance = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 TouchbackYardLine = 25;

    /** A returned kickoff ends between these, before the scheme and coverage move it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 KickoffReturnMinYardLine = 15;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 KickoffReturnMaxYardLine = 30;

    /** An onside kick travels this far: the yards it must go before the kickers can recover. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    int32 OnsideKickYards = 10;

    /** The kicking team recovers an onside kick the return team didn't expect this often ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float OnsideRecoveryChance = 0.3f;

    /** ... and this often against the hands team. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float OnsideRecoveryVsHandsTeamChance = 0.1f;

    /** The hands team is built to catch, not to block: a plain kickoff comes back this much shorter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float HandsTeamReturnPenaltyYards = 5.f;

    /** Laterals on a return: a touchdown this often, a fumble the kickers recover this often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float LateralTouchdownChance = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Kickoff")
    float LateralFumbleLostChance = 0.3f;

    // --- Punts ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Punt")
    int32 PuntGrossYardsMin = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Punt")
    int32 PuntGrossYardsMax = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Punt")
    int32 PuntTouchbackYardLine = 20;

    /** A punt return gains between these, before the scheme and coverage move it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Punt")
    int32 PuntReturnYardsMin = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Punt")
    int32 PuntReturnYardsMax = 12;

    // --- Field goals ---

    /** The kick's distance is the line of scrimmage to the goal line plus this (snap and end zone). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|FieldGoal")
    float FieldGoalSnapYards = 17.f;

    /** Make chances by distance, shortest first; beyond the last range a kick has no chance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|FieldGoal")
    TArray<FPSFieldGoalRangeDef> FieldGoalRanges;

    /** A missed field goal gives the defense the ball at the spot, but no closer to its goal than this ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|FieldGoal")
    int32 MissedFieldGoalMinYardLine = 20;

    /** ... and no further from it than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|FieldGoal")
    int32 MissedFieldGoalMaxYardLine = 80;

    // --- Blocks ---

    /** A kick's chance of being blocked by a return unit (most of it dropping back) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float PuntBlockChance = 0.01f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float FieldGoalBlockChance = 0.015f;

    /** ... multiplied by this when the defense calls the block and rushes everybody. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float BlockUnitMultiplier = 5.f;

    /** Edge timing: block chance per rating point the fastest rusher has over the fastest cover man. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float EdgeSpeedFactor = 0.002f;

    /** Interior push: block chance per rating point of the rushers' strength over the protection's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float InteriorStrengthFactor = 0.002f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float MaxBlockChance = 0.5f;

    /** A blocked punt goes backwards this far before the defense falls on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    int32 BlockedPuntRecoilYards = 8;

    /** A blocked kick is picked up and returned for a touchdown this often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float BlockedKickTouchdownChance = 0.1f;

    /** A block unit has rushers, not blockers: its return comes back this much shorter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Block")
    float BlockUnitReturnPenaltyYards = 6.f;

    // --- Returns and coverage ---

    /** Return schemes, by the receiving play's formation (wall, wedge, ...). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    TArray<FPSReturnSchemeDef> ReturnSchemes;

    /** A return with no scheme (no receiving call) breaks big this often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    float DefaultBigReturnChance = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    int32 BigReturnYards = 40;

    /** Lane discipline: coverage awareness this far above or below 50 is full discipline (or none). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    float CoverageAwarenessSpan = 50.f;

    /** Full discipline takes this many yards off a return ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    float LaneDisciplineYards = 4.f;

    /** ... and this share off its chance of breaking big. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Return")
    float LaneDisciplineBigReturnScale = 0.5f;

    // --- Fakes ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Fake")
    float FakePuntSuccessChance = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Fake")
    float FakeFieldGoalSuccessChance = 0.4f;

    /** A block unit has sold out to rush the kick: a fake against it succeeds this much more often. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Fake")
    float FakeVsBlockUnitDelta = 0.15f;

    /** A converted fake gains the line to gain plus up to this many yards. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|Fake")
    int32 FakeExtraYardsMax = 10;

    // --- The coaching AI's calls ---

    /** The longest field goal (in kick distance) the CPU tries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float MaxFieldGoalAttemptYards = 55.f;

    /** With this little left in a half, a field goal in range is kicked on any down when it's what
     *  the offense needs (any score in the 2nd quarter; a tie or the lead in the 4th). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float LastPlaySeconds = 8.f;

    /** A 4th-quarter offense trailing with this little left never punts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float NoPuntTrailingSeconds = 120.f;

    /** The fake risk model: on 4th and this or less, a coach this aggressive or more fakes with
     *  this chance times his aggression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    int32 FakeMaxDistance = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float FakeMinAggression = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float FakeCallChance = 0.3f;

    /** The onside kick: a 4th-quarter kicking team down by 1 to this many with this little left ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    int32 OnsideMaxDeficit = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float OnsideWindowSeconds = 180.f;

    /** ... or, by surprise, with this chance times the coach's aggression. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float SurpriseOnsideChance = 0.03f;

    /** The return team down by 1 to this many with this little left laterals on the kickoff. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    int32 LateralsMaxDeficit = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float LateralsWindowSeconds = 10.f;

    /** The defense blocks a 4th-quarter field goal that would tie or win in this window, a punt
     *  when it trails in it, and otherwise with this chance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float BlockWindowSeconds = 300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float BaseBlockCallChance = 0.1f;

    /** A special-teams play's weight when its call is due (it is never called otherwise). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpecialTeams|AI")
    float SpecialTeamsPlayWeight = 5.f;
};

namespace PSSpecialTeams
{
    /** The special-teams play a PlayCategory names; None for every other play. */
    PLAYSPORTS_API EPSSpecialTeamsPlay FromCategory(const FString& Category);

    /** Kickoff calls (both teams'): only called at a kickoff, which calls nothing else. */
    PLAYSPORTS_API bool IsKickoffOnly(EPSSpecialTeamsPlay Play);

    /** The kick a formation shows the defense: a fake looks like the real thing. */
    PLAYSPORTS_API EPSSpecialTeamsPlay GetShownKick(EPSSpecialTeamsPlay Play);

    /** Whether a play with this call can be run at a kickoff (bKickoff) or on a scrimmage down:
     *  a kickoff runs kickoff calls and returns only; a scrimmage down anything else. */
    PLAYSPORTS_API bool IsCallableAt(EPSSpecialTeamsPlay Play, bool bKickoff);
}
