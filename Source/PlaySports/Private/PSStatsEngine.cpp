#include "PSStatsEngine.h"
#include "PSFranchiseSaveGame.h"

namespace PSStatsEnginePrivate
{
    /** A snap at or past this yard line is inside the opponent's 20: the red zone. */
    constexpr int32 RedZoneYardLine = 80;

    /** Passer-rating terms are each held to 0 .. this (the NFL formula). */
    constexpr float PasserRatingTermCap = 2.375f;

    static bool IsKickResult(const FString& Result)
    {
        return Result == TEXT("FieldGoalGood") || Result == TEXT("FieldGoalMissed") || Result == TEXT("KickoffResult") || Result == TEXT("PuntResult");
    }

    static float Ratio(int32 Part, int32 Whole)
    {
        return Whole > 0 ? static_cast<float>(Part) / static_cast<float>(Whole) : 0.f;
    }

    static const TArray<EPSStatCategory>& PlayerCategories()
    {
        static const TArray<EPSStatCategory> Categories = {
            EPSStatCategory::PassingYards, EPSStatCategory::PassingTouchdowns, EPSStatCategory::Completions, EPSStatCategory::InterceptionsThrown,
            EPSStatCategory::RushingYards, EPSStatCategory::RushingTouchdowns, EPSStatCategory::Receptions, EPSStatCategory::ReceivingYards,
            EPSStatCategory::ReceivingTouchdowns, EPSStatCategory::Tackles, EPSStatCategory::Sacks, EPSStatCategory::Interceptions };
        return Categories;
    }

    static const TArray<EPSStatCategory>& TeamCategories()
    {
        static const TArray<EPSStatCategory> Categories = { EPSStatCategory::TeamPoints, EPSStatCategory::TeamTotalYards };
        return Categories;
    }

    static void SortLeaders(TArray<FPSLeaderEntry>& Leaders, int32 Count)
    {
        Leaders.RemoveAll([](const FPSLeaderEntry& Entry) { return Entry.Value <= 0; });
        Leaders.Sort([](const FPSLeaderEntry& A, const FPSLeaderEntry& B)
        {
            if (A.Value != B.Value)
            {
                return A.Value > B.Value;
            }
            return A.Id != B.Id ? A.Id.LexicalLess(B.Id) : A.Week < B.Week;
        });
        if (Count > 0 && Leaders.Num() > Count)
        {
            Leaders.SetNum(Count);
        }
    }
}

// --- Stat lines --------------------------------------------------------------------------

void FPSPlayerStatLine::Accumulate(const FPSPlayerStatLine& Other)
{
    if (!Other.TeamId.IsNone())
    {
        TeamId = Other.TeamId;
    }
    Games += Other.Games;
    PassAttempts += Other.PassAttempts;
    Completions += Other.Completions;
    PassingYards += Other.PassingYards;
    PassingTouchdowns += Other.PassingTouchdowns;
    InterceptionsThrown += Other.InterceptionsThrown;
    TimesSacked += Other.TimesSacked;
    RushAttempts += Other.RushAttempts;
    RushingYards += Other.RushingYards;
    RushingTouchdowns += Other.RushingTouchdowns;
    Targets += Other.Targets;
    Receptions += Other.Receptions;
    ReceivingYards += Other.ReceivingYards;
    ReceivingTouchdowns += Other.ReceivingTouchdowns;
    Tackles += Other.Tackles;
    Sacks += Other.Sacks;
    Interceptions += Other.Interceptions;
}

int32 FPSPlayerStatLine::GetValue(EPSStatCategory Category) const
{
    switch (Category)
    {
    case EPSStatCategory::PassingYards:        return PassingYards;
    case EPSStatCategory::PassingTouchdowns:   return PassingTouchdowns;
    case EPSStatCategory::Completions:         return Completions;
    case EPSStatCategory::InterceptionsThrown: return InterceptionsThrown;
    case EPSStatCategory::RushingYards:        return RushingYards;
    case EPSStatCategory::RushingTouchdowns:   return RushingTouchdowns;
    case EPSStatCategory::Receptions:          return Receptions;
    case EPSStatCategory::ReceivingYards:      return ReceivingYards;
    case EPSStatCategory::ReceivingTouchdowns: return ReceivingTouchdowns;
    case EPSStatCategory::Tackles:             return Tackles;
    case EPSStatCategory::Sacks:               return Sacks;
    case EPSStatCategory::Interceptions:       return Interceptions;
    default:                                   return 0;
    }
}

