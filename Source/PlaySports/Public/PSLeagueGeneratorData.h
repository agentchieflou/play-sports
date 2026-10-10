// PSLeagueGeneratorData.h - Epic 122: the roster and league generator's tuning and what it makes
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSLeagueData.h"
#include "PSLeagueGeneratorData.generated.h"

/** One attribute's curve for a role at his prime (Data/league_generator.json). A generated
 *  player's value is Mean + StdDev * (TalentWeight * his talent + sqrt(1 - TalentWeight^2) * a
 *  draw of his own), kept within Min..Max. Attribute names a float field of FPlayerAttributes: a
 *  rating (Speed ... Stamina, 0-100) or his body (WeightKg, HeightCm). */
USTRUCT(BlueprintType)
struct FPSAttributeCurve
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FName Attribute;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float Mean = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float StdDev = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float Min = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float Max = 99.f;

    /** How much of the spread is the player's talent, which all his ratings share (0-1); the
     *  rest is his own for this attribute. 0 for his body. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float TalentWeight = 0.f;
};

/** How the generator makes the players of one role. */
USTRUCT(BlueprintType)
struct FPSRoleProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    EPlayerRole Role = EPlayerRole::Quarterback;

    /** Players of this role on a generated roster. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 RosterCount = 0;

    /** The role's code in generated PlayerIds, e.g. QB in "T05_QB_001". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FString IdCode;

    /** The share of a year's entrants still in the league falls by this each year (above 0,
     *  below 1): how fast the role's age pyramid thins from rookies to veterans. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float Attrition = 0.2f;

    /** The oldest a generated player of this role is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 MaxAge = 36;

    /** One curve for every float field of FPlayerAttributes, at his prime: the progression
     *  curve (Data/player_progression.json) takes a younger player below it and an older one
     *  past his decline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPSAttributeCurve> Attributes;

    const FPSAttributeCurve* FindCurve(FName Attribute) const
    {
        return Attributes.FindByPredicate([Attribute](const FPSAttributeCurve& Curve) { return Curve.Attribute == Attribute; });
    }
};

/** A naming tradition: a player's first and last names come from one culture. */
USTRUCT(BlueprintType)
struct FPSNameCulture
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FName Culture;

    /** Its relative share of generated players. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float Weight = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FString> FirstNames;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FString> LastNames;
};

/** The draft-class mode (UPSLeagueGenerator::GenerateDraftClass; Epic 86 drafts from it). */
USTRUCT(BlueprintType)
struct FPSDraftClassTuning
{
    GENERATED_BODY()

    /** Prospects in a class per team in the league: the drafted rounds and an undrafted pool. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 ProspectsPerTeam = 8;

    /** A class's talent next to the league's at the same age: added to each prospect's talent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float TalentShift = -0.4f;

    /** The spread of a prospect's talent (the league's is 1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float TalentSpread = 1.1f;
};

/** Everything the roster and league generator draws from (Data/league_generator.json, Epic 122;
 *  Architecture rule 4). PSLeagueGenerator::ValidateTuning checks it. */
USTRUCT(BlueprintType)
struct FPSLeagueGeneratorTuning
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FString LeagueName = TEXT("PlaySports League");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 NumTeams = 32;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 NumWeeks = 17;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<int32> ByeWeekNumbers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 NumPlayoffTeams = 12;

    /** The league's divisions; a team the caller gives none joins the one with fewest teams. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FString> Divisions;

    /** A team the caller doesn't supply gets a stand-in identity until Epic 123 generates real
     *  ones: "<PlaceholderTeamName> 05", TeamId and abbreviation T05 ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FString PlaceholderTeamName = TEXT("League Team");

    /** ... and colors ("#RRGGBB") taken in turn from these. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FString> PlaceholderColors;

    /** The spread of a team's talent (in player-talent units), added to all its players'. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float TeamTalentSpread = 0.3f;

    /** A veteran's talent rises this much per year he has lasted in the league (the ones who
     *  last are the good ones) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float TalentPerExperienceYear = 0.06f;

    /** ... up to this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    float MaxExperienceTalent = 0.5f;

    /** A player enters the league (a rookie, or a prospect in a draft class) at an age from
     *  EntryAgeMin to EntryAgeMax. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 EntryAgeMin = 21;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 EntryAgeMax = 23;

    /** One profile for every EPlayerRole. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPSRoleProfile> RoleProfiles;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPSNameCulture> NameCultures;

    /** Real people the generator must never name a player after (the no-real-person policy):
     *  a generated name is drawn again when it matches one, or its initial form ("J. Allen"),
     *  ignoring case and punctuation. tools/validate_data.py holds every roster to the same list. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FString> NameBlocklist;

    /** Draws for a name that is new to the league and not blocked, before a middle initial
     *  separates the last one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 MaxNameAttempts = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FPSDraftClassTuning DraftClass;

    const FPSRoleProfile* FindRole(EPlayerRole Role) const
    {
        return RoleProfiles.FindByPredicate([Role](const FPSRoleProfile& Profile) { return Profile.Role == Role; });
    }
};

/** One generated team: its identity and its roster, each role's players best first (so
 *  UPSRoster::BuildDefaultDepthChart starts the best). */
USTRUCT(BlueprintType)
struct FPSGeneratedTeam
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FPSTeamInfo Team;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPlayerAttributes> Players;
};

/** A generated league (UPSLeagueGenerator::GenerateLeague): the same seed and tuning always
 *  make the same league. */
USTRUCT(BlueprintType)
struct FPSGeneratedLeague
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 Seed = 0;

    /** Its TeamsDataTablePath is where UPSLeagueGenerator::WriteLeague puts the teams file. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    FPSLeagueConfig Config;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPSGeneratedTeam> Teams;
};

/** A generated draft class (UPSLeagueGenerator::GenerateDraftClass): rookies at their entry
 *  age, PlayerIds DC<DraftYear>_<NNN>, grouped by role in RoleProfiles order and not ranked. */
USTRUCT(BlueprintType)
struct FPSDraftClass
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    int32 DraftYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LeagueGenerator")
    TArray<FPlayerAttributes> Prospects;
};
