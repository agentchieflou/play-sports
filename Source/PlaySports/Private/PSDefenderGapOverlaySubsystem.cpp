#include "PSDefenderGapOverlaySubsystem.h"
#include "PSDataIngestion.h"
#include "PSOverlayEmphasisSubsystem.h"
#include "PSPlayerPawn.h"
#include "PSUITeamCatalog.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"

namespace PSDefenderGapOverlayPrivate
{
    static TAutoConsoleVariable<int32> CVarGapOverlay(
        TEXT("ps.Overlay.GapIntegrity"),
        0,
        TEXT("1: show the run defense's gap integrity live (Epic 81): a marker per gap, and the owner of an open gap emphasized."),
        ECVF_Default);
}

const FName UPSDefenderGapOverlaySubsystem::EmphasisSource(TEXT("GapIntegrity"));

void UPSDefenderGapOverlaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    LoadStyleFromJson(GetDefaultStylePath());
    bEnabled = Style.bEnabledByDefault;

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnGapIntegrityMC.AddUObject(this, &UPSDefenderGapOverlaySubsystem::HandleGapIntegrity);
        Bus->OnSnapMC.AddUObject(this, &UPSDefenderGapOverlaySubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSDefenderGapOverlaySubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSDefenderGapOverlaySubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGapIntegrityMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSDefenderGapOverlaySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UPSDefenderGapOverlaySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    const bool bConsoleWants = PSDefenderGapOverlayPrivate::CVarGapOverlay.GetValueOnGameThread() != 0;
    if (bConsoleWants != bConsoleEnabled)
    {
        bConsoleEnabled = bConsoleWants;
        SetEnabled(bConsoleWants);
    }
    AdvanceTime(DeltaTime);
    DrawDebugMarkers();
}

TStatId UPSDefenderGapOverlaySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSDefenderGapOverlaySubsystem, STATGROUP_Tickables);
}

FString UPSDefenderGapOverlaySubsystem::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/gap_overlay.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSDefenderGapOverlaySubsystem::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSGapOverlayStyle Loaded;
    if (!Ingestion->LoadGapOverlayStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSDefenderGapOverlaySubsystem: Could not load the gap overlay style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSDefenderGapOverlaySubsystem: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSDefenderGapOverlaySubsystem::SetStyle(const FPSGapOverlayStyle& InStyle)
{
    Style = InStyle;
    if (bEnabled)
    {
        Refresh();
    }
}

TArray<FString> UPSDefenderGapOverlaySubsystem::ValidateStyle(const FPSGapOverlayStyle& InStyle)
{
    TArray<FString> Problems;
    if (InStyle.RefreshSeconds <= 0.f)
    {
        Problems.Add(TEXT("RefreshSeconds must be above 0"));
    }
    if (InStyle.MarkerHeight < 0.f || InStyle.MarkerRadius <= 0.f)
    {
        Problems.Add(TEXT("MarkerHeight must be 0 or more, and MarkerRadius above 0"));
    }
    struct FNamedColor
    {
        const TCHAR* Field;
        const FString* Hex;
    };
    const FNamedColor Colors[] = {
        { TEXT("FilledColor"), &InStyle.FilledColor }, { TEXT("BlockedColor"), &InStyle.BlockedColor },
        { TEXT("OpenColor"), &InStyle.OpenColor }, { TEXT("UnownedColor"), &InStyle.UnownedColor } };
    for (const FNamedColor& Color : Colors)
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Hex, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s: '%s' must be #RRGGBB"), Color.Field, **Color.Hex));
        }
    }
    return Problems;
}

void UPSDefenderGapOverlaySubsystem::SetEnabled(bool bInEnabled)
{
    bEnabled = bInEnabled;
    if (bEnabled)
    {
        Refresh();
    }
    else
    {
        Clear();
    }
}

const FPSGapMarker* UPSDefenderGapOverlaySubsystem::FindMarker(EPSRunGap Gap) const
{
    return Markers.FindByPredicate([Gap](const FPSGapMarker& Marker) { return Marker.Gap == Gap; });
}

void UPSDefenderGapOverlaySubsystem::HandleGapIntegrity(const FPSTelemetryGapIntegrityEvent& Event)
{
    if (bEnabled)
    {
        Refresh();
    }
}

void UPSDefenderGapOverlaySubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    // A new play: its gaps come with its first integrity update.
    bPlayLive = true;
    Clear();
}

void UPSDefenderGapOverlaySubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bPlayLive = false;
        Clear();
    }
}

void UPSDefenderGapOverlaySubsystem::Clear()
{
    Markers.Reset();
    RefreshClock = 0.f;
    SyncEmphasis({});
}