void FPSTeamStatLine::Accumulate(const FPSTeamStatLine& Other)
{
    Games += Other.Games;
    Points += Other.Points;
    PointsAllowed += Other.PointsAllowed;
    Plays += Other.Plays;
    TotalYards += Other.TotalYards;
    PassingYards += Other.PassingYards;
    RushingYards += Other.RushingYards;
    FirstDowns += Other.FirstDowns;
    Turnovers += Other.Turnovers;
    Takeaways += Other.Takeaways;
    FieldGoalsMade += Other.FieldGoalsMade;
    FieldGoalsAttempted += Other.FieldGoalsAttempted;
    for (const FPSSplitLine& Split : Other.Splits)
    {
        FPSSplitLine& Mine = FindOrAddSplit(Split.Situation);
        Mine.Plays += Split.Plays;
        Mine.Yards += Split.Yards;
        Mine.Conversions += Split.Conversions;
        Mine.Touchdowns += Split.Touchdowns;
    }
}

FPSSplitLine& FPSTeamStatLine::FindOrAddSplit(FName Situation)
{
    if (FPSSplitLine* Found = Splits.FindByPredicate([Situation](const FPSSplitLine& Split) { return Split.Situation == Situation; }))
    {
        return *Found;
    }
    FPSSplitLine& Added = Splits.AddDefaulted_GetRef();
    Added.Situation = Situation;
    return Added;
}

const FPSSplitLine* FPSTeamStatLine::FindSplit(FName Situation) const
{
    return Splits.FindByPredicate([Situation](const FPSSplitLine& Split) { return Split.Situation == Situation; });
}

int32 FPSTeamStatLine::GetValue(EPSStatCategory Category) const
{
    switch (Category)
    {
    case EPSStatCategory::TeamPoints:     return Points;
    case EPSStatCategory::TeamTotalYards: return TotalYards;
    default:                              return 0;
    }
}

// --- The engine --------------------------------------------------------------------------

const FName UPSStatsEngine::RedZoneSplit(TEXT("RedZone"));

FName UPSStatsEngine::DownSplit(int32 Down)
{
    return FName(*FString::Printf(TEXT("Down%d"), FMath::Clamp(Down, 1, 4)));
}

bool UPSStatsEngine::IsTeamCategory(EPSStatCategory Category)
{
    return Category == EPSStatCategory::TeamPoints || Category == EPSStatCategory::TeamTotalYards;
}

bool UPSStatsEngine::StartSeason(int32 Season)
{
    if (Book.Games.Num() > 0)
    {
        return false;
    }
    Book.Season = Season;
    return true;
}

void UPSStatsEngine::EndSeason()
{
    if (Book.Games.Num() > 0)
    {
        FPSSeasonStats& Archived = Book.History.AddDefaulted_GetRef();
        Archived.Season = Book.Season;
        SeasonPlayerTotals(Book.Season).GenerateValueArray(Archived.Players);
        SeasonTeamTotals(Book.Season).GenerateValueArray(Archived.Teams);
    }
    Book.Games.Reset();
    Book.Season = FMath::Max(Book.Season, 0) + 1;
    bGameInProgress = false;
}

void UPSStatsEngine::BeginGame(int32 Week, FName HomeTeamId, FName AwayTeamId)
{
    if (Book.Season <= 0)
    {
        Book.Season = 1;
    }
    Current = FPSBoxScore();
    Current.Season = Book.Season;
    Current.Week = Week;
    Current.Home.TeamId = HomeTeamId;
    Current.Home.Games = 1;
    Current.Away.TeamId = AwayTeamId;
    Current.Away.Games = 1;
    bGameInProgress = true;
}

FPSPlayerStatLine& UPSStatsEngine::FindOrAddPlayer(FName PlayerId, FName TeamId)
{
    if (FPSPlayerStatLine* Found = Current.Players.FindByPredicate([PlayerId](const FPSPlayerStatLine& Line) { return Line.PlayerId == PlayerId; }))
    {
        return *Found;
    }
    FPSPlayerStatLine& Added = Current.Players.AddDefaulted_GetRef();
    Added.PlayerId = PlayerId;
    Added.TeamId = TeamId;
    Added.Games = 1;
    return Added;
}

