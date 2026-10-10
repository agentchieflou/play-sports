// PSSpecialTeamsModel.h - Epic 75: how kicks, blocks, returns and fakes turn out
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSPlayerAttributes.h"
#include "PSSpecialTeamsData.h"
#include "PSSpecialTeamsModel.generated.h"

struct FPlayState;

/**
 * UPSSpecialTeamsModel resolves the kicking game for UPSPlaySimulation, the play's outcome
 * authority (Epic 75):
 *
 *  - a kick can be blocked: edge timing (the rushers' speed over the cover men's) and interior
 *    push (strength over the protection's), much more often when the defense calls the block;
 *  - a return runs the receiving play's scheme (its formation: wall, wedge, ...) against the
 *    coverage's lane discipline (its awareness); a block unit or the hands team returns short;
 *  - an onside kick is recovered more often by surprise than against the hands team, and laterals
 *    on a kickoff return score or are lost;
 *  - a fake converts more often against a block unit that sold out to rush.
 *
 * All of it is FPSSpecialTeamsTuning (Data/special_teams.json). Its random numbers come from
 * the engine's global stream, like the rest of the simulation, so a seeded game (FMath::RandInit)
 * replays the same kicks; Seed gives the model a stream of its own. A kick's quality can also be
 * passed in (KickRoll, 0 = perfect .. 1): it decides the touchback, the punt's distance and the
 * field goal, so a human kicker's meter can stand in for the roll. ApplyOutcome turns an
 * outcome into the next down: the score, who kicks off, who has the ball and where.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSpecialTeamsModel : public UObject
{
    GENERATED_BODY()

public:
    UPSSpecialTeamsModel();

    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSSpecialTeamsTuning& InTuning) { Tuning = InTuning; }

    const FPSSpecialTeamsTuning& GetTuning() const { return Tuning; }

    /** From now on, draws come from a stream of the model's own, seeded with InSeed. */
    UFUNCTION(BlueprintCallable, Category = "SpecialTeams")
    void Seed(int32 InSeed);

    /** A unit's edge speed (its fastest player), interior strength (its linemen's, else
     *  everyone's, average) and awareness (average). Defaults for an empty roster. */
    static FPSSpecialTeamsUnitRatings RateUnit(const TArray<FPlayerAttributes>& Players);

    /** The kick's distance from YardLine: to the goal line plus the snap and the end zone. */
    float GetFieldGoalDistance(int32 YardLine) const;

    /** The make chance at DistanceYards; 0 beyond the last range. */
    float GetFieldGoalChance(float DistanceYards) const;

    /** The chance Kick (Punt or FieldGoal) is blocked against the Defense's call. */
    float GetBlockChance(EPSSpecialTeamsPlay Kick, EPSSpecialTeamsPlay Defense, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Defending) const;

    /** The coverage's lane discipline, -1 (none) to 1 (full), from its awareness. */
    float GetCoverageDiscipline(const FPSSpecialTeamsUnitRatings& Coverage) const;

    /** The return scheme a receiving play's formation runs, or null for none. */
    const FPSReturnSchemeDef* FindReturnScheme(const FString& Formation) const;

    /** The chance a fake (FakePunt or FakeFieldGoal) converts against the Defense's call. */
    float GetFakeSuccessChance(EPSSpecialTeamsPlay Fake, EPSSpecialTeamsPlay Defense) const;

    /** A kickoff from KickYardLine (the kicking team's spot). KickRoll (0-1, negative to draw
     *  one) decides the touchback. */
    FPSSpecialTeamsOutcome ResolveKickoff(const FPSSpecialTeamsCall& Call, int32 KickYardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving, float KickRoll = -1.f);

    /** A punt from YardLine (the punting team's line of scrimmage). KickRoll (0-1, negative to
     *  draw one) decides its distance: 0 is the longest. */
    FPSSpecialTeamsOutcome ResolvePunt(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving, float KickRoll = -1.f);

    /** A field goal from YardLine (the kicking team's line of scrimmage). KickRoll (0-1,
     *  negative to draw one) is good under the make chance. */
    FPSSpecialTeamsOutcome ResolveFieldGoal(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Defending, float KickRoll = -1.f);

    /** The yards a fake gains on 4th and Distance: at least Distance when it converts, less
     *  when it's stopped. */
    int32 ResolveFake(const FPSSpecialTeamsCall& Call, int32 Distance);

    /**
     * Applies a kick's outcome to State, whose possessing team kicked: a field goal's points (that
     * team kicks off next), a return or block touchdown with its try (the scorers kick off), or
     * the next spot, 1st and 10, for whoever has the ball. True when the ball changes hands: the
     * simulation swaps the sides.
     */
    bool ApplyOutcome(FPlayState& State, const FPSSpecialTeamsOutcome& Outcome, int32 TouchdownPoints, int32 FieldGoalPoints, float ExtraPointChance);

private:
    /** Return yards between MinYards and MaxYards, moved by the receiving call's scheme or unit
     *  and the coverage's discipline, and sometimes broken for BigReturnYards more. */
    int32 RollReturn(const FPSSpecialTeamsCall& Call, int32 MinYards, int32 MaxYards, const FPSSpecialTeamsUnitRatings& Coverage);

    /** A random number in [0, 1): the model's own stream once seeded, else the global one. */
    float NextRoll();

    /** A random whole number in [Min, Max], the same way. */
    int32 NextInRange(int32 Min, int32 Max);

    UPROPERTY(Transient)
    FPSSpecialTeamsTuning Tuning;

    FRandomStream Stream;
    bool bSeeded = false;
};
