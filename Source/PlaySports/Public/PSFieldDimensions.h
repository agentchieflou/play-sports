// PSFieldDimensions.h - the field's one frame: yards to world space for every system
#pragma once

#include "CoreMinimal.h"
#include "PSFieldDimensions.generated.h"

/**
 * The field's dimensions and its scale (Data/field_dimensions.json; Architecture rule 4).
 * Defaults equal the file.
 */
USTRUCT(BlueprintType)
struct FPSFieldDimensions
{
    GENERATED_BODY()

    /** World units (cm) in one yard. The game's yard is a metre: every distance tuned in cm
     *  (lineups, routes, coverage depths) is read against it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float CentimetresPerYard = 100.f;

    /** Goal line to goal line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float FieldLengthYards = 100.f;

    /** Each end zone, from its goal line to its end line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float EndZoneDepthYards = 10.f;

    /** Sideline to sideline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float FieldWidthYards = 53.3333f;

    /** How far past the sidelines and end lines the out-of-bounds volumes reach. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float OutOfBoundsDepthYards = 14.f;

    /** The boundary volumes' height, in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Field")
    float BoundaryHeightCm = 500.f;
};

/**
 * PSField is the one authority on where the field is in the world. Yard line N (from the
 * offense's own goal line, 0, to the goal it attacks, 100) is at X = N * CentimetresPerYard:
 * the offense always attacks +X from its goal line at X = 0, and Y = 0 is the middle of the
 * field. The game mode lines up and snaps on it (PSGameStateEvents::LineOfScrimmageFor), the
 * field grid lays its end zones and boundaries out on it (APSFieldGrid), and every yard read
 * from a world location -- a tackle's spot, a boundary crossing, an interception, a loose
 * ball -- goes through it. Pure, apart from loading the dimensions once.
 */
namespace PSField
{
    PLAYSPORTS_API FString GetDefaultDataPath();

    /** The dimensions in use: Data/field_dimensions.json, read once through UPSDataIngestion;
     *  the defaults when it is missing or unsound. */
    PLAYSPORTS_API const FPSFieldDimensions& GetDimensions();

    /** Problems with Dimensions, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> Validate(const FPSFieldDimensions& Dimensions);

    PLAYSPORTS_API float YardsToCentimetres(float Yards);

    PLAYSPORTS_API float CentimetresToYards(float Centimetres);

    /** The world location of YardLine (from the offense's own goal line), LateralYards from the
     *  middle of the field (+Y), on the ground. */
    PLAYSPORTS_API FVector YardLineToWorld(float YardLine, float LateralYards = 0.f);

    /** Location's yard line, unrounded: below 0 or above FieldLengthYards in an end zone. */
    PLAYSPORTS_API float WorldToYardLine(const FVector& Location);

    /** Location's yard line as a spot: rounded to the nearest yard and kept on the field
     *  (0 .. FieldLengthYards). */
    PLAYSPORTS_API int32 WorldToSpot(const FVector& Location);

    /** World X of the offense's own goal line (bFar false, X = 0) or of the one it attacks. */
    PLAYSPORTS_API float GoalLineX(bool bFar);

    /** World X of the 50. */
    PLAYSPORTS_API float MidfieldX();

    /** World X of the end line behind the offense's own end zone (bFar false) or the far one. */
    PLAYSPORTS_API float EndLineX(bool bFar);

    /** |Y| of the sidelines. */
    PLAYSPORTS_API float SidelineY();
}
