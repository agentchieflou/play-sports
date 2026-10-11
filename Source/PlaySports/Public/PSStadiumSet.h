// PSStadiumSet.h - Epics 147.1 and 52: the stadium bowl around the field, built at runtime from data
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PSFieldDimensions.h"
#include "PSPlatformTiers.h"
#include "PSStadiumSetTypes.h"
#include "PSStadiumSet.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPSCrowdRenderComponent;

/**
 * The stadium around the field: a bowl of decks with real seat rows, aisles and vomitories behind
 * a padded field wall, suites and a press box between the decks, ribbon boards, a canopy with its
 * light banks, video boards over the ends, and on the field's edge the goal posts and team
 * benches. Like the field (APSFieldSurface) it is built at runtime from data, with no asset of its
 * own: where everything stands comes from the field's one frame (PSField), what it is and how it
 * looks from Data/stadium_set.json. Each kind of piece is one instanced mesh in one colour.
 *
 * The plan is a rounded rectangle at the ground's edge, and every row runs parallel to it; aisles
 * split the bowl into sections (FPSStadiumBowl). The active platform tier picks the detail
 * (Data/platform_tiers.json's StadiumDetail): every seat a pan and a back, with shadows, on a PC;
 * a strip per row and section, without shadows, on a phone.
 *
 * The press level stays open: under the upper deck's overhang, in front of the suites, the
 * broadcast position (70 m from the field's centre line, 25 m up) sees the whole field, as does the
 * all-22 position high behind the end zone (PlaySports.Field.StadiumBowlLayout checks both).
 *
 * Its seats are where the crowd sits: the set's UPSCrowdRenderComponent fills them once it is built.
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

    /** Builds the set for Dimensions in Style's look at Detail; a rebuild replaces what was there.
     *  False, with the reason logged, when a mesh or the material can't be loaded. The crowd is
     *  left to the caller (BuildFromData fills it). */
    bool Build(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style, EPSStadiumDetail Detail);

    /** Build with the field's dimensions (PSField::GetDimensions), Data/stadium_set.json (or the
     *  default look when that file is missing or unsound) and the active tier's detail, then fill
     *  the seats with the crowd. */
    bool BuildFromData();

    bool IsBuilt() const { return bBuilt; }

    /** Everything the set builds for Dimensions, Style and Detail: per end, the goal post (base
     *  post, pad, neck, crossbar, uprights, ribbons); a bench past each sideline; then the bowl,
     *  deck by deck and row by row (risers, aisle steps, vomitories, seats), the field wall and
     *  walkway, each deck's fascia and ribbon board, the cross-aisles and suites, the press box,
     *  the back wall, the canopy and its light banks, and the video boards. */
    static FPSStadiumLayout ComputeLayout(const FPSFieldDimensions& Dimensions, const FPSStadiumSetStyle& Style, EPSStadiumDetail Detail);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateStyle(const FPSStadiumSetStyle& Style);

    static FString GetDefaultStylePath();

    /** Data/stadium_set.json through UPSDataIngestion; the defaults when it is missing or unsound. */
    static FPSStadiumSetStyle LoadStyle(const FString& Path);

    /** The instanced mesh that draws one kind of piece. */
    UInstancedStaticMeshComponent* GetPieces(EPSStadiumPieceKind Kind) const;

    /** The seats of the last build, where the crowd sits. */
    const TArray<FPSStadiumSeat>& GetSeats() const { return Layout.Seats; }

    /** The light banks' faces of the last build (FPSStadiumLayout::LightBanks). */
    const TArray<FTransform>& GetLightBanks() const { return Layout.LightBanks; }

    int32 GetNumSections() const { return Layout.NumSections; }

    UPSCrowdRenderComponent* GetCrowd() const { return Crowd; }

protected:
    virtual void BeginPlay() override;

    /** One instanced mesh per EPSStadiumPieceKind, in the enum's order. */
    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    TArray<UInstancedStaticMeshComponent*> PieceMeshes;

    /** The fans in the seats. */
    UPROPERTY(VisibleAnywhere, Category = "Stadium")
    UPSCrowdRenderComponent* Crowd;

    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> Materials;

    /** The last build's pieces, seats and light banks. */
    FPSStadiumLayout Layout;

    bool bBuilt = false;
};
