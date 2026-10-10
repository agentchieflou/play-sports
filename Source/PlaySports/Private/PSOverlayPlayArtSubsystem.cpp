#include "PSOverlayPlayArtSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
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
    // The player can turn the art off (or on) from the settings at any time.
    const bool bSettingOn = IsSettingOn();
    if (bSettingOn != bSettingWasOn)
    {
        bSettingWasOn = bSettingOn;
        bStale = true;
    }
    AdvanceTime(DeltaTime);

    const UWorld* World = GetWorld();
    if (Style.bDrawDebug && World && IsVisibleTo(World->GetFirstPlayerController()))
    {
        PSPlayArt::DrawDebug(World, RouteArt, Style, GetOpacity());
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

bool UPSOverlayPlayArtSubsystem::IsVisibleTo(const APlayerController* Viewer) const
{
    if (RouteArt.Num() == 0)
    {
        return false;
    }
    // Head to head, the house rules say who may see it on a shared or split screen (Epic 107).
    const UWorld* World = GetWorld();
    const UPSVersusSubsystem* Versus = World ? World->GetSubsystem<UPSVersusSubsystem>() : nullptr;
    if (Versus && Versus->IsSessionActive())
    {
        return Versus->ShouldShowOverlay(EPSVersusOverlay::RouteArt, Viewer);
    }
    // Otherwise the offense's art is the offense's: like its call, a defending player doesn't
    // see it. A spectator does.
    const APSPlayerPawn* ViewerPawn = Viewer ? Cast<APSPlayerPawn>(Viewer->GetPawn()) : nullptr;
    return !ViewerPawn || ViewerPawn->TeamSide != EPSTeamSide::Defense;
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

bool UPSOverlayPlayArtSubsystem::IsSettingOn() const
{
    UPSSettingsSubsystem* Settings = GetSettings();
    return !Settings || !Settings->GetCatalog().FindSetting(RouteArtSettingId) || Settings->GetBool(RouteArtSettingId);
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
    PlayId = NAME_None;
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

    if (!bHasLine || OverlayDetail == EPSOverlayDetail::Minimal || !IsSettingOn())
    {
        return;
    }
    UWorld* World = GetWorld();
    UPSPlayCallSubsystem* PlayCall = World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
    if (!PlayCall)
    {
        return;
    }
    // The offense's call for the coming snap, as the play-call authority holds it.
    const FPSPlayCall& Call = PlayCall->GetCall(true);
    FPSPlayDefinition Play;
    if (!PlayCall->IsCallWindowOpen() || !Call.IsSet() || !PlayCall->FindPlay(Call.PlayId, Play) || !Play.bIsOffensivePlay
        || Style.NoRouteArtCategories.Contains(Play.PlayCategory))
    {
        return;
    }

    // Resolved exactly as the snap will resolve it (UPSPlayOrchestrator), against the line the
    // play simulation announced.
    const UDataTable* Routes = PlayCall->GetRouteLibrary();
    const TArray<FPSResolvedAssignment> Resolved = PSPlayResolution::ResolvePlay(Play, UPSAIFieldSnapshot::GetFieldPawns(World), Routes, LineOfScrimmage, false);
    RouteArt = PSPlayArt::CompileRouteArt(Resolved, Routes, Style, GetBreakMinAngleDegrees(), LineOfScrimmage.Z);
    PlayId = RouteArt.Num() > 0 ? Play.PlayId : NAME_None;
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
    // Before the snap the art is rebuilt after an event, and follows the players as they shift
    // and go in motion.
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
    // Rebuilt on the next tick, once every system has heard the call: the pre-snap authority
    // drops the old call's changes on it.
    if (Event.bOffense)
    {
        bStale = true;
    }
}

void UPSOverlayPlayArtSubsystem::HandlePreSnap(const FPSTelemetryPreSnapEvent& Event)
{
    // A hot route, a back kept in, a man in motion: the art shows the play as it now runs.
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
    RefreshClock = 0.f;
    // A Full tier fades the art out; the others take it away at once (no animated transitions).
    FadeRemaining = (OverlayDetail == EPSOverlayDetail::Full && RouteArt.Num() > 0) ? Style.SnapFadeSeconds : 0.f;
    if (FadeRemaining <= 0.f)
    {
        Clear();
    }
}

void UPSOverlayPlayArtSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        // A new down: its art comes with the offense's next call.
        bSnapped = false;
        Clear();
        bStale = true;
    }
}
