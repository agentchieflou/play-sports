// PSStadiumSet.cpp - Epic 147.1: the goal posts, team benches and stands, built at runtime from data
#include "PSStadiumSet.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace PSStadiumSetPrivate
{
    template <typename AssetType>
    AssetType* LoadAsset(const FString& Path, const TCHAR* What)
    {
        AssetType* Asset = Path.IsEmpty() ? nullptr : Cast<AssetType>(FSoftObjectPath(Path).TryLoad());
        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: Could not load the %s '%s'."), What, *Path);
        }
        return Asset;
    }

    FPSStadiumPiece MakePiece(EPSStadiumPieceKind Kind, bool bCylinder, const FVector& Center, const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
    {
        FPSStadiumPiece Piece;
        Piece.Kind = Kind;
        Piece.bCylinder = bCylinder;
        Piece.Center = Center;
        Piece.Size = Size;
        Piece.Rotation = Rotation;
        return Piece;
    }
}

APSStadiumSet::APSStadiumSet()
{
    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    GoalPosts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GoalPosts"));
    Benches = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Benches"));
    Stands = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Stands"));
    StandsAlt = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("StandsAlt"));
    for (UInstancedStaticMeshComponent* Pieces : { GoalPosts, Benches, Stands, StandsAlt })
    {
        // The set only draws: the field's ground is the one surface that collides.
        Pieces->SetupAttachment(RootComponent);
        Pieces->SetMobility(EComponentMobility::Movable);
        Pieces->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Pieces->SetGenerateOverlapEvents(false);
        Pieces->SetCanEverAffectNavigation(false);
        Pieces->SetCastShadow(false);
    }
}

void APSStadiumSet::BeginPlay()
{
    Super::BeginPlay();
    if (!bBuilt)
    {
        BuildFromData();
    }
}

UInstancedStaticMeshComponent* APSStadiumSet::GetPieces(EPSStadiumPieceKind Kind) const
{
    switch (Kind)
    {
    case EPSStadiumPieceKind::GoalPost:
        return GoalPosts;
    case EPSStadiumPieceKind::Bench:
        return Benches;
    case EPSStadiumPieceKind::StandAlt:
        return StandsAlt;
    default:
        return Stands;
    }
}

FString APSStadiumSet::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/stadium_set.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSStadiumSetStyle APSStadiumSet::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSStadiumSetStyle Loaded;
    if (!Ingestion->LoadStadiumSetStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: Could not load %s; using the default set."), *Path);
        return FPSStadiumSetStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSStadiumSet: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSStadiumSetStyle();
}

