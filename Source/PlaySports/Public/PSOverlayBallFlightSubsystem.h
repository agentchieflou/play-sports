// PSOverlayBallFlightSubsystem.h - Epic 32: follows the ball in flight for the pass and kick indicators
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSOverlayBallFlightTypes.h"
#include "PSPlatformTiers.h"
#include "PSTelemetryBus.h"
#include "PSOverlayBallFlightSubsystem.generated.h"

class APSBall;
class APSOverlayBallFlight;
class APSPlayerPawn;

/**
 * UPSOverlayBallFlightSubsystem is the ball-flight overlay's model (Epic 32): the predicted arc,
 * where the ball comes down, where the intended receiver will be by then, and for a kick where
 * it passes the uprights. APSOverlayBallFlight, which it spawns on the first flight, only draws
 * it.
 *
 *  - A pass starts with its Throw event on the bus. The arc comes from the ball's physics state
 *    at that instant -- its position, its projectile velocity and the gravity its projectile
 *    movement flies it under -- so it is the flight the ball will actually take until something
 *    touches it. It lands at the throw's catch height, the landing ring the receiver's catch
 *    radius (his capsule plus the ball).
 *  - A kick starts when the ball leaves its carrier in free flight while the game is in a kick
 *    phase (Kickoff, Punt, FieldGoal, from the simulation's GameState events). It comes down on
 *    the ground and is judged at the first goal posts ahead of it: good, wide left or right, or
 *    short.
 *  - Each step reads how far the ball is along the arc from its own position, and ends the
 *    flight when the ball leaves the arc (touched, deflected, bounced), is caught or comes down.
 *    The marks linger a moment (the style's LingerSeconds; ReadoutSeconds for a kick).
 *  - The receiver's lead is where he is now plus his velocity until the ball comes down.
 *  - Detail is the platform tier's OverlayDetail (see APSOverlayBallFlight).
 *
 * The look is Data/ball_flight_overlay.json. It ticks with its world; headless tests call
 * AdvanceTime and TrackFlight.
 */
UCLASS()
class PLAYSPORTS_API UPSOverlayBallFlightSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultStylePath();

    /** Replaces the style with JsonFilePath's, read through UPSDataIngestion. A style that fails
     *  ValidateStyle is refused and the current one kept. */
    bool LoadStyleFromJson(const FString& JsonFilePath);

    void SetStyle(const FPSBallFlightStyle& NewStyle);

    const FPSBallFlightStyle& GetStyle() const { return Style; }

    /** Problems with a style, one line each (empty when sound). */
    static TArray<FString> ValidateStyle(const FPSBallFlightStyle& InStyle);

    UFUNCTION(BlueprintCallable, Category = "Overlay")
    void SetOverlayDetail(EPSOverlayDetail InDetail);

    UFUNCTION(BlueprintPure, Category = "Overlay")
    EPSOverlayDetail GetOverlayDetail() const { return OverlayDetail; }

    /** What the overlay shows now. */
    UFUNCTION(BlueprintPure, Category = "Overlay")
    FPSBallFlightState GetFlight() const { return Flight; }

    /**
     * Starts following Ball's flight from its physics state now. ReceiverName (a pass's
     * intended target, by display name) gets the lead indicator; LandingHeight is where the
     * flight is judged to come down. False when the ball isn't in free flight.
     */
    bool TrackFlight(APSBall* Ball, EPSBallFlightKind Kind, const FString& ReceiverName, float LandingHeight);

    /** One step: follows the ball along its arc, notices a kick leaving the kicker, ends the
     *  flight, lets its marks linger, and redraws. The tick calls this; headless tests call it
     *  directly. */
    void AdvanceTime(float DeltaSeconds);

    /** The actor drawing the overlay; null until the first flight. */
    APSOverlayBallFlight* GetOverlayActor() const { return OverlayActor.Get(); }

    /** True for a ball that its projectile movement is flying: launched, not carried, not
     *  loose after a fumble. */
    static bool IsInFreeFlight(const APSBall& Ball);

private:
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandleCatch(const FPSTelemetryCatchEvent& Event);
    void HandleGameState(const FPSTelemetryGameStateEvent& Event);

    /** The flight is over: its marks linger, then go. */
    void EndFlight();
    void UpdateLead();
    void Redraw();
    APSBall* FindBall();
    APSPlayerPawn* FindPawn(const FString& DisplayName) const;
    float CatchRadiusFor(const APSPlayerPawn* Receiver, const APSBall& Ball) const;

    FPSBallFlightStyle Style;
    FPSBallFlightState Flight;
    EPSOverlayDetail OverlayDetail = EPSOverlayDetail::Full;

    TWeakObjectPtr<APSBall> TrackedBall;
    TWeakObjectPtr<APSPlayerPawn> Receiver;
    TWeakObjectPtr<APSOverlayBallFlight> OverlayActor;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** The simulation's play phase, from its latest GameState event. */
    FString Phase;
    /** Seconds since the flight started, for a ball with no speed across the ground. */
    float FlightClock = 0.f;
    float LingerRemaining = 0.f;
    /** Whether the ball was in free flight at the last step, so a kick starts on the change. */
    bool bBallWasInFlight = false;
};
