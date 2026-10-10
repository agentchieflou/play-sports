// PSFieldSurfaceTypes.h - Epic 146.3: how the field is drawn, from Data/field_markings.json
#pragma once

#include "CoreMinimal.h"
#include "PSFieldSurfaceTypes.generated.h"

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

    /** A material with a vector parameter named ColorParameter; each piece gets a dynamic
     *  instance in its color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FName ColorParameter = TEXT("Color");

    /** The grass between the end lines and sidelines. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString FieldColor = TEXT("#2E7D32");

    /** The ground out of bounds, out to the out-of-bounds volumes' depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field|Markings")
    FString SurroundColor = TEXT("#1E5B24");

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
};

/** What a piece of the field is. */
UENUM(BlueprintType)
enum class EPSFieldMarkKind : uint8
{
    Ground,
    Field,
    NearEndZone,
    FarEndZone,
    Line
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
};
