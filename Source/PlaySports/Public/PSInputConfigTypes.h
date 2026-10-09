// PSInputConfigTypes.h - Epic 142/126: the input action catalog as authored in Data/input_actions.json
#pragma once

#include "CoreMinimal.h"
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
