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
 * All of it is FPSSpecialTeamsTuning (Data/special_teams.json) over a seedable random stream,
 * so every outcome is testable without a world. ApplyOutcome turns an outcome into the next
 * down: the score, who kicks off, who has the ball and where.
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

    /** A kickoff from KickYardLine (the kicking team's spot). */
    FPSSpecialTeamsOutcome ResolveKickoff(const FPSSpecialTeamsCall& Call, int32 KickYardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving);

    /** A punt from YardLine (the punting team's line of scrimmage). */
    FPSSpecialTeamsOutcome ResolvePunt(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Receiving);

    /** A field goal from YardLine (the kicking team's line of scrimmage). */
    FPSSpecialTeamsOutcome ResolveFieldGoal(const FPSSpecialTeamsCall& Call, int32 YardLine, const FPSSpecialTeamsUnitRatings& Kicking, const FPSSpecialTeamsUnitRatings& Defending);

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

    UPROPERTY(Transient)
    FPSSpecialTeamsTuning Tuning;

    FRandomStream Stream;
};
