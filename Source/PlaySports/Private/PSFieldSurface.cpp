// PSFieldSurface.cpp - Epic 146.3: the field you see, built at runtime from data
#include "PSFieldSurface.h"
#include "PSDataIngestion.h"
#include "PSUITeamCatalog.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "UObject/SoftObjectPath.h"

namespace PSFieldSurfacePrivate
{
    /** A flat piece that only draws: no collision, no shadow, no navigation. */
    void MakeDecorative(UPrimitiveComponent* Piece)
    {
        Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Piece->SetGenerateOverlapEvents(false);
        Piece->SetCanEverAffectNavigation(false);
        Piece->SetCastShadow(false);
    }

    template <typename AssetType>
    AssetType* LoadAsset(const FString& Path, const TCHAR* What)
    {
        AssetType* Asset = Path.IsEmpty() ? nullptr : Cast<AssetType>(FSoftObjectPath(Path).TryLoad());
        if (!Asset)
        {
            UE_LOG(LogTemp, Warning, TEXT("APSFieldSurface: Could not load the %s '%s'."), What, *Path);
        }
        return Asset;
    }

    bool IsWholeMultiple(float Value, float Step)
    {
        const float Remainder = FMath::Fmod(Value, Step);
        return Remainder < KINDA_SMALL_NUMBER || Step - Remainder < KINDA_SMALL_NUMBER;
    }
}

APSFieldSurface::APSFieldSurface()
{
    using namespace PSFieldSurfacePrivate;

    PrimaryActorTick.bCanEverTick = false;

    USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Root->SetMobility(EComponentMobility::Movable);
    RootComponent = Root;

    // The ground blocks like any world geometry: the ball comes down on it.
    Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
    Ground->SetupAttachment(RootComponent);
    Ground->SetMobility(EComponentMobility::Movable);
    Ground->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
    Ground->SetCastShadow(false);

    FieldPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FieldPlane"));
    NearEndZone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NearEndZone"));
    FarEndZone = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FarEndZone"));
    Lines = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Lines"));
    for (UStaticMeshComponent* Piece : { FieldPlane, NearEndZone, FarEndZone, static_cast<UStaticMeshComponent*>(Lines) })
    {
        Piece->SetupAttachment(RootComponent);
        Piece->SetMobility(EComponentMobility::Movable);
        MakeDecorative(Piece);
    }
}

void APSFieldSurface::BeginPlay()
{
    Super::BeginPlay();
    if (!bBuilt)
    {
        BuildFromData();
    }
}

FString APSFieldSurface::GetDefaultStylePath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/field_markings.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

FPSFieldMarkingsStyle APSFieldSurface::LoadStyle(const FString& Path)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>();
    FPSFieldMarkingsStyle Loaded;
    if (!Ingestion->LoadFieldMarkingsStyleFromJson(Path, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("APSFieldSurface: Could not load %s; using the default look."), *Path);
        return FPSFieldMarkingsStyle();
    }
    const TArray<FString> Problems = ValidateStyle(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("APSFieldSurface: %s: %s"), *Path, *Problem);
    }
    return Problems.Num() == 0 ? Loaded : FPSFieldMarkingsStyle();
}

TArray<FString> APSFieldSurface::ValidateStyle(const FPSFieldMarkingsStyle& Style)
{
    TArray<FString> Problems;
    for (const TPair<const TCHAR*, const FString*>& Path : {
        TPair<const TCHAR*, const FString*>(TEXT("GroundMeshPath"), &Style.GroundMeshPath),
        TPair<const TCHAR*, const FString*>(TEXT("PlaneMeshPath"), &Style.PlaneMeshPath),
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
        TPair<const TCHAR*, const FString*>(TEXT("FieldColor"), &Style.FieldColor),
        TPair<const TCHAR*, const FString*>(TEXT("SurroundColor"), &Style.SurroundColor),
        TPair<const TCHAR*, const FString*>(TEXT("NearEndZoneColor"), &Style.NearEndZoneColor),
        TPair<const TCHAR*, const FString*>(TEXT("FarEndZoneColor"), &Style.FarEndZoneColor),
        TPair<const TCHAR*, const FString*>(TEXT("LineColor"), &Style.LineColor) })
    {
        FLinearColor Parsed;
        if (!UPSUITeamCatalog::ParseHexColor(*Color.Value, Parsed))
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not #RRGGBB"), Color.Key, **Color.Value));
        }
    }
    for (const TPair<const TCHAR*, float>& Positive : {
        TPair<const TCHAR*, float>(TEXT("MeshSizeCm"), Style.MeshSizeCm),
        TPair<const TCHAR*, float>(TEXT("GroundThicknessCm"), Style.GroundThicknessCm),
        TPair<const TCHAR*, float>(TEXT("LineWidthYards"), Style.LineWidthYards),
        TPair<const TCHAR*, float>(TEXT("YardLineSpacingYards"), Style.YardLineSpacingYards),
        TPair<const TCHAR*, float>(TEXT("HashSpacingYards"), Style.HashSpacingYards),
        TPair<const TCHAR*, float>(TEXT("HashLengthYards"), Style.HashLengthYards) })
    {
        if (!(Positive.Value > 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be above 0"), Positive.Key));
        }
    }
    if (!(Style.LayerLiftCm >= 0.f))
    {
        Problems.Add(TEXT("LayerLiftCm must be 0 or more"));
    }
    if (!(Style.HashOffsetYards >= 0.f))
    {
        Problems.Add(TEXT("HashOffsetYards must be 0 or more"));
    }
    return Problems;
}

