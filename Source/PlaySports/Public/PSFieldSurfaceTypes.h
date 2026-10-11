// PSFieldSurfaceTypes.h - Epic 146.3: how the field is drawn, from Data/field_markings.json
#pragma once

#include "CoreMinimal.h"
#include "PSFieldSurfaceTypes.generated.h"

/** A tier's own turf material (Data/field_markings.json's TierLooks), keyed by the tier's id in
 *  Data/platform_tiers.json: the low mobile tier draws a lighter turf. */
USTRUCT(BlueprintType)
struct FPSFieldTierLook
{
    GENERATED_BODY()

    FPSFieldTierLook() = default;
    FPSFieldTierLook(FName InTierId, const FString& InTurfMaterialPath)
        : TierId(InTierId), TurfMaterialPath(InTurfMaterialPath)
    {
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FName TierId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString TurfMaterialPath;
};

/**
 * How the field's surface and markings look (Data/field_markings.json; Architecture rule 4).
 * Where they go comes from the field's one frame (PSField, Data/field_dimensions.json), so the
 * markings always sit where the game spots the ball. Lengths are in yards of that frame, so they
 * keep the field's proportions at any scale. Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSFieldMarkingsStyle
{
    GENERATED_BODY()

    /** The file's MaterialScalars and TierLooks (the other defaults are below). */
    FPSFieldMarkingsStyle();

    /** The grass (the field and the ground around it): a world-aligned turf material made by the
     *  content pipeline's field_look step. Each piece gets a dynamic instance with ColorParameter
     *  set to its colour, which the material treats as the grass's average colour. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString TurfMaterialPath = TEXT("/Game/Field/Materials/M_Turf.M_Turf");

    /** The paint (end zones, lines, numerals, arrows, the border): painted grass, masked, with a
     *  scalar parameter Coverage (how much of the grass the paint hides) and ShapeTriangle (1 cuts
     *  an arrow from the piece). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString PaintMaterialPath = TEXT("/Game/Field/Materials/M_FieldPaint.M_FieldPaint");

    /** Scalar parameters set on every turf and paint instance (a parameter a material lacks is
     *  ignored): tiling, stripes, variation, roughness. The materials' own defaults apply to any
     *  not listed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    TMap<FName, float> MaterialScalars;

    /** The mowing stripes: a band this many yards wide along the field, starting at the near goal
     *  line, so the bands change at every yard line. Sets the materials' StripeWidthCm and
     *  StripeOriginCm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float StripeWidthYards = 5.f;

    /** How much of the grass the paint hides, 0-1: the lines, numerals, arrows and border, and the
     *  end zones (painted solid, so more). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float PaintCoverage = 0.93f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float EndZonePaintCoverage = 0.97f;

    /** Per-tier turf materials; a tier not listed draws TurfMaterialPath. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    TArray<FPSFieldTierLook> TierLooks;

    /** The ground slab: a box mesh, MeshSizeCm on a side at scale 1, centered on its pivot. It
     *  has collision, so the ball lands on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString GroundMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

    /** The flat pieces (the field, end zones, lines): a plane facing +Z, MeshSizeCm on a side at
     *  scale 1, centered on its pivot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString PlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

    /** The engine basic shapes' size at scale 1, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float MeshSizeCm = 100.f;

    /** The fallback for both: a material with a vector parameter named ColorParameter, used where
     *  the turf or paint material can't be loaded (a checkout without the field's content). Each
     *  piece gets a dynamic instance in its color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FName ColorParameter = TEXT("Color");

    /** The grass between the end lines and sidelines. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString FieldColor = TEXT("#3B6E2A");

    /** The ground out of bounds, out to the out-of-bounds volumes' depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString SurroundColor = TEXT("#335F27");

    /** The end zone behind the near goal line (X = 0) and the one behind the far goal line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString NearEndZoneColor = TEXT("#1F4E9C");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString FarEndZoneColor = TEXT("#9C1F1F");

    /** Every line and hash mark. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString LineColor = TEXT("#FFFFFF");

    /** The ground slab's thickness, in cm; its top is at Z = 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float GroundThicknessCm = 20.f;

    /** How far each layer sits above the one below (ground, field, end zones, lines), in cm,
     *  so they never fight for the same depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float LayerLiftCm = 0.5f;

    /** Every line's width: sidelines, end lines, goal lines, yard lines and hash marks (4 in). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float LineWidthYards = 0.1111f;

    /** A yard line every this many yards from the near goal line, goal lines included. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float YardLineSpacingYards = 5.f;

    /** A pair of hash marks every this many yards between the goal lines, except where a yard
     *  line already crosses. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float HashSpacingYards = 1.f;

    /** Each hash mark's length across the field (2 ft). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float HashLengthYards = 0.6667f;

    /** Each row of hash marks' distance from the middle of the field (the rows are 18 ft 6 in
     *  apart). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float HashOffsetYards = 3.0833f;

    /** The white border around the field, outside the sidelines and end lines (6 ft); 0 draws
     *  none. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float BorderWidthYards = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString BorderColor = TEXT("#FFFFFF");

    /** The yard numerals: 10, 20, 30, 40, 50, 40, 30, 20, 10 on both sides, one every
     *  NumeralEveryYards from the near goal line, each counting to the nearer goal line. A
     *  numeral's two digits sit either side of its yard line, NumeralGapYards from it, their bottoms
     *  NumeralBottomFromSidelineYards in from the sideline and their tops toward the middle of the
     *  field (so the far side's read upside down from the near sideline, as on television).
     *  Digits are NumeralWidthYards by NumeralHeightYards (4 ft by 6 ft), drawn in block strokes
     *  NumeralStrokeYards wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    bool bDrawNumerals = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString NumeralColor = TEXT("#FFFFFF");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralEveryYards = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralHeightYards = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralWidthYards = 1.3333f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralStrokeYards = 0.3333f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralBottomFromSidelineYards = 7.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float NumeralGapYards = 0.3333f;

    /** An arrow beside every numeral but the middle one, pointing to the goal line it counts to:
     *  ArrowLengthYards long (36 in), ArrowBaseYards across (18 in), ArrowGapYards beyond the
     *  outer digit, its centre ArrowCenterFromSidelineYards in from the sideline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    bool bDrawArrows = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float ArrowLengthYards = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float ArrowBaseYards = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float ArrowGapYards = 0.3333f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float ArrowCenterFromSidelineYards = 8.5f;
};

/** What a piece of the field is. */
UENUM(BlueprintType)
enum class EPSFieldMarkKind : uint8
{
    Ground,
    Field,
    NearEndZone,
    FarEndZone,
    Line,
    /** The white border outside the boundary. */
    Border,
    /** One block stroke of a yard numeral's digit. */
    Numeral,
    /** A triangle beside a numeral; the mark's Direction is the way it points along X. */
    Arrow
};

/** One flat rectangle of the field, on the ground: its center and size in world cm. */
USTRUCT(BlueprintType)
struct FPSFieldMark
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    EPSFieldMarkKind Kind = EPSFieldMarkKind::Line;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FVector2D Center = FVector2D::ZeroVector;

    /** X along the field, Y across it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FVector2D Size = FVector2D::ZeroVector;

    /** An arrow's direction along X, +1 or -1 (its tip at Center.X + Direction * Size.X / 2);
     *  0 for every other mark. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    float Direction = 0.f;
};
