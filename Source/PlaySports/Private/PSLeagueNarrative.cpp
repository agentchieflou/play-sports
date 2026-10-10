// PSLeagueNarrative.cpp - Epic 93: the league's storylines, weekly news, awards and talking points
#include "PSLeagueNarrative.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSFranchiseSeason.h"
#include "PSGameIntelligenceSubsystem.h"
#include "PSGameStateSerializer.h"
#include "PSLocalization.h"
#include "PSOverlayBroadcastSubsystem.h"
#include "PSStatsEngine.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

namespace PSLeagueNarrativePrivate
{
    /** The categories a rookie can surge in: the ones a broadcast quotes. */
    const EPSStatCategory SurgeCategories[] = {
        EPSStatCategory::PassingYards, EPSStatCategory::RushingYards, EPSStatCategory::ReceivingYards,
        EPSStatCategory::Tackles, EPSStatCategory::Sacks, EPSStatCategory::Interceptions };

    const EPSAwardKind SeasonAwards[] = {
        EPSAwardKind::MostValuablePlayer, EPSAwardKind::OffensivePlayerOfYear,
        EPSAwardKind::DefensivePlayerOfYear, EPSAwardKind::RookieOfYear };

    template <typename EnumType>
    FString EnumName(EnumType Value)
    {
        return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
    }

    /** Narrative.<Group>.<Name>, from the string table. */
    FText TableText(const TCHAR* Group, const FString& Name)
    {
        const FString Key = FString::Printf(TEXT("Narrative.%s.%s"), Group, *Name);
        return UPSLocalization::GetText(Key);
    }

    FName MakeId(const TCHAR* Kind, const FString& Rest)
    {
        return FName(*FString::Printf(TEXT("%s.%s"), Kind, *Rest));
    }

    /** True when A outranks B: the higher score, then the lower id. */
    bool Outranks(const FPSAwardRecord& A, const FPSAwardRecord& B)
    {
        return A.Score != B.Score ? A.Score > B.Score : A.PlayerId.LexicalLess(B.PlayerId);
    }
}

// --- Tuning --------------------------------------------------------------------------------

FString UPSLeagueNarrative::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/league_narrative.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSNarrativeTuning& UPSLeagueNarrative::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSLeagueNarrative::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSNarrativeTuning Loaded;
    if (!Ingestion->LoadNarrativeTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueNarrative: Could not load %s; keeping the current tuning."), *JsonFilePath);
        return false;
    }
    return SetTuning(Loaded);
}

bool UPSLeagueNarrative::SetTuning(const FPSNarrativeTuning& InTuning)
{
    const TArray<FString> Problems = ValidateTuning(InTuning);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLeagueNarrative: Tuning refused: %s"), *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSLeagueNarrative::ValidateTuning(const FPSNarrativeTuning& InTuning)
{
    using namespace PSLeagueNarrativePrivate;

    TArray<FString> Problems;
    if (InTuning.StreakMin < 2)
    {
        Problems.Add(TEXT("StreakMin must be 2 or more."));
    }
    if (InTuning.RookieSurgeTopN < 1 || InTuning.AwardRaceFromWeek < 1 || InTuning.MaxDigestItems < 1 || InTuning.DigestsKept < 1 || InTuning.VoterCount < 1)
    {
        Problems.Add(TEXT("RookieSurgeTopN, AwardRaceFromWeek, MaxDigestItems, DigestsKept and VoterCount must be 1 or more."));
    }
    if (InTuning.MaxBroadcastStorylines < 0 || InTuning.MvpWinWeight < 0.f)
    {
        Problems.Add(TEXT("MaxBroadcastStorylines and MvpWinWeight can't be negative."));
    }
    if (InTuning.AwardRaceMargin < 0.f || InTuning.AwardRaceMargin > 1.f || InTuning.VoterNoise < 0.f || InTuning.VoterNoise >= 1.f)
    {
        Problems.Add(TEXT("AwardRaceMargin must be 0 to 1 and VoterNoise 0 or more, under 1."));
    }
    const UEnum* Kinds = StaticEnum<EPSStorylineKind>();
    for (int32 Index = 0; Index < Kinds->NumEnums() - 1; ++Index)
    {
        const EPSStorylineKind Kind = static_cast<EPSStorylineKind>(Kinds->GetValueByIndex(Index));
        const int32 Count = InTuning.StorylineKinds.FilterByPredicate([Kind](const FPSStorylineKindDef& Def) { return Def.Kind == Kind; }).Num();
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("StorylineKinds needs exactly one %s (has %d)."), *EnumName(Kind), Count));
        }
    }
    if (InTuning.StorylineKinds.ContainsByPredicate([](const FPSStorylineKindDef& Def) { return Def.Weight < 0.f; }))
    {
        Problems.Add(TEXT("A storyline weight can't be negative."));
    }
    if (InTuning.OffenseScoring.Num() == 0 || InTuning.DefenseScoring.Num() == 0)
    {
        Problems.Add(TEXT("OffenseScoring and DefenseScoring each need a statistic."));
    }
    if (InTuning.BallotPoints.Num() == 0)
    {
        Problems.Add(TEXT("BallotPoints needs at least first place."));
    }
    for (int32 Place = 0; Place < InTuning.BallotPoints.Num(); ++Place)
    {
        if (InTuning.BallotPoints[Place] <= 0 || (Place > 0 && InTuning.BallotPoints[Place] > InTuning.BallotPoints[Place - 1]))
        {
            Problems.Add(TEXT("BallotPoints must be above 0 and never more for a lower place."));
            break;
        }
    }
    if (InTuning.DigestTask.IsEmpty() || InTuning.DigestInstructions.IsEmpty() || InTuning.DigestContextChars < 512)
    {
        Problems.Add(TEXT("The digest needs a model-router task, instructions and at least 512 characters of context."));
    }
    return Problems;
}

