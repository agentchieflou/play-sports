#include "PSOverlayPlayArtSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSCoverageMatchupSubsystem.h"
#include "PSDataIngestion.h"
#include "PSGameStateEvents.h"
#include "PSPerfBudget.h"
#include "PSPlayArt.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayResolution.h"
#include "PSPlayerPawn.h"
#include "PSRouteRunnerComponent.h"
#include "PSSettingsSubsystem.h"
#include "PSVersusSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"

const FName UPSOverlayPlayArtSubsystem::RouteArtSettingId(TEXT("RouteArt"));
const FName UPSOverlayPlayArtSubsystem::DefenseIconsSettingId(TEXT("DefenseIcons"));
const FName UPSOverlayPlayArtSubsystem::StudyModeSettingId(TEXT("StudyMode"));

void UPSOverlayPlayArtSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadStyleFromJson(GetDefaultStylePath());
    const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
    OverlayDetail = Tier.OverlayDetail;
    RefreshHz = Tier.PlayArtRefreshHz;

    if (UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>())
    {
        Bus->OnPlayCallMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandlePlayCall);
        Bus->OnPreSnapMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandlePreSnap);
        Bus->OnDefensivePreSnapMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandleDefensivePreSnap);
        Bus->OnGameStateMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandleGameState);
        Bus->OnSnapMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSOverlayPlayArtSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSOverlayPlayArtSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.RemoveAll(this);
        Bus->OnPreSnapMC.RemoveAll(this);
        Bus->OnDefensivePreSnapMC.RemoveAll(this);
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSOverlayPlayArtSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSOverlayPlayArtSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // The art's cost is the Overlays budget's (Epic 114): rebuilding it, and the debug draw.
    PS_PERF_SCOPE(Overlays);

    // The player can turn either side's art off (or on) from the settings at any time.
    const bool bRouteSettingOn = IsSettingOn(RouteArtSettingId);
    const bool bDefenseSettingOn = IsSettingOn(DefenseIconsSettingId);
    if (bRouteSettingOn != bRouteSettingWasOn || bDefenseSettingOn != bDefenseSettingWasOn)
    {
        bRouteSettingWasOn = bRouteSettingOn;
        bDefenseSettingWasOn = bDefenseSettingOn;
        bStale = true;
    }
    AdvanceTime(DeltaTime);

    const UWorld* World = GetWorld();
    if (!Style.bDrawDebug || !World)
    {
        return;
    }
    const APlayerController* Viewer = World->GetFirstPlayerController();
    if (IsVisibleTo(Viewer))
    {
        PSPlayArt::DrawDebug(World, RouteArt, Style, GetOpacity());
    }
    if (IsDefenseArtVisibleTo(Viewer))
    {
        PSPlayArt::DrawDebug(World, DefenseArt, Style, GetOpacity());
    }
}

TStatId UPSOverlayPlayArtSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSOverlayPlayArtSubsystem, STATGROUP_Tickables);
}