void UPSStatsEngine::RecordPlay(const FPSTelemetryPlayResultEvent& Event)
{
    using namespace PSStatsEnginePrivate;

    if (!bGameInProgress)
    {
        return;
    }

    FPSTeamStatLine& Offense = Event.bHomeOffense ? Current.Home : Current.Away;
    FPSTeamStatLine& Defense = Event.bHomeOffense ? Current.Away : Current.Home;
    ++Current.PlayCount;
    Current.Home.Points += Event.HomePoints;
    Current.Home.PointsAllowed += Event.AwayPoints;
    Current.Away.Points += Event.AwayPoints;
    Current.Away.PointsAllowed += Event.HomePoints;

    if (IsKickResult(Event.Result))
    {
        if (Event.Result == TEXT("FieldGoalGood") || Event.Result == TEXT("FieldGoalMissed"))
        {
            ++Offense.FieldGoalsAttempted;
            Offense.FieldGoalsMade += Event.Result == TEXT("FieldGoalGood") ? 1 : 0;
        }
        return;
    }

    // A scrimmage play: the offense's, its situation's split, and each player's part in it.
    const int32 Yards = Event.YardsGained;
    const bool bTouchdown = Event.Result == TEXT("Touchdown");
    ++Offense.Plays;
    Offense.FirstDowns += Event.bFirstDown ? 1 : 0;
    for (const FName Situation : { DownSplit(Event.Down), Event.YardLine >= RedZoneYardLine ? RedZoneSplit : FName() })
    {
        if (!Situation.IsNone())
        {
            FPSSplitLine& Split = Offense.FindOrAddSplit(Situation);
            ++Split.Plays;
            Split.Yards += Yards;
            Split.Conversions += Event.bFirstDown ? 1 : 0;
            Split.Touchdowns += bTouchdown ? 1 : 0;
        }
    }

    const FName OffenseTeam = Offense.TeamId;
    const FName DefenseTeam = Defense.TeamId;
    if (!Event.TacklerId.IsNone() && !bTouchdown)
    {
        FPSPlayerStatLine& Tackler = FindOrAddPlayer(Event.TacklerId, DefenseTeam);
        ++Tackler.Tackles;
        Tackler.Sacks += Event.bSack ? 1 : 0;
    }

    if (!Event.bPass)
    {
        Offense.TotalYards += Yards;
        Offense.RushingYards += Yards;
        if (!Event.RusherId.IsNone())
        {
            FPSPlayerStatLine& Rusher = FindOrAddPlayer(Event.RusherId, OffenseTeam);
            ++Rusher.RushAttempts;
            Rusher.RushingYards += Yards;
            Rusher.RushingTouchdowns += bTouchdown ? 1 : 0;
        }
        return;
    }

    if (Event.bSack)
    {
        Offense.TotalYards += Yards;
        Offense.PassingYards += Yards;
        if (!Event.PasserId.IsNone())
        {
            ++FindOrAddPlayer(Event.PasserId, OffenseTeam).TimesSacked;
        }
        return;
    }

    if (!Event.PasserId.IsNone())
    {
        ++FindOrAddPlayer(Event.PasserId, OffenseTeam).PassAttempts;
    }
    if (!Event.ReceiverId.IsNone())
    {
        ++FindOrAddPlayer(Event.ReceiverId, OffenseTeam).Targets;
    }

    if (Event.bInterception)
    {
        ++Offense.Turnovers;
        ++Defense.Takeaways;
        if (!Event.PasserId.IsNone())
        {
            ++FindOrAddPlayer(Event.PasserId, OffenseTeam).InterceptionsThrown;
        }
        if (!Event.InterceptorId.IsNone())
        {
            ++FindOrAddPlayer(Event.InterceptorId, DefenseTeam).Interceptions;
        }
        return;
    }

    if (Event.bComplete)
    {
        Offense.TotalYards += Yards;
        Offense.PassingYards += Yards;
        if (!Event.PasserId.IsNone())
        {
            FPSPlayerStatLine& Passer = FindOrAddPlayer(Event.PasserId, OffenseTeam);
            ++Passer.Completions;
            Passer.PassingYards += Yards;
            Passer.PassingTouchdowns += bTouchdown ? 1 : 0;
        }
        if (!Event.ReceiverId.IsNone())
        {
            FPSPlayerStatLine& Receiver = FindOrAddPlayer(Event.ReceiverId, OffenseTeam);
            ++Receiver.Receptions;
            Receiver.ReceivingYards += Yards;
            Receiver.ReceivingTouchdowns += bTouchdown ? 1 : 0;
        }
    }
}