float UPSLeagueNarrative::KindWeight(EPSStorylineKind Kind)
{
    const FPSStorylineKindDef* Def = GetTuning().StorylineKinds.FindByPredicate([Kind](const FPSStorylineKindDef& Candidate) { return Candidate.Kind == Kind; });
    return Def ? Def->Weight : 0.f;
}

// --- Sources -------------------------------------------------------------------------------

void UPSLeagueNarrative::SetStats(UPSStatsEngine* InStats)
{
    if (UPSStatsEngine* Old = Stats.Get())
    {
        Old->OnRecordBroken.Remove(RecordHandle);
    }
    RecordHandle.Reset();
    Stats = InStats;
    if (InStats)
    {
        RecordHandle = InStats->OnRecordBroken.AddUObject(this, &UPSLeagueNarrative::HandleRecordBroken);
    }
}

void UPSLeagueNarrative::SetIntelligence(UPSGameIntelligenceSubsystem* InIntelligence)
{
    if (UPSGameIntelligenceSubsystem* Old = Intelligence.Get())
    {
        Old->OnRequestAnsweredMC.Remove(AnsweredHandle);
    }
    AnsweredHandle.Reset();
    Intelligence = InIntelligence;
    if (InIntelligence)
    {
        AnsweredHandle = InIntelligence->OnRequestAnsweredMC.AddUObject(this, &UPSLeagueNarrative::HandleRequestAnswered);
    }
}

void UPSLeagueNarrative::HandleRecordBroken(const FPSTelemetryRecordBrokenEvent& Event)
{
    RecordsThisWeek.Add(Event);
}

void UPSLeagueNarrative::HandleRequestAnswered(const FPSIntelRequest& Request)
{
    if (Request.Kind != EPSIntelRequestKind::NewsDigest)
    {
        return;
    }
    for (FPSNewsDigest& Digest : State.Digests)
    {
        if (Digest.ModelRequestId == Request.RequestId)
        {
            Digest.ModelText = Request.Answer;
        }
    }
}

// --- Storylines ------------------------------------------------------------------------------

