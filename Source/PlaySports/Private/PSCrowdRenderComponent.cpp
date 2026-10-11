// PSCrowdRenderComponent.cpp - Epic 48: the fans in the stadium's seats, instanced, in team colours
#include "PSCrowdRenderComponent.h"
#include "PSCharacterLook.h"
#include "PSCrowdExcitementSubsystem.h"
#include "PSDataIngestion.h"
#include "PSFieldDimensions.h"
#include "PSPerfBudget.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace PSCrowdRenderPrivate
{
    FLinearColor ParseOr(const FString& Hex, const FLinearColor& Fallback)
    {
        FLinearColor Parsed;
        return UPSUITeamCatalog::ParseHexColor(Hex, Parsed) ? Parsed : Fallback;
    }

    /** Pockets are whole sections of one deck. */
    int32 PocketKey(const FPSStadiumSeat& Seat)
    {
        return Seat.Deck * 100000 + Seat.Section;
    }

    int32 RoundToCount(double Value)
    {
        return static_cast<int32>(FMath::RoundToDouble(Value));
    }
}

UPSCrowdRenderComponent::UPSCrowdRenderComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
}

FString UPSCrowdRenderComponent::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/crowd_look.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSCrowdLookStyle UPSCrowdRenderComponent::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSCrowdLookStyle Loaded;
    if (!Ingestion->LoadCrowdLookStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCrowdRenderComponent: Could not load %s; using the default crowd."), *Path);
        return FPSCrowdLookStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCrowdRenderComponent: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSCrowdLookStyle();
}

