#include "PSOverlayReticleComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSUITeamCatalog.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSOverlayReticlePrivate
{
    /** The visible states, each needing a style entry. */
    const EPSReticleState VisibleStates[] = { EPSReticleState::PreSnap, EPSReticleState::InPlay, EPSReticleState::BallCarrier };
}

UPSOverlayReticleComponent::UPSOverlayReticleComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    Reticle = nullptr;
}

FString UPSOverlayReticleComponent::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/overlay_reticle.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSOverlayReticleStyle& UPSOverlayReticleComponent::GetStyle()
{
    if (!bStyleLoaded)
    {
        LoadStyleFromJson(GetDefaultStylePath());
    }
    return Style;
}

bool UPSOverlayReticleComponent::LoadStyleFromJson(const FString& JsonFilePath)
{
    bStyleLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSOverlayReticleStyle Loaded;
    if (!Ingestion->LoadOverlayReticleStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayReticleComponent: Could not load the reticle style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayReticleComponent: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }

    Style = Loaded;
    if (Reticle)
    {
        Reticle->ApplyStyle(Style);
    }
    return true;
}

TArray<FString> UPSOverlayReticleComponent::ValidateStyle(const FPSOverlayReticleStyle& InStyle)
{
    TArray<FString> Problems;
    FLinearColor Parsed;
    if (!UPSUITeamCatalog::ParseHexColor(InStyle.OffenseColor, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("OffenseColor '%s' is not #RRGGBB"), *InStyle.OffenseColor));
    }
    if (!UPSUITeamCatalog::ParseHexColor(InStyle.DefenseColor, Parsed))
    {
        Problems.Add(FString::Printf(TEXT("DefenseColor '%s' is not #RRGGBB"), *InStyle.DefenseColor));
    }
    if (!(InStyle.MeshDiameter > 0.f) || InStyle.Thickness < 0.f || InStyle.GroundClearance < 0.f)
    {
        Problems.Add(TEXT("MeshDiameter must be above 0; Thickness and GroundClearance 0 or more"));
    }
    for (const EPSReticleState State : PSOverlayReticlePrivate::VisibleStates)
    {
        const FPSOverlayReticleStateStyle* Entry = InStyle.FindState(State);
        if (!Entry)
        {
            Problems.Add(FString::Printf(TEXT("ReticleStates has no %s entry"), *UEnum::GetValueAsString(State)));
            continue;
        }
        if (!(Entry->Radius > 0.f) || Entry->Brightness < 0.f || Entry->PulseHz < 0.f || Entry->PulseAmount < 0.f || Entry->PulseAmount > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: Radius must be above 0, Brightness and PulseHz 0 or more, PulseAmount 0 to 1"), *UEnum::GetValueAsString(State)));
        }
    }
    for (int32 Index = 0; Index < InStyle.ReticleStates.Num(); ++Index)
    {
        const EPSReticleState State = InStyle.ReticleStates[Index].State;
        if (State == EPSReticleState::Hidden)
        {
            Problems.Add(FString::Printf(TEXT("ReticleStates[%d]: Hidden has no look"), Index));
        }
        for (int32 Other = 0; Other < Index; ++Other)
        {
            if (InStyle.ReticleStates[Other].State == State)
            {
                Problems.Add(FString::Printf(TEXT("ReticleStates[%d]: %s is listed twice"), Index, *UEnum::GetValueAsString(State)));
            }
        }
    }
    return Problems;
}

void UPSOverlayReticleComponent::SetTeamColor(const FLinearColor& InTeamColor)
{
    TeamColor = InTeamColor;
    bHasTeamColor = true;
}

void UPSOverlayReticleComponent::ClearTeamColor()
{
    bHasTeamColor = false;
}

void UPSOverlayReticleComponent::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
    if (OverlayDetail != EPSOverlayDetail::Full)
    {
        PulseClock = 0.f;
    }
}

void UPSOverlayReticleComponent::BeginPlay()
{
    Super::BeginPlay();
    GetStyle();
    SetOverlayDetail(PSPlatformTiers::GetActiveTier().OverlayDetail);

    FLinearColor FromLevel;
    if (ResolveTeamColorFromLevel(FromLevel))
    {
        SetTeamColor(FromLevel);
    }
}

void UPSOverlayReticleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (IsValid(Reticle))
    {
        Reticle->Destroy();
    }
    Reticle = nullptr;
    Super::EndPlay(EndPlayReason);
}

void UPSOverlayReticleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AdvanceAnimation(DeltaTime);
    Refresh();
}

APSPlayerController* UPSOverlayReticleComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

bool UPSOverlayReticleComponent::ResolveTeamColorFromLevel(FLinearColor& OutColor) const
{
    // Team select travels with team=<TeamId> (UPSMenuComponent::BuildTravelOptions).
    const UWorld* World = GetWorld();
    const TCHAR* TeamOption = World ? World->URL.GetOption(TEXT("team="), nullptr) : nullptr;
    if (!TeamOption || !*TeamOption)
    {
        return false;
    }

    TArray<FPSTeamSummary> Teams;
    TArray<FString> Errors;
    UPSUITeamCatalog::BuildSummaries(UPSUITeamCatalog::GetDefaultTeamsPath(), Teams, Errors);
    const FName TeamId(TeamOption);
    if (const FPSTeamSummary* Team = Teams.FindByPredicate([TeamId](const FPSTeamSummary& Candidate) { return Candidate.TeamId == TeamId; }))
    {
        OutColor = Team->PrimaryColor;
        return true;
    }
    return false;
}

EPSReticleState UPSOverlayReticleComponent::ComputeState() const
{
    const APSPlayerController* Controller = GetPlayerController();
    const APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Controlled)
    {
        return EPSReticleState::Hidden;
    }

    const UPSPlayContextComponent* PlayContext = Controller->GetPlayContextComponent();
    if (!PlayContext || !PlayContext->IsPlayLive())
    {
        return EPSReticleState::PreSnap;
    }
    return Controlled->HasPossession() ? EPSReticleState::BallCarrier : EPSReticleState::InPlay;
}

FLinearColor UPSOverlayReticleComponent::ComputeBaseColor(EPSTeamSide Side)
{
    const FPSOverlayReticleStyle& Current = GetStyle();
    if (Current.bUseTeamColor && bHasTeamColor)
    {
        return TeamColor;
    }
    FLinearColor SideColor = FLinearColor::White;
    UPSUITeamCatalog::ParseHexColor(Side == EPSTeamSide::Offense ? Current.OffenseColor : Current.DefenseColor, SideColor);
    return SideColor;
}

void UPSOverlayReticleComponent::AdvanceAnimation(float DeltaSeconds)
{
    if (OverlayDetail == EPSOverlayDetail::Full && DeltaSeconds > 0.f)
    {
        PulseClock += DeltaSeconds;
    }
}

APSOverlayReticle* UPSOverlayReticleComponent::EnsureReticle()
{
    if (IsValid(Reticle))
    {
        return Reticle;
    }
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }
    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = GetOwner();
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Reticle = World->SpawnActor<APSOverlayReticle>(APSOverlayReticle::StaticClass(), FTransform::Identity, SpawnParams);
    if (Reticle)
    {
        Reticle->ApplyStyle(GetStyle());
    }
    return Reticle;
}

void UPSOverlayReticleComponent::Refresh()
{
    const EPSReticleState State = ComputeState();
    if (State == EPSReticleState::Hidden && !IsValid(Reticle))
    {
        // Nothing to hide yet: the reticle is made the first time it has someone to mark.
        return;
    }

    APSOverlayReticle* Ring = EnsureReticle();
    if (!Ring)
    {
        return;
    }

    const APSPlayerController* Controller = GetPlayerController();
    APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    Ring->Follow(State == EPSReticleState::Hidden ? nullptr : Controlled);
    if (!Controlled || State == EPSReticleState::Hidden)
    {
        Ring->SetLook(EPSReticleState::Hidden, Ring->GetColor(), 0.f);
        return;
    }

    const FPSOverlayReticleStateStyle* Look = GetStyle().FindState(State);
    const float Brightness = Look ? Look->Brightness : 1.f;
    float Radius = Look ? Look->Radius : 70.f;
    if (Look && OverlayDetail == EPSOverlayDetail::Full && Look->PulseHz > 0.f && Look->PulseAmount > 0.f)
    {
        // Swells from Radius to Radius * (1 + PulseAmount) and back, PulseHz times a second.
        const float Phase = 0.5f - 0.5f * static_cast<float>(FMath::Cos(UE_TWO_PI * Look->PulseHz * PulseClock));
        Radius *= 1.f + Look->PulseAmount * Phase;
    }

    FLinearColor Shown = ComputeBaseColor(Controlled->TeamSide) * Brightness;
    Shown.A = 1.f;
    Ring->SetLook(State, Shown, Radius);
}
