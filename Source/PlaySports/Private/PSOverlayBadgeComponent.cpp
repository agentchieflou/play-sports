#include "PSOverlayBadgeComponent.h"
#include "PSPerfBudget.h"
#include "PSBall.h"
#include "PSDataIngestion.h"
#include "PSInputConfig.h"
#include "PSInputDeviceComponent.h"
#include "PSOverlayBadgeLayout.h"
#include "PSPassingComponent.h"
#include "PSPlayContextComponent.h"
#include "PSPlayerController.h"
#include "PSUITeamCatalog.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/Paths.h"

namespace PSOverlayBadgePrivate
{
    /** Every group and every role needs a style entry. */
    const EPSBadgeGroup AllGroups[] = { EPSBadgeGroup::Receiver, EPSBadgeGroup::Back, EPSBadgeGroup::Quarterback, EPSBadgeGroup::Line, EPSBadgeGroup::Defense };
    const EPlayerRole AllRoles[] = { EPlayerRole::Quarterback, EPlayerRole::RunningBack, EPlayerRole::WideReceiver, EPlayerRole::TightEnd,
        EPlayerRole::OffensiveLineman, EPlayerRole::DefensiveLineman, EPlayerRole::Linebacker, EPlayerRole::DefensiveBack };

    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed = Fallback;
        UPSUITeamCatalog::ParseHexColor(Hex, Parsed);
        return Parsed;
    }
}

UPSOverlayBadgeComponent::UPSOverlayBadgeComponent()
{
    // The widget lays the badges out as it draws (UpdateForCurrentView); nothing to tick.
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSOverlayBadgeComponent::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/overlay_badges.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSOverlayBadgeStyle& UPSOverlayBadgeComponent::GetStyle()
{
    if (!bStyleLoaded)
    {
        bStyleLoaded = true;
        LoadStyleFromJson(GetDefaultStylePath());
    }
    return Style;
}

bool UPSOverlayBadgeComponent::LoadStyleFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSOverlayBadgeStyle Loaded;
    if (!Ingestion->LoadOverlayBadgeStyleFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBadgeComponent: Could not load the badge style from %s; keeping the current one."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    if (Problems.Num() > 0)
    {
        for (const FString& Problem : Problems)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSOverlayBadgeComponent: %s: %s"), *JsonFilePath, *Problem);
        }
        return false;
    }
    SetStyle(Loaded);
    return true;
}

void UPSOverlayBadgeComponent::SetStyle(const FPSOverlayBadgeStyle& InStyle)
{
    Style = InStyle;
    bStyleLoaded = true;
}

TArray<FString> UPSOverlayBadgeComponent::ValidateStyle(const FPSOverlayBadgeStyle& InStyle)
{
    using namespace PSOverlayBadgePrivate;

    TArray<FString> Problems;
    for (const EPSBadgeGroup Group : AllGroups)
    {
        const FString GroupName = UEnum::GetDisplayValueAsText(Group).ToString();
        int32 Count = 0;
        for (const FPSBadgeGroupStyle& Entry : InStyle.Groups)
        {
            if (Entry.Group != Group)
            {
                continue;
            }
            ++Count;
            FLinearColor Parsed;
            if (!UPSUITeamCatalog::ParseHexColor(Entry.Color, Parsed))
            {
                Problems.Add(FString::Printf(TEXT("Groups %s: Color '%s' must be #RRGGBB"), *GroupName, *Entry.Color));
            }
            if (!UPSUITeamCatalog::ParseHexColor(Entry.TextColor, Parsed))
            {
                Problems.Add(FString::Printf(TEXT("Groups %s: TextColor '%s' must be #RRGGBB"), *GroupName, *Entry.TextColor));
            }
        }
        if (Count != 1)
        {
            Problems.Add(FString::Printf(TEXT("Groups: %s is listed %d times; it needs exactly one entry"), *GroupName, Count));
        }
    }
    for (const EPlayerRole Role : AllRoles)
    {
        if (InStyle.LabelForRole(Role).IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("RoleLabels: no label for %s"), *UEnum::GetDisplayValueAsText(Role).ToString()));
        }
    }

    if (InStyle.BadgeWidth <= 0.f || InStyle.BadgeHeight <= 0.f)
    {
        Problems.Add(TEXT("BadgeWidth and BadgeHeight must be above 0"));
    }
    if (InStyle.FontSize < 1)
    {
        Problems.Add(FString::Printf(TEXT("FontSize: %d must be 1 or more"), InStyle.FontSize));
    }
    if (InStyle.ReferenceDistance <= 0.f)
    {
        Problems.Add(TEXT("ReferenceDistance must be above 0"));
    }
    if (InStyle.MinScale <= 0.f || InStyle.MaxScale < InStyle.MinScale)
    {
        Problems.Add(FString::Printf(TEXT("Scale: MinScale %.2f must be above 0 and at most MaxScale %.2f"), InStyle.MinScale, InStyle.MaxScale));
    }
    if (InStyle.NudgeStep <= 0.f)
    {
        Problems.Add(TEXT("NudgeStep must be above 0"));
    }
    if (InStyle.MaxNudges < 0 || InStyle.HeadClearance < 0.f || InStyle.BallClearance < 0.f || InStyle.FadeInSeconds < 0.f)
    {
        Problems.Add(TEXT("MaxNudges, HeadClearance, BallClearance and FadeInSeconds must be 0 or more"));
    }
    return Problems;
}