TArray<FString> UPSCrowdRenderComponent::ValidateStyle(const FPSCrowdLookStyle& InStyle)
{
    TArray<FString> Problems;
    if (!InStyle.BodyMeshPath.StartsWith(TEXT("/")) || !InStyle.MaterialPath.StartsWith(TEXT("/")))
    {
        Problems.Add(TEXT("BodyMeshPath and MaterialPath must be asset paths"));
    }
    if (InStyle.ColorParameter.IsNone())
    {
        Problems.Add(TEXT("ColorParameter must name the material's color parameter"));
    }
    FLinearColor Parsed;
    for (const TPair<const TCHAR*, const FString*>& Color : {
        TPair<const TCHAR*, const FString*>(TEXT("WhiteColor"), &InStyle.WhiteColor),
        TPair<const TCHAR*, const FString*>(TEXT("DefaultHomePrimary"), &InStyle.DefaultHomePrimary),
        TPair<const TCHAR*, const FString*>(TEXT("DefaultHomeSecondary"), &InStyle.DefaultHomeSecondary),
        TPair<const TCHAR*, const FString*>(TEXT("DefaultAwayPrimary"), &InStyle.DefaultAwayPrimary),
        TPair<const TCHAR*, const FString*>(TEXT("DefaultAwaySecondary"), &InStyle.DefaultAwaySecondary) })
    {
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
        }
    }
    if (InStyle.SkinTones.Num() == 0)
    {
        Problems.Add(TEXT("SkinTones must hold at least one colour"));
    }
    for (const TArray<FString>* Palette : { &InStyle.NeutralColors, &InStyle.SkinTones })
    {
        for (const FString& Hex : *Palette)
        {
            if (!UPSUITeamCatalog::ParseHexColor(Hex, Parsed))
            {
                Problems.Add(FString::Printf(TEXT("'%s' in NeutralColors or SkinTones is not #RRGGBB"), *Hex));
            }
        }
    }
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("MeshSizeCm"), InStyle.MeshSizeCm),
        TPair<const TCHAR*, float>(TEXT("TorsoWidthCm"), InStyle.TorsoWidthCm),
        TPair<const TCHAR*, float>(TEXT("TorsoDepthCm"), InStyle.TorsoDepthCm),
        TPair<const TCHAR*, float>(TEXT("TorsoHeightCm"), InStyle.TorsoHeightCm),
        TPair<const TCHAR*, float>(TEXT("HeadSizeCm"), InStyle.HeadSizeCm),
        TPair<const TCHAR*, float>(TEXT("CardDepthCm"), InStyle.CardDepthCm) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    for (const TPair<const TCHAR*, float>& Share : {
        TPair<const TCHAR*, float>(TEXT("PrimaryShare"), InStyle.PrimaryShare),
        TPair<const TCHAR*, float>(TEXT("SecondaryShare"), InStyle.SecondaryShare),
        TPair<const TCHAR*, float>(TEXT("WhiteShare"), InStyle.WhiteShare),
        TPair<const TCHAR*, float>(TEXT("AwayPocketShare"), InStyle.AwayPocketShare),
        TPair<const TCHAR*, float>(TEXT("AlwaysStandShare"), InStyle.AlwaysStandShare),
        TPair<const TCHAR*, float>(TEXT("StandExcitementMin"), InStyle.StandExcitementMin),
        TPair<const TCHAR*, float>(TEXT("StandExcitementMax"), InStyle.StandExcitementMax) })
    {
        if (!(Share.Value >= 0.f && Share.Value <= 1.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be from 0 to 1"), Share.Key));
        }
    }
    if (InStyle.PrimaryShare + InStyle.SecondaryShare + InStyle.WhiteShare > 1.0001f)
    {
        Problems.Add(TEXT("PrimaryShare, SecondaryShare and WhiteShare must add up to 1 or less"));
    }
    if (InStyle.StandExcitementMin > InStyle.StandExcitementMax)
    {
        Problems.Add(TEXT("StandExcitementMin must not be above StandExcitementMax"));
    }
    if (!(InStyle.SizeVariation >= 0.f && InStyle.SizeVariation <= 0.5f) || !(InStyle.StandHysteresis >= 0.f && InStyle.StandHysteresis <= 0.5f))
    {
        Problems.Add(TEXT("SizeVariation and StandHysteresis must be from 0 to 0.5"));
    }
    if (!(InStyle.YawJitterDegrees >= 0.f && InStyle.YawJitterDegrees <= 90.f))
    {
        Problems.Add(TEXT("YawJitterDegrees must be from 0 to 90"));
    }
    if (!(InStyle.StandRiseCm >= 0.f) || !(InStyle.StandForwardCm >= 0.f))
    {
        Problems.Add(TEXT("StandRiseCm and StandForwardCm must be 0 or more"));
    }
    if (InStyle.MaxStandChangesPerUpdate < 1)
    {
        Problems.Add(TEXT("MaxStandChangesPerUpdate must be 1 or more"));
    }
    return Problems;
}

void UPSCrowdRenderComponent::BeginPlay()
{
    Super::BeginPlay();
    const float Rate = PSPlatformTiers::GetActiveTier().CrowdUpdateHz;
    SetComponentTickInterval(Rate > 0.f ? 1.f / Rate : 0.f);
}

void UPSCrowdRenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    PS_PERF_SCOPE(Crowd);
    UpdateFromMatch();
}

UInstancedStaticMeshComponent* UPSCrowdRenderComponent::GetShirtMesh(int32 Shirt) const
{
    return ShirtMeshes.IsValidIndex(Shirt) ? ShirtMeshes[Shirt] : nullptr;
}

UInstancedStaticMeshComponent* UPSCrowdRenderComponent::GetHeadMesh(int32 Skin) const
{
    return HeadMeshes.IsValidIndex(Skin) ? HeadMeshes[Skin] : nullptr;
}

FLinearColor UPSCrowdRenderComponent::GetShirtColor(int32 Shirt) const
{
    return ShirtColors.IsValidIndex(Shirt) ? ShirtColors[Shirt] : FLinearColor::Black;
}

int32 UPSCrowdRenderComponent::GetNumStanding() const
{
    int32 Standing = 0;
    for (const FPSCrowdFan& Fan : Fans)
    {
        Standing += Fan.bStanding ? 1 : 0;
    }
    return Standing;
}

