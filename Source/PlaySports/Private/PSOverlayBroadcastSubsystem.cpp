#include "PSOverlayBroadcastSubsystem.h"
#include "PSCoachingData.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSPlayCallSubsystem.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSOverlayBroadcastPrivate
{
    /** Every kind needs a theme entry. */
    const EPSChyronKind AllKinds[] = { EPSChyronKind::ScoreAlert, EPSChyronKind::DriveSummary, EPSChyronKind::PlayStat, EPSChyronKind::StatLine, EPSChyronKind::Custom };

    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed = Fallback;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }

    /** What points on the board were, by how many there were. */
    FString ScoreHeadline(int32 Points)
    {
        switch (Points)
        {
        case 1:  return TEXT("EXTRA POINT");
        case 2:  return TEXT("SAFETY");
        case 3:  return TEXT("FIELD GOAL");
        default: return Points >= 6 ? TEXT("TOUCHDOWN") : TEXT("SCORE");
        }
    }

    /** "12-yard gain", "loss of 3", "no gain". */
    FString GainText(int32 Yards)
    {
        if (Yards > 0)
        {
            return FString::Printf(TEXT("%d-yard gain"), Yards);
        }
        return Yards < 0 ? FString::Printf(TEXT("loss of %d"), -Yards) : FString(TEXT("no gain"));
    }
}

void UPSOverlayBroadcastSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    LoadThemeFromJson(GetDefaultThemePath());
    SetOverlayDetail(PSPlatformTiers::GetActiveTier().OverlayDetail);
    RefreshTeams();

    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnGameStateMC.AddUObject(this, &UPSOverlayBroadcastSubsystem::HandleGameState);
        Bus->OnScoreMC.AddUObject(this, &UPSOverlayBroadcastSubsystem::HandleScore);
        Bus->OnCatchMC.AddUObject(this, &UPSOverlayBroadcastSubsystem::HandleCatch);
        Bus->OnTackleMC.AddUObject(this, &UPSOverlayBroadcastSubsystem::HandleTackle);
        BoundBus = Bus;
    }
}

void UPSOverlayBroadcastSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnScoreMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSOverlayBroadcastSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSOverlayBroadcastSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    // Team select travels with team=<TeamId> (UPSMenuComponent::BuildTravelOptions): the home
    // side. An away= option names the opponent when the front end passes one.
    const TCHAR* HomeOption = InWorld.URL.GetOption(TEXT("team="), nullptr);
    const TCHAR* AwayOption = InWorld.URL.GetOption(TEXT("away="), nullptr);
    SetTeams(HomeOption && *HomeOption ? FName(HomeOption) : NAME_None, AwayOption && *AwayOption ? FName(AwayOption) : NAME_None);
}

void UPSOverlayBroadcastSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AdvanceTime(DeltaTime);
}

TStatId UPSOverlayBroadcastSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSOverlayBroadcastSubsystem, STATGROUP_Tickables);
}

