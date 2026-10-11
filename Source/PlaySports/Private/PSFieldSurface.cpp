// PSFieldSurface.cpp - Epic 146.3: the field you see, built at runtime from data (look: lane V2)
#include "PSFieldSurface.h"
#include "PSDataIngestion.h"
#include "PSPlatformTiers.h"
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

    /** A material the field can do without (the content pipeline's): None when it isn't there,
     *  logged quietly, so a checkout without the field's content draws the flat fallback. */
    UMaterialInterface* LoadOptionalMaterial(const FString& Path)
    {
        UMaterialInterface* Material = Path.IsEmpty() ? nullptr : Cast<UMaterialInterface>(FSoftObjectPath(Path).TryLoad());
        if (!Material)
        {
            UE_LOG(LogTemp, Log, TEXT("APSFieldSurface: '%s' isn't available; drawing that part with the fallback material."), *Path);
        }
        return Material;
    }

    void SetUpPaintLayer(UInstancedStaticMeshComponent* Layer, UStaticMesh* Mesh, UMaterialInterface* Look)
    {
        Layer->ClearInstances();
        Layer->SetStaticMesh(Mesh);
        Layer->SetMaterial(0, Look);
    }
}

FPSFieldMarkingsStyle::FPSFieldMarkingsStyle()
{
    MaterialScalars.Add(TEXT("TileSizeCm"), 150.f);
    MaterialScalars.Add(TEXT("StripeContrast"), 0.08f);
    MaterialScalars.Add(TEXT("StripeViewContrast"), 0.08f);
    MaterialScalars.Add(TEXT("MacroStrength"), 0.12f);
    MaterialScalars.Add(TEXT("DryAmount"), 0.2f);
    MaterialScalars.Add(TEXT("NormalStrength"), 0.8f);
    MaterialScalars.Add(TEXT("PaintTextureStrength"), 0.6f);
    TierLooks.Add(FPSFieldTierLook(TEXT("MobileLow"), TEXT("/Game/Field/Materials/M_TurfLite.M_TurfLite")));
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
    Border = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Border"));
    Numerals = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Numerals"));
    Arrows = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Arrows"));
    for (UStaticMeshComponent* Piece : { FieldPlane, NearEndZone, FarEndZone, static_cast<UStaticMeshComponent*>(Lines),
        static_cast<UStaticMeshComponent*>(Border), static_cast<UStaticMeshComponent*>(Numerals), static_cast<UStaticMeshComponent*>(Arrows) })
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
        TPair<const TCHAR*, const FString*>(TEXT("MaterialPath"), &Style.MaterialPath),
        TPair<const TCHAR*, const FString*>(TEXT("TurfMaterialPath"), &Style.TurfMaterialPath),
        TPair<const TCHAR*, const FString*>(TEXT("PaintMaterialPath"), &Style.PaintMaterialPath) })
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
        TPair<const TCHAR*, const FString*>(TEXT("LineColor"), &Style.LineColor),
        TPair<const TCHAR*, const FString*>(TEXT("BorderColor"), &Style.BorderColor),
        TPair<const TCHAR*, const FString*>(TEXT("NumeralColor"), &Style.NumeralColor) })
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
        TPair<const TCHAR*, float>(TEXT("HashLengthYards"), Style.HashLengthYards),
        TPair<const TCHAR*, float>(TEXT("StripeWidthYards"), Style.StripeWidthYards),
        TPair<const TCHAR*, float>(TEXT("NumeralEveryYards"), Style.NumeralEveryYards),
        TPair<const TCHAR*, float>(TEXT("NumeralHeightYards"), Style.NumeralHeightYards),
        TPair<const TCHAR*, float>(TEXT("NumeralWidthYards"), Style.NumeralWidthYards),
        TPair<const TCHAR*, float>(TEXT("NumeralStrokeYards"), Style.NumeralStrokeYards),
        TPair<const TCHAR*, float>(TEXT("ArrowLengthYards"), Style.ArrowLengthYards),
        TPair<const TCHAR*, float>(TEXT("ArrowBaseYards"), Style.ArrowBaseYards) })
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
    for (const TPair<const TCHAR*, float>& NonNegative : {
        TPair<const TCHAR*, float>(TEXT("BorderWidthYards"), Style.BorderWidthYards),
        TPair<const TCHAR*, float>(TEXT("NumeralBottomFromSidelineYards"), Style.NumeralBottomFromSidelineYards),
        TPair<const TCHAR*, float>(TEXT("NumeralGapYards"), Style.NumeralGapYards),
        TPair<const TCHAR*, float>(TEXT("ArrowGapYards"), Style.ArrowGapYards),
        TPair<const TCHAR*, float>(TEXT("ArrowCenterFromSidelineYards"), Style.ArrowCenterFromSidelineYards) })
    {
        if (!(NonNegative.Value >= 0.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be 0 or more"), NonNegative.Key));
        }
    }
    for (const TPair<const TCHAR*, float>& Fraction : {
        TPair<const TCHAR*, float>(TEXT("PaintCoverage"), Style.PaintCoverage),
        TPair<const TCHAR*, float>(TEXT("EndZonePaintCoverage"), Style.EndZonePaintCoverage) })
    {
        if (!(Fraction.Value >= 0.f && Fraction.Value <= 1.f))
        {
            Problems.Add(FString::Printf(TEXT("%s must be from 0 to 1"), Fraction.Key));
        }
    }
    if (Style.NumeralStrokeYards * 2.f >= Style.NumeralWidthYards || Style.NumeralStrokeYards * 3.f >= Style.NumeralHeightYards)
    {
        Problems.Add(TEXT("NumeralStrokeYards must leave room inside a digit: under half its width and a third of its height"));
    }
    for (const TPair<FName, float>& Scalar : Style.MaterialScalars)
    {
        if (Scalar.Key.IsNone())
        {
            Problems.Add(TEXT("MaterialScalars: every entry must name a material parameter"));
        }
    }
    TSet<FName> TierIds;
    for (const FPSFieldTierLook& Look : Style.TierLooks)
    {
        if (Look.TierId.IsNone() || Look.TurfMaterialPath.IsEmpty())
        {
            Problems.Add(TEXT("TierLooks: every entry needs a TierId and a TurfMaterialPath"));
        }
        else if (TierIds.Contains(Look.TierId))
        {
            Problems.Add(FString::Printf(TEXT("TierLooks: %s is listed twice"), *Look.TierId.ToString()));
        }
        TierIds.Add(Look.TierId);
    }
    return Problems;
}