TArray<FString> APSStadiumSet::ValidateStyle(const FPSStadiumSetStyle& Style)
{
    TArray<FString> Problems;
    for (const TPair<const TCHAR*, const FString*>& Path : {
        TPair<const TCHAR*, const FString*>(TEXT("BoxMeshPath"), &Style.BoxMeshPath),
        TPair<const TCHAR*, const FString*>(TEXT("CylinderMeshPath"), &Style.CylinderMeshPath),
        TPair<const TCHAR*, const FString*>(TEXT("MaterialPath"), &Style.MaterialPath) })
    {
        if (Path.Value->IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("%s must name an asset"), Path.Key));
        }
    }
    if (Style.ColorParameter.IsNone())
    {
        Problems.Add(TEXT("ColorParameter must name the material's color parameter"));
    }
    for (const TPair<const TCHAR*, const FString*>& Color : {
        TPair<const TCHAR*, const FString*>(TEXT("GoalPostColor"), &Style.GoalPostColor),
        TPair<const TCHAR*, const FString*>(TEXT("BenchColor"), &Style.BenchColor),
        TPair<const TCHAR*, const FString*>(TEXT("StandColor"), &Style.StandColor),
        TPair<const TCHAR*, const FString*>(TEXT("StandAltColor"), &Style.StandAltColor) })
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
        }
    }
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("MeshSizeCm"), Style.MeshSizeCm),
        TPair<const TCHAR*, float>(TEXT("CrossbarHeightYards"), Style.CrossbarHeightYards),
        TPair<const TCHAR*, float>(TEXT("CrossbarWidthYards"), Style.CrossbarWidthYards),
        TPair<const TCHAR*, float>(TEXT("UprightHeightYards"), Style.UprightHeightYards),
        TPair<const TCHAR*, float>(TEXT("PostDiameterYards"), Style.PostDiameterYards),
        TPair<const TCHAR*, float>(TEXT("BasePostDiameterYards"), Style.BasePostDiameterYards),
        TPair<const TCHAR*, float>(TEXT("BaseSetbackYards"), Style.BaseSetbackYards),
        TPair<const TCHAR*, float>(TEXT("BenchDepthYards"), Style.BenchDepthYards),
        TPair<const TCHAR*, float>(TEXT("BenchHeightYards"), Style.BenchHeightYards),
        TPair<const TCHAR*, float>(TEXT("StandTierDepthYards"), Style.StandTierDepthYards),
        TPair<const TCHAR*, float>(TEXT("StandTierRiseYards"), Style.StandTierRiseYards) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    if (!(Style.BenchDistanceYards >= 0.f) || !(Style.StandGapYards >= 0.f))
    {
        Problems.Add(TEXT("BenchDistanceYards and StandGapYards must be 0 or more"));
    }
    if (!(Style.BenchFromYardLine >= 0.f && Style.BenchFromYardLine < Style.BenchToYardLine))
    {
        Problems.Add(TEXT("BenchFromYardLine must be 0 or more and before BenchToYardLine"));
    }
    if (Style.StandTiers < 0)
    {
        Problems.Add(TEXT("StandTiers must be 0 or more"));
    }
    return Problems;
}

TArray<FPSStadiumPiece> APSStadiumSet::ComputePieces(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style)
{
    using namespace PSStadiumSetPrivate;

    TArray<FPSStadiumPiece> Pieces;
    const float Cm = Dimensions.CentimetresPerYard;
    const float FieldLength = Dimensions.FieldLengthYards * Cm;
    const float EndZoneDepth = Dimensions.EndZoneDepthYards * Cm;
    const float SidelineY = Dimensions.FieldWidthYards * Cm * 0.5f;
    const float OutDepth = Dimensions.OutOfBoundsDepthYards * Cm;

    // The goal posts, on each end line: the near one behind X = 0, the far one past the far goal.
    const float CrossbarZ = Style.CrossbarHeightYards * Cm;
    const float CrossbarWidth = Style.CrossbarWidthYards * Cm;
    const float UprightLength = Style.UprightHeightYards * Cm;
    const float Post = Style.PostDiameterYards * Cm;
    const float BasePost = Style.BasePostDiameterYards * Cm;
    const float Setback = Style.BaseSetbackYards * Cm;
    for (const float Outward : { -1.f, 1.f })
    {
        const float EndLineX = Outward < 0.f ? -EndZoneDepth : FieldLength + EndZoneDepth;
        const float BaseX = EndLineX + Outward * Setback;
        Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(BaseX, 0.f, CrossbarZ * 0.5f), FVector(BasePost, BasePost, CrossbarZ)));
        // The neck reaches forward from the base post's top to the crossbar over the end line.
        Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector((BaseX + EndLineX) * 0.5f, 0.f, CrossbarZ),
            FVector(BasePost, BasePost, Setback), FRotator(90.f, 0.f, 0.f)));
        Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(EndLineX, 0.f, CrossbarZ),
            FVector(Post, Post, CrossbarWidth), FRotator(0.f, 0.f, 90.f)));
        for (const float Side : { -1.f, 1.f })
        {
            Pieces.Add(MakePiece(EPSStadiumPieceKind::GoalPost, true, FVector(EndLineX, Side * CrossbarWidth * 0.5f, CrossbarZ + UprightLength * 0.5f),
                FVector(Post, Post, UprightLength)));
        }
    }

    // A team bench past each sideline, between the yard lines the data names.
    const float BenchFrom = Style.BenchFromYardLine * Cm;
    const float BenchTo = Style.BenchToYardLine * Cm;
    const float BenchDepth = Style.BenchDepthYards * Cm;
    const float BenchHeight = Style.BenchHeightYards * Cm;
    for (const float Side : { -1.f, 1.f })
    {
        Pieces.Add(MakePiece(EPSStadiumPieceKind::Bench, false,
            FVector((BenchFrom + BenchTo) * 0.5f, Side * (SidelineY + Style.BenchDistanceYards * Cm + BenchDepth * 0.5f), BenchHeight * 0.5f),
            FVector(BenchTo - BenchFrom, BenchDepth, BenchHeight)));
    }

    // The stands: tiers rising from the ground's edge, along each sideline and behind each end line.
    const float GroundMinX = -EndZoneDepth - OutDepth;
    const float GroundMaxX = FieldLength + EndZoneDepth + OutDepth;
    const float GroundHalfY = SidelineY + OutDepth;
    const float Gap = Style.StandGapYards * Cm;
    const float TierDepth = Style.StandTierDepthYards * Cm;
    const float TierRise = Style.StandTierRiseYards * Cm;
    for (int32 Tier = 0; Tier < Style.StandTiers; ++Tier)
    {
        const EPSStadiumPieceKind Kind = Tier % 2 == 0 ? EPSStadiumPieceKind::Stand : EPSStadiumPieceKind::StandAlt;
        const float Height = (Tier + 1) * TierRise;
        const float Out = Gap + (Tier + 0.5f) * TierDepth;
        for (const float Side : { -1.f, 1.f })
        {
            Pieces.Add(MakePiece(Kind, false, FVector((GroundMinX + GroundMaxX) * 0.5f, Side * (GroundHalfY + Out), Height * 0.5f),
                FVector(GroundMaxX - GroundMinX, TierDepth, Height)));
        }
        Pieces.Add(MakePiece(Kind, false, FVector(GroundMinX - Out, 0.f, Height * 0.5f), FVector(TierDepth, 2.f * GroundHalfY, Height)));
        Pieces.Add(MakePiece(Kind, false, FVector(GroundMaxX + Out, 0.f, Height * 0.5f), FVector(TierDepth, 2.f * GroundHalfY, Height)));
    }
    return Pieces;
}

