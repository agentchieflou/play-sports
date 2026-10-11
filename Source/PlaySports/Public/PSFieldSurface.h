// PSFieldSurface.h - Epic 146.3: the field you see, built at runtime from data (look: lane V2)
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSFieldDimensions.h"
#include "PSFieldSurfaceTypes.h"
#include "PSFieldSurface.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * The field you see: the ground, the grass between the end lines and sidelines, both end zones,
 * and every line and hash mark. It is built at runtime from data, with engine basic shapes and
 * dynamic material instances, so it needs no level or mesh asset of its own (Epic 146,
 * Specs/ADR_Content_Pipeline.md). Where each piece goes comes from the field's one frame
 * (PSField: the near goal line at X = 0, yard line N at X = N yards, Y = 0 the middle of the
 * field); how it looks comes from Data/field_markings.json. So the lines are always where the
 * game spots the ball, and the end zones are where APSFieldGrid's end-zone volumes score.
 *
 * The ground reaches as far out of bounds as the out-of-bounds volumes and has collision: the
 * ball lands on it. The flat pieces have none. APSFieldGrid spawns one when the level has none.
 */
UCLASS()
class PLAYSPORTS_API APSFieldSurface : public AActor
{
    GENERATED_BODY()

public:
    APSFieldSurface();

    /** Lays the field out for Dimensions in Style's look. Rebuilding replaces what was there.
     *  False, with the reason logged, when a mesh or the material can't be loaded. */
    bool Build(const FPSFieldDimensions& Dimensions, const FPSFieldMarkingsStyle& Style);

    /** Build with the field's dimensions (PSField::GetDimensions) and Data/field_markings.json,
     *  or the default look when that file is missing or unsound. */
    bool BuildFromData();

    /** True once Build has succeeded. */
    bool IsBuilt() const { return bBuilt; }

    /** Every piece of the field for Dimensions and Style, ground first and lines last: the ground
     *  (out to the out-of-bounds depth), the field (end line to end line, sideline to sideline),
     *  the near and far end zones, then the lines: sidelines, end lines, a yard line every
     *  YardLineSpacingYards from the near goal line (goal lines included), and a pair of hash
     *  marks every HashSpacingYards between the goal lines where no yard line crosses. Then the
     *  paint around and on them: the border outside the boundary (four bands), each numeral's
     *  digit strokes, and an arrow beside each numeral but the middle one. */
    static TArray<FPSFieldMark> ComputeMarks(const FPSFieldDimensions& Dimensions, const FPSFieldMarkingsStyle& Style);

    /** A digit's block strokes, as rectangles (min X, min Y, max X, max Y) in a Width x Height cell
     *  with its origin at the digit's bottom left, X to the reader's right and Y up: a seven-segment
     *  block face, Stroke thick, with a single centred bar for 1. Empty for anything but 0-9. */
    static TArray<FBox2D> GetDigitStrokes(int32 Digit, float Width, float Height, float Stroke);

    /** The turf material TierId draws: its TierLooks entry, else TurfMaterialPath. */
    static FString ResolveTurfMaterialPath(const FPSFieldMarkingsStyle& Style, FName TierId);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateStyle(const FPSFieldMarkingsStyle& Style);

    static FString GetDefaultStylePath();

    /** Data/field_markings.json, read through UPSDataIngestion; the defaults when it is missing
     *  or unsound. */
    static FPSFieldMarkingsStyle LoadStyle(const FString& Path);

    UStaticMeshComponent* GetGround() const { return Ground; }
    UStaticMeshComponent* GetFieldPlane() const { return FieldPlane; }
    UStaticMeshComponent* GetEndZone(bool bFar) const { return bFar ? FarEndZone : NearEndZone; }
    UInstancedStaticMeshComponent* GetLines() const { return Lines; }
    UInstancedStaticMeshComponent* GetBorder() const { return Border; }
    UInstancedStaticMeshComponent* GetNumerals() const { return Numerals; }
    UInstancedStaticMeshComponent* GetArrows() const { return Arrows; }

    /** Whether the last Build drew the grass and the paint with the field's own materials (the
     *  content pipeline's), rather than the flat fallback. */
    bool UsesFieldMaterials() const { return bUsesFieldMaterials; }

    /** How many floats of per-instance data each arrow carries for the paint material: its
     *  direction, centre X and Y, length and base (M_FieldPaint reads them). */
    static constexpr int32 ArrowCustomDataFloats = 5;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UStaticMeshComponent* Ground;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UStaticMeshComponent* FieldPlane;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UStaticMeshComponent* NearEndZone;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UStaticMeshComponent* FarEndZone;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UInstancedStaticMeshComponent* Lines;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UInstancedStaticMeshComponent* Border;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UInstancedStaticMeshComponent* Numerals;

    UPROPERTY(VisibleAnywhere, Category = "Field")
    UInstancedStaticMeshComponent* Arrows;

    /** One dynamic material instance per piece colour. */
    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> Materials;

    bool bBuilt = false;

    bool bUsesFieldMaterials = false;
};
