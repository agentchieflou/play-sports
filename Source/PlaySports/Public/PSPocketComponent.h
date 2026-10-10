// PSPocketComponent.h - Epic 71: the quarterback's pocket -- climb, slide, escape, scramble, and how sacks end
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "UObject/ObjectKey.h"
#include "PSPocketComponent.generated.h"

class APSPlayerPawn;

/** What the pocket asks of the quarterback right now. */
UENUM(BlueprintType)
enum class EPSPocketMove : uint8
{
    /** Clean: stand and read. */
    Hold,
    /** Pressure off the edges: step up. */
    Climb,
    /** Pressure inside, from one side: slide away from it. */
    SlideLeft,
    SlideRight
};

/** The pocket as the quarterback reads it (UPSPocketComponent::ReadPocket). */
USTRUCT(BlueprintType)
struct FPSPocketRead
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    EPSPocketMove Move = EPSPocketMove::Hold;

    /** Where to move in the pocket (unit, on the ground; zero to hold). */
    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    FVector Direction = FVector::ZeroVector;

    /** The summed pressure of rushers around him (a free rusher at his feet counts 1). */
    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    float Pressure = 0.f;

    /** The away-from-pressure push across the field: its sign is the side with room. */
    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    float LateralPush = 0.f;

    /** The nearest rusher no blocker holds, and how far he is. */
    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    APSPlayerPawn* NearestFreeRusher = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    float NearestFreeDistance = 0.f;

    /** The pocket has caved in: a free rusher is on him, or the pressure is too much. Time to
     *  get out. */
    UPROPERTY(BlueprintReadOnly, Category = "Pocket")
    bool bCollapsed = false;
};

/** Pocket and scramble tuning (Data/pocket_tuning.json; Architecture rule 4). Distances in cm,
 *  chances 0-1. */
USTRUCT(BlueprintType)
struct FPocketTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Rushers within this distance of the quarterback press on the pocket. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float PocketRadius = 400.f;

    /** A rusher a blocker holds presses this much as a free one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float EngagedPressureWeight = 0.5f;

    /** A rusher further than this across the field from the quarterback comes off the edge. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float EdgeWidth = 200.f;

    /** Below this pressure the quarterback holds his spot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float MinPressure = 0.2f;

    /** At this pressure the pocket has collapsed ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float CollapsePressure = 1.2f;

    /** ... as it has with a free rusher this close. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float EscapeRadius = 250.f;

    /** He climbs no closer to the line of scrimmage than this. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ClimbStopDistance = 150.f;

    /** A free rusher this close is about to sack him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float SackImminentRadius = 150.f;

    /** A sacking rusher who comes from behind the quarterback (his blind side) tries to strip
     *  the ball: this chance ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float StripBaseChance = 0.25f;

    /** ... plus this per point of Strength the rusher has on him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float StripStrengthWeight = 0.005f;

    /** A quarterback who sees the sack coming throws the ball away from this Awareness on ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ThrowawayMinAwareness = 50.f;

    /** ... and from this Awareness on he knows a throwaway would be grounding, and takes the
     *  sack instead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float GroundingAvoidAwareness = 80.f;

    /** The tackle box: this far either side of the ball. A throwaway from inside it needs a
     *  receiver near where it lands. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float TackleBoxHalfWidth = 400.f;

    /** A receiver within this distance is someone to throw it away at (at his feet) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ThrowawayReceiverRange = 1500.f;

    /** ... this far short of him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ThrowawayShort = 200.f;

    /** With nobody to throw at, the ball goes this far past the line ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ThrowawayDepth = 300.f;

    /** ... and this far out toward his side's sideline. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ThrowawayWidth = 1500.f;

    /** A scrambling quarterback runs mostly across the field, this much upfield. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleForwardBias = 0.35f;

    /** After this long scrambling he stops looking to throw on the run. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleMaxSeconds = 3.f;

    /** With no defender this far ahead of him in his lane, he tucks it and runs upfield ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float RunLaneClearance = 500.f;

    /** ... the lane being this wide either side of him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float RunLaneWidth = 200.f;

    /** Running past the line, he slides when a defender ahead gets this close ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float SlideTriggerRadius = 250.f;

    /** ... once he has gained this much past the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float SlideMinGain = 300.f;

    /** Scramble drill (Epic 17's rules): a receiver works this far upfield of the scrambling
     *  quarterback ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleDrillDepth = 400.f;

    /** ... this far toward the side he scrambles to ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleDrillWidth = 400.f;

    /** ... give or take this much, so the receivers spread out. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleDrillJitter = 150.f;

    /** A receiver this far downfield of the quarterback is deep: he keeps going ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleDeepDepth = 1000.f;

    /** ... this much further, toward the scramble side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pocket")
    float ScrambleDeepRunOn = 500.f;
};

/**
 * Pure pocket rules, shared by UPSPocketComponent and the scramble drill (UPSPlayOrchestrator).
 */