TArray<FBox2D> APSFieldSurface::GetDigitStrokes(int32 Digit, float Width, float Height, float Stroke)
{
    // A seven-segment block face: a top, a middle and a bottom bar, and two half-height bars down
    // each side. The bars overlap at the corners; the paint is opaque, so overlaps don't show.
    const double W = Width;
    const double H = Height;
    const double S = Stroke;
    const FBox2D Top(FVector2D(0.0, H - S), FVector2D(W, H));
    const FBox2D UpperRight(FVector2D(W - S, H * 0.5), FVector2D(W, H));
    const FBox2D LowerRight(FVector2D(W - S, 0.0), FVector2D(W, H * 0.5));
    const FBox2D Bottom(FVector2D(0.0, 0.0), FVector2D(W, S));
    const FBox2D LowerLeft(FVector2D(0.0, 0.0), FVector2D(S, H * 0.5));
    const FBox2D UpperLeft(FVector2D(0.0, H * 0.5), FVector2D(S, H));
    const FBox2D Middle(FVector2D(0.0, (H - S) * 0.5), FVector2D(W, (H + S) * 0.5));
    // 1: a full-height bar on the right with a flag at its top.
    const FBox2D OneBar(FVector2D(W - S, 0.0), FVector2D(W, H));
    const FBox2D OneFlag(FVector2D(W - 2.0 * S, H - S), FVector2D(W - S, H));

    TArray<FBox2D> Strokes;
    switch (Digit)
    {
    case 0: Strokes = { Top, UpperRight, LowerRight, Bottom, LowerLeft, UpperLeft }; break;
    case 1: Strokes = { OneBar, OneFlag }; break;
    case 2: Strokes = { Top, UpperRight, Middle, LowerLeft, Bottom }; break;
    case 3: Strokes = { Top, UpperRight, Middle, LowerRight, Bottom }; break;
    case 4: Strokes = { UpperLeft, Middle, UpperRight, LowerRight }; break;
    case 5: Strokes = { Top, UpperLeft, Middle, LowerRight, Bottom }; break;
    case 6: Strokes = { Top, UpperLeft, Middle, LowerLeft, LowerRight, Bottom }; break;
    case 7: Strokes = { Top, UpperRight, LowerRight }; break;
    case 8: Strokes = { Top, UpperRight, LowerRight, Bottom, LowerLeft, UpperLeft, Middle }; break;
    case 9: Strokes = { Top, UpperRight, LowerRight, Bottom, UpperLeft, Middle }; break;
    default: break;
    }
    return Strokes;
}