TArray<FPSStoryline> UPSLeagueNarrative::DetectStorylines(const UPSFranchiseSeason* Season, int32 Week)
{
    using namespace PSLeagueNarrativePrivate;

    TArray<FPSStoryline> Found;
    if (!Season)
    {
        return Found;
    }
    const FPSNarrativeTuning& Settings = GetTuning();
    const UPSStatsEngine* Book = Stats.Get();
    const int32 SeasonNumber = Book ? Book->GetSeason() : 0;
    auto Tell = [this, &Found, SeasonNumber, Week](FPSStoryline Storyline)
    {
        Storyline.Season = SeasonNumber;
        Storyline.Week = Week;
        Storyline.Weight = KindWeight(Storyline.Kind);
        Found.Add(Storyline);
    };

    const TArray<FPSTeamStanding> Standings = Season->GetStandings();

    // Streaks: each team's results through Week, in order.
    TArray<FPSWeekMatchup> Played = Season->GetAllMatchups().FilterByPredicate([Week](const FPSWeekMatchup& Matchup)
    {
        return Matchup.bPlayed && Matchup.WeekNumber <= Week;
    });
    Played.StableSort([](const FPSWeekMatchup& A, const FPSWeekMatchup& B) { return A.WeekNumber < B.WeekNumber; });
    TMap<FName, TArray<int32>> Results;
    for (const FPSWeekMatchup& Matchup : Played)
    {
        const int32 HomeResult = FMath::Sign(Matchup.HomeScore - Matchup.AwayScore);
        Results.FindOrAdd(Matchup.HomeTeamId).Add(HomeResult);
        Results.FindOrAdd(Matchup.AwayTeamId).Add(-HomeResult);
    }
    for (const FPSTeamStanding& Standing : Standings)
    {
        const TArray<int32>* TeamResults = Results.Find(Standing.TeamId);
        if (!TeamResults || TeamResults->Num() == 0 || TeamResults->Last() == 0)
        {
            continue;
        }
        const int32 Last = TeamResults->Last();
        int32 Run = 0;
        for (int32 Index = TeamResults->Num() - 1; Index >= 0 && (*TeamResults)[Index] == Last; --Index)
        {
            ++Run;
        }
        if (Run >= Settings.StreakMin)
        {
            FPSStoryline Streak;
            Streak.Kind = Last > 0 ? EPSStorylineKind::WinStreak : EPSStorylineKind::LosingStreak;
            Streak.StorylineId = MakeId(Last > 0 ? TEXT("WinStreak") : TEXT("LosingStreak"), Standing.TeamId.ToString());
            Streak.TeamId = Standing.TeamId;
            Streak.Value = Run;
            Tell(Streak);
        }
    }

    // Revenge games: next week's opponent beat the team earlier this season.
    for (const FPSWeekMatchup& Upcoming : Season->GetMatchupsForWeek(Week + 1))
    {
        if (Upcoming.bPlayed)
        {
            continue;
        }
        for (int32 Index = Played.Num() - 1; Index >= 0; --Index)
        {
            const FPSWeekMatchup& Earlier = Played[Index];
            const bool bSamePair = (Earlier.HomeTeamId == Upcoming.HomeTeamId && Earlier.AwayTeamId == Upcoming.AwayTeamId)
                || (Earlier.HomeTeamId == Upcoming.AwayTeamId && Earlier.AwayTeamId == Upcoming.HomeTeamId);
            if (!bSamePair)
            {
                continue;
            }
            if (Earlier.HomeScore != Earlier.AwayScore)
            {
                const bool bHomeWon = Earlier.HomeScore > Earlier.AwayScore;
                FPSStoryline Revenge;
                Revenge.Kind = EPSStorylineKind::RevengeGame;
                Revenge.TeamId = bHomeWon ? Earlier.AwayTeamId : Earlier.HomeTeamId;
                Revenge.OtherTeamId = bHomeWon ? Earlier.HomeTeamId : Earlier.AwayTeamId;
                Revenge.StorylineId = MakeId(TEXT("RevengeGame"), Revenge.TeamId.ToString() + TEXT(".") + Revenge.OtherTeamId.ToString());
                Revenge.Value = FMath::Abs(Earlier.HomeScore - Earlier.AwayScore);
                Tell(Revenge);
            }
            break;
        }
    }

    // Records the statistics engine announced broken since the last week.
    for (const FPSTelemetryRecordBrokenEvent& Record : RecordsThisWeek)
    {
        FPSStoryline Broken;
        Broken.Kind = EPSStorylineKind::RecordBroken;
        Broken.StorylineId = MakeId(TEXT("RecordBroken"), EnumName(Record.Category) + TEXT(".") + EnumName(Record.Scope));
        Broken.PlayerId = Record.bTeamRecord ? NAME_None : Record.HolderId;
        Broken.TeamId = Record.bTeamRecord ? Record.HolderId : Record.TeamId;
        Broken.Category = Record.Category;
        Broken.Scope = Record.Scope;
        Broken.Value = Record.Value;
        Found.RemoveAll([&Broken](const FPSStoryline& Earlier) { return Earlier.StorylineId == Broken.StorylineId; });
        Tell(Broken);
    }

    if (!Book)
    {
        return Found;
    }

    // Rookies among a category's best.
    TSet<FName> Surging;
    for (const EPSStatCategory Category : SurgeCategories)
    {
        const TArray<FPSLeaderEntry> Leaders = Book->GetLeaders(Category, EPSStatScope::Season, SeasonNumber, Settings.RookieSurgeTopN);
        for (int32 Rank = 0; Rank < Leaders.Num(); ++Rank)
        {
            const FPSLeaderEntry& Leader = Leaders[Rank];
            if (Leader.Value <= 0 || Surging.Contains(Leader.Id) || !IsRookie(Leader.Id))
            {
                continue;
            }
            Surging.Add(Leader.Id);
            FPSStoryline Surge;
            Surge.Kind = EPSStorylineKind::RookieSurge;
            Surge.StorylineId = MakeId(TEXT("RookieSurge"), Leader.Id.ToString());
            Surge.PlayerId = Leader.Id;
            Surge.TeamId = Leader.TeamId;
            Surge.Category = Category;
            Surge.Scope = EPSStatScope::Season;
            Surge.Value = Leader.Value;
            Surge.Rank = Rank + 1;
            Tell(Surge);
        }
    }

    // A close MVP race.
    if (Week >= Settings.AwardRaceFromWeek)
    {
        TArray<FPSAwardRecord> Race;
        TMap<FName, FName> Teams;
        for (const FPSBoxScore& Game : Book->GetSeasonGames())
        {
            for (const FPSPlayerStatLine& Line : Game.Players)
            {
                Teams.Add(Line.PlayerId, Line.TeamId);
            }
        }
        for (const TPair<FName, FName>& Player : Teams)
        {
            const FPSPlayerStatLine Line = Book->GetPlayerSeason(Player.Key, SeasonNumber);
            const FPSTeamStanding* Standing = Standings.FindByPredicate([&Player](const FPSTeamStanding& Row) { return Row.TeamId == Player.Value; });
            FPSAwardRecord& Entry = Race.AddDefaulted_GetRef();
            Entry.PlayerId = Player.Key;
            Entry.TeamId = Player.Value;
            Entry.Score = ScoreLine(Line, Settings.OffenseScoring) + ScoreLine(Line, Settings.DefenseScoring)
                + (Standing ? Standing->GetWinPercentage() * Settings.MvpWinWeight : 0.f);
        }
        Race.Sort(&Outranks);
        if (Race.Num() >= 2 && Race[0].Score > 0.f && Race[1].Score >= Race[0].Score * (1.f - Settings.AwardRaceMargin))
        {
            FPSStoryline Close;
            Close.Kind = EPSStorylineKind::AwardRace;
            Close.StorylineId = MakeId(TEXT("AwardRace"), TEXT("MostValuablePlayer"));
            Close.PlayerId = Race[0].PlayerId;
            Close.TeamId = Race[0].TeamId;
            Close.OtherPlayerId = Race[1].PlayerId;
            Close.OtherTeamId = Race[1].TeamId;
            Close.Value = FMath::RoundToInt(Race[0].Score - Race[1].Score);
            Tell(Close);
        }
    }
    return Found;
}