bool APSStadiumSet::BuildFromData()
{
    return Build(PSField::GetDimensions(), LoadStyle(GetDefaultStylePath()));
}

bool APSStadiumSet::Build(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style)
{
    using namespace PSStadiumSetPrivate;

    UStaticMesh* BoxMesh = LoadAsset<UStaticMesh>(Style.BoxMeshPath, TEXT("box mesh"));
    UStaticMesh* CylinderMesh = LoadAsset<UStaticMesh>(Style.CylinderMeshPath, TEXT("cylinder mesh"));
    UMaterialInterface* Material = LoadAsset<UMaterialInterface>(Style.MaterialPath, TEXT("material"));
    if (!BoxMesh || !CylinderMesh || !Material)
    {
        return false;
    }

    // The set is the world's frame, wherever this actor was put.
    SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    SetActorScale3D(FVector::OneVector);

    Materials.Reset();
    auto Prepare = [this, Material, &Style](UInstancedStaticMeshComponent* Pieces, UStaticMesh* Mesh, const FString& Hex)
    {
        FLinearColor Color = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(Style.ColorParameter, Color);
        Materials.Add(Instance);
        Pieces->ClearInstances();
        Pieces->SetStaticMesh(Mesh);
        Pieces->SetMaterial(0, Instance);
        Pieces->SetCastShadow(Style.bCastShadows);
    };
    Prepare(GoalPosts, CylinderMesh, Style.GoalPostColor);
    Prepare(Benches, BoxMesh, Style.BenchColor);
    Prepare(Stands, BoxMesh, Style.StandColor);
    Prepare(StandsAlt, BoxMesh, Style.StandAltColor);

    const float MeshSize = FMath::Max(Style.MeshSizeCm, 1.f);
    for (const FPSStadiumPiece& Piece : ComputePieces(Dimensions, Style))
    {
        GetPieces(Piece.Kind)->AddInstance(FTransform(Piece.Rotation, Piece.Center, Piece.Size / MeshSize));
    }
    bBuilt = true;
    return true;
}
