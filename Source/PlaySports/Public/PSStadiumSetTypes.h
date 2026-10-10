// PSStadiumSetTypes.h - Epic 147.1: the goal posts, team benches and stands, from Data/stadium_set.json
#pragma once

#include "CoreMinimal.h"
#include "PSStadiumSetTypes.generated.h"

/**
 * How the stadium shell around the field is built (Data/stadium_set.json; Architecture rule 4).
 * Where it stands comes from the field's one frame (PSField, Data/field_dimensions.json): the goal
 * posts on the end lines, the benches past the sidelines, the stands past the ground's edge. Lengths
 * are in yards of that frame, so the set keeps the field's proportions at any scale. Defaults equal
 * the file.
 */
USTRUCT(BlueprintType)
struct FPSStadiumSetStyle
{
    GENERATED_BODY()

    /** A box and a cylinder (axis up), MeshSizeCm across and tall at scale 1, centred on their
     *  pivots, and a material with a vector parameter named ColorParameter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BoxMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString CylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float MeshSizeCm = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FName ColorParameter = TEXT("Color");

    /** The goal posts (NFL): the crossbar 10 ft up over the end line, uprights 18 ft 6 in apart
     *  rising 35 ft above it, on a base post set back behind the end line with a neck reaching
     *  forward to the crossbar. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString GoalPostColor = TEXT("#F2C230");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CrossbarHeightYards = 3.3333f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float CrossbarWidthYards = 6.1667f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float UprightHeightYards = 11.6667f;

    /** The crossbar's and uprights' thickness, and the base post's and neck's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float PostDiameterYards = 0.1111f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BasePostDiameterYards = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BaseSetbackYards = 2.f;

    /** A bench along each sideline, from one yard line to another, BenchDistanceYards past the
     *  sideline (inside the out-of-bounds depth). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString BenchColor = TEXT("#3A3F47");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchFromYardLine = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchToYardLine = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchDistanceYards = 6.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchDepthYards = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float BenchHeightYards = 0.5f;

    /** The stands: StandTiers rising rows on all four sides, StandGapYards past the ground's edge,
     *  each StandTierDepthYards deep and StandTierRiseYards higher than the one in front;
     *  alternate tiers in StandAltColor. The corners are open. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString StandColor = TEXT("#5A616B");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FString StandAltColor = TEXT("#4B5159");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    int32 StandTiers = 10;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float StandGapYards = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float StandTierDepthYards = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    float StandTierRiseYards = 0.6f;

    /** Dynamic shadows from the set cost every frame on a phone; off by default. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    bool bCastShadows = false;
};

/** What a piece of the stadium set is. */
UENUM(BlueprintType)
enum class EPSStadiumPieceKind : uint8
{
    GoalPost,
    Bench,
    Stand,
    StandAlt
};

/** One piece of the set: a box, or a cylinder along its local Z, placed and sized in world cm. */
USTRUCT(BlueprintType)
struct FPSStadiumPiece
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    EPSStadiumPieceKind Kind = EPSStadiumPieceKind::Stand;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    bool bCylinder = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FVector Center = FVector::ZeroVector;

    /** A box's extent along X, Y and Z; a cylinder's diameter (X, Y) and length (Z), before Rotation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FVector Size = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stadium")
    FRotator Rotation = FRotator::ZeroRotator;
};