FString UPSOverlayPlayArtSubsystem::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/play_art.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSOverlayPlayArtSubsystem::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPlayArtStyle Loaded;
    if (!Ingestion->LoadPlayArtStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayPlayArtSubsystem: Could not load the play art style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayPlayArtSubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSOverlayPlayArtSubsystem::SetStyle(const FPSPlayArtStyle& InStyle)
{
    Style = InStyle;
    Refresh();
}

TArray<FString> UPSOverlayPlayArtSubsystem::ValidateStyle(const FPSPlayArtStyle& InStyle)
{
    return PSPlayArt::ValidateStyle(InStyle);
}

float UPSOverlayPlayArtSubsystem::GetOpacity() const
{
    if (!bSnapped)
    {
        return 1.f;
    }
    return Style.SnapFadeSeconds > 0.f ? FMath::Clamp(FadeRemaining / Style.SnapFadeSeconds, 0.f, 1.f) : 0.f;
}

bool UPSOverlayPlayArtSubsystem::GetVersusVerdict(EPSVersusOverlay Overlay, const APlayerController* Viewer, bool& bOutShown) const
{
    const UWorld* World = GetWorld();
    const UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (!Versus || !Versus->IsSessionActive())
    {
        return false;
    }
    bOutShown = Versus->ShouldShowOverlay(Overlay, Viewer);
    return true;
}

bool UPSOverlayPlayArtSubsystem::IsVisibleTo(const APlayerController* Viewer) const
{
    if (RouteArt.Num() == 0)
    {
        return false;
    }
    // Head to head, the house rules say who may see it on a shared or split screen (Epic 107).
    bool bShown = false;
    if (GetVersusVerdict(EPSVersusOverlay::RouteArt, Viewer, bShown))
    {
        return bShown;
    }
    // Otherwise the offense's art is the offense's: like its call, a defending player doesn't
    // see it. A spectator does.
    const APSPlayerPawn* ViewerPawn = Viewer ? Cast<APSPlayerPawn>(Viewer->GetPawn()) : nullptr;
    return !ViewerPawn || ViewerPawn->TeamSide != EPSTeamSide::Defense;
}

bool UPSOverlayPlayArtSubsystem::IsDefenseArtVisibleTo(const APlayerController* Viewer) const
{
    if (DefenseArt.Num() == 0)
    {
        return false;
    }
    // Head to head -- the competitive context -- only the house rules decide: study mode doesn't
    // reach past them (Epic 107).
    bool bShown = false;
    if (GetVersusVerdict(EPSVersusOverlay::DefensiveIcons, Viewer, bShown))
    {
        return bShown;
    }
    // Otherwise the defense's icons are the defense's, unless the player studies them from the
    // offense. A spectator sees them.
    const APSPlayerPawn* ViewerPawn = Viewer ? Cast<APSPlayerPawn>(Viewer->GetPawn()) : nullptr;
    return !ViewerPawn || ViewerPawn->TeamSide != EPSTeamSide::Offense || IsSettingOn(StudyModeSettingId, false);
}

void UPSOverlayPlayArtSubsystem::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    if (OverlayDetail == EPSOverlayDetail::Minimal)
    {
        Clear();
    }
    else
    {
        Refresh();
    }
}

void UPSOverlayPlayArtSubsystem::SetRefreshHz(float InRefreshHz)
{
    RefreshHz = FMath::Max(InRefreshHz, 0.f);
    RefreshClock = 0.f;
}

UPSSettingsSubsystem* UPSOverlayPlayArtSubsystem::GetSettings() const
{
    return SettingsOverride ? SettingsOverride : UPSSettingsSubsystem::Get(this);
}

bool UPSOverlayPlayArtSubsystem::IsSettingOn(FName SettingId, bool bWithoutIt) const
{
    UPSSettingsSubsystem* Settings = GetSettings();
    if (!Settings || !Settings->GetCatalog().FindSetting(SettingId))
    {
        return bWithoutIt;
    }
    return Settings->GetBool(SettingId);
}

float UPSOverlayPlayArtSubsystem::GetBreakMinAngleDegrees()
{
    // A cut in the art is a break to the route-running model (Epic 68): one definition.
    if (!bBreakAngleLoaded)
    {
        bBreakAngleLoaded = true;
        UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
        FRouteRunningTuningRow Tuning;
        if (Ingestion->LoadRouteRunningTuningFromJson(UPSRouteRunnerComponent::GetDefaultTuningPath(), Tuning))
        {
            BreakMinAngleDegrees = Tuning.BreakMinAngleDegrees;
        }
        else
        {
            BreakMinAngleDegrees = FRouteRunningTuningRow().BreakMinAngleDegrees;
        }
    }
    return BreakMinAngleDegrees;
}

void UPSOverlayPlayArtSubsystem::Clear()
{
    RouteArt.Reset();
    DefenseArt.Reset();
    OffenseBadgeLetters.Reset();
    DefenseBadgeLetters.Reset();
    PlayId = NAME_None;
    DefensePlayId = NAME_None;
    FadeRemaining = 0.f;
}