FTransform UPSCrowdRenderComponent::ComputeBodyTransform(const FPSStadiumSeat& Seat, const FPSCrowdFan& Fan, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, EPSCrowdDetail InDetail, bool bStanding)
{
    const FVector Forward = FRotator(0.f, Seat.Yaw, 0.f).Vector();
    const float Scale = Fan.Scale;
    const float Width = InStyle.TorsoWidthCm * Scale;
    const float Height = InStyle.TorsoHeightCm * Scale;
    // Seated, he sits back on the pan; standing, he is up in front of his seat.
    const FVector Hips = bStanding
        ? Seat.Location + FVector(0.f, 0.f, SeatHeightCm + InStyle.StandRiseCm) + Forward * InStyle.StandForwardCm
        : Seat.Location + FVector(0.f, 0.f, SeatHeightCm) - Forward * (InStyle.TorsoDepthCm * 0.3f);
    FVector Size(InStyle.TorsoDepthCm * Scale, Width, Height);
    if (InDetail == EPSCrowdDetail::Cards)
    {
        Size = FVector(InStyle.CardDepthCm, Width, Height + InStyle.HeadSizeCm * Scale);
    }
    const float MeshSize = FMath::Max(InStyle.MeshSizeCm, 1.f);
    return FTransform(FRotator(0.f, Seat.Yaw + Fan.YawOffset, 0.f), Hips + FVector(0.f, 0.f, Size.Z * 0.5f), Size / MeshSize);
}

FTransform UPSCrowdRenderComponent::ComputeHeadTransform(const FPSStadiumSeat& Seat, const FPSCrowdFan& Fan, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, bool bStanding)
{
    const FTransform Body = ComputeBodyTransform(Seat, Fan, SeatHeightCm, InStyle, EPSCrowdDetail::Figures, bStanding);
    const float MeshSize = FMath::Max(InStyle.MeshSizeCm, 1.f);
    const float Head = InStyle.HeadSizeCm * Fan.Scale;
    const float TorsoTop = Body.GetLocation().Z + Body.GetScale3D().Z * MeshSize * 0.5f;
    const FVector Centre(Body.GetLocation().X, Body.GetLocation().Y, TorsoTop + 3.f + Head * 0.5f);
    return FTransform(Body.GetRotation(), Centre, FVector(Head / MeshSize));
}

void UPSCrowdRenderComponent::PrepareMeshes(TArray<UInstancedStaticMeshComponent*>& Meshes, TArray<UMaterialInstanceDynamic*>& Looks, int32 Count, const TCHAR* BaseName)
{
    AActor* OwningActor = GetOwner();
    while (Meshes.Num() > Count)
    {
        UInstancedStaticMeshComponent* Extra = Meshes.Pop();
        if (Extra)
        {
            Extra->ClearInstances();
            Extra->DestroyComponent();
        }
    }
    while (OwningActor && Meshes.Num() < Count)
    {
        UInstancedStaticMeshComponent* Mesh = NewObject<UInstancedStaticMeshComponent>(OwningActor,
            MakeUniqueObjectName(OwningActor, UInstancedStaticMeshComponent::StaticClass(), FName(BaseName)));
        // The crowd only draws: it neither collides nor shadows (tens of thousands of movers).
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        if (USceneComponent* AttachTo = OwningActor->GetRootComponent())
        {
            Mesh->SetupAttachment(AttachTo);
        }
        Mesh->RegisterComponent();
        OwningActor->AddInstanceComponent(Mesh);
        Meshes.Add(Mesh);
    }
    Looks.Reset();
}

bool UPSCrowdRenderComponent::PopulateFromData(const TArray<FPSStadiumSeat>& Seats, float SeatHeightCm)
{
    const FPSPlatformTier& Tier = PSPlatformTiers::GetActiveTier();
    UWorld* World = GetWorld();
    UPSCrowdExcitementSubsystem* Excitement = World ? World->GetSubsystem<UPSCrowdExcitementSubsystem>() : nullptr;
    const float Share = Excitement ? Excitement->GetHomeShare() : 1.f;
    if (!Populate(Seats, SeatHeightCm, LoadStyle(GetDefaultStylePath()), Tier.CrowdDensity, Tier.CrowdDetail, Share))
    {
        return false;
    }
    SetTeamColors(Excitement ? Excitement->GetHomeTeamId() : NAME_None, Excitement ? Excitement->GetAwayTeamId() : NAME_None);
    return true;
}