FPSNewsDigest UPSLeagueNarrative::CloseWeek(UPSFranchiseSeason* Season, int32 Week)
{
    if (!Season)
    {
        return FPSNewsDigest();
    }
    TArray<FPSStoryline> Detected = DetectStorylines(Season, Week);
    Detected.StableSort([](const FPSStoryline& A, const FPSStoryline& B) { return A.Weight > B.Weight; });
    State.ActiveStorylines = Detected;
    RecordsThisWeek.Reset();

    const TArray<FPSAwardRecord> Honors = AwardWeeklyHonors(Week);

    // The digest: the heaviest storylines, then the week's honors.
    FPSNewsDigest Digest;
    Digest.Season = Stats.IsValid() ? Stats->GetSeason() : 0;
    Digest.Week = Week;
    const int32 StoryRoom = FMath::Max(0, GetTuning().MaxDigestItems - Honors.Num());
    for (int32 Index = 0; Index < FMath::Min(StoryRoom, Detected.Num()); ++Index)
    {
        Digest.Items.Add(DescribeStoryline(Detected[Index]));
    }
    for (const FPSAwardRecord& Honor : Honors)
    {
        if (Digest.Items.Num() < GetTuning().MaxDigestItems)
        {
            Digest.Items.Add(DescribeAward(Honor));
        }
    }
    UE_LOG(LogTemp, Display, TEXT("UPSLeagueNarrative: Week %d: %d storylines, %d honors."), Week, Detected.Num(), Honors.Num());
    PublishDigest(Digest);
    return State.Digests.Num() > 0 ? State.Digests.Last() : Digest;
}

