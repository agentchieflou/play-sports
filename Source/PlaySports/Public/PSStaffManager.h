// PSStaffManager.h - Epic 89: coaching staffs, scheme identity and the coaching carousel
#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "PSCoachingData.h"
#include "PSLeagueData.h"
#include "PSPlaybookData.h"
#include "PSPlayerAttributes.h"
#include "PSStaffData.h"
#include "PSStaffManager.generated.h"

class UPSFranchiseSaveGame;
class UPSPlayCallSubsystem;

/**
 * UPSStaffManager is the one authority on who coaches each team (Epic 89), and what that means on
 * the field:
 *
 *  - Coaches (Data/coaching_staffs.json): a head coach sets his team's aggression, and each
 *    coordinator runs a scheme -- West Coast, Air Raid, Power Run, Zone Run; Cover 2 Zone, 3-4
 *    Pressure, Nickel Man -- as faithfully as his play-calling rating allows.
 *  - Scheme-playbook binding: a team's playbook is the plays in its coordinators' formations, plus
 *    the special-teams and clock plays every team keeps. BuildTeamPlan packs it with the
 *    coordinators' tendencies for UPSPlayCallSubsystem, the play-call authority, which then
 *    calls (CPU) and offers (human) only those plays, leaning the scheme's way.
 *  - Player-scheme fit: a player's ratings in his coordinator's scheme against his average across
 *    the side's schemes; an agile zone lineman misfits in a power scheme and plays below his
 *    ratings there (ApplySchemeFit), less so under a coordinator who develops players.
 *  - The carousel between seasons (RunCarousel): losing head coaches are fired after their grace
 *    seasons, the worst units' coordinators too; winning teams' coordinators are hired away as
 *    head coaches; a new head coach brings his scheme and replaces the coordinator who runs
 *    another; vacancies fill with the best free coach, preferring the head coach's scheme.
 *
 * The staffs persist in the franchise save (SaveTo / LoadFrom); the schemes and tuning are data.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSStaffManager : public UObject
{
    GENERATED_BODY()

public:
    static FString GetDefaultDataPath();

    /** Replaces the league with JsonFilePath's, read through UPSDataIngestion. */
    UFUNCTION(BlueprintCallable, Category = "Staff")
    bool LoadFromJson(const FString& JsonFilePath);

    void SetLeague(const FPSCoachingLeague& InLeague) { League = InLeague; }

    const FPSCoachingLeague& GetLeague() const { return League; }

    const FPSStaffTuning& GetTuning() const { return League.Tuning; }

    const FPSSchemeDef* FindScheme(FName SchemeId) const;

    const FPSCoachDef* FindCoach(FName CoachId) const;

    const FPSTeamStaffDef* FindStaff(FName TeamId) const;

    /** The team's coach in Role; null when the job is vacant or the team unknown. */
    const FPSCoachDef* FindTeamCoach(FName TeamId, EPSCoachRole Role) const;

    /** The scheme the team's coordinator on a side runs; None without one. */
    UFUNCTION(BlueprintPure, Category = "Staff")
    FName GetTeamScheme(FName TeamId, bool bOffense) const;

    /** How far a coordinator's calls follow his scheme: MinSchemeAdherence at play calling 0 to
     *  MaxSchemeAdherence at 100. */
    float GetSchemeAdherence(const FPSCoachDef& Coordinator) const;

    /** The team's tendency on a side: the head coach's aggression and the coordinator's scheme
     *  weights scaled by his adherence, labelled with the scheme. Neutral when a job is vacant. */
    UFUNCTION(BlueprintPure, Category = "Staff")
    FPSTendencyProfile BuildTendency(FName TeamId, bool bOffense) const;

    /** The plays out of Plays the team runs: on each side the formations its coordinator's scheme
     *  runs (all of them without a coordinator), and every special-teams and clock play. */
    TArray<FName> BuildPlaybook(FName TeamId, const TArray<FPSPlayDefinition>& Plays) const;

    /** The team's tendencies and playbook for a game. */
    FPSTeamPlan BuildTeamPlan(FName TeamId, const TArray<FPSPlayDefinition>& Plays) const;

    /** Hands both teams' plans to the play-call authority. A team without a staff calls from the
     *  whole playbook; false when either team has none. */
    bool ApplyToPlayCall(UPSPlayCallSubsystem* PlayCall, FName HomeTeamId, FName AwayTeamId) const;

    /** How Player fits SchemeId, a coordinator with Development teaching it. Neutral when the
     *  scheme asks nothing of his position or plays the other side. */
    FPSSchemeFitResult GetSchemeFitIn(FName SchemeId, const FPlayerAttributes& Player, float Development) const;

    /** How Player fits the scheme his side's coordinator on TeamId runs. */
    UFUNCTION(BlueprintPure, Category = "Staff")
    FPSSchemeFitResult GetSchemeFit(FName TeamId, const FPlayerAttributes& Player) const;

    /** Player as he plays on TeamId: his Speed, Agility, Strength, Acceleration and Awareness
     *  times his fit multiplier (size and stamina untouched). The roster keeps his own ratings. */
    UFUNCTION(BlueprintPure, Category = "Staff")
    FPlayerAttributes ApplySchemeFit(FName TeamId, const FPlayerAttributes& Player) const;

    /** Runs the off-season coaching carousel on the season's standings and returns what happened,
     *  in order. Deterministic: the same staffs and standings give the same moves. */
    UFUNCTION(BlueprintCallable, Category = "Staff")
    TArray<FPSCarouselEvent> RunCarousel(const TArray<FPSTeamStanding>& Standings);

    /** Writes the coaches and staffs into the franchise save. */
    void SaveTo(UPSFranchiseSaveGame* Save) const;

    /** Reads them back; false (keeping the current ones) when the save has none. */
    bool LoadFrom(const UPSFranchiseSaveGame* Save);

    /** Problems with a league, one line each: empty or duplicate IDs, a coach's unknown scheme or
     *  a coordinator's scheme on the wrong side, a staff job held by an unknown coach or one of
     *  the wrong role, a coach on two staffs, a rating out of range. */
    static TArray<FString> Validate(const FPSCoachingLeague& InLeague);

private:
    FPSCoachDef* FindMutableCoach(FName CoachId);

    FPSTeamStaffDef* FindMutableStaff(FName TeamId);

    /** The team employing CoachId; null for a free coach. */
    const FPSTeamStaffDef* FindEmployer(FName CoachId) const;

    /** A player's weighted rating in a scheme; false when it asks nothing of his position. */
    bool GetFitComposite(const FPSSchemeDef& Scheme, const FPlayerAttributes& Player, float& OutComposite) const;

    UPROPERTY(Transient)
    FPSCoachingLeague League;
};