void UPSStatsEngine::FinishGame()
{
    if (!bGameInProgress)
    {
        return;
    }
    bGameInProgress = false;
    Current.bFinal = true;
    Book.Games.Add(Current);
    UpdateRecords(Current);
}

void UPSStatsEngine::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (Bus)
    {
        BoundBus = Bus;
        PlayResultHandle = Bus->OnPlayResultMC.AddUObject(this, &UPSStatsEngine::RecordPlay);
    }
}

void UPSStatsEngine::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayResultMC.Remove(PlayResultHandle);
    }
    BoundBus.Reset();
    PlayResultHandle.Reset();
}

// --- Aggregation -------------------------------------------------------------------------

TMap<FName, FPSPlayerStatLine> UPSStatsEngine::SeasonPlayerTotals(int32 Season) const
{
    TMap<FName, FPSPlayerStatLine> Totals;
    if (Season == Book.Season)
    {
        for (const FPSBoxScore& Game : Book.Games)
        {
            for (const FPSPlayerStatLine& Line : Game.Players)
            {
                FPSPlayerStatLine& Total = Totals.FindOrAdd(Line.PlayerId);
                Total.PlayerId = Line.PlayerId;
                Total.Accumulate(Line);
            }
        }
    }
    else if (const FPSSeasonStats* Past = Book.History.FindByPredicate([Season](const FPSSeasonStats& Stats) { return Stats.Season == Season; }))
    {
        for (const FPSPlayerStatLine& Line : Past->Players)
        {
            Totals.Add(Line.PlayerId, Line);
        }
    }
    return Totals;
}

TMap<FName, FPSTeamStatLine> UPSStatsEngine::SeasonTeamTotals(int32 Season) const
{
    TMap<FName, FPSTeamStatLine> Totals;
    if (Season == Book.Season)
    {
        for (const FPSBoxScore& Game : Book.Games)
        {
            for (const FPSTeamStatLine* Line : { &Game.Home, &Game.Away })
            {
                FPSTeamStatLine& Total = Totals.FindOrAdd(Line->TeamId);
                Total.TeamId = Line->TeamId;
                Total.Accumulate(*Line);
            }
        }
    }
    else if (const FPSSeasonStats* Past = Book.History.FindByPredicate([Season](const FPSSeasonStats& Stats) { return Stats.Season == Season; }))
    {
        for (const FPSTeamStatLine& Line : Past->Teams)
        {
            Totals.Add(Line.TeamId, Line);
        }
    }
    return Totals;
}

TMap<FName, FPSPlayerStatLine> UPSStatsEngine::CareerPlayerTotals() const
{
    TMap<FName, FPSPlayerStatLine> Totals;
    const auto Add = [&Totals](const FPSPlayerStatLine& Line)
    {
        FPSPlayerStatLine& Total = Totals.FindOrAdd(Line.PlayerId);
        Total.PlayerId = Line.PlayerId;
        Total.Accumulate(Line);
    };
    for (const FPSSeasonStats& Past : Book.History)
    {
        for (const FPSPlayerStatLine& Line : Past.Players)
        {
            Add(Line);
        }
    }
    for (const TPair<FName, FPSPlayerStatLine>& Pair : SeasonPlayerTotals(Book.Season))
    {
        Add(Pair.Value);
    }
    return Totals;
}

TMap<FName, FPSTeamStatLine> UPSStatsEngine::FranchiseTeamTotals() const
{
    TMap<FName, FPSTeamStatLine> Totals;
    const auto Add = [&Totals](const FPSTeamStatLine& Line)
    {
        FPSTeamStatLine& Total = Totals.FindOrAdd(Line.TeamId);
        Total.TeamId = Line.TeamId;
        Total.Accumulate(Line);
    };
    for (const FPSSeasonStats& Past : Book.History)
    {
        for (const FPSTeamStatLine& Line : Past.Teams)
        {
            Add(Line);
        }
    }
    for (const TPair<FName, FPSTeamStatLine>& Pair : SeasonTeamTotals(Book.Season))
    {
        Add(Pair.Value);
    }
    return Totals;
}

