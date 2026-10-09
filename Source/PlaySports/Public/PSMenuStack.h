// PSMenuStack.h - Epic 101: the screen stack every front-end and in-game menu sits on
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSMenuTypes.h"
#include "PSMenuStack.generated.h"

DECLARE_MULTICAST_DELEGATE_ThreeParams(FPSMenuStackChanged, FName /* PreviousTop */, FName /* NewTop */, EPSMenuTransition /* Transition */);

/**
 * UPSMenuStack is the navigation model: an ordered stack of screen IDs with change events.
 * It knows nothing about widgets or the catalog -- UPSMenuComponent decides what Back may do
 * and presents the top screen -- so navigation is testable without a viewport.
 */
UCLASS(BlueprintType)
class PLAYSPORTS_API UPSMenuStack : public UObject
{
    GENERATED_BODY()

public:
    /** Opens ScreenId on top. False (and no event) when it is already the top screen. */
    bool Push(FName ScreenId);

    /** Closes the top screen. False when the stack is empty. */
    bool Pop();

    /** Swaps the top screen for ScreenId (pushes when empty). */
    void Replace(FName ScreenId);

    /** Closes every screen. */
    void Clear();

    FName Top() const { return Screens.Num() > 0 ? Screens.Last() : NAME_None; }
    int32 Depth() const { return Screens.Num(); }
    bool IsEmpty() const { return Screens.Num() == 0; }
    const TArray<FName>& GetScreens() const { return Screens; }

    FPSMenuStackChanged OnChanged;

private:
    UPROPERTY(Transient)
    TArray<FName> Screens;
};