void UPSOverlayPlayArtSubsystem::Refresh()
{
    RefreshClock = 0.f;
    bStale = false;
    if (bSnapped)
    {
        // From the snap the art only fades, as it was; the next down starts afresh.
        return;
    }
    Clear();
    if (!bHasLine || OverlayDetail == EPSOverlayDetail::Minimal)
    {
        return;
    }
    RebuildRouteArt();
    RebuildDefenseArt();
}

void UPSOverlayPlayArtSubsystem::RebuildRouteArt()
{
    UWorld* World = GetWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!PlayCall || !IsSettingOn(RouteArtSettingId))
    {
        return;
    }
    // The offense's call for the coming snap, as the play-call authority holds it.
    const FPSPlayCall& Call = PlayCall->GetCall(true);
    FPSPlayDefinition Play;
    if (!PlayCall->IsCallWindowOpen() || !Call.IsSet() || !PlayCall->FindPlay(Call.PlayId, Play) || !Play.bIsOffensivePlay
        || PSPlayArt::DrawsNoArt(Play, Style))
    {
        return;
    }

    // Resolved exactly as the snap will resolve it (UPSPlayOrchestrator), against the line the
    // play simulation announced, and compiled with the play's annotations (Epic 35).
    TArray<FPSResolvedAssignment> Resolved;
    RouteArt = ResolveAndCompile(Play, LineOfScrimmage, Resolved);
    PlayId = RouteArt.Num() > 0 ? Play.PlayId : NAME_None;
    KeepBadgeLetters(Resolved, OffenseBadgeLetters);
}

void UPSOverlayPlayArtSubsystem::RebuildDefenseArt()
{
    UWorld* World = GetWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!PlayCall || !IsSettingOn(DefenseIconsSettingId))
    {
        return;
    }
    // The defense's call as it will run, its adjustment applied (the play-call authority's).
    FPSPlayDefinition Play;
    if (!PlayCall->IsCallWindowOpen() || !PlayCall->GetCall(false).IsSet() || !PlayCall->GetDefensivePlayToRun(Play) || Play.bIsOffensivePlay
        || PSPlayArt::DrawsNoArt(Play, Style))
    {
        return;
    }

    // Resolved as the snap will resolve it, its man matchups taken as the defense AI takes them.
    TArray<FPSResolvedAssignment> Resolved;
    DefenseArt = ResolveAndCompile(Play, LineOfScrimmage, Resolved);
    DefensePlayId = DefenseArt.Num() > 0 ? Play.PlayId : NAME_None;
    KeepBadgeLetters(Resolved, DefenseBadgeLetters);
}

TArray<FPSPlayArtPrimitive> UPSOverlayPlayArtSubsystem::ResolveAndCompile(const FPSPlayDefinition& Play, const FVector& Line, TArray<FPSResolvedAssignment>& OutResolved)
{
    OutResolved.Reset();
    UWorld* World = GetWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    UPSAIFieldSnapshot* Field = World ? World->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!PlayCall || !Field)
    {
        return TArray<FPSPlayArtPrimitive>();
    }
    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const UDataTable* Routes = PlayCall->GetRouteLibrary();
    OutResolved = PSPlayResolution::ResolvePlay(Play, Pawns, Routes, Line, false);
    const UPSCoverageMatchupSubsystem* Matchups = nullptr;
    if (!Play.bIsOffensivePlay)
    {
        Matchups = World->GetSubsystem<UPSCoverageMatchupSubsystem>();
        PSPlayResolution::ResolveManMatchups(OutResolved, Pawns, Field->GetRoles(), Matchups);
    }
    return PSPlayArt::CompilePlayArt(Play, OutResolved, Routes, Style, GetBreakMinAngleDegrees(), Line, Matchups);
}

bool UPSOverlayPlayArtSubsystem::BuildPlayDiagram(FName InPlayId, FPSPlayDiagram& OutDiagram)
{
    OutDiagram = FPSPlayDiagram();
    UWorld* World = GetWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    FPSPlayDefinition Play;
    if (!PlayCall || !PlayCall->FindPlay(InPlayId, Play))
    {
        return false;
    }
    // The announced line; before one, where the situation puts it (the line the snap will use).
    const FVector Line = bHasLine ? LineOfScrimmage : PSGameStateEvents::LineOfScrimmageFor(PlayCall->GetSituation().YardLine);
    TArray<FPSResolvedAssignment> Resolved;
    const TArray<FPSPlayArtPrimitive> Art = ResolveAndCompile(Play, Line, Resolved);
    if (Resolved.Num() == 0)
    {
        return false;
    }
    OutDiagram = PSPlayDiagram::BuildDiagram(Play, Resolved, Art, Line, Style);
    return true;
}

