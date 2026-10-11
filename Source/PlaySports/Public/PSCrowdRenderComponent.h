// PSCrowdRenderComponent.h - Epic 48: the fans in the stadium's seats, instanced, in team colours
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCrowdLookTypes.h"
#include "PSPlatformTiers.h"
#include "PSStadiumSetTypes.h"
#include "PSCrowdRenderComponent.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Which shirt group a fan wears (the first five; NeutralColors follow). */
namespace PSCrowdShirt
{
    constexpr int32 HomePrimary = 0;
    constexpr int32 HomeSecondary = 1;
    constexpr int32 AwayPrimary = 2;
    constexpr int32 AwaySecondary = 3;
    constexpr int32 White = 4;
    constexpr int32 FirstNeutral = 5;
}

/** One fan in his seat. */
struct FPSCrowdFan
{
    int32 Seat = INDEX_NONE;
    bool bHome = true;
    int32 Shirt = 0;
    int32 Skin = 0;
    /** His instance in his shirt's mesh, and in his skin's (figures only). */
    int32 ShirtInstance = INDEX_NONE;
    int32 HeadInstance = INDEX_NONE;
    /** The excitement he stands at; below 0, he always stands. */
    float StandThreshold = 1.f;
    bool bStanding = false;
    float Scale = 1.f;
    float YawOffset = 0.f;
};

/**
 * UPSCrowdRenderComponent draws the crowd (Epic 48): a fan in most of the stadium's seats, each an
 * instance of one box mesh per shirt colour and one per skin tone, so tens of thousands of fans cost
 * a few draw calls. APSStadiumSet owns one and fills it once its seats are built.
 *
 *  - How many: the platform tier's CrowdDensity of the seats (Data/platform_tiers.json), chosen by
 *    a fixed seed so the crowd is the same every time. How: its CrowdDetail, a torso and head per
 *    fan on a PC, a card per fan on a phone, or none.
 *  - Who: the home crowd's share of the stadium is UPSCrowdExcitementSubsystem's (the match's);
 *    the away fans sit together in pockets, plus a few among the home fans. Their shirts are their
 *    team's colours from the team identity data, the shirt colour groups recoloured when the match's
 *    teams are known.
 *  - How they move: they stand up as the crowd's excitement (UPSCrowdExcitementSubsystem, the one
 *    authority) reaches each fan's threshold, and sit down as it settles, checked the tier's
 *    CrowdUpdateHz times a second.
 *
 * The look is Data/crowd_look.json. Headless tests call Populate, SetTeamColors and
 * ApplyExcitement themselves.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCrowdRenderComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCrowdRenderComponent();

    static FString GetDefaultStylePath();

    /** Data/crowd_look.json through UPSDataIngestion; the defaults when missing or unsound. */
    static FPSCrowdLookStyle LoadStyle(const FString& Path);

    /** Problems with Style, one line each (empty when sound); mirrors tools/validate_data.py. */
    static TArray<FString> ValidateStyle(const FPSCrowdLookStyle& Style);

    /** Seats the crowd: InDensity (0-1) of Seats get a fan, InHomeShare (0-1) of them home fans,
     *  drawn at InDetail. SeatHeightCm is the seats' height above their tread (the stadium's).
     *  A second call replaces the first crowd. False when the mesh or material can't be loaded. */
    bool Populate(const TArray<FPSStadiumSeat>& Seats, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, float InDensity, EPSCrowdDetail InDetail, float InHomeShare);

    /** Populate with Data/crowd_look.json, the active tier's density and detail, and the world's
     *  match: its home share and teams (UPSCrowdExcitementSubsystem). */
    bool PopulateFromData(const TArray<FPSStadiumSeat>& Seats, float SeatHeightCm);

    /** Dresses the home and away fans in HomeTeamId's and AwayTeamId's colours (team identity
     *  data), or the style's defaults for a team that has none. */
    void SetTeamColors(FName HomeTeamId, FName AwayTeamId);

    /** Stands up the fans whose threshold InExcitement (0-1) reaches and sits down those it has
     *  fallen StandHysteresis under, at most MaxStandChangesPerUpdate of them; returns how many
     *  moved. The tick calls it with the crowd's excitement. */
    int32 ApplyExcitement(float InExcitement);

    /** The tick's work: follow the match's teams, home share and excitement. */
    void UpdateFromMatch();

    int32 GetNumFans() const { return Fans.Num(); }
    int32 GetNumStanding() const;
    const TArray<FPSCrowdFan>& GetFans() const { return Fans; }
    EPSCrowdDetail GetDetail() const { return Detail; }
    float GetHomeShare() const { return HomeShare; }

    /** The mesh drawing one shirt group's fans (PSCrowdShirt), or one skin tone's heads. */
    UInstancedStaticMeshComponent* GetShirtMesh(int32 Shirt) const;
    UInstancedStaticMeshComponent* GetHeadMesh(int32 Skin) const;

    /** The colour a shirt group wears now. */
    FLinearColor GetShirtColor(int32 Shirt) const;

    /** A fan's shirt (or card) and head transforms, seated or standing. */
    static FTransform ComputeBodyTransform(const FPSStadiumSeat& Seat, const FPSCrowdFan& Fan, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, EPSCrowdDetail InDetail, bool bStanding);
    static FTransform ComputeHeadTransform(const FPSStadiumSeat& Seat, const FPSCrowdFan& Fan, float SeatHeightCm, const FPSCrowdLookStyle& InStyle, bool bStanding);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    /** Makes (or reuses) Count instanced meshes in Meshes, each with its own colour. */
    void PrepareMeshes(TArray<UInstancedStaticMeshComponent*>& Meshes, TArray<UMaterialInstanceDynamic*>& Looks, int32 Count, const TCHAR* BaseName);

    FPSCrowdLookStyle Style;
    EPSCrowdDetail Detail = EPSCrowdDetail::Figures;
    float HomeShare = 1.f;
    float Density = 1.f;
    float SeatHeight = 44.f;
    float LastExcitement = 0.f;
    int32 NextFanToCheck = 0;

    /** The seats the crowd sits in (a copy of the stadium's). */
    TArray<FPSStadiumSeat> CrowdSeats;
    TArray<FPSCrowdFan> Fans;

    UPROPERTY(Transient)
    TArray<UInstancedStaticMeshComponent*> ShirtMeshes;

    UPROPERTY(Transient)
    TArray<UInstancedStaticMeshComponent*> HeadMeshes;

    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> ShirtLooks;

    UPROPERTY(Transient)
    TArray<UMaterialInstanceDynamic*> HeadLooks;

    /** What each shirt group wears now. */
    TArray<FLinearColor> ShirtColors;

    FName ColoredHomeTeam;
    FName ColoredAwayTeam;
    bool bColored = false;
};
