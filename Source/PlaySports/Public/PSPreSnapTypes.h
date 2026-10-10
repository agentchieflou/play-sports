// PSPreSnapTypes.h - Epic 66: the offense's pre-snap calls and their tuning
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PSPreSnapTypes.generated.h"

/** Where an eligible receiver lines up, which decides the routes he may be hot-routed to. */
UENUM(BlueprintType)
enum class EPSReceiverAlignment : uint8
{
    /** A wide receiver split outside SlotMaxSplit. */
    Wide,
    /** A wide receiver inside SlotMaxSplit. */
    Slot,
    /** A tight end. */
    Tight,
    /** A running back. */
    Backfield
};

/** A back's or tight end's protection call for this snap. */
UENUM(BlueprintType)
enum class EPSProtectionCall : uint8
{
    /** As the play has him. */
    AsCalled,
    /** Kept in to block although the play gives him a route. */
    Block,
    /** Released into a route although the play has him blocking. */
    Release
};

/** Which way the offensive line slides its protection. */
UENUM(BlueprintType)
enum class EPSSlideDirection : uint8
{
    None,
    /** Toward -Y, the passer's left as he faces upfield. */
    Left,
    /** Toward +Y. */
    Right
};

/** The routes a receiver of one alignment may be hot-routed to. */
USTRUCT(BlueprintType)
struct FPSHotRouteSet
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    EPSReceiverAlignment Alignment = EPSReceiverAlignment::Wide;

    /** Route library IDs (Data/sample_routes.json), in the order a hot-route button cycles them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    TArray<FName> Routes;

    /** The route a blocker of this alignment runs when he is released. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName ReleaseRoute;
};

/** One player's pre-snap changes for this snap. */
USTRUCT(BlueprintType)
struct FPSPreSnapPlayerAdjustment
{
    GENERATED_BODY()

    /** The hot route replacing his called route; NAME_None keeps the call. */
    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    FName HotRouteId;

    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    EPSProtectionCall Protection = EPSProtectionCall::AsCalled;
};

/** What the offense can see of the defense before the snap. With an offensive line to read
 *  against, the shell and the blitz are what the defense shows (UPSDefenderPreSnapSubsystem,
 *  Epic 67), which a disguise makes differ from what it plays; without one they fall back to
 *  the call. */
USTRUCT(BlueprintType)
struct FPSDefensiveLook
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    FString Front;

    /** The shown safety structure ("TwoHigh", "SingleHigh", "ZeroHigh"), or the call's shell
     *  with no line to read against. */
    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    FString CoverageShell;

    /** Defenders in the box: within BoxWidth of the ball across the field and BoxDepth of the
     *  line down it. */
    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    int32 BoxCount = 0;

    /** Of those, how many are left (-Y) and right (+Y) of the ball. */
    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    int32 BoxLeft = 0;

    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    int32 BoxRight = 0;

    /** Linebackers or backs are walked up to the line (or, with no line to read against, the
     *  call is a blitz). */
    UPROPERTY(BlueprintReadOnly, Category = "PreSnap")
    bool bShowsBlitz = false;
};

/** Pre-snap tuning (Data/presnap_tuning.json; Architecture rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FPreSnapTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** One set per alignment: what each receiver may be hot-routed to and released into. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    TArray<FPSHotRouteSet> HotRouteSets;

    /** A wide receiver lined up no further than this from the ball is in the slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float SlotMaxSplit = 700.f;

    /** A man in motion runs across the formation to this far on the other side of the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float MotionEndSplit = 300.f;

    /** How close to his spot counts as the motion (or a travelling defender) arriving. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float MotionArrivalRadius = 50.f;

    /** In man coverage, the defender lined up within this distance across the field of the man
     *  in motion travels with him -- the man-coverage tell. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float ManTravelLateralRadius = 300.f;

    /** A sliding lineman looks for his man this far toward the slide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float SlideAimOffset = 200.f;

    /** The box: this far either side of the ball ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float BoxWidth = 600.f;

    /** ... and this far off the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float BoxDepth = 700.f;

    /** A CPU quarterback reads the defense before the snap only with at least this Awareness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float CpuReadMinAwareness = 60.f;

    /** A run into this many defenders in the box (or into a blitz look) is checked out of, to a
     *  pass in the same formation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    int32 HeavyBoxCount = 8;

    /** A pass against this few in the box and no blitz look is checked out of, to a run. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    int32 LightBoxCount = 5;

    /** Against a blitz look the CPU sends his slot receiver on this quick route ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName BlitzHotRoute = FName(TEXT("Slant"));

    /** ... and keeps a back with a route in to block. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bCpuKeepsBackInVsBlitz = true;

    /** On a pass the CPU motions his slot receiver across to see man or zone. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bCpuMotionOnPass = true;

    /** The catalog actions of the human's pre-snap controls (the PreSnap context). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName AudibleAction = FName(TEXT("Audible"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName SelectAction = FName(TEXT("PreSnapSelect"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName HotRouteAction = FName(TEXT("HotRoute"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName MotionAction = FName(TEXT("Motion"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName SlideAction = FName(TEXT("SlideProtection"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName ProtectionAction = FName(TEXT("BlockRelease"));
};
