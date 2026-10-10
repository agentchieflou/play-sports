// PSPlayRecognitionSubsystem.h - Epic 80: defenders read formations and keys at attribute-gated speed
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSPlayRecognitionTypes.h"
#include "PSTelemetryBus.h"
#include "PSPlayRecognitionSubsystem.generated.h"

class APSPlayerPawn;

/**
 * UPSPlayRecognitionSubsystem is how the defense reads the offense (Epic 80), the one authority
 * on what each defender has recognized:
 *
 *  - The formation (80.1): at the snap it classifies the offense's alignment, read from the field
 *    (UPSAIFieldSnapshot): personnel, the receivers' splits and strength, the quarterback's depth
 *    and the backfield set, named by the first matching class in Data/play_recognition.json, whose
 *    run lean is what the look says. A man in motion counts where he is at the snap.
 *  - The keys (80.2): each look after the snap reads the line (its linemen firing off or setting
 *    to pass) and the backfield (a drop, a hand-off, backs flowing downhill) and notes when each
 *    key first showed. A defender's diagnosis is the keys
 *    he has had time to read: a true key beats the backfield's flow, which a fake shows too.
 *  - The speed (80.3): a defender reads in his reaction (UPSDefenderAIComponent::GetReactionSeconds,
 *    from his Awareness, with his style and the difficulty applied) times the read's scale, which
 *    Data/player_dna.json binds by style (Target "Recognition"): a ball hawk breaks on throws
 *    sooner and bites on fakes longer. A read against what he expects -- the formation's lean,
 *    pulled toward the offense's recent run share (UPSDeceptionSubsystem::GetRunShare) -- takes
 *    longer. Elite defenders read the drop and jump the throw; poor ones bite on fakes.
 *  - The bite (80.4, replacing Epic 72's interim chance): a run-fit defender bites on a play-action
 *    fake when he hasn't seen through it by the time it is sold, and holds for the rest of his
 *    read (GetBiteSeconds). UPSDeceptionSubsystem asks as it sells the fake.
 *
 * UPSDefenderAIComponent plays the diagnosis: reading pass a run-fit defender drops, reading run
 * he fills his gap while the quarterback still has the ball, and he breaks on a throw in his
 * throw read. The formation and every defender's new diagnosis go on UPSTelemetryBus
 * (Recognition): the formation on the first look after the snap, never during the snap's own
 * broadcast. Looks come from the defenders' decisions, at their pace (headless tests call
 * Observe and GetDiagnosis with their own clock). Tuning: Data/play_recognition.json.
 */
UCLASS()
class PLAYSPORTS_API UPSPlayRecognitionSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** World's recognition subsystem, or null. */
    static UPSPlayRecognitionSubsystem* Get(const UWorld* World);

    /** The tuning in use, loaded from the default path on first use. */
    const FPSPlayRecognitionTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests, a mode with its own) and forgets this play's read
     *  times. */
    void SetTuning(const FPSPlayRecognitionTuning& InTuning);

    // --- The formation ---

    /** The offense's formation as it stands now, read from the field. */
    FPSFormationRead ReadFormation();

    /** The formation the defense read at this play's snap. */
    UFUNCTION(BlueprintPure, Category = "Recognition")
    const FPSFormationRead& GetFormationRead() const { return FormationRead; }

    /** How much the defense expects a run this play, 0-1: the formation's lean pulled toward the
     *  offense's recent run share. */
    float GetExpectedRunShare();

    // --- The keys ---

    /** One look at the keys, TimeSinceSnap seconds after the snap: each key the field shows is
     *  noted the first time it shows. A look no later than the last one sees nothing new. */
    void Observe(float TimeSinceSnap);

    /** The keys this play has shown so far, in the order they first showed. */
    const TArray<FPSKeySighting>& GetSightings() const { return Sightings; }

    // --- The defenders ---

    /** How long Defender takes over each read this play (worked out once a play). */
    FPSDefenderReadTimes GetReadTimes(const APSPlayerPawn* Defender);

    /** What Defender has diagnosed TimeSinceSnap seconds after the snap (looking first). A new
     *  diagnosis is announced on the bus. */
    FPSPlayDiagnosis GetDiagnosis(const APSPlayerPawn* Defender, float TimeSinceSnap);

    /** How long Defender holds on a play-action fake that took FakeSeconds to sell: the rest of
     *  his fake read; 0 when he saw through it in time. */
    float GetBiteSeconds(const APSPlayerPawn* Defender, float FakeSeconds);

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** What the field shows now; OutPlayers gets the player each key would point at, by key. */
    FPSKeyLook LookAtField(TMap<FName, TWeakObjectPtr<APSPlayerPawn>>& OutPlayers);

    /** Defender's reaction: his defense AI's (GetReactionSeconds), or the defense tuning's at his
     *  Awareness when no defense AI has him. */
    float GetReaction(const APSPlayerPawn* Defender) const;

    /** The formation read at the snap goes on the bus, once. */
    void AnnounceFormation();

    void Publish(EPSRecognitionEventKind Kind, const APSPlayerPawn* Defender, const FPSPlayDiagnosis* Diagnosis);

    UPROPERTY(Transient)
    FPSPlayRecognitionTuning Tuning;

    UPROPERTY(Transient)
    FPSFormationRead FormationRead;

    UPROPERTY(Transient)
    TArray<FPSKeySighting> Sightings;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    /** By defender: his read times this play. */
    TMap<FObjectKey, FPSDefenderReadTimes> ReadTimesByDefender;
    /** By defender: the key of the diagnosis last announced for him. */
    TMap<FObjectKey, FName> Announced;
    /** By lineman: where he stood (X) at the snap. */
    TMap<FObjectKey, float> LineStanceX;
    FVector LineOfScrimmage = FVector::ZeroVector;
    float PasserStanceX = 0.f;
    float LastLookTime = -1.f;
    float ExpectedRun = 0.5f;
    uint32 SnapSeed = 0;
    bool bHasPasserStance = false;
    bool bExpectationKnown = false;
    bool bFormationPending = false;
    bool bLive = false;
    bool bThrown = false;
    bool bTuningLoaded = false;
};