TArray<FPSFieldMark> APSFieldSurface::ComputeMarks(const FPSFieldDimensions& Dimensions, const FPSFieldMarkingsStyle& Style)
{
    using namespace PSFieldSurfacePrivate;

    TArray<FPSFieldMark> Marks;
    auto AddMark = [&Marks](EPSFieldMarkKind Kind, float CenterX, float CenterY, float SizeX, float SizeY)
    {
        FPSFieldMark& Mark = Marks.AddDefaulted_GetRef();
        Mark.Kind = Kind;
        Mark.Center = FVector2D(CenterX, CenterY);
        Mark.Size = FVector2D(SizeX, SizeY);
    };

    // The field's one frame (PSField): the near goal line at X = 0, the far one at the field's
    // length, the end lines an end zone's depth beyond them, the sidelines half the width out.
    const float Cm = Dimensions.CentimetresPerYard;
    const float FieldLength = Dimensions.FieldLengthYards * Cm;
    const float EndZoneDepth = Dimensions.EndZoneDepthYards * Cm;
    const float Width = Dimensions.FieldWidthYards * Cm;
    const float OutDepth = Dimensions.OutOfBoundsDepthYards * Cm;
    const float NearEndLineX = -EndZoneDepth;
    const float FarEndLineX = FieldLength + EndZoneDepth;
    const float SpanX = FarEndLineX - NearEndLineX;
    const float MidX = (NearEndLineX + FarEndLineX) * 0.5f;
    const float LineWidth = Style.LineWidthYards * Cm;

    AddMark(EPSFieldMarkKind::Ground, MidX, 0.f, SpanX + 2.f * OutDepth, Width + 2.f * OutDepth);
    AddMark(EPSFieldMarkKind::Field, MidX, 0.f, SpanX, Width);
    AddMark(EPSFieldMarkKind::NearEndZone, -EndZoneDepth * 0.5f, 0.f, EndZoneDepth, Width);
    AddMark(EPSFieldMarkKind::FarEndZone, FieldLength + EndZoneDepth * 0.5f, 0.f, EndZoneDepth, Width);

    // The boundary: sidelines along the whole field, end lines across it, meeting at the corners.
    for (const float Side : { -1.f, 1.f })
    {
        AddMark(EPSFieldMarkKind::Line, MidX, Side * Width * 0.5f, SpanX + LineWidth, LineWidth);
    }
    AddMark(EPSFieldMarkKind::Line, NearEndLineX, 0.f, LineWidth, Width + LineWidth);
    AddMark(EPSFieldMarkKind::Line, FarEndLineX, 0.f, LineWidth, Width + LineWidth);

    if (Style.YardLineSpacingYards <= 0.f || Style.HashSpacingYards <= 0.f)
    {
        return Marks;
    }

    // Yard lines, sideline to sideline, from the near goal line to the far one.
    const int32 YardLineSteps = FMath::FloorToInt(Dimensions.FieldLengthYards / Style.YardLineSpacingYards + KINDA_SMALL_NUMBER);
    for (int32 Step = 0; Step <= YardLineSteps; ++Step)
    {
        AddMark(EPSFieldMarkKind::Line, Step * Style.YardLineSpacingYards * Cm, 0.f, LineWidth, Width);
    }

    // Hash marks between the goal lines, where no yard line already crosses.
    const float HashLength = Style.HashLengthYards * Cm;
    const float HashY = Style.HashOffsetYards * Cm;
    const int32 HashSteps = FMath::FloorToInt(Dimensions.FieldLengthYards / Style.HashSpacingYards + KINDA_SMALL_NUMBER);
    for (int32 Step = 1; Step < HashSteps; ++Step)
    {
        const float Yard = Step * Style.HashSpacingYards;
        if (IsWholeMultiple(Yard, Style.YardLineSpacingYards))
        {
            continue;
        }
        for (const float Side : { -1.f, 1.f })
        {
            AddMark(EPSFieldMarkKind::Line, Yard * Cm, Side * HashY, LineWidth, HashLength);
        }
    }
    return Marks;
}