EPSBadgeGroup UPSOverlayBadgeComponent::GroupFor(EPlayerRole Role, EPSTeamSide Side)
{
    if (Side == EPSTeamSide::Defense)
    {
        return EPSBadgeGroup::Defense;
    }
    switch (Role)
    {
    case EPlayerRole::WideReceiver:
    case EPlayerRole::TightEnd:
        return EPSBadgeGroup::Receiver;
    case EPlayerRole::RunningBack:
        return EPSBadgeGroup::Back;
    case EPlayerRole::Quarterback:
        return EPSBadgeGroup::Quarterback;
    case EPlayerRole::OffensiveLineman:
        return EPSBadgeGroup::Line;
    default:
        return EPSBadgeGroup::Defense;
    }
}

void UPSOverlayBadgeComponent::SetOverlayDetail(EPSOverlayDetail InDetail)
{
    OverlayDetail = InDetail;
}

void UPSOverlayBadgeComponent::BeginPlay()
{
    Super::BeginPlay();
    SetOverlayDetail(PSPlatformTiers::GetActiveTier().OverlayDetail);
}

APSPlayerController* UPSOverlayBadgeComponent::GetPlayerController() const
{
    return Cast<APSPlayerController>(GetOwner());
}

void UPSOverlayBadgeComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += FMath::Max(DeltaSeconds, 0.f);
}

void UPSOverlayBadgeComponent::UpdateForCurrentView(float DeltaSeconds)
{
    PS_PERF_SCOPE(Overlays);
    AdvanceTime(DeltaSeconds);
    FPSBadgeView View;
    if (GetCurrentView(View))
    {
        Refresh(View);
    }
    else
    {
        Badges.Reset();
    }
}

bool UPSOverlayBadgeComponent::GetCurrentView(FPSBadgeView& OutView) const
{
    const APSPlayerController* Controller = GetPlayerController();
    if (!Controller || !Controller->IsLocalController() || !Controller->PlayerCameraManager)
    {
        return false;
    }
    int32 SizeX = 0;
    int32 SizeY = 0;
    Controller->GetViewportSize(SizeX, SizeY);
    if (SizeX <= 0 || SizeY <= 0)
    {
        return false;
    }
    OutView.CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
    OutView.CameraRotation = Controller->PlayerCameraManager->GetCameraRotation();
    OutView.FOVDegrees = Controller->PlayerCameraManager->GetFOVAngle();
    OutView.ViewportSize = FVector2D(SizeX, SizeY);
    return true;
}

