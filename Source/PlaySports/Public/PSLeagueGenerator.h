// PSLeagueGenerator.h - Epic 122: fictional leagues, rosters and draft classes from a seed
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Math/RandomStream.h"
#include "PSLeagueGeneratorData.h"
#include "PSPlayerDNA.h"
#include "PSPlayerProgression.h"
#include "PSLeagueGenerator.generated.h"

class UPSPlayerAging;
class UPSRoster;

/** The pure rules of the league generator. */
namespace PSLeagueGenerator
{
    /** Name as the no-real-person policy compares it: lowercase letters and digits and single
     *  spaces, a hyphen read as a space and other punctuation dropped ("Ja'Marr Chase" ->
     *  "jamarr chase"). */
    PLAYSPORTS_API FString NormalizeName(const FString& Name);

    /** Every form a blocklisted name takes: each entry normalized, and its initial form ("j allen"
     *  for "Josh Allen"). */
    PLAYSPORTS_API TSet<FString> MakeBlockedForms(const TArray<FString>& NameBlocklist);

    /** True when Name, normalized, is one of BlockedForms. */
    PLAYSPORTS_API bool IsBlockedName(const TSet<FString>& BlockedForms, const FString& Name);

    /** A draw from the standard normal distribution (Box-Muller over Stream). */
    PLAYSPORTS_API float StandardNormal(FRandomStream& Stream);

    /** Prime's ratings at Age: the progression model (UPSPlayerProgression with Progression) is
     *  run over the years between Age and the prime window. A player younger than PeakAgeStart is
     *  below Prime by what those offseasons will add, so they bring him to it; one older than
     *  PeakAgeEnd has lost what the offseasons since took. Inside the window he is at Prime. */
    PLAYSPORTS_API FPlayerAttributes ApplyCareerArc(const FPlayerAttributes& Prime, int32 Age, const FPSProgressionTuning& Progression);

    /** Problems with Tuning, one line each; empty when it is sound. */
    PLAYSPORTS_API TArray<FString> ValidateTuning(const FPSLeagueGeneratorTuning& Tuning);
}

/**
 * UPSLeagueGenerator makes fictional leagues (Epic 122): teams of full rosters with plausible
 * names, league-realistic ratings, ages and DNA, all from a seed, so the same seed always makes
 * the same league. The game can generate one at runtime for a new franchise (MakeRoster gives
 * each team its UPSRoster), and WriteLeague lays one out as Data/ files that the content
 * contracts and the game's loaders accept.
 *
 *  - Names: a first and last name from one culture (Data/league_generator.json's NameCultures),
 *    drawn again when the name is already in the league or on the NameBlocklist of real people.
 *  - Ratings: each role's curves at his prime. A player's talent (normal, plus his team's and a
 *    veteran's experience) is shared across his ratings; each role's players are ranked best
 *    first by the contract market's overall rating (UPSContractNegotiation::RatePlayer).
 *  - Ages: an entry age, then years in the league drawn from the role's attrition, so rookies
 *    are the largest group and veterans thin out. Ratings follow his role's age curve to that
 *    age (PSLeagueGenerator::ApplyCareerArc with UPSPlayerAging::GetCurve, Epic 94's curves),
 *    so a young player grows into his prime and a veteran has declined from it, as he will go on
 *    to age in a franchise.
 *  - DNA: Epic 79's catalog and generation rule (PSPlayerDNA::GenerateProfile), centered on the
 *    league's own players.
 *  - Draft classes: GenerateDraftClass makes a year's prospects the same way, all rookies.
 */
UCLASS(Blueprintable)
class PLAYSPORTS_API UPSLeagueGenerator : public UObject
{
    GENERATED_BODY()

public:
    /** Data/league_generator.json under the project directory. */
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion. False, with the
     *  tuning unchanged, on a missing or malformed file. */
    UFUNCTION(BlueprintCallable, Category = "LeagueGenerator")
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSLeagueGeneratorTuning& GetTuning();

    void SetTuning(const FPSLeagueGeneratorTuning& InTuning);

    /** The DNA catalog generated players' styles come from. By default the world's
     *  UPSPlayerDNASubsystem's when there is one, else Data/player_dna.json. */
    void SetDNACatalog(const FPSPlayerDNACatalog& InCatalog);

    /** The age curve ratings follow for a role without its own. By default
     *  Data/player_progression.json. */
    void SetProgressionTuning(const FPSProgressionTuning& InTuning);