void UPSLeagueNarrative::PublishDigest(FPSNewsDigest Digest)
{
    const FPSNarrativeTuning& Settings = GetTuning();
    State.Digests.Add(Digest);
    while (State.Digests.Num() > Settings.DigestsKept)
    {
        State.Digests.RemoveAt(0);
    }

    // With the bridge online, a model writes the week up (Epic 82); the templates stand without.
    UPSGameIntelligenceSubsystem* Hooks = Intelligence.Get();
    if (!Hooks || !UPSGameIntelligenceSubsystem::IsBridgeOnline() || Digest.Items.Num() == 0)
    {
        return;
    }
    TArray<FPSNewsItem> Items = Digest.Items;
    FString Context;
    while (true)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("contract"), TEXT("play-sports.league-news/1"));
        Root->SetNumberField(TEXT("season"), Digest.Season);
        Root->SetNumberField(TEXT("week"), Digest.Week);
        Root->SetBoolField(TEXT("seasonEnd"), Digest.bSeasonEnd);
        TArray<TSharedPtr<FJsonValue>> Facts;
        for (const FPSNewsItem& Item : Items)
        {
            TSharedRef<FJsonObject> Fact = MakeShared<FJsonObject>();
            Fact->SetStringField(TEXT("headline"), Item.Headline);
            Fact->SetStringField(TEXT("body"), Item.Body);
            Facts.Add(MakeShared<FJsonValueObject>(Fact));
        }
        Root->SetArrayField(TEXT("items"), Facts);
        Context = PSGameStateSerializer::ToCompactJson(Root);
        if (Context.Len() <= Settings.DigestContextChars || Items.Num() <= 1)
        {
            break;
        }
        Items.Pop();
    }
    const int32 RequestId = Hooks->OpenTextRequest(EPSIntelRequestKind::NewsDigest, Settings.DigestTask, Settings.DigestInstructions, Context);
    FPSNewsDigest& Kept = State.Digests.Last();
    Kept.ModelRequestId = RequestId;

    // A listener may have answered as the request opened.
    FPSIntelRequest Request;
    if (RequestId != 0 && Hooks->FindRequest(RequestId, Request) && Request.State == EPSIntelRequestState::Answered)
    {
        Kept.ModelText = Request.Answer;
    }
}

FPSNewsItem UPSLeagueNarrative::DescribeStoryline(const FPSStoryline& Storyline) const
{
    using namespace PSLeagueNarrativePrivate;

    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(Storyline.TeamId.ToString()));
    Arguments.Add(TEXT("Opponent"), UPSLocalization::Verbatim(Storyline.OtherTeamId.ToString()));
    Arguments.Add(TEXT("Player"), UPSLocalization::Verbatim(Storyline.PlayerId.ToString()));
    Arguments.Add(TEXT("Rival"), UPSLocalization::Verbatim(Storyline.OtherPlayerId.ToString()));
    Arguments.Add(TEXT("Count"), UPSLocalization::FormatNumber(static_cast<float>(Storyline.Value), 0));
    Arguments.Add(TEXT("Rank"), UPSLocalization::FormatNumber(static_cast<float>(Storyline.Rank), 0));
    Arguments.Add(TEXT("Stat"), TableText(TEXT("Stat"), EnumName(Storyline.Category)));
    Arguments.Add(TEXT("Scope"), TableText(TEXT("Scope"), EnumName(Storyline.Scope)));

    FPSNewsItem Item;
    Item.StorylineId = Storyline.StorylineId;
    Item.Weight = Storyline.Weight;
    switch (Storyline.Kind)
    {
    case EPSStorylineKind::WinStreak:
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.WinStreak.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.WinStreak.Body"), Arguments).ToString();
        break;
    case EPSStorylineKind::LosingStreak:
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.LosingStreak.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.LosingStreak.Body"), Arguments).ToString();
        break;
    case EPSStorylineKind::RookieSurge:
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.RookieSurge.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.RookieSurge.Body"), Arguments).ToString();
        break;
    case EPSStorylineKind::RevengeGame:
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.RevengeGame.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.RevengeGame.Body"), Arguments).ToString();
        break;
    case EPSStorylineKind::RecordBroken:
        Arguments.Add(TEXT("Holder"), UPSLocalization::Verbatim((Storyline.PlayerId.IsNone() ? Storyline.TeamId : Storyline.PlayerId).ToString()));
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.RecordBroken.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.RecordBroken.Body"), Arguments).ToString();
        break;
    case EPSStorylineKind::AwardRace:
        Item.Headline = UPSLocalization::Format(TEXT("Narrative.AwardRace.Headline"), Arguments).ToString();
        Item.Body = UPSLocalization::Format(TEXT("Narrative.AwardRace.Body"), Arguments).ToString();
        break;
    default:
        break;
    }
    return Item;
}