FString UPSOverlayBroadcastSubsystem::GetDefaultThemePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/broadcast_overlay.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSOverlayBroadcastSubsystem::LoadThemeFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSBroadcastOverlayTheme Loaded;
    if (!Ingestion->LoadBroadcastOverlayThemeFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBroadcastSubsystem: Could not load the broadcast theme from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateTheme(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBroadcastSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetTheme(Loaded);
    return true;
}

void UPSOverlayBroadcastSubsystem::SetTheme(const FPSBroadcastOverlayTheme& NewTheme)
{
    Theme = NewTheme;
    RefreshTeams();
    RefreshDerived();
}

TArray<FString> UPSOverlayBroadcastSubsystem::ValidateTheme(const FPSBroadcastOverlayTheme& InTheme)
{
    TArray<FString> Problems;
    struct FNamedColor
    {
        const TCHAR* Name;
        const FString* Value;
    };
    const FNamedColor Colors[] = {
        { TEXT("HomeColor"), &InTheme.HomeColor }, { TEXT("AwayColor"), &InTheme.AwayColor },
        { TEXT("BarColor"), &InTheme.BarColor }, { TEXT("TextColor"), &InTheme.TextColor },
        { TEXT("RedZoneColor"), &InTheme.RedZoneColor }, { TEXT("TwoMinuteColor"), &InTheme.TwoMinuteColor },
        { TEXT("TimeoutColor"), &InTheme.TimeoutColor }, { TEXT("TimeoutUsedColor"), &InTheme.TimeoutUsedColor },
        { TEXT("ChyronColor"), &InTheme.ChyronColor }
    };
    for (const FNamedColor& Entry : Colors)
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Entry.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Entry.Name, **Entry.Value));
        }
    }
    if (InTheme.HomeLabel.IsEmpty() || InTheme.AwayLabel.IsEmpty())
    {
        Problems.Add(TEXT("HomeLabel and AwayLabel must not be empty"));
    }
    if (InTheme.ScoreFontSize < 1 || InTheme.TextFontSize < 1)
    {
        Problems.Add(TEXT("ScoreFontSize and TextFontSize must be 1 or more"));
    }
    if (InTheme.RedZoneYardLine < 1 || InTheme.RedZoneYardLine > 99 || InTheme.TwoMinuteSeconds < 0.f)
    {
        Problems.Add(TEXT("RedZoneYardLine must be 1 to 99 and TwoMinuteSeconds 0 or more"));
    }
    if (InTheme.ChyronMaxQueued < 1 || InTheme.ChyronMinShowSeconds < 0.f || InTheme.ChyronGapSeconds < 0.f)
    {
        Problems.Add(TEXT("ChyronMaxQueued must be 1 or more; ChyronMinShowSeconds and ChyronGapSeconds 0 or more"));
    }
    for (const EPSChyronKind Kind : PSOverlayBroadcastPrivate::AllKinds)
    {
        const FPSChyronKindStyle* Style = InTheme.FindChyronKind(Kind);
        if (!Style)
        {
            Problems.Add(FString::Printf(TEXT("ChyronKinds has no %s entry"), *UEnum::GetValueAsString(Kind)));
        }
        else if (!(Style->Seconds > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s: Seconds must be above 0"), *UEnum::GetValueAsString(Kind)));
        }
    }
    for (int32 Index = 0; Index < InTheme.ChyronKinds.Num(); ++Index)
    {
        for (int32 Other = 0; Other < Index; ++Other)
        {
            if (InTheme.ChyronKinds[Other].Kind == InTheme.ChyronKinds[Index].Kind)
            {
                Problems.Add(FString::Printf(TEXT("ChyronKinds[%d]: %s is listed twice"), Index, *UEnum::GetValueAsString(InTheme.ChyronKinds[Index].Kind)));
            }
        }
    }
    return Problems;
}

void UPSOverlayBroadcastSubsystem::SetTeams(FName InHomeTeamId, FName InAwayTeamId)
{
    HomeTeamId = InHomeTeamId;
    AwayTeamId = InAwayTeamId;
    RefreshTeams();
}

const FPSTeamSummary* UPSOverlayBroadcastSubsystem::FindTeam(FName TeamId)
{
    if (TeamId.IsNone())
    {
        return nullptr;
    }
    if (!bTeamsLoaded)
    {
        bTeamsLoaded = true;
        TArray<FString> Errors;
        UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), Teams, Errors);
    }
    return Teams.FindByPredicate([TeamId](const FPSTeamSummary& Team) { return Team.TeamId == TeamId; });
}

