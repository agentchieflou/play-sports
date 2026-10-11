// PSStadiumSet.h - Epic 147.1: the goal posts, team benches and stands, built at runtime from data
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSFieldDimensions.h"
#include "PSStadiumSetTypes.h"
#include "PSStadiumSet.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * The stadium shell around the field (Epic 147.1): a goal post on each end line, a team bench
 * along each sideline, and stands rising on all four sides. Like the field (APSFieldSurface), it
 * is built at runtime from engine basic shapes and dynamic material instances, with no asset of
 * its own: where each piece goes comes from the field's one frame (PSField), how it looks from
 * Data/stadium_set.json. Each kind of piece is one instanced mesh.
 *
 * Nothing in the set collides; the field's ground is the only collision surface. That includes
 * the goal posts: whether a kick is good is UPSSpecialTeamsModel's to decide, so a post that
 * stopped the ball would contradict the play's one result. APSFieldGrid spawns one when the level
 * has none.
 */
UCLASS()
class PLAYSPORTS_API APSStadiumSet : public AActor
{
    GENERATED_BODY()

public:
    APSStadiumSet();

    /** Builds the set for Dimensions in Style's look; a rebuild replaces what was there. False,
     *  with the reason logged, when a mesh or the material can't be loaded. */
    bool Build(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style);

    /** Build with the field's dimensions (PSField::GetDimensions) and Data/stadium_set.json, or
     *  the default look when that file is missing or unsound. */
    bool BuildFromData();

    bool IsBuilt() const { return bBuilt; }

    /** Every piece for Dimensions and Style: per end, the base post, the neck, the crossbar and two
     *  uprights; a bench past each sideline; then the stands, tier by tier, along each sideline and
     *  behind each end line. */
    static TArray<FPSStadiumPiece> ComputePieces(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateStyle(const FPSStadiumSetStyle& Style);

    static FString GetDefaultStylePath();

    /** Data/stadium_set.json through UPSDataIngestion; the defaults when it is missing or unsound. */
    static FPSStadiumSetStyle LoadStyle(const FString& Path);

    /** The instanced mesh that draws one kind of piece. */
    UInstancedStaticMeshComponent* GetPieces(EPSStadiumPieceKind Kind) const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    UInstancedStaticMeshComponent* GoalPosts;

    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    UInstancedStaticMeshComponent* Benches;

    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    UInstancedStaticMeshComponent* Stands;

    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    UInstancedStaticMeshComponent* StandsAlt;

    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> Materials;

    bool bBuilt = false;
};