void UPSOverlayBadgeComponent::Refresh(const FPSBadgeView& View)
{
    using namespace PSOverlayBadgePrivate;

    const FPSOverlayBadgeStyle& Current = GetStyle();
    Badges.Reset();

    APSPlayerController* Controller = GetPlayerController();
    UWorld* World = GetWorld();
    const APSPlayerPawn* Controlled = Controller ? Cast<APSPlayerPawn>(Controller->GetPawn()) : nullptr;
    if (!Controlled || !World)
    {
        // The human controls no football player: no badges.
        ShownSince.Reset();
        return;
    }

    const UPSPlayContextComponent* PlayContext = Controller->GetPlayContextComponent();
    const bool bLive = PlayContext && PlayContext->IsPlayLive();

    // The pass buttons: while the human's quarterback can throw (before the snap, or holding the
    // ball during the play), each receiver slot wears the button of its slot action.
    UPSPassingComponent* Passing = Controller->GetPassingComponent();
    const bool bCanThrow = Passing && Controlled->GetAttributes().Role == EPlayerRole::Quarterback && (!bLive || Passing->CanPass());
    TMap<const APSPlayerPawn*, int32> SlotOf;
    TArray<FName> SlotActions;
    if (bCanThrow)
    {
        const TArray<APSPlayerPawn*> Slots = Passing->GetReceiverSlots();
        SlotActions = Passing->GetTuning().SlotActions;
        for (int32 Slot = 0; Slot < Slots.Num() && Slot < SlotActions.Num(); ++Slot)
        {
            SlotOf.Add(Slots[Slot], Slot);
        }
    }
    const UPSInputDeviceComponent* Devices = Controller->GetInputDeviceComponent();
    const EPSInputDevice Device = Devices ? Devices->GetActiveDevice() : EPSInputDevice::KeyboardMouse;
    const FName PassingContext = PlayContext ? PlayContext->PassingContextId : FName(TEXT("Passing"));
    UPSInputConfig* Config = SlotOf.Num() > 0 ? Controller->GetInputConfig() : nullptr;

    for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
    {
        APSPlayerPawn* Player = *It;
        if (!IsValid(Player) || (Player == Controlled && !Current.bBadgeControlledPlayer))
        {
            continue;
        }
        const FPlayerAttributes Attributes = Player->GetAttributes();
        FPSPositionBadge Badge;
        Badge.Pawn = Player;
        Badge.PlayerId = Attributes.PlayerId;
        Badge.Group = GroupFor(Attributes.Role, Player->TeamSide);
        const FPSBadgeGroupStyle* GroupStyle = Current.FindGroup(Badge.Group);
        if (!GroupStyle)
        {
            continue;
        }
        Badge.Color = ParseOr(GroupStyle->Color, FLinearColor::White);
        Badge.TextColor = ParseOr(GroupStyle->TextColor, FLinearColor::Black);

        // What it says: his pass button, else his role.
        if (const int32* Slot = SlotOf.Find(Player))
        {
            FPSInputGlyph Glyph;
            if (Config && Config->GetGlyphForAction(SlotActions[*Slot], PassingContext, Device, Glyph))
            {
                Badge.Label = Glyph.Label;
                Badge.GlyphId = Glyph.GlyphId;
                Badge.PassSlot = *Slot;
            }
        }
        if (Badge.Label.IsEmpty())
        {
            Badge.Label = Current.LabelForRole(Attributes.Role);
        }

        // Whether it shows now.
        bool bShown = bLive
            ? (GroupStyle->InPlay == EPSBadgeInPlay::Always || (GroupStyle->InPlay == EPSBadgeInPlay::WhilePassing && bCanThrow))
            : GroupStyle->bPreSnap;
        if (OverlayDetail == EPSOverlayDetail::Minimal && !GroupStyle->bEssential)
        {
            bShown = false;
        }

        // Where: over his head, sized by distance.
        Badge.WorldAnchor = Player->GetActorLocation() + FVector(0.0, 0.0, Player->GetSimpleCollisionHalfHeight() + Current.HeadClearance);
        Badge.Distance = static_cast<float>(FVector::Dist(Badge.WorldAnchor, View.CameraLocation));
        Badge.Scale = PSOverlayBadgeLayout::ScaleForDistance(Current, Badge.Distance);
        Badge.Size = FVector2D(Current.BadgeWidth, Current.BadgeHeight) * Badge.Scale;
        float Depth = 0.f;
        Badge.bOnScreen = PSOverlayBadgeLayout::ProjectToScreen(View, Badge.WorldAnchor, Badge.ScreenPosition, Depth)
            && PSOverlayBadgeLayout::IsInsideViewport(PSOverlayBadgeLayout::BadgeRect(Badge), View.ViewportSize);
        Badge.bVisible = bShown && !Badge.Label.IsEmpty() && Badge.bOnScreen;
        Badges.Add(Badge);
    }
    Badges.Sort([](const FPSPositionBadge& A, const FPSPositionBadge& B) { return A.PlayerId.LexicalLess(B.PlayerId); });

    // Clear of each other and of the ball.
    FBox2D BallRect(ForceInit);
    bool bBallOnScreen = false;
    for (TActorIterator<APSBall> It(World); It; ++It)
    {
        FVector2D BallScreen;
        float BallDepth = 0.f;
        if (IsValid(*It) && PSOverlayBadgeLayout::ProjectToScreen(View, It->GetActorLocation(), BallScreen, BallDepth))
        {
            const FVector2D Clearance(Current.BallClearance, Current.BallClearance);
            BallRect = FBox2D(BallScreen - Clearance, BallScreen + Clearance);
            bBallOnScreen = true;
        }
        break;
    }
    PSOverlayBadgeLayout::ResolveOverlaps(Badges, bBallOnScreen ? &BallRect : nullptr, Current, View.ViewportSize);

    // A badge that has just appeared fades in, on a Full tier.
    TMap<TWeakObjectPtr<APSPlayerPawn>, float> StillShown;
    for (FPSPositionBadge& Badge : Badges)
    {
        if (!Badge.bVisible)
        {
            continue;
        }
        const float* Since = ShownSince.Find(Badge.Pawn);
        const float Start = Since ? *Since : Clock;
        StillShown.Add(Badge.Pawn, Start);
        Badge.Opacity = (OverlayDetail == EPSOverlayDetail::Full && Current.FadeInSeconds > 0.f)
            ? FMath::Clamp((Clock - Start) / Current.FadeInSeconds, 0.f, 1.f)
            : 1.f;
    }
    ShownSince = MoveTemp(StillShown);
}

TArray<FPSPositionBadge> UPSOverlayBadgeComponent::GetVisibleBadges() const
{
    TArray<FPSPositionBadge> Shown;
    for (const FPSPositionBadge& Badge : Badges)
    {
        if (Badge.bVisible)
        {
            Shown.Add(Badge);
        }
    }
    return Shown;
}

const FPSPositionBadge* UPSOverlayBadgeComponent::FindBadge(const APSPlayerPawn* Pawn) const
{
    return Badges.FindByPredicate([Pawn](const FPSPositionBadge& Badge) { return Badge.Pawn.Get() == Pawn; });
}