FPSNewsItem UPSLeagueNarrative::DescribeAward(const FPSAwardRecord& Award) const
{
    using namespace PSLeagueNarrativePrivate;

    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Award"), TableText(TEXT("Award"), EnumName(Award.Award)));
    Arguments.Add(TEXT("Player"), UPSLocalization::Verbatim(Award.PlayerId.ToString()));
    Arguments.Add(TEXT("Team"), UPSLocalization::Verbatim(Award.TeamId.ToString()));
    Arguments.Add(TEXT("Score"), UPSLocalization::FormatNumber(Award.Score, 1));
    Arguments.Add(TEXT("Week"), UPSLocalization::FormatNumber(static_cast<float>(Award.Week), 0));
    Arguments.Add(TEXT("Points"), UPSLocalization::FormatNumber(static_cast<float>(Award.VotePoints), 0));
    Arguments.Add(TEXT("FirstPlace"), UPSLocalization::FormatNumber(static_cast<float>(Award.FirstPlaceVotes), 0));

    FPSNewsItem Item;
    Item.Headline = UPSLocalization::Format(TEXT("Narrative.AwardHeadline"), Arguments).ToString();
    Item.Body = Award.Week > 0
        ? UPSLocalization::Format(TEXT("Narrative.WeeklyHonorBody"), Arguments).ToString()
        : UPSLocalization::Format(TEXT("Narrative.SeasonAwardBody"), Arguments).ToString();
    return Item;
}

// --- Awards ----------------------------------------------------------------------------------

float UPSLeagueNarrative::ScoreLine(const FPSPlayerStatLine& Line, const TArray<FPSAwardStatWeight>& Weights)
{
    float Score = 0.f;
    for (const FPSAwardStatWeight& Weight : Weights)
    {
        Score += Weight.Weight * Line.GetValue(Weight.Category);
    }
    return Score;
}

bool UPSLeagueNarrative::IsRookie(FName PlayerId) const
{
    const UPSStatsEngine* Book = Stats.Get();
    if (!Book || PlayerId.IsNone() || Book->GetStatBook().History.Num() == 0)
    {
        return false;
    }
    for (const FPSSeasonStats& Past : Book->GetStatBook().History)
    {
        if (Past.Players.ContainsByPredicate([PlayerId](const FPSPlayerStatLine& Line) { return Line.PlayerId == PlayerId; }))
        {
            return false;
        }
    }
    return true;
}

void UPSLeagueNarrative::GiveAward(const FPSAwardRecord& Award)
{
    State.Awards.Add(Award);
    UE_LOG(LogTemp, Display, TEXT("UPSLeagueNarrative: Season %d week %d: %s to %s (%s)."), Award.Season, Award.Week,
        *PSLeagueNarrativePrivate::EnumName(Award.Award), *Award.PlayerId.ToString(), *Award.TeamId.ToString());
    OnAwardGiven.Broadcast(Award);
}