    /** The role age curves ratings follow (UPSPlayerAging::GetCurve, Epic 94): the same curves a
     *  franchise's players age on. By default one loaded from Data/legacy.json, falling back to
     *  the progression tuning above for a role it doesn't list. */
    void SetPlayerAging(UPSPlayerAging* InAging);

    /** Role's age curve: the aging system's for it, else the progression tuning. */
    FPSProgressionTuning GetCareerCurve(EPlayerRole Role);

    /**
     * A league of the tuning's NumTeams teams. BaseTeams (e.g. a teams file's rows) keep their
     * identities and come first; the rest get stand-in identities. Every team gets a generated
     * roster at Data/rosters/team_<TeamId>.json, and the config names Data/league_teams.json as
     * its teams file. An unsound tuning (ValidateTuning) generates an empty league.
     */
    UFUNCTION(BlueprintCallable, Category = "LeagueGenerator")
    FPSGeneratedLeague GenerateLeague(int32 Seed, const TArray<FPSTeamInfo>& BaseTeams);

    /**
     * The draft-class mode (Epic 86 drafts from it each year): NumTeams times the tuning's
     * ProspectsPerTeam rookies, roles in the shares a roster has, talent shifted by TalentShift.
     * The class depends only on Seed and DraftYear, so a franchise can regenerate any year's.
     * Names are new to LeaguePlayers (the league's rostered players), and DNA is centered on
     * them (on the class itself when they have none of a role).
     */
    UFUNCTION(BlueprintCallable, Category = "LeagueGenerator")
    FPSDraftClass GenerateDraftClass(int32 Seed, int32 DraftYear, int32 NumTeams, const TArray<FPlayerAttributes>& LeaguePlayers);

    /**
     * Writes League under RootDir the way Data/ is laid out: the league config at
     * Data/sample_league_config.json (where the game and tools/content.py read it), the teams
     * file the config names and each team's roster, UTF-8. OutWrittenFiles lists them, relative
     * to RootDir. False when a file could not be written.
     */
    UFUNCTION(BlueprintCallable, Category = "LeagueGenerator")
    static bool WriteLeague(const FPSGeneratedLeague& League, const FString& RootDir, TArray<FString>& OutWrittenFiles);

    /** A roster of Team's players with the default depth chart (best first), for a new
     *  franchise (UPSFranchiseFlow::SetTeamRoster). */
    UFUNCTION(BlueprintCallable, Category = "LeagueGenerator")
    static UPSRoster* MakeRoster(const FPSGeneratedTeam& Team, UObject* Outer);

private:
    /** Loads whatever has not been set: the tuning, the DNA catalog and the age curve. */
    void EnsureLoaded();

    /** False, logging why, when the tuning can't generate. */
    bool CheckTuning();

    /** The league's teams: BaseTeams, then stand-ins, each with its roster path. */
    TArray<FPSTeamInfo> MakeTeamIdentities(const TArray<FPSTeamInfo>& BaseTeams) const;

    /** One player of Profile: entry age and years in the league (none for a rookie), talent,
     *  prime ratings walked to his age, and a name. PlayerId and DNA come later. */
    FPlayerAttributes MakePlayer(const FPSRoleProfile& Profile, float TalentShift, float TalentSpread, bool bRookie, FRandomStream& Stream, TSet<FString>& UsedNames, const TSet<FString>& BlockedForms) const;

    /** A name new to UsedNames (which it joins) and not blocked. */
    FString DrawName(FRandomStream& Stream, TSet<FString>& UsedNames, const TSet<FString>& BlockedForms) const;

    /** Gives every player of Players his DNA, centered on CenterPool's players of his role (on
     *  Players' own when CenterPool has none of it). */
    void GiveDNA(TArray<FPlayerAttributes*>& Players, const TArray<FPlayerAttributes>& CenterPool, FRandomStream& Stream) const;

    UPROPERTY(Transient)
    FPSLeagueGeneratorTuning Tuning;

    UPROPERTY(Transient)
    FPSPlayerDNACatalog DNACatalog;

    UPROPERTY(Transient)
    FPSProgressionTuning Progression;

    bool bTuningLoaded = false;
    bool bDNACatalogSet = false;
    bool bProgressionSet = false;

    /** The role age curves (Epic 94); loaded in EnsureLoaded unless set. */
    UPROPERTY(Transient)
    UPSPlayerAging* Aging = nullptr;

    bool bAgingSet = false;

    /** Aging is the generator's own (its base curve follows SetProgressionTuning). */
    bool bOwnAging = false;
};
