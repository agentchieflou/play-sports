// PSSpecialTeamsAI.h - Epic 75: the coaching AI's special-teams calls
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSCoachingData.h"
#include "PSPlaybookData.h"
#include "PSSpecialTeamsData.h"
#include "PSSpecialTeamsAI.generated.h"

/**
 * UPSSpecialTeamsAI makes the special-teams calls for UPSCoachingAI (Epic 75):
 *
 *  - the offense: punt or kick the field goal on 4th down (unless the 4th-down read says go for
 *    it, or a trailing team late in the game can't give the ball away), kick a field goal on any
 *    down when the half is about to end and it's what the score needs, and fake a kick by the
 *    Epic 18 risk model: 4th and short, an aggressive coach, a roll against his aggression;
 *  - the kicking team: an onside kick when it needs the ball late, or rarely by surprise;
 *  - the receiving team: laterals on a last-gasp kickoff, the hands team when it expects an
 *    onside kick, a block when a field goal would tie or win late or the punt is the last
 *    chance, else a return (the playbook's return plays carry the schemes).
 *
 * Every read is from the possessing team's side (FPSSituationContext), the defense's included.
 * Rolls are passed in, so the calls are deterministic for a seeded play caller and RankPlays
 * (which passes 1: no gamble) shows the percentage call.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSSpecialTeamsAI : public UObject
{
    GENERATED_BODY()

public:
    /** Replaces the tuning with JsonFilePath's (Data/special_teams.json), read through UPSDataIngestion. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSSpecialTeamsTuning& InTuning) { Tuning = InTuning; }

    const FPSSpecialTeamsTuning& GetTuning() const { return Tuning; }

    /** The kick's distance from the situation's yard line. */
    float GetFieldGoalDistance(const FPSSituationContext& Situation) const;

    bool IsInFieldGoalRange(const FPSSituationContext& Situation) const;

    /** The offense's call on a scrimmage down, or None to run a play. bGoForIt is the 4th-down
     *  read (UPSCoachingAI::ShouldGoForItOnFourthDown); Roll (0-1) drives the fake. */
    EPSSpecialTeamsPlay DecideOffense(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bGoForIt, float Roll) const;

    /** The kicking team's call at a kickoff (None otherwise). Roll drives the surprise onside kick. */
    EPSSpecialTeamsPlay DecideKickoff(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, float Roll) const;

    /** The receiving team's call at a kickoff, or against the kick the offense shows (None
     *  otherwise). Roll drives the occasional block. */
    EPSSpecialTeamsPlay DecideReceiving(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, float Roll) const;

    /** The side's special-teams call, or None for a regular play. */
    EPSSpecialTeamsPlay DecideCall(const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, bool bGoForIt, float Roll) const;

    /**
     * A play's weight change for the side calling it: SpecialTeamsPlayWeight for the special-teams
     * play that is due, with the reason; bOutExcluded for one that isn't (never call it), and at a
     * kickoff for every regular play.
     */
    float GetPlayAdjustment(const FPSPlayDefinition& Play, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency, bool bOffense, bool bGoForIt, float Roll,
        TArray<FString>* OutReasons, bool& bOutExcluded) const;

    /** "4th down: punt it away", ... for the call screen's suggestion. */
    static FString DescribeCall(EPSSpecialTeamsPlay Play);

private:
    UPROPERTY(Transient)
    FPSSpecialTeamsTuning Tuning;
};