void UPSOverlayBroadcastSubsystem::RefreshTeams()
{
    const FPSTeamSummary* Home = FindTeam(HomeTeamId);
    const FPSTeamSummary* Away = FindTeam(AwayTeamId);
    ScoreBug.HomeLabel = Home && !Home->Abbreviation.IsEmpty() ? Home->Abbreviation : Theme.HomeLabel;
    ScoreBug.AwayLabel = Away && !Away->Abbreviation.IsEmpty() ? Away->Abbreviation : Theme.AwayLabel;
    ScoreBug.HomeColor = Home && Theme.bUseTeamColors ? Home->PrimaryColor : PSOverlayBroadcastPrivate::ParseOr(Theme.HomeColor, FLinearColor::Blue);
    ScoreBug.AwayColor = Away && Theme.bUseTeamColors ? Away->PrimaryColor : PSOverlayBroadcastPrivate::ParseOr(Theme.AwayColor, FLinearColor::Red);
}

void UPSOverlayBroadcastSubsystem::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    if (OverlayDetail == EPSOverlayDetail::Minimal)
    {
        // The minimal tier draws the score bug only.
        ChyronQueue.Reset();
        bChyronShowing = false;
        GapRemaining = 0.f;
    }
}

void UPSOverlayBroadcastSubsystem::RefreshDerived()
{
    if (!ScoreBug.bValid)
    {
        return;
    }
    ScoreBug.QuarterText = PSGameStateEvents::QuarterLabel(ScoreBug.Quarter);
    ScoreBug.GameClockText = PSGameStateEvents::ClockText(ScoreBug.GameClockSeconds);
    ScoreBug.PlayClockText = ScoreBug.bPlayClockRunning ? FString::FromInt(FMath::Max(0, FMath::CeilToInt(ScoreBug.PlayClockSeconds))) : FString();
    ScoreBug.bTwoMinute = (ScoreBug.Quarter == 2 || ScoreBug.Quarter == 4) && ScoreBug.GameClockSeconds <= Theme.TwoMinuteSeconds;

    const bool bKickoff = ScoreBug.Phase == TEXT("Kickoff");
    ScoreBug.bRedZone = !bKickoff && LastGameState.YardLine >= Theme.RedZoneYardLine;
    if (bKickoff)
    {
        ScoreBug.SituationText = TEXT("Kickoff");
    }
    else
    {
        // The play-call screen's wording, so the two never disagree (rule 3).
        FPSSituationContext Situation;
        Situation.Down = LastGameState.Down;
        Situation.Distance = LastGameState.Distance;
        Situation.YardLine = LastGameState.YardLine;
        Situation.Quarter = LastGameState.Quarter;
        Situation.GameClockSeconds = ScoreBug.GameClockSeconds;
        ScoreBug.SituationText = UPSPlayCallSubsystem::DescribeSituation(Situation);
    }
}

FString UPSOverlayBroadcastSubsystem::ScoreLine() const
{
    return FString::Printf(TEXT("%s %d - %s %d"), *ScoreBug.HomeLabel, ScoreBug.HomeScore, *ScoreBug.AwayLabel, ScoreBug.AwayScore);
}

void UPSOverlayBroadcastSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    const FPSTelemetryGameStateEvent Previous = LastGameState;
    const bool bHadState = bHasGameState;
    LastGameState = Event;
    bHasGameState = true;

    ScoreBug.bValid = true;
    ScoreBug.Phase = Event.Phase;
    ScoreBug.Quarter = Event.Quarter;
    ScoreBug.GameClockSeconds = Event.GameClockSeconds;
    ScoreBug.bGameClockRunning = Event.bGameClockRunning;
    ScoreBug.PlayClockSeconds = Event.PlayClockSeconds;
    ScoreBug.bPlayClockRunning = Event.bPlayClockRunning;
    ScoreBug.HomeScore = Event.HomeScore;
    ScoreBug.AwayScore = Event.AwayScore;
    ScoreBug.bHomeHasPossession = Event.bHomeHasPossession;
    ScoreBug.HomeTimeouts = Event.HomeTimeoutsRemaining;
    ScoreBug.AwayTimeouts = Event.AwayTimeoutsRemaining;
    ScoreBug.MaxTimeouts = Event.MaxTimeouts;
    RefreshDerived();

    if (!bHadState)
    {
        AnnouncedHomeScore = Event.HomeScore;
        AnnouncedAwayScore = Event.AwayScore;
        return;
    }

    // Points on the board, unless a Score event already said so.
    const int32 Points = (Event.HomeScore - AnnouncedHomeScore) + (Event.AwayScore - AnnouncedAwayScore);
    if (Points > 0)
    {
        const bool bNewDrive = Event.CompletedDrives > Previous.CompletedDrives;
        const bool bDriveScored = bNewDrive && (Event.LastDriveResult == TEXT("Touchdown") || Event.LastDriveResult == TEXT("Safety"));
        const FString Headline = bDriveScored ? Event.LastDriveResult.ToUpper() : PSOverlayBroadcastPrivate::ScoreHeadline(Points);
        PushChyron(EPSChyronKind::ScoreAlert, Headline, ScoreLine());
    }
    AnnouncedHomeScore = Event.HomeScore;
    AnnouncedAwayScore = Event.AwayScore;

    // A finished drive, credited to the side that had the ball.
    if (Event.CompletedDrives > Previous.CompletedDrives)
    {
        const FString& Team = Previous.bHomeHasPossession ? ScoreBug.HomeLabel : ScoreBug.AwayLabel;
        FString Detail = FString::Printf(TEXT("%d plays, %d yards"), Event.LastDrivePlays, Event.LastDriveYards);
        if (!Event.LastDriveResult.IsEmpty())
        {
            Detail += FString::Printf(TEXT(", %s"), *Event.LastDriveResult);
        }
        PushChyron(EPSChyronKind::DriveSummary, FString::Printf(TEXT("%s DRIVE"), *Team), Detail);
    }
}

void UPSOverlayBroadcastSubsystem::HandleScore(const FPSTelemetryScoreEvent& Event)
{
    const FString Headline = Event.ScoreType.IsEmpty() ? PSOverlayBroadcastPrivate::ScoreHeadline(Event.Points) : Event.ScoreType.ToUpper();
    PushChyron(EPSChyronKind::ScoreAlert, Headline, FString::Printf(TEXT("%s %d - %s %d"),
        *ScoreBug.HomeLabel, Event.HomeScore, *ScoreBug.AwayLabel, Event.AwayScore));
    AnnouncedHomeScore = Event.HomeScore;
    AnnouncedAwayScore = Event.AwayScore;
}

void UPSOverlayBroadcastSubsystem::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    // A catch's line comes with the tackle that ends the play; a pick is its own moment.
    if (Event.bIsInterception)
    {
        PushChyron(EPSChyronKind::PlayStat, TEXT("INTERCEPTION"), Event.ReceiverName);
    }
}

void UPSOverlayBroadcastSubsystem::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    if (Event.bIsSack)
    {
        PushChyron(EPSChyronKind::PlayStat, TEXT("SACK"), FString::Printf(TEXT("%s on %s, %s"),
            *Event.TacklerName, *Event.BallCarrierName, *PSOverlayBroadcastPrivate::GainText(Event.YardsGained)));
        return;
    }
    PushChyron(EPSChyronKind::PlayStat, Event.BallCarrierName, PSOverlayBroadcastPrivate::GainText(Event.YardsGained));
}

bool UPSOverlayBroadcastSubsystem::PushStatLine(const FString& PlayerName, const FString& StatText)
{
    return PushChyron(EPSChyronKind::StatLine, PlayerName, StatText);
}