bool UPSCrowdRenderComponent::Populate(const TArray<FPSStadiumSeat>& Seats, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, float InDensity, EPSCrowdDetail InDetail, float InHomeShare)
{
    using namespace PSCrowdRenderPrivate;

    Style = InStyle;
    Detail = InDetail;
    Density = FMath::Clamp(InDensity, 0.f, 1.f);
    HomeShare = FMath::Clamp(InHomeShare, 0.f, 1.f);
    SeatHeight = SeatHeightCm;
    CrowdSeats = Seats;
    Fans.Reset();
    NextFanToCheck = 0;
    LastExcitement = 0.f;

    UStaticMesh* Body = Style.BodyMeshPath.IsEmpty() ? nullptr : Cast<UStaticMesh>(FSoftObjectPath(Style.BodyMeshPath).TryLoad());
    UMaterialInterface* Material = Style.MaterialPath.IsEmpty() ? nullptr : Cast<UMaterialInterface>(FSoftObjectPath(Style.MaterialPath).TryLoad());
    if (!Body || !Material)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCrowdRenderComponent: Could not load the fan mesh '%s' or material '%s'."), *Style.BodyMeshPath, *Style.MaterialPath);
        PrepareMeshes(ShirtMeshes, ShirtLooks, 0, TEXT("CrowdShirts"));
        PrepareMeshes(HeadMeshes, HeadLooks, 0, TEXT("CrowdHeads"));
        return false;
    }

    // The colour groups: a mesh per shirt colour, and per skin tone for the figures' heads.
    const int32 NumNeutrals = Style.NeutralColors.Num();
    const int32 NumShirts = PSCrowdShirt::FirstNeutral + NumNeutrals;
    const int32 NumSkins = FMath::Max(1, Style.SkinTones.Num());
    const bool bDrawn = Detail != EPSCrowdDetail::None;
    const bool bFigures = Detail == EPSCrowdDetail::Figures;
    PrepareMeshes(ShirtMeshes, ShirtLooks, bDrawn ? NumShirts : 0, TEXT("CrowdShirts"));
    PrepareMeshes(HeadMeshes, HeadLooks, bFigures ? NumSkins : 0, TEXT("CrowdHeads"));

    ShirtColors.Init(FLinearColor::White, NumShirts);
    ShirtColors[PSCrowdShirt::HomePrimary] = ParseOr(Style.DefaultHomePrimary, FLinearColor::Red);
    ShirtColors[PSCrowdShirt::HomeSecondary] = ParseOr(Style.DefaultHomeSecondary, FLinearColor::Yellow);
    ShirtColors[PSCrowdShirt::AwayPrimary] = ParseOr(Style.DefaultAwayPrimary, FLinearColor::Blue);
    ShirtColors[PSCrowdShirt::AwaySecondary] = ParseOr(Style.DefaultAwaySecondary, FLinearColor::White);
    ShirtColors[PSCrowdShirt::White] = ParseOr(Style.WhiteColor, FLinearColor::White);
    for (int32 Neutral = 0; Neutral < NumNeutrals; ++Neutral)
    {
        ShirtColors[PSCrowdShirt::FirstNeutral + Neutral] = ParseOr(Style.NeutralColors[Neutral], FLinearColor::Gray);
    }
    auto Dress = [this, Body, Material](UInstancedStaticMeshComponent* Mesh, TArray<UMaterialInstanceDynamic*>& Looks, const FLinearColor& Color)
    {
        UMaterialInstanceDynamic* Look = UMaterialInstanceDynamic::Create(Material, this);
        Look->SetVectorParameterValue(Style.ColorParameter, Color);
        Looks.Add(Look);
        Mesh->ClearInstances();
        Mesh->SetStaticMesh(Body);
        Mesh->SetMaterial(0, Look);
    };
    for (int32 Shirt = 0; Shirt < ShirtMeshes.Num(); ++Shirt)
    {
        Dress(ShirtMeshes[Shirt], ShirtLooks, ShirtColors[Shirt]);
    }
    for (int32 Skin = 0; Skin < HeadMeshes.Num(); ++Skin)
    {
        Dress(HeadMeshes[Skin], HeadLooks, ParseOr(Style.SkinTones.IsValidIndex(Skin) ? Style.SkinTones[Skin] : FString(), FLinearColor(0.6f, 0.45f, 0.35f)));
    }
    bColored = false;
    if (!bDrawn || Seats.Num() == 0)
    {
        return true;
    }

    FRandomStream Stream(Style.Seed);

    // Who came: Density of the seats, the same seats every time.
    TArray<int32> Occupied;
    Occupied.Reserve(Seats.Num());
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        if (Stream.FRand() < Density)
        {
            Occupied.Add(Index);
        }
    }

    // The away pockets: whole sections, nearest the pocket's spot first, until they hold
    // AwayPocketShare of the away fans. The rest of the away fans are scattered.
    const int32 AwayFans = RoundToCount((1.0 - HomeShare) * Occupied.Num());
    const int32 PocketTarget = RoundToCount(AwayFans * FMath::Clamp<double>(Style.AwayPocketShare, 0.0, 1.0));
    TMap<int32, int32> SectionFans;
    TMap<int32, FVector> SectionSums;
    for (const int32 Index : Occupied)
    {
        const int32 Key = PocketKey(Seats[Index]);
        if (int32* Count = SectionFans.Find(Key))
        {
            ++*Count;
            SectionSums[Key] += Seats[Index].Location;
        }
        else
        {
            SectionFans.Add(Key, 1);
            SectionSums.Add(Key, Seats[Index].Location);
        }
    }
    const FVector Anchor = PSField::YardLineToWorld(Style.AwayPocketYardLine, Style.AwayPocketLateralYards);
    TArray<int32> Sections;
    SectionFans.GetKeys(Sections);
    Sections.Sort([&SectionFans, &SectionSums, &Anchor](int32 A, int32 B)
    {
        const double DistA = FVector::DistSquared2D(SectionSums[A] / SectionFans[A], Anchor);
        const double DistB = FVector::DistSquared2D(SectionSums[B] / SectionFans[B], Anchor);
        return DistA != DistB ? DistA < DistB : A < B;
    });
    TSet<int32> Pockets;
    int32 InPockets = 0;
    for (const int32 Key : Sections)
    {
        if (InPockets >= PocketTarget)
        {
            break;
        }
        Pockets.Add(Key);
        InPockets += SectionFans[Key];
    }
    const int32 Scattered = FMath::Max(0, AwayFans - InPockets);
    const int32 OutsidePockets = FMath::Max(1, Occupied.Num() - InPockets);
    const float ScatterChance = static_cast<float>(Scattered) / OutsidePockets;

    // Each fan: his team, his shirt, his skin, his size and when he stands.
    const int32 NumTeamShirts = PSCrowdShirt::White + 1;
    Fans.Reserve(Occupied.Num());
    for (const int32 Index : Occupied)
    {
        FPSCrowdFan& Fan = Fans.AddDefaulted_GetRef();
        Fan.Seat = Index;
        const float TeamRoll = Stream.FRand();
        Fan.bHome = !(Pockets.Contains(PocketKey(Seats[Index])) || TeamRoll < ScatterChance);
        const float ShirtRoll = Stream.FRand();
        const int32 NeutralPick = Stream.RandHelper(FMath::Max(NumNeutrals, 1));
        if (ShirtRoll < Style.PrimaryShare)
        {
            Fan.Shirt = Fan.bHome ? PSCrowdShirt::HomePrimary : PSCrowdShirt::AwayPrimary;
        }
        else if (ShirtRoll < Style.PrimaryShare + Style.SecondaryShare)
        {
            Fan.Shirt = Fan.bHome ? PSCrowdShirt::HomeSecondary : PSCrowdShirt::AwaySecondary;
        }
        else if (ShirtRoll < Style.PrimaryShare + Style.SecondaryShare + Style.WhiteShare || NumNeutrals == 0)
        {
            Fan.Shirt = PSCrowdShirt::White;
        }
        else
        {
            Fan.Shirt = NumTeamShirts + NeutralPick;
        }
        Fan.Skin = Stream.RandHelper(NumSkins);
        Fan.Scale = 1.f + Stream.FRandRange(-Style.SizeVariation, Style.SizeVariation);
        Fan.YawOffset = Stream.FRandRange(-Style.YawJitterDegrees, Style.YawJitterDegrees);
        const float StandRoll = Stream.FRand();
        const float ThresholdRoll = Stream.FRand();
        Fan.StandThreshold = StandRoll < Style.AlwaysStandShare ? -1.f : FMath::Lerp(Style.StandExcitementMin, Style.StandExcitementMax, ThresholdRoll);
        Fan.bStanding = Fan.StandThreshold < 0.f;
    }

    // Their instances, in one batch per mesh.
    TArray<TArray<FTransform>> ShirtTransforms;
    ShirtTransforms.SetNum(NumShirts);
    TArray<TArray<FTransform>> HeadTransforms;
    HeadTransforms.SetNum(NumSkins);
    for (FPSCrowdFan& Fan : Fans)
    {
        const FPSStadiumSeat& Seat = Seats[Fan.Seat];
        Fan.ShirtInstance = ShirtTransforms[Fan.Shirt].Add(ComputeBodyTransform(Seat, Fan, SeatHeight, Style, Detail, Fan.bStanding));
        if (bFigures)
        {
            Fan.HeadInstance = HeadTransforms[Fan.Skin].Add(ComputeHeadTransform(Seat, Fan, SeatHeight, Style, Fan.bStanding));
        }
    }
    for (int32 Shirt = 0; Shirt < ShirtMeshes.Num(); ++Shirt)
    {
        if (ShirtMeshes[Shirt] && ShirtTransforms[Shirt].Num() > 0)
        {
            ShirtMeshes[Shirt]->AddInstances(ShirtTransforms[Shirt], false);
        }
    }
    for (int32 Skin = 0; Skin < HeadMeshes.Num(); ++Skin)
    {
        if (HeadMeshes[Skin] && HeadTransforms[Skin].Num() > 0)
        {
            HeadMeshes[Skin]->AddInstances(HeadTransforms[Skin], false);
        }
    }
    return true;
}