FString APSFieldSurface::ResolveTurfMaterialPath(const FPSFieldMarkingsStyle& Style, FName TierId)
{
    for (const FPSFieldTierLook& Look : Style.TierLooks)
    {
        if (Look.TierId == TierId && !Look.TurfMaterialPath.IsEmpty())
        {
            return Look.TurfMaterialPath;
        }
    }
    return Style.TurfMaterialPath;
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

    // The border: a band outside each sideline (reaching past the corners) and each end line.
    const float BorderWidth = Style.BorderWidthYards * Cm;
    if (BorderWidth > 0.f)
    {
        for (const float Side : { -1.f, 1.f })
        {
            AddMark(EPSFieldMarkKind::Border, MidX, Side * (Width + BorderWidth) * 0.5f, SpanX + 2.f * BorderWidth, BorderWidth);
        }
        AddMark(EPSFieldMarkKind::Border, NearEndLineX - BorderWidth * 0.5f, 0.f, BorderWidth, Width);
        AddMark(EPSFieldMarkKind::Border, FarEndLineX + BorderWidth * 0.5f, 0.f, BorderWidth, Width);
    }

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

    // The numerals, both sides: each counts to the nearer goal line, its digits either side of its
    // yard line. The reader stands at the sideline facing the middle of the field, so the digits'
    // tops point to the middle and they read along -X on the near side (Y < 0), +X on the far.
    if (!Style.bDrawNumerals || Style.NumeralEveryYards <= 0.f)
    {
        return Marks;
    }
    const float DigitWidth = Style.NumeralWidthYards * Cm;
    const float DigitHeight = Style.NumeralHeightYards * Cm;
    const float Stroke = Style.NumeralStrokeYards * Cm;
    const float DigitGap = Style.NumeralGapYards * Cm;
    const int32 NumeralSteps = FMath::FloorToInt(Dimensions.FieldLengthYards / Style.NumeralEveryYards + KINDA_SMALL_NUMBER);
    for (int32 Step = 1; Step <= NumeralSteps; ++Step)
    {
        const float Yard = Step * Style.NumeralEveryYards;
        const int32 Label = FMath::RoundToInt(FMath::Min(Yard, Dimensions.FieldLengthYards - Yard));
        if (Label <= 0)
        {
            continue;
        }
        const FString Digits = FString::FromInt(Label);
        const float LineX = Yard * Cm;
        const float NumeralWidth = Digits.Len() * DigitWidth + (Digits.Len() - 1) * 2.f * DigitGap;
        for (const float Side : { -1.f, 1.f })
        {
            const float ReadX = Side;
            const float UpY = -Side;
            const float BottomY = Side * (Width * 0.5f - Style.NumeralBottomFromSidelineYards * Cm);
            for (int32 Index = 0; Index < Digits.Len(); ++Index)
            {
                // The digit's left edge, along the reading direction from the yard line.
                const float Start = -NumeralWidth * 0.5f + Index * (DigitWidth + 2.f * DigitGap);
                for (const FBox2D& Box : GetDigitStrokes(Digits[Index] - TEXT('0'), DigitWidth, DigitHeight, Stroke))
                {
                    const float X0 = LineX + ReadX * (Start + static_cast<float>(Box.Min.X));
                    const float X1 = LineX + ReadX * (Start + static_cast<float>(Box.Max.X));
                    const float Y0 = BottomY + UpY * static_cast<float>(Box.Min.Y);
                    const float Y1 = BottomY + UpY * static_cast<float>(Box.Max.Y);
                    AddMark(EPSFieldMarkKind::Numeral, (X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, FMath::Abs(X1 - X0), FMath::Abs(Y1 - Y0));
                }
            }

            // The arrow, beyond the numeral on the side of the goal line it counts to.
            if (Style.bDrawArrows && !FMath::IsNearlyEqual(Yard * 2.f, Dimensions.FieldLengthYards))
            {
                const float Direction = Yard * 2.f < Dimensions.FieldLengthYards ? -1.f : 1.f;
                const float ArrowLength = Style.ArrowLengthYards * Cm;
                AddMark(EPSFieldMarkKind::Arrow,
                    LineX + Direction * (NumeralWidth * 0.5f + Style.ArrowGapYards * Cm + ArrowLength * 0.5f),
                    Side * (Width * 0.5f - Style.ArrowCenterFromSidelineYards * Cm),
                    ArrowLength, Style.ArrowBaseYards * Cm);
                Marks.Last().Direction = Direction;
            }
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
    UMaterialInterface* Fallback = LoadAsset<UMaterialInterface>(Style.MaterialPath, TEXT("material"));
    if (!GroundMesh || !PlaneMesh || !Fallback)
    {
        return false;
    }

    // The grass and the paint are the content pipeline's materials (field_look), the turf chosen
    // by the run's platform tier; the flat fallback stands in for either when it isn't there.
    UMaterialInterface* Turf = LoadOptionalMaterial(ResolveTurfMaterialPath(Style, PSPlatformTiers::GetActiveTier().TierId));
    UMaterialInterface* Paint = LoadOptionalMaterial(Style.PaintMaterialPath);
    bUsesFieldMaterials = Turf && Paint;
    Turf = Turf ? Turf : Fallback;
    Paint = Paint ? Paint : Fallback;

    // The field is the world's frame, wherever this actor was put.
    SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
    SetActorScale3D(FVector::OneVector);

    const float Cm = Dimensions.CentimetresPerYard;
    Materials.Reset();
    auto MakeMaterial = [this, &Style, Cm](UMaterialInterface* Base, const FString& Hex, float Coverage, bool bTriangle)
    {
        FLinearColor Color = FLinearColor::White;
        UPSUITeamCatalog::ParseHexColor(Hex, Color);
        UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Base, this);
        Instance->SetVectorParameterValue(Style.ColorParameter, Color);
        for (const TPair<FName, float>& Scalar : Style.MaterialScalars)
        {
            Instance->SetScalarParameterValue(Scalar.Key, Scalar.Value);
        }
        // Stripes change at the yard lines, on the field's own yard.
        Instance->SetScalarParameterValue(TEXT("StripeWidthCm"), Style.StripeWidthYards * Cm);
        Instance->SetScalarParameterValue(TEXT("StripeOriginCm"), 0.f);
        Instance->SetScalarParameterValue(TEXT("Coverage"), Coverage);
        Instance->SetScalarParameterValue(TEXT("ShapeTriangle"), bTriangle ? 1.f : 0.f);
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
    auto MarkTransform = [MeshSize](const FPSFieldMark& Mark, float Height)
    {
        return FTransform(FRotator::ZeroRotator, FVector(Mark.Center.X, Mark.Center.Y, Height),
            FVector(Mark.Size.X / MeshSize, Mark.Size.Y / MeshSize, 1.f));
    };

    // The paint layers, bottom to top: the border under the lines, then the lines, numerals and
    // arrows. Each is one instanced mesh.
    SetUpPaintLayer(Lines, PlaneMesh, MakeMaterial(Paint, Style.LineColor, Style.PaintCoverage, false));
    SetUpPaintLayer(Border, PlaneMesh, MakeMaterial(Paint, Style.BorderColor, Style.PaintCoverage, false));
    SetUpPaintLayer(Numerals, PlaneMesh, MakeMaterial(Paint, Style.NumeralColor, Style.PaintCoverage, false));
    SetUpPaintLayer(Arrows, PlaneMesh, MakeMaterial(Paint, Style.NumeralColor, Style.PaintCoverage, true));
    Arrows->SetNumCustomDataFloats(ArrowCustomDataFloats);

    for (const FPSFieldMark& Mark : ComputeMarks(Dimensions, Style))
    {
        switch (Mark.Kind)
        {
        case EPSFieldMarkKind::Ground:
            // A box whose top is the ground, Z = 0.
            Ground->SetStaticMesh(GroundMesh);
            Ground->SetMaterial(0, MakeMaterial(Turf, Style.SurroundColor, 1.f, false));
            Ground->SetRelativeLocation(FVector(Mark.Center.X, Mark.Center.Y, -Thickness * 0.5f));
            Ground->SetRelativeScale3D(FVector(Mark.Size.X / MeshSize, Mark.Size.Y / MeshSize, Thickness / MeshSize));
            break;
        case EPSFieldMarkKind::Field:
            PlacePlane(FieldPlane, Mark, Lift, MakeMaterial(Turf, Style.FieldColor, 1.f, false));
            break;
        case EPSFieldMarkKind::NearEndZone:
            PlacePlane(NearEndZone, Mark, 2.f * Lift, MakeMaterial(Paint, Style.NearEndZoneColor, Style.EndZonePaintCoverage, false));
            break;
        case EPSFieldMarkKind::FarEndZone:
            PlacePlane(FarEndZone, Mark, 2.f * Lift, MakeMaterial(Paint, Style.FarEndZoneColor, Style.EndZonePaintCoverage, false));
            break;
        case EPSFieldMarkKind::Border:
            Border->AddInstance(MarkTransform(Mark, 2.5f * Lift));
            break;
        case EPSFieldMarkKind::Line:
            Lines->AddInstance(MarkTransform(Mark, 3.f * Lift));
            break;
        case EPSFieldMarkKind::Numeral:
            Numerals->AddInstance(MarkTransform(Mark, 3.f * Lift));
            break;
        case EPSFieldMarkKind::Arrow:
        {
            // The paint material cuts the triangle in world space from these (M_FieldPaint).
            const int32 Index = Arrows->AddInstance(MarkTransform(Mark, 3.f * Lift));
            const float ArrowData[ArrowCustomDataFloats] = {
                Mark.Direction, static_cast<float>(Mark.Center.X), static_cast<float>(Mark.Center.Y),
                static_cast<float>(Mark.Size.X), static_cast<float>(Mark.Size.Y) };
            Arrows->SetCustomData(Index, MakeArrayView(ArrowData), true);
            break;
        }
        default:
            break;
        }
    }
    bBuilt = true;
    return true;
}