void UPSOverlayPlayArtSubsystem::KeepBadgeLetters(const TArray<FPSResolvedAssignment>& Resolved, TMap<FObjectKey, FString>& OutLetters)
{
    OutLetters.Reset();
    for (const FPSResolvedAssignment& Entry : Resolved)
    {
        const APSPlayerPawn* Player = Entry.Pawn.Get();
        if (Player && Entry.bHasSlot && PSPlayArt::IsValidBadgeLetter(Entry.Assignment.Art.BadgeLetter))
        {
            OutLetters.Add(FObjectKey(Player), Entry.Assignment.Art.BadgeLetter);
        }
    }
}

FString UPSOverlayPlayArtSubsystem::GetBadgeLetter(const APSPlayerPawn* Player, const APlayerController* Viewer) const
{
    if (!Player)
    {
        return FString();
    }
    // A letter is part of its side's art: shown where that art may be seen.
    const FString* Letter = OffenseBadgeLetters.Find(FObjectKey(Player));
    if (Letter && IsVisibleTo(Viewer))
    {
        return *Letter;
    }
    Letter = DefenseBadgeLetters.Find(FObjectKey(Player));
    if (Letter && IsDefenseArtVisibleTo(Viewer))
    {
        return *Letter;
    }
    return FString();
}

void UPSOverlayPlayArtSubsystem::AdvanceTime(float DeltaSeconds)
{
    if (bSnapped)
    {
        if (FadeRemaining > 0.f)
        {
            FadeRemaining -= DeltaSeconds;
            if (FadeRemaining <= 0.f)
            {
                Clear();
            }
        }
        return;
    }
    // Before the snap the art is rebuilt after an event, and follows the players as they shift,
    // go in motion and line up.
    if (RefreshHz > 0.f)
    {
        RefreshClock += DeltaSeconds;
    }
    if (bStale || (RefreshHz > 0.f && RefreshClock >= 1.f / RefreshHz))
    {
        Refresh();
    }
}

void UPSOverlayPlayArtSubsystem::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    // Rebuilt on the next tick, once every system has heard the call: the pre-snap authorities
    // drop the old call's changes on it.
    bStale = true;
}

void UPSOverlayPlayArtSubsystem::HandlePreSnap(const FPSTelemetryPreSnapEvent& Event)
{
    // A hot route, a back kept in, a man in motion: the art shows the play as it now runs.
    bStale = true;
}

void UPSOverlayPlayArtSubsystem::HandleDefensivePreSnap(const FPSTelemetryDefensivePreSnapEvent& Event)
{
    // A shadow, a disguise lining the defense up again, an audible.
    bStale = true;
}

void UPSOverlayPlayArtSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    const bool bMoved = !bHasLine || !Event.LineOfScrimmage.Equals(LineOfScrimmage);
    LineOfScrimmage = Event.LineOfScrimmage;
    bHasLine = true;
    if (bMoved)
    {
        bStale = true;
    }
}

void UPSOverlayPlayArtSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bSnapped = true;
    bStale = false;
    RefreshClock = 0.f;
    // A Full tier fades the art out; the others take it away at once (no animated transitions).
    const bool bAnyArt = RouteArt.Num() > 0 || DefenseArt.Num() > 0;
    FadeRemaining = (OverlayDetail == EPSOverlayDetail::Full && bAnyArt) ? Style.SnapFadeSeconds : 0.f;
    if (FadeRemaining <= 0.f)
    {
        Clear();
    }
}

void UPSOverlayPlayArtSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        // A new down: its art comes with the next calls.
        bSnapped = false;
        Clear();
        bStale = true;
    }
}