bool UPSStatsEngine::FindGame(int32 Week, FName TeamId, FPSBoxScore& OutGame) const
{
    const FPSBoxScore* Found = Book.Games.FindByPredicate([Week, TeamId](const FPSBoxScore& Game)
    {
        return Game.Week == Week && (Game.Home.TeamId == TeamId || Game.Away.TeamId == TeamId);
    });
    if (Found)
    {
        OutGame = *Found;
    }
    return Found != nullptr;
}

FPSPlayerStatLine UPSStatsEngine::GetPlayerSeason(FName PlayerId, int32 Season) const
{
    FPSPlayerStatLine Line = SeasonPlayerTotals(Season).FindRef(PlayerId);
    Line.PlayerId = PlayerId;
    return Line;
}

FPSPlayerStatLine UPSStatsEngine::GetPlayerCareer(FName PlayerId) const
{
    FPSPlayerStatLine Line = CareerPlayerTotals().FindRef(PlayerId);
    Line.PlayerId = PlayerId;
    return Line;
}

FPSTeamStatLine UPSStatsEngine::GetTeamSeason(FName TeamId, int32 Season) const
{
    FPSTeamStatLine Line = SeasonTeamTotals(Season).FindRef(TeamId);
    Line.TeamId = TeamId;
    return Line;
}

FPSTeamStatLine UPSStatsEngine::GetFranchiseTotals(FName TeamId) const
{
    FPSTeamStatLine Line = FranchiseTeamTotals().FindRef(TeamId);
    Line.TeamId = TeamId;
    return Line;
}

FPSTeamStatLine UPSStatsEngine::GetLeagueTotals(int32 Season) const
{
    FPSTeamStatLine League;
    for (const TPair<FName, FPSTeamStatLine>& Pair : SeasonTeamTotals(Season))
    {
        League.Accumulate(Pair.Value);
    }
    return League;
}

TArray<FPSLeaderEntry> UPSStatsEngine::GetLeaders(EPSStatCategory Category, EPSStatScope Scope, int32 Season, int32 Count) const
{
    TArray<FPSLeaderEntry> Leaders;
    const bool bTeam = IsTeamCategory(Category);
    const auto AddEntry = [&Leaders](FName Id, FName TeamId, int32 Value, int32 Week)
    {
        FPSLeaderEntry& Entry = Leaders.AddDefaulted_GetRef();
        Entry.Id = Id;
        Entry.TeamId = TeamId;
        Entry.Value = Value;
        Entry.Week = Week;
    };

    if (Scope == EPSStatScope::Game)
    {
        for (const FPSBoxScore& Game : Book.Games)
        {
            if (bTeam)
            {
                AddEntry(Game.Home.TeamId, Game.Home.TeamId, Game.Home.GetValue(Category), Game.Week);
                AddEntry(Game.Away.TeamId, Game.Away.TeamId, Game.Away.GetValue(Category), Game.Week);
                continue;
            }
            for (const FPSPlayerStatLine& Line : Game.Players)
            {
                AddEntry(Line.PlayerId, Line.TeamId, Line.GetValue(Category), Game.Week);
            }
        }
    }
    else if (bTeam)
    {
        for (const TPair<FName, FPSTeamStatLine>& Pair : Scope == EPSStatScope::Season ? SeasonTeamTotals(Season) : FranchiseTeamTotals())
        {
            AddEntry(Pair.Key, Pair.Key, Pair.Value.GetValue(Category), 0);
        }
    }
    else
    {
        for (const TPair<FName, FPSPlayerStatLine>& Pair : Scope == EPSStatScope::Season ? SeasonPlayerTotals(Season) : CareerPlayerTotals())
        {
            AddEntry(Pair.Key, Pair.Value.TeamId, Pair.Value.GetValue(Category), 0);
        }
    }

    PSStatsEnginePrivate::SortLeaders(Leaders, Count);
    return Leaders;
}

bool UPSStatsEngine::FindRecord(EPSStatCategory Category, EPSStatScope Scope, FPSRecordEntry& OutRecord) const
{
    const FPSRecordEntry* Found = Book.Records.FindByPredicate([Category, Scope](const FPSRecordEntry& Record)
    {
        return Record.Category == Category && Record.Scope == Scope;
    });
    if (Found)
    {
        OutRecord = *Found;
    }
    return Found != nullptr;
}

