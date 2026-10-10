// PSDefenderPreSnapTypes.h - Epic 67: the defense's pre-snap look and its tuning
#pragma once

#include "CoreMinimal.h"
#include "PSDefenderPreSnapTypes.generated.h"

/** How the defense's pre-snap look departs from its call this snap. */
USTRUCT(BlueprintType)
struct FPSDefensiveDisguise
{
    GENERATED_BODY()

    /** Show the other safety structure -- two-high for a single-high call, single-high for a
     *  two-high one -- and rotate to the real one at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bDisguiseShell = false;

    /** Linebackers who aren't coming walk up as if they were, and drop at the snap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bShowBlitz = false;

    /** The blitzers line up as if in coverage and creep up late. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bCreep = false;
};

/** How many deep safeties a coverage shell plays (the play's CoverageShell). */
USTRUCT(BlueprintType)
struct FPSShellSafeties
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FString Shell;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    int32 DeepSafeties = 1;
};

/** Defensive pre-snap tuning (Data/defensive_presnap.json; Architecture rule 4). Depths are cm
 *  past the front of the offensive line, widths cm across the field from its centre. */
USTRUCT(BlueprintType)
struct FPSDefensivePreSnapTuning
{
    GENERATED_BODY()

    /** Each shell's deep safeties; a shell not listed plays one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    TArray<FPSShellSafeties> ShellSafeties;

    /** Two-high: both safeties this deep, this far either side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float TwoHighDepth = 1300.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float TwoHighWidth = 500.f;

    /** Single-high: one safety this deep in the middle ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float SingleHighDepth = 1400.f;

    /** ... the other rolled down into the box, this deep and this far to his side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float RobberDepth = 550.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float RobberWidth = 250.f;

    /** The offense counts a defender at least this deep as a deep safety. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float DeepSafetyDepth = 1000.f;

    /** A linebacker or back showing blitz walks up to this depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float ShowBlitzDepth = 150.f;

    /** The offense reads a blitz from a linebacker or back this close to the line and within
     *  BlitzLookWidth of its centre. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float BlitzLookDepth = 250.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float BlitzLookWidth = 600.f;

    /** How many linebackers, nearest the ball, walk up to show a blitz that isn't coming. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    int32 ShowBlitzCount = 2;

    /** Creeping blitzers start walking up this long after the defense lines up ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float CreepDelaySeconds = 0.8f;

    /** ... at this fraction of their speed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float CreepSpeedScale = 0.5f;

    /** At Awareness 0 a disguising safety lines up this fraction of the way from his shown spot
     *  to his real one, giving the disguise away; at 100 he holds it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float MaxDisguiseLeak = 0.6f;

    /** How often a CPU defense disguises its shell, shows a blitz that isn't coming, or creeps
     *  a real one: from a conservative coach (AggressionScore 0) to an aggressive one (1),
     *  times the involved defenders' average Awareness / 100. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float DisguiseChanceConservative = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float DisguiseChanceAggressive = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float ShowBlitzChanceConservative = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float ShowBlitzChanceAggressive = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float CreepChanceConservative = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    float CreepChanceAggressive = 0.5f;

    /** On a CPU man-coverage call, its best defensive back shadows the offense's best
     *  receiver. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    bool bCpuShadowsTopReceiver = true;

    /** The human defense's pre-snap buttons: catalog actions of the PreSnap context, shared
     *  with the offense's (Specs/Input_Architecture.md). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName AudibleAction = FName(TEXT("Audible"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName SelectAction = FName(TEXT("PreSnapSelect"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName ShadowAction = FName(TEXT("HotRoute"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName ShowBlitzAction = FName(TEXT("Motion"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName DisguiseAction = FName(TEXT("SlideProtection"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PreSnap")
    FName CreepAction = FName(TEXT("BlockRelease"));
};