TArray<FPSAwardRecord> UPSLeagueNarrative::AwardWeeklyHonors(int32 Week)
{
    using namespace PSLeagueNarrativePrivate;

    TArray<FPSAwardRecord> Given;
    const UPSStatsEngine* Book = Stats.Get();
    if (!Book)
    {
        return Given;
    }
    const FPSNarrativeTuning& Settings = GetTuning();
    const int32 SeasonNumber = Book->GetSeason();
    for (const EPSAwardKind Kind : { EPSAwardKind::OffensivePlayerOfWeek, EPSAwardKind::DefensivePlayerOfWeek })
    {
        const bool bAlready = State.Awards.ContainsByPredicate([Kind, SeasonNumber, Week](const FPSAwardRecord& Earlier)
        {
            return Earlier.Award == Kind && Earlier.Season == SeasonNumber && Earlier.Week == Week;
        });
        if (bAlready)
        {
            continue;
        }
        const TArray<FPSAwardStatWeight>& Weights = Kind == EPSAwardKind::OffensivePlayerOfWeek ? Settings.OffenseScoring : Settings.DefenseScoring;
        FPSAwardRecord Best;
        for (const FPSBoxScore& Game : Book->GetSeasonGames())
        {
            if (Game.Week != Week)
            {
                continue;
            }
            for (const FPSPlayerStatLine& Line : Game.Players)
            {
                FPSAwardRecord Candidate;
                Candidate.PlayerId = Line.PlayerId;
                Candidate.TeamId = Line.TeamId;
                Candidate.Score = ScoreLine(Line, Weights);
                if (Candidate.Score > 0.f && (Best.PlayerId.IsNone() || Outranks(Candidate, Best)))
                {
                    Best = Candidate;
                }
            }
        }
        if (!Best.PlayerId.IsNone())
        {
            Best.Award = Kind;
            Best.Season = SeasonNumber;
            Best.Week = Week;
            GiveAward(Best);
            Given.Add(Best);
        }
    }
    return Given;
}

TArray<FPSAwardRecord> UPSLeagueNarrative::RunVote(const TArray<FPSAwardRecord>& Candidates, int32 Voters, const TArray<int32>& BallotPoints, float Noise, FRandomStream& Stream)
{
    TArray<FPSAwardRecord> Tally = Candidates;
    for (FPSAwardRecord& Entry : Tally)
    {
        Entry.VotePoints = 0;
        Entry.FirstPlaceVotes = 0;
    }
    TArray<TPair<float, int32>> Ballot;
    for (int32 Voter = 0; Voter < Voters; ++Voter)
    {
        Ballot.Reset();
        for (int32 Index = 0; Index < Tally.Num(); ++Index)
        {
            Ballot.Add(TPair<float, int32>(Tally[Index].Score * (1.f + Stream.FRandRange(-Noise, Noise)), Index));
        }
        Ballot.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B)
        {
            return A.Key != B.Key ? A.Key > B.Key : A.Value < B.Value;
        });
        for (int32 Place = 0; Place < FMath::Min(BallotPoints.Num(), Ballot.Num()); ++Place)
        {
            FPSAwardRecord& Votes = Tally[Ballot[Place].Value];
            Votes.VotePoints += BallotPoints[Place];
            Votes.FirstPlaceVotes += Place == 0 ? 1 : 0;
        }
    }
    Tally.Sort([](const FPSAwardRecord& A, const FPSAwardRecord& B)
    {
        return A.VotePoints != B.VotePoints ? A.VotePoints > B.VotePoints : PSLeagueNarrativePrivate::Outranks(A, B);
    });
    return Tally;
}

