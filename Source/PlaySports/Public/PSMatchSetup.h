// PSMatchSetup.h - which teams play this game: the match's one authority on home and away
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlayerAttributes.h"
#include "PSMatchSetup.generated.h"

class UPSFranchiseSeason;
class UPSPlayCallSubsystem;
class UPSStaffManager;

/** How the game came about: the front end's modes (Epic 101). */
UENUM(BlueprintType)
enum class EPSMatchMode : uint8
{
    PlayNow,
    Franchise,
    Practice
};

/**
 * UPSMatchSetup is the one authority on who plays this game: the home team, the away team, the
 * team the player controls and, for a franchise game, the season week. APSGameMode owns one per
 * match and fills it from its travel options; nothing else keeps the pairing.
 *
 *  - Team select (UPSMenuComponent, Epic 101) travels with "mode=PlayNow?team=<TeamId>": the
 *    player's team is at home against the next team in league order.
 *  - A franchise game comes from the season's schedule (InitializeForSeasonGame, or
 *    UPSFranchiseFlow::BuildUserMatch): the week's matchup with the player's team in it. It
 *    travels as ToOptions writes it, with explicit home=, away= and week=.
 *  - Without options (a level opened in the editor), the league's first two teams play.
 *  - At kickoff (ApplyStaffs) both teams' coaching staffs take over (Epic 89): each team's plan
 *    goes to the play-call authority, and each team's players play at their scheme fit.
 *  - The field (LoadFieldPlayers, ApplyStaffsToField): the game mode spawns the home team's
 *    offense against the away team's defense, each from its own team's roster and at its own
 *    staff's scheme fit.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSMatchSetup : public UObject
{
    GENERATED_BODY()

public:
    /** The travel options a front-end mode starts with: "mode=PlayNow", plus "?team=<TeamId>"
     *  when the player picked a team. */
    static FString BuildOptions(EPSMatchMode InMode, FName InUserTeamId);

    /** The options that travel this setup to the game's level: its mode, home and away teams,
     *  the player's team and the week. InitializeFromOptions reads them back unchanged. */
    UFUNCTION(BlueprintPure, Category = "Match")
    FString ToOptions() const;

    /** Reads travel Options (mode=, team=, home=, away=, week=) against the league's teams, in
     *  league order. home= and away= name the teams; otherwise the player's team (team=) is at
     *  home against the next team in league order, and without either the league's first two
     *  teams play. A team the league doesn't have is ignored with a warning. False, with nothing
     *  set, when the league has fewer than two teams. */
    UFUNCTION(BlueprintCallable, Category = "Match")
    bool InitializeFromOptions(const FString& Options, const TArray<FName>& LeagueTeamIds);

    /** The player's game in Week of Season, as the schedule has it: Franchise mode, the
     *  matchup's home and away teams. With no player team, the week's first matchup. False, with
     *  nothing changed, when the week has no such game (a bye, or past the season). */
    bool InitializeForSeasonGame(const UPSFranchiseSeason* Season, int32 Week, FName InUserTeamId);

    /** Sets both teams; false, with nothing changed, when either is None or they are the same. */
    UFUNCTION(BlueprintCallable, Category = "Match")
    bool SetTeams(FName InHomeTeamId, FName InAwayTeamId);

    /**
     * Kickoff (Epic 89): hands both teams' plans to the play-call authority
     * (UPSStaffManager::ApplyToPlayCall; PlayCall may be null for a quick sim) and puts every
     * player of HomePlayers and AwayPlayers at his own team's scheme fit
     * (UPSStaffManager::ApplySchemeFit). The rosters keep the players' own ratings; these are
     * the copies the game plays with. False when Staffs is null or a team has no staff (it then
     * calls from the whole playbook and its players play at their ratings).
     */
    bool ApplyStaffs(const UPSStaffManager* Staffs, UPSPlayCallSubsystem* PlayCall, TArray<FPlayerAttributes>& HomePlayers, TArray<FPlayerAttributes>& AwayPlayers) const;

    /**
     * The players who take the field at kickoff, with the home team on offense (it has the ball
     * first): the home team's offensive players, then the away team's defensive players (sides
     * by APSFieldGrid::GetSideForRole), each from its own team's roster (LoadTeamPlayers), at
     * their own ratings. False, with OutPlayers untouched, when the teams aren't set or either
     * team's roster can't be read; the game mode then keeps its RosterJsonPath players.
     */
    bool LoadFieldPlayers(const FString& TeamsJsonPath, TArray<FPlayerAttributes>& OutPlayers) const;

    /**
     * ApplyStaffs for the players on the field, both sides in one list: the offense plays at the
     * home team's scheme fit and the defense at the away team's, in place and in order. The game
     * mode applies it to the roster rows its pawns point at, so the pawns and the play
     * simulation's copies of them play at the same ratings.
     */
    bool ApplyStaffsToField(const UPSStaffManager* Staffs, UPSPlayCallSubsystem* PlayCall, TArray<FPlayerAttributes>& Players) const;

    /** The league's TeamIds in file order (Data/sample_teams.json), read through
     *  UPSDataIngestion; empty when the file can't be read. */
    static TArray<FName> LoadLeagueTeamIds(const FString& TeamsJsonPath);

    /** TeamId's players, from the roster file its TeamsJsonPath row names (RosterDataTablePath,
     *  under the project directory), read through UPSDataIngestion. False, with OutPlayers
     *  untouched, when the league has no such team or its roster can't be read. */
    static bool LoadTeamPlayers(const FString& TeamsJsonPath, FName TeamId, TArray<FPlayerAttributes>& OutPlayers);

    UFUNCTION(BlueprintPure, Category = "Match")
    EPSMatchMode GetMode() const { return Mode; }

    UFUNCTION(BlueprintPure, Category = "Match")
    FName GetHomeTeamId() const { return HomeTeamId; }

    UFUNCTION(BlueprintPure, Category = "Match")
    FName GetAwayTeamId() const { return AwayTeamId; }

    UFUNCTION(BlueprintPure, Category = "Match")
    FName GetTeamId(bool bHome) const { return bHome ? HomeTeamId : AwayTeamId; }

    /** The team the player controls; None when the CPU plays both. */
    UFUNCTION(BlueprintPure, Category = "Match")
    FName GetUserTeamId() const { return UserTeamId; }

    /** The season week of a franchise game; 0 otherwise. */
    UFUNCTION(BlueprintPure, Category = "Match")
    int32 GetSeasonWeek() const { return SeasonWeek; }

    /** True once both teams are set. */
    UFUNCTION(BlueprintPure, Category = "Match")
    bool HasTeams() const { return !HomeTeamId.IsNone() && !AwayTeamId.IsNone(); }

    static FString ModeToString(EPSMatchMode InMode);

    /** The mode a "mode=" option names; PlayNow for anything else. */
    static EPSMatchMode ModeFromString(const FString& Text);

private:
    UPROPERTY(Transient)
    EPSMatchMode Mode = EPSMatchMode::PlayNow;

    UPROPERTY(Transient)
    FName HomeTeamId;

    UPROPERTY(Transient)
    FName AwayTeamId;

    UPROPERTY(Transient)
    FName UserTeamId;

    UPROPERTY(Transient)
    int32 SeasonWeek = 0;
};
