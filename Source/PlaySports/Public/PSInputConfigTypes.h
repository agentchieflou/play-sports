// PSInputConfigTypes.h - Epic 142/126: the input action catalog as authored in Data/input_actions.json
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputActionValue.h"
#include "PSInputConfigTypes.generated.h"

/** One physical key bound to a catalog action. Modifiers cover what a 1D key needs to
 *  drive a 2D action (W/S/A/D on Move); response curves and dead zones are Epic 127's
 *  FInputTuningRow, not per-binding data. */
USTRUCT(BlueprintType)
struct FPSInputKeyBinding
{
    GENERATED_BODY()

    /** Engine key name (EKeys), e.g. "W", "Mouse2D", "Gamepad_Left2D". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName Key;

    /** Route a 1D key onto the Y axis of a 2D action (forward/back on Move). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bSwizzleYX = false;

    /** Negate the key's value (back/left on Move). Applied after the swizzle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bNegate = false;
};

/** One action in the catalog: what it is, which contexts it lives in, and its keys.
 *  The same bindings apply in every context the action is declared for -- a physical
 *  button keeps one meaning per context (Specs/Input_Architecture.md section 1). */
USTRUCT(BlueprintType)
struct FPSInputActionDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName ActionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    EInputActionValueType ValueType = EInputActionValueType::Boolean;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FString Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FName> Contexts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSInputKeyBinding> Bindings;
};

/** A situation (on-field, world walking, ...) with its mapping-context priority. */
USTRUCT(BlueprintType)
struct FPSInputContextDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName ContextId;

    /** Higher priority contexts win when two active contexts bind the same key. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    int32 Priority = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FString Description;

    /** False for a context whose actions keep their authored keys: the menus read theirs
     *  through Slate, so remapping them could lock a player out (Epic 103.4). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bRemappable = true;
};

/** The player's own key for an action on one kind of device (Epic 103.4). It replaces every
 *  key the catalog gives the action for that kind of device. */
USTRUCT(BlueprintType)
struct FPSInputRemap
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName ActionId;

    /** True for the gamepad's key, false for the keyboard and mouse's. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    bool bGamepad = false;

    /** Engine key name (EKeys). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    FName Key;
};

/** Top-level shape of Data/input_actions.json. Loaded as a single JSON object by
 *  UPSDataIngestion::LoadInputCatalogFromJson. */
USTRUCT(BlueprintType)
struct FPSInputCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSInputContextDef> Contexts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    TArray<FPSInputActionDef> Actions;
};

/** Gamepad response tuning (Epic 127), authored in Data/input_tuning.json. UPSInputConfig
 *  turns the stick values into Enhanced Input modifiers on every gamepad stick binding, so
 *  dead zones and curves are data, not constants (Architecture rule 4). */
USTRUCT(BlueprintType)
struct FInputTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Radial dead zone: stick deflection below this reads as zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float StickDeadZoneLower = 0.25f;

    /** Deflection at or above this reads as full. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float StickDeadZoneUpper = 1.f;

    /** Response curve applied after the dead zone: 1 is linear, above 1 gives finer
     *  control near the centre of the stick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float StickResponseExponent = 1.f;

    /** How far an analog input must move before it counts as "the player picked up the
     *  gamepad" for active-device tracking, so stick drift never flips the device. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Input")
    float DeviceSwitchAnalogThreshold = 0.5f;
};
