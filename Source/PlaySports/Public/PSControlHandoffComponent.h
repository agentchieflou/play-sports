// PSControlHandoffComponent.h - Epic 30: which player the human's switch and pick buttons go to
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSInputConfigTypes.h"
#include "PSPlayerPawn.h"
#include "PSControlHandoffComponent.generated.h"

class APSPlayerController;

/** Player-switch tuning (Data/control_handoff.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FControlHandoffTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** A switch press this soon after the last one moves on to the next player in the same
     *  nearest-to-the-ball order instead of ranking again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
    float CycleWindowSeconds = 1.5f;

    /** Pre-snap catalog actions that pick the next teammate to the left and to the right
     *  across the field (PreSnap context). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
    FName PickLeftAction = TEXT("PickPlayerLeft");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
    FName PickRightAction = TEXT("PickPlayerRight");
};

/**
 * UPSControlHandoffComponent decides which player the human's control moves to (Epic 30);
 * APSPlayerController::TakeControlOf moves it, and publishes the change on the bus.
 *
 *  - Switch (SwitchPlayer): the ball carrier when the human's side has the ball; otherwise
 *    the teammate nearest the ball. Pressed again within CycleWindowSeconds it moves on to
 *    the next nearest, and so on around, in the order of the first press.
 *  - Direct pick, before the snap only: PickPlayerLeft and PickPlayerRight move to the
 *    nearest teammate on that side across the field (left is -Y, facing upfield +X), and
 *    PickPlayer takes a named teammate.
 *
 * Head-to-head rules (Epic 107) arrive as two switches UPSVersusSubsystem sets per seat:
 * bSwitchDuringPlay off holds a player to the one they had at the snap (switching to their own
 * side's ball carrier after a takeaway still works), and bPreSnapPicks off disables the picks.
 * Another human's player is never taken (APSPlayerController::TakeControlOf).
 *
 * Downed players are never picked. Whether the play is live comes from the controller's
 * UPSPlayContextComponent; who has the ball from the possession component (rule 6). Pick
 * presses arrive through the controller's UPSInputBufferComponent.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSControlHandoffComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSControlHandoffComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FControlHandoffTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Problems with InTuning (empty when sound): a negative window; with Catalog, a pick
     *  action that isn't a Boolean action in the PreSnap context. */
    static TArray<FString> ValidateTuning(const FControlHandoffTuningRow& InTuning, const FPSInputCatalog* Catalog = nullptr);

    /** Listens for the pick actions on the controller's input buffer. Idempotent. */
    void BindToController();

    /** Teammates in switch order for a ball at BallLocation: the side's ball carrier first,
     *  then every other teammate that isn't downed, nearest the ball first. The side is the
     *  controlled pawn's, or the controller's HumanSide. The controlled pawn is listed only
     *  when it is the carrier. */
    TArray<APSPlayerPawn*> RankSwitchCandidates(const FVector& BallLocation) const;

    /** One switch press at time Now (seconds). False when there is nobody better to take. */
    bool CycleSwitch(const FVector& BallLocation, double Now);

    /** The SwitchPlayer press: CycleSwitch at the ball's location and the world's time. */
    UFUNCTION(BlueprintCallable, Category = "Control")
    bool SwitchPlayer();

    /** Before the snap: control to the nearest teammate across the field on Direction's side
     *  (negative left, positive right). False during the play or with nobody there. */
    UFUNCTION(BlueprintCallable, Category = "Control")
    bool PickAcross(int32 Direction);

    /** Before the snap: control to the teammate with PlayerId. */
    UFUNCTION(BlueprintCallable, Category = "Control")
    bool PickPlayer(FName PlayerId);

    /** True from the snap to the whistle, as the controller's play context sees it. */
    UFUNCTION(BlueprintPure, Category = "Control")
    bool IsPlayLive() const;

    /** Whether the switch works once the ball is snapped. Off (a head-to-head defense under
     *  Data/versus_rules.json), a press during the play only ever goes to the side's own ball
     *  carrier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
    bool bSwitchDuringPlay = true;

    /** Whether the pre-snap picks (PickAcross, PickPlayer) work. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Control")
    bool bPreSnapPicks = true;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void HandleActionPressed(FName ActionId, float HeldSeconds);

    APSPlayerController* GetPlayerController() const;

    /** The controlled side's teammates that can be picked: on the field and not downed. */
    TArray<APSPlayerPawn*> GetPickableTeammates() const;

    FControlHandoffTuningRow Tuning;
    bool bTuningLoaded = false;

    /** The order the first press of a run of switch presses ranked. */
    TArray<TWeakObjectPtr<APSPlayerPawn>> CycleOrder;
    int32 CycleIndex = INDEX_NONE;
    double LastSwitchAt = -1.0e9;
};