void UPSCrowdRenderComponent::SetTeamColors(FName HomeTeamId, FName AwayTeamId)
{
    using namespace PSCrowdRenderPrivate;

    ColoredHomeTeam = HomeTeamId;
    ColoredAwayTeam = AwayTeamId;
    bColored = true;
    if (ShirtColors.Num() < PSCrowdShirt::FirstNeutral)
    {
        return;
    }
    const FString TeamsPath = UPSUITeamCatalog::GetDefaultTeamsPath();
    FLinearColor Primary;
    FLinearColor Secondary;
    if (HomeTeamId.IsNone() || !UPSCharacterLookComponent::FindTeamColors(TeamsPath, HomeTeamId, Primary, Secondary))
    {
        Primary = ParseOr(Style.DefaultHomePrimary, FLinearColor::Red);
        Secondary = ParseOr(Style.DefaultHomeSecondary, FLinearColor::Yellow);
    }
    ShirtColors[PSCrowdShirt::HomePrimary] = Primary;
    ShirtColors[PSCrowdShirt::HomeSecondary] = Secondary;
    if (AwayTeamId.IsNone() || !UPSCharacterLookComponent::FindTeamColors(TeamsPath, AwayTeamId, Primary, Secondary))
    {
        Primary = ParseOr(Style.DefaultAwayPrimary, FLinearColor::Blue);
        Secondary = ParseOr(Style.DefaultAwaySecondary, FLinearColor::White);
    }
    ShirtColors[PSCrowdShirt::AwayPrimary] = Primary;
    ShirtColors[PSCrowdShirt::AwaySecondary] = Secondary;
    for (int32 Shirt = 0; Shirt < ShirtLooks.Num() && Shirt < ShirtColors.Num(); ++Shirt)
    {
        if (ShirtLooks[Shirt])
        {
            ShirtLooks[Shirt]->SetVectorParameterValue(Style.ColorParameter, ShirtColors[Shirt]);
        }
    }
}

