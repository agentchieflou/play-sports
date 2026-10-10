// PSTelestratorTypes.h - Epic 44: what the telestrator draws, keeps and asks for
#pragma once

#include "CoreMinimal.h"
#include "PSCameraAll22Component.h"
#include "PSPlaySimulation.h"
#include "PSReplayFormat.h"
#include "PSTelestratorTypes.generated.h"

/** What a stroke on the telestrator draws. */
UENUM(BlueprintType)
enum class EPSTelestratorTool : uint8
{
    /** A line following the stroke. */
    Freehand,
    /** From where the stroke starts to where it ends, with a head there. */
    Arrow,
    /** Around where the stroke starts, out to where it ends. */
    Circle,
    /** A tap on a player: he is highlighted (Epic 36). */
    Player
};

/** One drawing on the frame. Screen points are where it was drawn, normalized like the film
 *  frame's ((0,0) top left); field points are the same points on the field, so a drawing stays
 *  pinned to the ground whatever the camera does next. */
USTRUCT(BlueprintType)
struct FPSTelestratorMark
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    EPSTelestratorTool Tool = EPSTelestratorTool::Freehand;

    /** Freehand: the stroke; Arrow: tail then head; Circle: centre then a point on the rim;
     *  Player: where he stood. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    TArray<FVector2D> ScreenPoints;

    /** The screen points on the field, where they reach it (a point above the horizon has
     *  none). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    TArray<FVector> FieldPoints;

    /** Circle: its radius on the field (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    float FieldRadiusCm = 0.f;

    /** Player: who. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    FName PlayerId;

    /** Drawn by the auto-annotation hook rather than by hand. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    bool bAuto = false;

    /** Player: the emphasis request lighting him up (UPSOverlayEmphasisSubsystem), while it
     *  lasts. */
    UPROPERTY(Transient)
    int32 EmphasisHandle = INDEX_NONE;
};

/** An annotated still: the frame and what was drawn on it, as it is saved. The screenshot
 *  taken beside it is the frame's ImageFile (empty when there was no viewport to take it). */
USTRUCT(BlueprintType)
struct FPSTelestration
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    FPSFilmFrame Frame;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    TArray<FPSTelestratorMark> Marks;

    /** Coaching notes from the auto-annotation hook, one per answer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    TArray<FString> Notes;
};

/** One mark an annotator suggests, in field terms (it never sees the screen). */
USTRUCT(BlueprintType)
struct FPSAnnotationSuggestion
{
    GENERATED_BODY()

    /** Freehand, Arrow, Circle or Player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    EPSTelestratorTool Tool = EPSTelestratorTool::Player;

    /** Player: who. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    FName PlayerId;

    /** Freehand: the path; Arrow: tail then head; Circle: its centre. World space (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    TArray<FVector> FieldPoints;

    /** Circle: its radius (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    float RadiusCm = 0.f;
};

/** What an annotator is shown of the play: the frame, the play's events, its situation. */
USTRUCT(BlueprintType)
struct FPSAnnotationRequest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Telestrator")
    int32 RequestId = 0;

    /** Every player and where he is in the frame. */
    UPROPERTY(BlueprintReadOnly, Category = "Telestrator")
    FPSFilmFrame Frame;

    /** The play's events, from the replay clip being analysed (empty in film view). */
    UPROPERTY(BlueprintReadOnly, Category = "Telestrator")
    TArray<FPSReplayEventRecord> Events;

    /** Down, distance, score and clock as the play began, when a replay clip says. */
    UPROPERTY(BlueprintReadOnly, Category = "Telestrator")
    FPlayState Situation;
};

/** How the telestrator draws (Data/telestrator.json; Architecture rule 4). Defaults equal the
 *  file. */
USTRUCT(BlueprintType)
struct FPSTelestratorTuning
{
    GENERATED_BODY()

    /** The field's height in the world (cm): drawings are pinned to this plane. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    float FieldHeightCm = 0.f;

    /** A freehand point closer than this to the last one kept is dropped (screen fraction). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    float MinPointSpacing = 0.005f;

    /** The most points one freehand stroke keeps. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    int32 MaxStrokePoints = 256;

    /** A player tap picks the player standing within this of it (screen fraction). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    float PlayerPickRadius = 0.04f;

    /** The most marks on one frame; a stroke past it is refused. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telestrator")
    int32 MaxMarks = 64;
};
