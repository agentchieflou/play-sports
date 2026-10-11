// PSCrowdLookTypes.h - Epic 48: what the fans in the stands look like, from Data/crowd_look.json
#pragma once

#include "CoreMinimal.h"
#include "PSCrowdLookTypes.generated.h"

/**
 * How the crowd in the stands looks and moves (Data/crowd_look.json; Architecture rule 4). Defaults
 * equal the file. Where the fans sit is the stadium's (APSStadiumSet's seats); how many there are
 * and how each is drawn is the platform tier's (CrowdDensity, CrowdDetail); how excited they are
 * is UPSCrowdExcitementSubsystem's, and the home crowd's share of the stadium is the match's.
 *
 *  - A fan is a box mesh (BodyMeshPath, MeshSizeCm across at scale 1, centred) in one colour
 *    through ColorParameter: as a figure, a torso TorsoWidthCm by TorsoDepthCm by TorsoHeightCm on
 *    his seat and a head HeadSizeCm across; as a card, one slab CardDepthCm thick, as tall as both.
 *    Each fan is up to SizeVariation bigger or smaller and turned up to YawJitterDegrees off his
 *    seat's facing.
 *  - His shirt: his team's primary colour (PrimaryShare of fans), secondary (SecondaryShare),
 *    WhiteColor (WhiteShare), or else one of NeutralColors. His skin: one of SkinTones. Until the
 *    match's teams are known, the teams wear the Default colours.
 *  - Away fans: AwayPocketShare of them sit together in the sections nearest the spot
 *    AwayPocketYardLine, AwayPocketLateralYards (the field's frame); the rest are scattered.
 *  - Standing: AlwaysStandShare of the fans stand all game; each other one stands once the crowd's
 *    excitement reaches his own threshold, between StandExcitementMin and StandExcitementMax, and
 *    sits again StandHysteresis under it. Standing lifts him StandRiseCm and moves him
 *    StandForwardCm forward. At most MaxStandChangesPerUpdate fans move per update.
 *  - Seed makes the crowd the same every time.
 */
USTRUCT(BlueprintType)
struct FPSCrowdLookStyle
{
    GENERATED_BODY()

    FPSCrowdLookStyle()
    {
        for (const TCHAR* Neutral : { TEXT("#1C1C1E"), TEXT("#4F545B"), TEXT("#2B3A55"), TEXT("#6B5A48") })
        {
            NeutralColors.Add(Neutral);
        }
        for (const TCHAR* Skin : { TEXT("#F1C7A5"), TEXT("#E0AC88"), TEXT("#C68A64"), TEXT("#9C6644"), TEXT("#6F4630"), TEXT("#4A2E20") })
        {
            SkinTones.Add(Skin);
        }
    }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString BodyMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float MeshSizeCm = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString MaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FName ColorParameter = TEXT("Color");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    int32 Seed = 1987;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float TorsoWidthCm = 42.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float TorsoDepthCm = 26.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float TorsoHeightCm = 58.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float HeadSizeCm = 21.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float CardDepthCm = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float SizeVariation = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float YawJitterDegrees = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float PrimaryShare = 0.55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float SecondaryShare = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float WhiteShare = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString WhiteColor = TEXT("#EDEDED");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    TArray<FString> NeutralColors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    TArray<FString> SkinTones;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString DefaultHomePrimary = TEXT("#B3202A");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString DefaultHomeSecondary = TEXT("#F2C230");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString DefaultAwayPrimary = TEXT("#1F4E9C");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    FString DefaultAwaySecondary = TEXT("#E6E6E6");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float AwayPocketShare = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float AwayPocketYardLine = 110.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float AwayPocketLateralYards = 45.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float AlwaysStandShare = 0.06f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float StandExcitementMin = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float StandExcitementMax = 0.95f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float StandHysteresis = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float StandRiseCm = 46.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    float StandForwardCm = 14.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd")
    int32 MaxStandChangesPerUpdate = 6000;
};