int32 UPSCrowdRenderComponent::ApplyExcitement(float InExcitement)
{
    LastExcitement = InExcitement;
    const int32 Total = Fans.Num();
    if (Total == 0 || Detail == EPSCrowdDetail::None)
    {
        return 0;
    }
    const bool bFigures = Detail == EPSCrowdDetail::Figures;
    const int32 Budget = FMath::Max(1, Style.MaxStandChangesPerUpdate);
    TArray<UInstancedStaticMeshComponent*, TInlineAllocator<16>> Touched;
    int32 Moved = 0;
    int32 Checked = 0;
    for (; Checked < Total && Moved < Budget; ++Checked)
    {
        FPSCrowdFan& Fan = Fans[(NextFanToCheck + Checked) % Total];
        if (Fan.StandThreshold < 0.f)
        {
            continue;
        }
        const bool bStand = Fan.bStanding ? InExcitement >= Fan.StandThreshold - Style.StandHysteresis : InExcitement >= Fan.StandThreshold;
        if (bStand == Fan.bStanding)
        {
            continue;
        }
        Fan.bStanding = bStand;
        const FPSStadiumSeat& Seat = CrowdSeats[Fan.Seat];
        if (UInstancedStaticMeshComponent* Shirt = GetShirtMesh(Fan.Shirt))
        {
            Shirt->UpdateInstanceTransform(Fan.ShirtInstance, ComputeBodyTransform(Seat, Fan, SeatHeight, Style, Detail, bStand), false, false, true);
            Touched.AddUnique(Shirt);
        }
        UInstancedStaticMeshComponent* Head = bFigures ? GetHeadMesh(Fan.Skin) : nullptr;
        if (Head && Fan.HeadInstance != INDEX_NONE)
        {
            Head->UpdateInstanceTransform(Fan.HeadInstance, ComputeHeadTransform(Seat, Fan, SeatHeight, Style, bStand), false, false, true);
            Touched.AddUnique(Head);
        }
        ++Moved;
    }
    NextFanToCheck = (NextFanToCheck + Checked) % Total;
    for (UInstancedStaticMeshComponent* Mesh : Touched)
    {
        Mesh->MarkRenderStateDirty();
    }
    return Moved;
}

void UPSCrowdRenderComponent::UpdateFromMatch()
{
    UWorld* World = GetWorld();
    UPSCrowdExcitementSubsystem* Excitement = World ? World->GetSubsystem<UPSCrowdExcitementSubsystem>() : nullptr;
    if (!Excitement || CrowdSeats.Num() == 0)
    {
        return;
    }
    // The match's split of the stadium decides who sits where: a new split reseats the crowd.
    const float Share = Excitement->GetHomeShare();
    if (FMath::Abs(Share - HomeShare) > 0.02f)
    {
        const TArray<FPSStadiumSeat> Seats = CrowdSeats;
        Populate(Seats, SeatHeight, Style, Density, Detail, Share);
    }
    if (!bColored || Excitement->GetHomeTeamId() != ColoredHomeTeam || Excitement->GetAwayTeamId() != ColoredAwayTeam)
    {
        SetTeamColors(Excitement->GetHomeTeamId(), Excitement->GetAwayTeamId());
    }
    ApplyExcitement(Excitement->GetExcitement());
}
