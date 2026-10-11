// PSRenderCaptureTypes.h - Lane V1: the fixed views CI renders the game scene from, and what a capture reports
#pragma once

#include "CoreMinimal.h"
#include "PSPlayerAttributes.h"
#include "PSRenderCaptureTypes.generated.h"

/** What a view's camera and target positions are measured from. */
UENUM(BlueprintType)
enum class EPSRenderViewAnchor : uint8
{
    /** The field's frame (PSField): X is the yard line from the offense's own goal line (0 to
     *  100), Y yards from the middle of the field, Z yards above the ground. */
    Field,
    /** One player on the field, picked by role and index: X yards downfield of him (+X, the way
     *  the offense attacks), Y yards across, Z yards above the ground he stands on. */
    Player
};

/** One fixed camera the capture renders a PNG from (Data/render_views.json). */
USTRUCT(BlueprintType)
struct FPSRenderView
{
    GENERATED_BODY()

    /** Unique; the PNG is named after it ("<ViewId>.png"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    FName ViewId;

    /** What the view is for, for people reading the index. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    EPSRenderViewAnchor Anchor = EPSRenderViewAnchor::Field;

    /** A Player view's player: the role ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    EPlayerRole PlayerRole = EPlayerRole::WideReceiver;

    /** ... and which of that role's players, counted from the -Y sideline (0 is the one with the
     *  lowest Y). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 PlayerIndex = 0;

    /** The camera's position, in yards, in the anchor's frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    FVector CameraYards = FVector::ZeroVector;

    /** The point the camera looks at, in yards, in the anchor's frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    FVector TargetYards = FVector(1.f, 0.f, 0.f);

    /** Horizontal field of view. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float FieldOfViewDegrees = 50.f;
};

/** How the capture runs and what it renders (Data/render_views.json; Architecture rule 4).
 *  Defaults equal the file. */
USTRUCT(BlueprintType)
struct FPSRenderCaptureSettings
{
    GENERATED_BODY()

    /** The resolution the game renders at (the workflow also passes -ResX/-ResY). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 ResolutionX = 1920;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 ResolutionY = 1080;

    /** Players the match must have on the field before the capture starts (Play Now lines up
     *  22). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 ExpectedPlayers = 22;

    /** How long to wait for the level and the match; past it the views are still rendered and
     *  the run fails. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float MatchTimeoutSeconds = 300.f;

    /** How long to wait for shaders and assets still compiling (the editor binary compiles them
     *  on demand; a cold cache takes long). Past it the views are rendered as they are and the
     *  run fails. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float CompileTimeoutSeconds = 2400.f;

    /** Frames and seconds (both) rendered from the first view's camera before any view settles,
     *  for shaders, auto exposure, Lumen, virtual shadow maps and streaming to settle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 WarmupFrames = 600;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float WarmupSeconds = 10.f;

    /** Frames and seconds (both) each view renders before its PNG, for the camera cut's TSR
     *  history and Lumen to settle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 SettleFrames = 180;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float SettleSeconds = 3.f;

    /** The last frames of each view's settle whose GPU time is averaged (no more than
     *  SettleFrames). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 MeasureFrames = 90;

    /** Frames to wait for a requested PNG to reach the disk before the view fails. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    int32 ScreenshotTimeoutFrames = 120;

    /** Take each view only while the play is pre-snap, so both elevens stand lined up: the
     *  match plays on while the capture runs, and a snap during a view (a PhaseChange on the
     *  bus) starts that view again at the next pre-snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    bool bCaptureOnlyPreSnap = true;

    /** Seconds each view may spend streaming in the textures and meshes it sees before it
     *  settles (0: leave streaming to the engine). Streaming stays within the engine's pool. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float StreamingWaitSeconds = 5.f;

    /** The whole run's limit; past it the capture writes what it has and exits 1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    float TotalTimeoutSeconds = 3600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Render")
    TArray<FPSRenderView> RenderViews;
};

/** One view's result: its PNG and what rendering it cost. */
USTRUCT(BlueprintType)
struct FPSRenderShot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FName ViewId;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString Description;

    /** The PNG's name in the output folder. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString FileName;

    /** The PNG reached the disk. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    bool bWritten = false;

    /** The viewport's size when the PNG was taken. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    int32 ResolutionX = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    int32 ResolutionY = 0;

    /** GPU time per frame over the view's measured frames: mean and worst, in ms (0 when the RHI
     *  reports none). */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    float GpuMs = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    float GpuMsMax = 0.f;

    /** Whole-frame time over the same frames, in ms. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    float FrameMs = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FVector CameraLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FRotator CameraRotation = FRotator::ZeroRotator;

    /** Times a snap interrupted the view and it started again (bCaptureOnlyPreSnap). */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    int32 Restarts = 0;

    /** Why the view has no PNG, or what was wrong when it was taken; empty when all went well. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString Error;
};

/** A whole capture: written as index.json beside the PNGs. */
USTRUCT(BlueprintType)
struct FPSRenderCaptureReport
{
    GENERATED_BODY()

    /** Every view has its PNG and nothing failed: the run exits 0. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    bool bPassed = false;

    /** The commit the build was made from (-PSRenderCaptureCommit=), for comparing runs. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString Commit;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString MapName;

    /** The RHI and the GPU adapter that rendered. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString RHIName;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    FString AdapterName;

    /** Players on the field when the capture started. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    int32 PlayersOnField = 0;

    /** Seconds from the capture's start until the match was ready, and until compilation had
     *  finished. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    float MatchReadySeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    float CompileDoneSeconds = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Render")
    TArray<FPSRenderShot> Shots;

    /** One line per problem; empty on a pass. */
    UPROPERTY(BlueprintReadOnly, Category = "Render")
    TArray<FString> Failures;
};