bool APSFieldSurface::BuildFromData()
{
    return Build(PSField::GetDimensions(), LoadStyle(GetDefaultStylePath()));
}

bool APSFieldSurface::Build(const FPSFieldDimensions& Dimensions, const FPSFieldMarkingsStyle& Style)
{
    using namespace PSFieldSurfacePrivate;

    UStaticMesh* GroundMesh = LoadAsset<UStaticMesh>(Style.GroundMeshPath, TEXT("ground mesh"));
    UStaticMesh* PlaneMesh = LoadAsset<UStaticMesh>(Style.PlaneMeshPath, TEXT("plane mesh"));
    UMaterialInterface* Material = LoadAsset<UMaterialInterface>(Style.MaterialPath, TEXT("material"));
    if (!GroundMesh || !PlaneMesh || !Material)
    {
        return false;
    }

    // The field is the world's frame, wherever this actor was put.
    SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    SetActorScale3D(FVector::OneVector);

    Materials.Reset();
    auto MakeMaterial = [this, Material, &Style](const FString& Hex)
    {
        FLinearColor Color = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Material, this);
        Instance->SetVectorParameterValue(Style.ColorParameter, Color);
        Materials.Add(Instance);
        return Instance;
    };

    const float MeshSize = FMath::Max(Style.MeshSizeCm, 1.f);
    const float Lift = FMath::Max(Style.LayerLiftCm, 0.f);
    const float Thickness = FMath::Max(Style.GroundThicknessCm, 1.f);
    auto PlacePlane = [PlaneMesh, MeshSize](UStaticMeshComponent* Piece, const FPSFieldMark& Mark, float Height, UMaterialInstanceDynamic* Look)
    {
        Piece->SetStaticMesh(PlaneMesh);
        Piece->SetMaterial(0, Look);
        Piece->SetRelativeLocation(FVector(Mark.Center.X, Mark.Center.Y, Height));
        Piece->SetRelativeScale3D(FVector(Mark.Size.X / MeshSize, Mark.Size.Y / MeshSize, 1.f));
    };

    Lines->ClearInstances();
    Lines->SetStaticMesh(PlaneMesh);
    Lines->SetMaterial(0, MakeMaterial(Style.LineColor));
    for (const FPSFieldMark& Mark : ComputeMarks(Dimensions, Style))
    {
        switch (Mark.Kind)
        {
        case EPSFieldMarkKind::Ground:
            // A box whose top is the ground, Z = 0.
            Ground->SetStaticMesh(GroundMesh);
            Ground->SetMaterial(0, MakeMaterial(Style.SurroundColor));
            Ground->SetRelativeLocation(FVector(Mark.Center.X, Mark.Center.Y, -Thickness * 0.5f));
            Ground->SetRelativeScale3D(FVector(Mark.Size.X / MeshSize, Mark.Size.Y / MeshSize, Thickness / MeshSize));
            break;
        case EPSFieldMarkKind::Field:
            PlacePlane(FieldPlane, Mark, Lift, MakeMaterial(Style.FieldColor));
            break;
        case EPSFieldMarkKind::NearEndZone:
            PlacePlane(NearEndZone, Mark, 2.f * Lift, MakeMaterial(Style.NearEndZoneColor));
            break;
        case EPSFieldMarkKind::FarEndZone:
            PlacePlane(FarEndZone, Mark, 2.f * Lift, MakeMaterial(Style.FarEndZoneColor));
            break;
        case EPSFieldMarkKind::Line:
            Lines->AddInstance(FTransform(FRotator::ZeroRotator, FVector(Mark.Center.X, Mark.Center.Y, 3.f * Lift),
                FVector(Mark.Size.X / MeshSize, Mark.Size.Y / MeshSize, 1.f)));
            break;
        default:
            break;
        }
    }
    bBuilt = true;
    return true;
}