// --- The record book ---------------------------------------------------------------------

void UPSStatsEngine::UpdateRecords(const FPSBoxScore& Game)
{
    using namespace PSStatsEnginePrivate;

    // Single games: this game's lines.
    for (const EPSStatCategory Category : PlayerCategories())
    {
        for (const FPSPlayerStatLine& Line : Game.Players)
        {
            CheckRecord(Category, EPSStatScope::Game, Line.PlayerId, Line.TeamId, Line.GetValue(Category), Game.Week);
        }
    }
    for (const EPSStatCategory Category : TeamCategories())
    {
        for (const FPSTeamStatLine* Line : { &Game.Home, &Game.Away })
        {
            CheckRecord(Category, EPSStatScope::Game, Line->TeamId, Line->TeamId, Line->GetValue(Category), Game.Week);
        }
    }

    // Seasons and careers: the totals of those who played in it.
    const TMap<FName, FPSPlayerStatLine> SeasonPlayers = SeasonPlayerTotals(Book.Season);
    const TMap<FName, FPSPlayerStatLine> CareerPlayers = CareerPlayerTotals();
    const TMap<FName, FPSTeamStatLine> SeasonTeams = SeasonTeamTotals(Book.Season);
    const TMap<FName, FPSTeamStatLine> FranchiseTeams = FranchiseTeamTotals();
    for (const EPSStatCategory Category : PlayerCategories())
    {
        for (const FPSPlayerStatLine& Line : Game.Players)
        {
            const FPSPlayerStatLine SeasonLine = SeasonPlayers.FindRef(Line.PlayerId);
            const FPSPlayerStatLine CareerLine = CareerPlayers.FindRef(Line.PlayerId);
            CheckRecord(Category, EPSStatScope::Season, Line.PlayerId, Line.TeamId, SeasonLine.GetValue(Category), Game.Week);
            CheckRecord(Category, EPSStatScope::Career, Line.PlayerId, Line.TeamId, CareerLine.GetValue(Category), Game.Week);
        }
    }
    for (const EPSStatCategory Category : TeamCategories())
    {
        for (const FName TeamId : { Game.Home.TeamId, Game.Away.TeamId })
        {
            CheckRecord(Category, EPSStatScope::Season, TeamId, TeamId, SeasonTeams.FindRef(TeamId).GetValue(Category), Game.Week);
            CheckRecord(Category, EPSStatScope::Career, TeamId, TeamId, FranchiseTeams.FindRef(TeamId).GetValue(Category), Game.Week);
        }
    }
}

void UPSStatsEngine::CheckRecord(EPSStatCategory Category, EPSStatScope Scope, FName HolderId, FName TeamId, int32 Value, int32 Week)
{
    if (Value <= 0 || HolderId.IsNone())
    {
        return;
    }

    FPSRecordEntry* Record = Book.Records.FindByPredicate([Category, Scope](const FPSRecordEntry& Entry)
    {
        return Entry.Category == Category && Entry.Scope == Scope;
    });
    if (!Record)
    {
        // The first mark in a category sets the record quietly: there was nothing to break.
        Record = &Book.Records.AddDefaulted_GetRef();
        Record->Category = Category;
        Record->Scope = Scope;
        Record->HolderId = HolderId;
        Record->TeamId = TeamId;
        Record->Value = Value;
        Record->Season = Book.Season;
        Record->Week = Week;
        return;
    }
    if (Value <= Record->Value)
    {
        return;
    }

    // A single-game record falls every time it is beaten; a season or career record when someone
    // new passes the holder (or, for a season, the holder in a later season). Otherwise the holder
    // is only adding to his own mark.
    const bool bNewHolder = Record->HolderId != HolderId;
    const bool bBroken = Scope == EPSStatScope::Game || bNewHolder || (Scope == EPSStatScope::Season && Record->Season != Book.Season);
    FPSTelemetryRecordBrokenEvent Event;
    Event.Category = Category;
    Event.Scope = Scope;
    Event.bTeamRecord = IsTeamCategory(Category);
    Event.HolderId = HolderId;
    Event.TeamId = TeamId;
    Event.Value = Value;
    Event.PreviousHolderId = Record->HolderId;
    Event.PreviousValue = Record->Value;
    Event.Season = Book.Season;
    Event.Week = Week;
    Event.Description = FString::Printf(TEXT("%s %s record: %s %d (was %d by %s)"),
        *StaticEnum<EPSStatScope>()->GetNameStringByValue(static_cast<int64>(Scope)), *StaticEnum<EPSStatCategory>()->GetNameStringByValue(static_cast<int64>(Category)),
        *HolderId.ToString(), Value, Record->Value, *Record->HolderId.ToString());

    Record->HolderId = HolderId;
    Record->TeamId = TeamId;
    Record->Value = Value;
    Record->Season = Book.Season;
    Record->Week = Week;

    if (bBroken)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSStatsEngine: %s"), *Event.Description);
        OnRecordBroken.Broadcast(Event);
        if (UPSTelemetryBus* Bus = BoundBus.Get())
        {
            Bus->PublishRecordBroken(Event);
        }
    }
}