TArray<FPSAwardRecord> UPSLeagueNarrative::AwardSeason(const UPSFranchiseSeason* Season)
{
    using namespace PSLeagueNarrativePrivate;

    TArray<FPSAwardRecord> Given;
    const UPSStatsEngine* Book = Stats.Get();
    if (!Book || SeasonsAwarded.Contains(Book->GetSeason()))
    {
        return Given;
    }
    const FPSNarrativeTuning& Settings = GetTuning();
    const int32 SeasonNumber = Book->GetSeason();

    // The candidates: everyone with a line this season, on his latest team. A season with no
    // games (one just begun) has nothing to vote on yet.
    TMap<FName, FName> Teams;
    for (const FPSBoxScore& Game : Book->GetSeasonGames())
    {
        for (const FPSPlayerStatLine& Line : Game.Players)
        {
            Teams.Add(Line.PlayerId, Line.TeamId);
        }
    }
    if (Teams.Num() == 0)
    {
        return Given;
    }
    SeasonsAwarded.Add(SeasonNumber);
    const TArray<FPSTeamStanding> Standings = Season ? Season->GetStandings() : TArray<FPSTeamStanding>();

    for (const EPSAwardKind Kind : SeasonAwards)
    {
        TArray<FPSAwardRecord> Candidates;
        for (const TPair<FName, FName>& Player : Teams)
        {
            if (Kind == EPSAwardKind::RookieOfYear && !IsRookie(Player.Key))
            {
                continue;
            }
            const FPSPlayerStatLine Line = Book->GetPlayerSeason(Player.Key, SeasonNumber);
            const float Offense = ScoreLine(Line, Settings.OffenseScoring);
            const float Defense = ScoreLine(Line, Settings.DefenseScoring);
            float Score = Kind == EPSAwardKind::OffensivePlayerOfYear ? Offense
                : Kind == EPSAwardKind::DefensivePlayerOfYear ? Defense : Offense + Defense;
            if (Kind == EPSAwardKind::MostValuablePlayer)
            {
                const FPSTeamStanding* Standing = Standings.FindByPredicate([&Player](const FPSTeamStanding& Row) { return Row.TeamId == Player.Value; });
                Score += Standing ? Standing->GetWinPercentage() * Settings.MvpWinWeight : 0.f;
            }
            if (Score > 0.f)
            {
                FPSAwardRecord& Candidate = Candidates.AddDefaulted_GetRef();
                Candidate.PlayerId = Player.Key;
                Candidate.TeamId = Player.Value;
                Candidate.Score = Score;
            }
        }
        if (Candidates.Num() == 0)
        {
            continue;
        }
        Candidates.Sort(&Outranks);
        FRandomStream Stream(Settings.VotingSeed + SeasonNumber * 101 + static_cast<int32>(Kind) * 7);
        FPSAwardRecord Winner = RunVote(Candidates, Settings.VoterCount, Settings.BallotPoints, Settings.VoterNoise, Stream)[0];
        Winner.Award = Kind;
        Winner.Season = SeasonNumber;
        Winner.Week = 0;
        GiveAward(Winner);
        Given.Add(Winner);
    }

    // The season's-end digest: the awards.
    FPSNewsDigest Digest;
    Digest.Season = SeasonNumber;
    Digest.bSeasonEnd = true;
    if (Season)
    {
        for (const FPSWeekMatchup& Matchup : Season->GetAllMatchups())
        {
            Digest.Week = FMath::Max(Digest.Week, Matchup.WeekNumber + 1);
        }
    }
    for (const FPSAwardRecord& Award : Given)
    {
        Digest.Items.Add(DescribeAward(Award));
    }
    PublishDigest(Digest);
    return Given;
}

TArray<FPSAwardRecord> UPSLeagueNarrative::GetAwardsForPlayer(FName PlayerId) const
{
    return State.Awards.FilterByPredicate([PlayerId](const FPSAwardRecord& Award) { return Award.PlayerId == PlayerId; });
}

int32 UPSLeagueNarrative::CountAwards(FName PlayerId, EPSAwardKind Award) const
{
    int32 Count = 0;
    for (const FPSAwardRecord& Given : State.Awards)
    {
        Count += Given.PlayerId == PlayerId && Given.Award == Award ? 1 : 0;
    }
    return Count;
}

// --- The broadcast ---------------------------------------------------------------------------

TArray<FPSNewsItem> UPSLeagueNarrative::GetTalkingPoints(FName HomeTeamId, FName AwayTeamId, int32 Max) const
{
    TArray<FPSNewsItem> Points;
    for (const FPSStoryline& Storyline : State.ActiveStorylines)
    {
        if (Points.Num() >= Max)
        {
            break;
        }
        if (Storyline.Involves(HomeTeamId) || Storyline.Involves(AwayTeamId))
        {
            Points.Add(DescribeStoryline(Storyline));
        }
    }
    return Points;
}

int32 UPSLeagueNarrative::FeedBroadcast(UPSOverlayBroadcastSubsystem* Broadcast, FName HomeTeamId, FName AwayTeamId)
{
    if (!Broadcast)
    {
        return 0;
    }
    int32 Fed = 0;
    for (const FPSNewsItem& Item : GetTalkingPoints(HomeTeamId, AwayTeamId, GetTuning().MaxBroadcastStorylines))
    {
        Fed += Broadcast->PushChyron(EPSChyronKind::Custom, Item.Headline, Item.Body) ? 1 : 0;
    }
    return Fed;
}

// --- Persistence -----------------------------------------------------------------------------

void UPSLeagueNarrative::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->Narrative = State;
    }
}

bool UPSLeagueNarrative::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->Narrative.ActiveStorylines.Num() == 0 && Save->Narrative.Digests.Num() == 0 && Save->Narrative.Awards.Num() == 0))
    {
        return false;
    }
    State = Save->Narrative;
    SeasonsAwarded.Reset();
    for (const FPSAwardRecord& Award : State.Awards)
    {
        if (Award.Week == 0)
        {
            SeasonsAwarded.Add(Award.Season);
        }
    }
    return true;
}
