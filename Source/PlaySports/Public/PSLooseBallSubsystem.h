// PSLooseBallSubsystem.h - Epic 17.4: the players play a blocked kick's loose ball
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSTelemetryBus.h"
#include "PSLooseBallSubsystem.generated.h"

class APSBall;
class APSPlayerPawn;

/** How the players play a loose ball (Data/loose_ball.json; Architecture rule 4). Distances
 *  in cm. */
USTRUCT(BlueprintType)
struct FPSLooseBallTuning : public FTableRowBase
{
    GENERATED_BODY()

    /** A blocked field goal comes loose this many yards behind the line, where it was held (a
     *  blocked punt's recoil is the special-teams model's). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    int32 BlockedFieldGoalYards = 7;

    /** The players this close to the loose ball go for it, whatever their job ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float ChaseRadius = 2000.f;

    /** ... and the nearest of them this close to it tries to take it (his fumble-recovery
     *  chance, the ball's catch tuning). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float RecoverRadius = 120.f;

    /** A defender with no opponent this close scoops it up and returns it; one with an
     *  opponent on him falls on it. The kicking team always falls on it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float ScoopClearRadius = 300.f;

    /** A muffed ball squirts this far ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float SquirtDistance = 250.f;

    /** ... and the player who muffed it can't try again for this long. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float RetrySeconds = 0.5f;

    /** Nobody has it by then: the officials blow it dead where it lies, the defense's ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float MaxLooseSeconds = 4.f;

    /** A return still going after this long is whistled dead where the returner is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LooseBall")
    float MaxReturnSeconds = 10.f;
};

/** Where a loose ball's play stands. */
enum class EPSLooseBallStage : uint8
{
    None,
    /** On the ground. */
    Loose,
    /** A defender scooped it up and is returning it. */
    Returning,
    /** Whistled dead. */
    Dead
};

/**
 * UPSLooseBallSubsystem plays a blocked kick's loose ball with the players on the field (Epic
 * 17.4's blocked-kick chaos). Epic 75's special-teams model decides that a kick is blocked;
 * when it is, UPSPlaySimulation announces it on UPSTelemetryBus (LooseBall, Blocked) and, if
 * this takes the ball live, waits for the players to finish the play:
 *
 *  - Loose: the ball comes out of the holder's hands (UPSPossessionComponent, through the pawn)
 *    and lies on the ground behind the line -- a punt's recoil, a field goal's hold spot.
 *  - The chase: every player within ChaseRadius goes for it, whatever his call
 *    (UPSDefenderAIComponent and UPSSkillPlayerAIComponent read GetChaseTarget).
 *  - Recovery: the nearest player to reach it tries to take it with his fumble-recovery chance
 *    (PSBallResolutionHelpers, the ball's catch tuning); a muff squirts it away. A defender in
 *    the clear scoops it up and returns it -- his teammates' job is done, the kicking team
 *    chases him -- a defender with an opponent on him, or anyone of the kicking team, falls on
 *    it. Taking it attaches the ball (APSBall) and gives him possession.
 *  - Dead: fallen on, the returner tackled or in the end zone, nobody has it after
 *    MaxLooseSeconds, or a return still going after MaxReturnSeconds. The Dead event carries
 *    the spot and who has the ball; the simulation makes it the kick's outcome.
 *
 * Every step is a LooseBall event. Rolls are seeded from the snap's situation. It steps at the
 * AI's decision rate from Tick; headless tests call UpdateLooseBall. Tuning:
 * Data/loose_ball.json.
 */
UCLASS()
class PLAYSPORTS_API UPSLooseBallSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSLooseBallTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FPSLooseBallTuning& InTuning);

    /** Problems with InTuning (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSLooseBallTuning& InTuning);

    /** One step: a try at the ball by the nearest player on it, the returner's progress, and the
     *  whistle when time is up. */
    void UpdateLooseBall(float DeltaSeconds);

    /** True while the ball is on the ground. */
    UFUNCTION(BlueprintPure, Category = "LooseBall")
    bool IsLoose() const { return Stage == EPSLooseBallStage::Loose; }

    /** True while a defender who scooped it up is returning it. */
    UFUNCTION(BlueprintPure, Category = "LooseBall")
    bool IsReturning() const { return Stage == EPSLooseBallStage::Returning; }

    /** Where Player should go now: the loose ball if he is within ChaseRadius of it, the
     *  returner if he is on the other side and within ChaseRadius of him. False when the loose
     *  ball isn't his business (his AI plays on as usual). */
    bool GetChaseTarget(const APSPlayerPawn* Player, FVector& OutTarget) const;

    /** The defender returning the ball, if one is. */
    APSPlayerPawn* GetReturner() const { return Returner.Get(); }

    /** The loose ball, while there is one. */
    APSBall* GetBall() const { return Ball.Get(); }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleLooseBall(const FPSTelemetryLooseBallEvent& Event);
    void HandleTackle(const FPSTelemetryTackleEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** Takes the ball out of the holder's hands and puts it on the ground YardsBehindLine
     *  behind the line. False when there is no ball to play. */
    bool MakeLoose(const FString& InKickType, int32 YardsBehindLine);

    void TryRecover(APSPlayerPawn* Player);
    void Recover(APSPlayerPawn* Player);
    void Squirt(APSPlayerPawn* Player);
    void BlowDead(const FVector& Spot, bool bKickingTeam, bool bTouchdown, const APSPlayerPawn* Holder);

    /** The kicking team's own goal line, where a return scores (world X). */
    float GetGoalLineX() const;

    void Publish(FPSTelemetryLooseBallEvent& Event);

    UPROPERTY(Transient)
    FPSLooseBallTuning Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TWeakObjectPtr<APSBall> Ball;
    TWeakObjectPtr<APSPlayerPawn> Returner;
    /** By player: when he may try again after a muff (LooseSeconds). */
    TMap<FObjectKey, float> RetryAt;

    FRandomStream Rolls;
    FString KickType;
    FVector LineOfScrimmage = FVector::ZeroVector;
    int32 SnapYardLine = 0;
    /** Since the ball came loose, and since the return began. */
    float LooseSeconds = 0.f;
    float ReturnSeconds = 0.f;
    float SinceUpdate = 0.f;
    EPSLooseBallStage Stage = EPSLooseBallStage::None;
    bool bTuningLoaded = false;
};