void UPSDefenderGapOverlaySubsystem::Refresh()
{
    RefreshClock = 0.f;
    UWorld* World = GetWorld();
    UPSDefenderGapSubsystem* Gaps = World ? World->GetSubsystem<UPSDefenderGapSubsystem>() : nullptr;
    if (!bEnabled || !bPlayLive || !Gaps)
    {
        Clear();
        return;
    }
    Markers.Reset();
    TArray<FVector> Spots;
    const bool bLine = Gaps->GetGapSpots(Spots);
    TArray<APSPlayerPawn*> OpenOwners;
    for (const FPSGapStatus& Status : Gaps->GetIntegrity())
    {
        FPSGapMarker& Marker = Markers.AddDefaulted_GetRef();
        Marker.Gap = Status.Gap;
        APSPlayerPawn* Owner = Status.Owner.Get();
        Marker.Owner = Owner;
        if (!Owner)
        {
            Marker.State = EPSGapMarkerState::Unowned;
        }
        else if (!Status.bFilled)
        {
            Marker.State = EPSGapMarkerState::Open;
            OpenOwners.Add(Owner);
        }
        else
        {
            Marker.State = Status.bOwnerBlocked ? EPSGapMarkerState::Blocked : EPSGapMarkerState::Filled;
        }
        Marker.OwnerName = Owner ? Owner->GetAttributes().DisplayName : FString();
        Marker.Color = ColorFor(Marker.State);
        const int32 SpotIndex = static_cast<int32>(Marker.Gap);
        Marker.Location = (bLine && Spots.IsValidIndex(SpotIndex) ? Spots[SpotIndex] : FVector::ZeroVector) + FVector(0.f, 0.f, Style.MarkerHeight);
    }
    SyncEmphasis(Style.bEmphasizeOpenOwners ? OpenOwners : TArray<APSPlayerPawn*>());
}

void UPSDefenderGapOverlaySubsystem::SyncEmphasis(const TArray<APSPlayerPawn*>& Owners)
{
    bool bSame = Owners.Num() == EmphasizedOwners.Num();
    for (int32 Index = 0; bSame && Index < Owners.Num(); ++Index)
    {
        bSame = EmphasizedOwners[Index].Get() == Owners[Index];
    }
    if (bSame)
    {
        return;
    }
    EmphasizedOwners.Reset();
    const UWorld* World = GetWorld();
    UPSOverlayEmphasisSubsystem* Emphasis = World ? World->GetSubsystem<UPSOverlayEmphasisSubsystem>() : nullptr;
    if (!Emphasis)
    {
        return;
    }
    Emphasis->ClearSource(EmphasisSource);
    for (APSPlayerPawn* Owner : Owners)
    {
        Emphasis->Emphasize(Owner, Style.OpenOwnerEmphasis, EmphasisSource);
        EmphasizedOwners.Add(Owner);
    }
}

void UPSDefenderGapOverlaySubsystem::AdvanceTime(float DeltaSeconds)
{
    if (!bEnabled || Markers.Num() == 0)
    {
        return;
    }
    RefreshClock += DeltaSeconds;
    if (RefreshClock >= Style.RefreshSeconds)
    {
        Refresh();
    }
}

FLinearColor UPSDefenderGapOverlaySubsystem::ColorFor(EPSGapMarkerState State) const
{
    const FString* Hex = &Style.UnownedColor;
    switch (State)
    {
    case EPSGapMarkerState::Filled: Hex = &Style.FilledColor; break;
    case EPSGapMarkerState::Blocked: Hex = &Style.BlockedColor; break;
    case EPSGapMarkerState::Open: Hex = &Style.OpenColor; break;
    default: break;
    }
    FLinearColor Parsed = FLinearColor::White;
    UPSUITeamCatalog::ParseHexColor(*Hex, Parsed);
    return Parsed;
}

void UPSDefenderGapOverlaySubsystem::DrawDebugMarkers() const
{
#if ENABLE_DRAW_DEBUG
    const UWorld* World = GetWorld();
    if (!World || !bEnabled || !Style.bDrawDebug)
    {
        return;
    }
    for (const FPSGapMarker& Marker : Markers)
    {
        // A flat ring on the turf; an open gap's ring is joined to its owner.
        DrawDebugCircle(World, Marker.Location, Style.MarkerRadius, 24, Marker.Color.ToFColor(true), false, -1.f, 0, 4.f,
            FVector(0.f, 1.f, 0.f), FVector(1.f, 0.f, 0.f), false);
        const APSPlayerPawn* Owner = Marker.Owner.Get();
        if (Owner && Marker.State == EPSGapMarkerState::Open)
        {
            DrawDebugLine(World, Marker.Location, Owner->GetActorLocation(), Marker.Color.ToFColor(true), false, -1.f, 0, 2.f);
        }
    }
#endif
}