bool UPSOverlayBroadcastSubsystem::PushChyron(EPSChyronKind Kind, const FString& Headline, const FString& Detail)
{
    if (OverlayDetail == EPSOverlayDetail::Minimal || (Headline.IsEmpty() && Detail.IsEmpty()))
    {
        return false;
    }

    FPSChyron Entry;
    Entry.Kind = Kind;
    Entry.Headline = Headline;
    Entry.Detail = Detail;
    const FPSChyronKindStyle* Style = Theme.FindChyronKind(Kind);
    Entry.Priority = Style ? Style->Priority : 0;
    Entry.Seconds = Style ? Style->Seconds : 4.f;
    Entry.Order = NextChyronOrder++;

    ChyronQueue.Add(Entry);
    ChyronQueue.StableSort([](const FPSChyron& A, const FPSChyron& B)
    {
        return A.Priority != B.Priority ? A.Priority > B.Priority : A.Order < B.Order;
    });

    // Over the limit: the lowest priority goes, the oldest of them first.
    while (ChyronQueue.Num() > FMath::Max(1, Theme.ChyronMaxQueued))
    {
        int32 Drop = 0;
        for (int32 Index = 1; Index < ChyronQueue.Num(); ++Index)
        {
            const FPSChyron& Candidate = ChyronQueue[Index];
            const FPSChyron& Worst = ChyronQueue[Drop];
            if (Candidate.Priority < Worst.Priority || (Candidate.Priority == Worst.Priority && Candidate.Order < Worst.Order))
            {
                Drop = Index;
            }
        }
        ChyronQueue.RemoveAt(Drop);
    }

    if (!bChyronShowing && GapRemaining <= 0.f)
    {
        ShowNextChyron();
    }
    return true;
}

void UPSOverlayBroadcastSubsystem::ShowNextChyron()
{
    if (ChyronQueue.Num() == 0)
    {
        bChyronShowing = false;
        return;
    }
    CurrentChyron = ChyronQueue[0];
    ChyronQueue.RemoveAt(0);
    bChyronShowing = true;
    ChyronElapsed = 0.f;
    GapRemaining = 0.f;
}

bool UPSOverlayBroadcastSubsystem::GetCurrentChyron(FPSChyron& OutChyron) const
{
    if (!bChyronShowing)
    {
        return false;
    }
    OutChyron = CurrentChyron;
    return true;
}

void UPSOverlayBroadcastSubsystem::AdvanceTime(float DeltaSeconds)
{
    if (DeltaSeconds <= 0.f)
    {
        return;
    }

    // The clocks run on between the simulation's announcements.
    if (ScoreBug.bValid)
    {
        if (ScoreBug.bGameClockRunning)
        {
            ScoreBug.GameClockSeconds = FMath::Max(0.f, ScoreBug.GameClockSeconds - DeltaSeconds);
        }
        if (ScoreBug.bPlayClockRunning)
        {
            ScoreBug.PlayClockSeconds = FMath::Max(0.f, ScoreBug.PlayClockSeconds - DeltaSeconds);
        }
        RefreshDerived();
    }

    if (bChyronShowing)
    {
        ChyronElapsed += DeltaSeconds;
        const bool bExpired = ChyronElapsed >= CurrentChyron.Seconds;
        const bool bCutIn = ChyronQueue.Num() > 0 && ChyronQueue[0].Priority > CurrentChyron.Priority
            && ChyronElapsed >= Theme.ChyronMinShowSeconds;
        if (bCutIn)
        {
            // Cut straight to the more important one; the one cut is dropped.
            ShowNextChyron();
        }
        else if (bExpired)
        {
            // Time past the end counts toward the gap, so the timing doesn't depend on the
            // size of the steps.
            const float Overflow = ChyronElapsed - CurrentChyron.Seconds;
            bChyronShowing = false;
            GapRemaining = Theme.ChyronGapSeconds - Overflow;
            if (GapRemaining <= 0.f)
            {
                ShowNextChyron();
            }
        }
        return;
    }

    if (GapRemaining > 0.f)
    {
        GapRemaining = FMath::Max(0.f, GapRemaining - DeltaSeconds);
    }
    if (GapRemaining <= 0.f)
    {
        ShowNextChyron();
    }
}
