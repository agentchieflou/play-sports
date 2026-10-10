// PSCarrierMoveComponent.h - Epic 104.2: the ball carrier's move set
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PSCarrierMoveComponent.generated.h"

class APSPlayerPawn;

/** A ball carrier's move (Epic 104.2). */
UENUM(BlueprintType)
enum class EPSCarrierMove : uint8
{
    None,
    /** A hard cut to one side: tacklers whiff. */
    Juke,
    /** A spin through contact: tacklers slide off, at a cost in speed. */
    Spin,
    /** Lowering the shoulder: power through the tackler. */
    Truck,
    /** An arm bar that keeps a tackler off. */
    StiffArm,
    /** Leaping a low tackle. */
    Hurdle,
    /** Giving yourself up: down at the next contact, with no hit and no fumble. */
    Slide
};

/** One move as data (Data/carrier_moves.json; Architecture rule 4). */
USTRUCT(BlueprintType)
struct FPSCarrierMoveDef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    EPSCarrierMove Move = EPSCarrierMove::None;

    /** The catalog action (BallCarrier context) that does the move. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    FName ActionId;

    /** The rating the move runs on: Agility, Strength or Speed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    FName Attribute;

    /** Below this rating the carrier can't do the move at all. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float MinAttribute = 0.f;

    /** How long the move protects the carrier. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float WindowSeconds = 0.4f;

    /** How long the carrier is committed to the move once it starts: the animation's
     *  commitment window (Track D's animations will own it; until then it is data). No other
     *  move starts until it ends; a press that arrives meanwhile waits in
     *  UPSInputBufferComponent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float CommitSeconds = 0.3f;

    /** How long before the same move can be done again. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float CooldownSeconds = 1.f;

    /** Stamina it costs; a carrier without that much can't do it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float StaminaCost = 5.f;

    /** During the window a tackle succeeds this many times as often for a carrier rated 100
     *  (1 = no help); a lower rating helps proportionally less. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float TackleChanceScale = 1.f;

    /** The carrier's speed is multiplied by this as the move starts (a spin bleeds speed, a
     *  slide stops). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float SpeedRetained = 1.f;

    /** Sideways speed added (cm/s), toward the Move stick's side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float LateralSpeed = 0.f;

    /** Speed added straight ahead (cm/s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    float ForwardSpeed = 0.f;

    /** The carrier gives himself up: the next contact downs him with no hit and no fumble. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    bool bGivesUp = false;
};

USTRUCT(BlueprintType)
struct FPSCarrierMoveCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moves")
    TArray<FPSCarrierMoveDef> Moves;
};

/**
 * UPSCarrierMoveComponent is the ball carrier's move set (Epic 104.2): juke, spin, truck,
 * stiff-arm, hurdle and slide, each defined in Data/carrier_moves.json.
 *
 * A move is attribute-gated (a rating below MinAttribute can't do it) and costs stamina. It is
 * physics-coupled: it changes the carrier's velocity the moment it starts, so the movement
 * rules (FMovementTuningRow) carry it on from there. For WindowSeconds it scales the tackle
 * chance by TackleChanceScale, in proportion to the carrier's rating. For CommitSeconds the
 * carrier is committed to it and no other move starts (Epic 104.4).
 * UPSBallActionComponent::ResolveTackle reads GetTackleChanceMultiplier and HasGivenUp.
 *
 * Every APSPlayerPawn has one; a human's moves come through UPSCarrierInputComponent on the
 * player controller, and an AI could call TryMove the same way. It keeps its own clock (ticked,
 * or advanced by AdvanceTime in headless tests).
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSCarrierMoveComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSCarrierMoveComponent();

    static FString GetDefaultCatalogPath();

    /** The move set in use, loaded from the default path on first use. */
    const FPSCarrierMoveCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Problems with Catalog (empty when sound): duplicate moves or actions, an unknown
     *  attribute, out-of-range numbers. */
    static TArray<FString> ValidateCatalog(const FPSCarrierMoveCatalog& InCatalog);

    /** The move a catalog action does, or None. */
    EPSCarrierMove FindMoveForAction(FName ActionId);

    /** Does Move if the owner carries the ball, is rated for it, has the stamina, is not
     *  committed to another move, and it is off cooldown. Stick is the Move stick (X right,
     *  Y forward): a juke goes to its side. */
    UFUNCTION(BlueprintCallable, Category = "Moves")
    bool TryMove(EPSCarrierMove Move, FVector2D Stick);

    /** True while the carrier is committed to the move he started (its CommitSeconds). */
    UFUNCTION(BlueprintPure, Category = "Moves")
    bool IsCommitted() const { return Clock < CommittedUntil; }

    /** True while Move can't start only because of timing: the carrier is committed to a move
     *  or Move is cooling down. False when it could start now, or never could (no ball, a
     *  slide), so a buffered press waits only when waiting can help. */
    UFUNCTION(BlueprintPure, Category = "Moves")
    bool IsMoveBusy(EPSCarrierMove Move) const;

    /** True while a move protects the carrier. */
    UFUNCTION(BlueprintPure, Category = "Moves")
    bool IsMoveActive() const;

    UFUNCTION(BlueprintPure, Category = "Moves")
    EPSCarrierMove GetActiveMove() const { return IsMoveActive() ? ActiveMove : EPSCarrierMove::None; }

    /** What the active move does to a tackle's chance of success (1 with none). */
    UFUNCTION(BlueprintPure, Category = "Moves")
    float GetTackleChanceMultiplier() const;

    /** True once the carrier slid: the next contact downs him with no hit and no fumble. */
    UFUNCTION(BlueprintPure, Category = "Moves")
    bool HasGivenUp() const { return bGaveUp; }

    /** Clears the move, the cooldowns and the slide (the ball changed hands, a new play). */
    UFUNCTION(BlueprintCallable, Category = "Moves")
    void ResetMoves();

    /** Moves the component's clock on. Ticking does this; headless tests call it. */
    void AdvanceTime(float DeltaSeconds);

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    const FPSCarrierMoveDef* FindDef(EPSCarrierMove Move);
    static float GetRating(const APSPlayerPawn* Carrier, FName Attribute);
    APSPlayerPawn* GetCarrier() const;

    UPROPERTY(Transient)
    FPSCarrierMoveCatalog Catalog;

    /** The active move's definition, copied so the multiplier needs no lookup. */
    FPSCarrierMoveDef ActiveDef;
    EPSCarrierMove ActiveMove = EPSCarrierMove::None;
    float ActiveRating = 0.f;
    float Clock = 0.f;
    float ActiveUntil = -1.f;
    float CommittedUntil = -1.f;
    TMap<EPSCarrierMove, float> ReadyAt;
    bool bGaveUp = false;
    bool bCatalogLoaded = false;
};