// --- Derived metrics ---------------------------------------------------------------------

float UPSStatsEngine::ComputePasserRating(const FPSPlayerStatLine& Line)
{
    using namespace PSStatsEnginePrivate;

    if (Line.PassAttempts <= 0)
    {
        return 0.f;
    }
    const float Attempts = static_cast<float>(Line.PassAttempts);
    const float Completion = FMath::Clamp((Line.Completions / Attempts - 0.3f) * 5.f, 0.f, PasserRatingTermCap);
    const float YardsPer = FMath::Clamp((Line.PassingYards / Attempts - 3.f) * 0.25f, 0.f, PasserRatingTermCap);
    const float TouchdownRate = FMath::Clamp(Line.PassingTouchdowns / Attempts * 20.f, 0.f, PasserRatingTermCap);
    const float InterceptionRate = FMath::Clamp(PasserRatingTermCap - Line.InterceptionsThrown / Attempts * 25.f, 0.f, PasserRatingTermCap);
    return (Completion + YardsPer + TouchdownRate + InterceptionRate) / 6.f * 100.f;
}

FPSPlayerMetrics UPSStatsEngine::ComputePlayerMetrics(const FPSPlayerStatLine& Line)
{
    using namespace PSStatsEnginePrivate;

    FPSPlayerMetrics Metrics;
    Metrics.CompletionPercentage = Ratio(Line.Completions, Line.PassAttempts) * 100.f;
    Metrics.YardsPerAttempt = Ratio(Line.PassingYards, Line.PassAttempts);
    Metrics.TouchdownPercentage = Ratio(Line.PassingTouchdowns, Line.PassAttempts) * 100.f;
    Metrics.InterceptionPercentage = Ratio(Line.InterceptionsThrown, Line.PassAttempts) * 100.f;
    Metrics.PasserRating = ComputePasserRating(Line);
    Metrics.YardsPerCarry = Ratio(Line.RushingYards, Line.RushAttempts);
    Metrics.YardsPerReception = Ratio(Line.ReceivingYards, Line.Receptions);
    Metrics.CatchRate = Ratio(Line.Receptions, Line.Targets);
    return Metrics;
}

FPSTeamMetrics UPSStatsEngine::ComputeTeamMetrics(const FPSTeamStatLine& Line)
{
    using namespace PSStatsEnginePrivate;

    FPSTeamMetrics Metrics;
    Metrics.YardsPerPlay = Ratio(Line.TotalYards, Line.Plays);
    Metrics.PointsPerGame = Ratio(Line.Points, Line.Games);
    if (const FPSSplitLine* Third = Line.FindSplit(DownSplit(3)))
    {
        Metrics.ThirdDownConversionRate = Ratio(Third->Conversions, Third->Plays);
    }
    if (const FPSSplitLine* RedZone = Line.FindSplit(RedZoneSplit))
    {
        Metrics.RedZoneTouchdownRate = Ratio(RedZone->Touchdowns, RedZone->Plays);
    }
    Metrics.FieldGoalPercentage = Ratio(Line.FieldGoalsMade, Line.FieldGoalsAttempted) * 100.f;
    Metrics.TurnoverMargin = Line.Takeaways - Line.Turnovers;
    return Metrics;
}

// --- Persistence -------------------------------------------------------------------------

void UPSStatsEngine::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->StatBook = Book;
    }
}

bool UPSStatsEngine::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || Save->StatBook.Season <= 0)
    {
        return false;
    }
    Book = Save->StatBook;
    bGameInProgress = false;
    return true;
}