namespace PSPocket
{
    /** True while Location is inside the tackle box around LineOfScrimmage. */
    PLAYSPORTS_API bool IsInsideTackleBox(const FVector& Location, const FVector& LineOfScrimmage, const FPocketTuningRow& Tuning);

    /** The chance a rusher strips the ball from the quarterback he sacks from the blind side. */
    PLAYSPORTS_API float StripChance(const FPlayerAttributes& Rusher, const FPlayerAttributes& Passer, const FPocketTuningRow& Tuning);

    /** Where a receiver breaks to in the scramble drill: upfield of the quarterback toward his
     *  side, or, already deep, further on toward it. Jitter is in [-1, 1] on each axis. */
    PLAYSPORTS_API FVector ScrambleDrillSpot(const FVector& Receiver, const FVector& Passer, float ScrambleSide, const FVector2D& Jitter, const FPocketTuningRow& Tuning);
}

/**
 * UPSPocketComponent is the quarterback's sense of the pocket and the scramble (Epic 71). It
 * reads the live line play -- which rushers are free, which a blocker holds, where they come
 * from -- and says whether to hold, climb, slide or get out. Once he is out it steers the
 * scramble (across the field, up a clear lane), resolves a sack about to land (a blind-side
 * strip attempt, or an Awareness-gated throwaway with its grounding risk) and, past the line,
 * a protective slide (UPSCarrierMoveComponent's Slide).
 *
 * UPSSkillPlayerAIComponent makes the decisions with it; this says what the pocket allows.
 * Its own calls -- the escape (which starts the scramble drill), throwaways, grounding,
 * strips, slides -- go on UPSTelemetryBus as Pocket events. Tuning: Data/pocket_tuning.json.
 * Rolls come from the play's seed (ResetPlay).
 *
 * APSOffenseController owns one.
 */
UCLASS(ClassGroup = "PlaySports", BlueprintType, meta = (BlueprintSpawnableComponent))
class PLAYSPORTS_API UPSPocketComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPSPocketComponent();

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPocketTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FPocketTuningRow& InTuning);

    /** This play's tuning for Passer: the tuning as loaded, scaled by Data/player_dna.json's Pocket
     *  bindings for his style (Epic 79), then by the difficulty tier when he plays for the CPU
     *  (Epic 84). UPSSkillPlayerAIComponent calls it as each play starts. */
    void ApplyPlayTuning(const APSPlayerPawn* Passer);

    /** A new play: not scrambling, no strips tried, rolls seeded. */
    void ResetPlay(int32 Seed);

    /** The pocket around Passer now. */
    FPSPocketRead ReadPocket(const APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage);

    /** A sack about to land: a free rusher from the blind side tries to strip the ball; one he
     *  sees coming may get the ball thrown away (Awareness-gated; inside the tackle box with
     *  nobody near the throw it is a grounding risk, which a sharp enough quarterback won't
     *  take). True when the ball has left his hands. */
    bool ResolveImminentSack(APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage);

    /** He leaves the pocket at TimeSinceSnap: picks his side (the one with room) and
     *  announces the escape, which sends the receivers into the scramble drill. */
    void BeginScramble(const APSPlayerPawn* Passer, const FPSPocketRead& Read, float TimeSinceSnap);

    UFUNCTION(BlueprintPure, Category = "Pocket")
    bool IsScrambling() const { return bScrambling; }

    UFUNCTION(BlueprintPure, Category = "Pocket")
    float GetScrambleSide() const { return ScrambleSide; }

    /** True while the scramble is young enough to still look for a throw. */
    bool CanThrowOnTheRun(float TimeSinceSnap);

    /** Where a scrambling quarterback runs behind the line: up a clear lane, else across the
     *  field to his side, away from the nearest defender. */
    FVector SteerScramble(const APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns);

    /** Past the line he is a runner: he slides ahead of a closing defender once he has gained
     *  SlideMinGain. True when he slid. */
    bool MaybeSlide(APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage);

    /** The defender a carrier past the line should slide ahead of, or null: the nearest
     *  opponent, ahead of him and within SlideTriggerRadius, once he has gained SlideMinGain.
     *  MaybeSlide and the human's auto-slide assist (UPSDifficultySubsystem) both read it. */
    static const APSPlayerPawn* FindSlideThreat(const FPocketTuningRow& Settings, const APSPlayerPawn* Carrier, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage);

private:
    void Publish(EPSPocketEventKind Kind, const APSPlayerPawn* Passer, const APSPlayerPawn* Defender, bool bSuccess);

    UPROPERTY(Transient)
    FPocketTuningRow Tuning;

    /** The tuning as loaded (or set); Tuning is this with the passer's DNA applied. */
    UPROPERTY(Transient)
    FPocketTuningRow BaseTuning;

    /** Rushers who already tried to strip the ball this play. */
    TSet<FObjectKey> StripTried;
    FRandomStream Rolls;
    float ScrambleSide = 1.f;
    float ScrambleStartedAt = 0.f;
    bool bScrambling = false;
    bool bTuningLoaded = false;
};
