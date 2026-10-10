// PSDefenderPreSnapSubsystem.h - Epic 67: the defense disguises and adjusts before the snap
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "UObject/ObjectKey.h"
#include "PSCoachingData.h"
#include "PSDefenderPreSnapTypes.h"
#include "PSDefenseController.h"
#include "PSPlaybookData.h"
#include "PSTelemetryBus.h"
#include "PSDefenderPreSnapSubsystem.generated.h"

class APSPlayerPawn;
class UPSPlayCallSubsystem;

/**
 * UPSDefenderPreSnapSubsystem is the authority on the defense's pre-snap play (Epic 67): what
 * it shows the offense, kept apart from what it calls.
 *
 *  - Alignment: once the defense has called, it lines up for the call -- the safeties in the
 *    shell's structure (two-high, or one deep and one rolled down), the blitzers walked up --
 *    and for any disguise. It does so on its next tick, or at the first read of its look,
 *    moving defenders from the formation spots the side stands on. Those stay the lineup's
 *    (a personnel change lines the side up afresh, UPSPersonnelManager). At the snap
 *    UPSDefenderAIComponent plays the real assignment from wherever each man stands: the
 *    disguise rotates.
 *  - Disguise: show the other shell; show a blitz with linebackers who drop at the snap; or
 *    creep the real blitzers up late from a coverage look.
 *  - Audibles: another defensive play of the same front, made as a new call at
 *    UPSPlayCallSubsystem.
 *  - Matchups: a defender shadows a receiver in man coverage, whatever the call gives him,
 *    until cleared. UPSPlayOrchestrator applies it at the snap (ApplyMatchup).
 *  - The CPU: a CPU call disguises as often as its coach's aggression and its defenders'
 *    Awareness say, and its best back shadows the best receiver on a man call. A low-Awareness
 *    safety leaks his disguise by lining up part-way to his real spot.
 *
 * The offense reads the defense through ReadShownLook -- where the defenders stand, never the
 * call (UPSPreSnapSubsystem::GetDefensiveLook). Every change is announced on UPSTelemetryBus
 * (DefensivePreSnap). Creeping moves pawns from Tick; headless tests call TickPreSnap.
 */
UCLASS()
class PLAYSPORTS_API UPSDefenderPreSnapSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSDefensivePreSnapTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    void SetTuning(const FPSDefensivePreSnapTuning& InTuning);

    /** Problems with Tuning (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSDefensivePreSnapTuning& InTuning);

    /** True while the call window is open and the defense has a call to change. */
    UFUNCTION(BlueprintPure, Category = "PreSnap")
    bool IsAdjustable() const;

    // --- Alignment and disguise ---

    /** Lines the defense up for its call and disguise, unless already lined up for them. */
    void EnsureAligned();

    UFUNCTION(BlueprintPure, Category = "PreSnap")
    FPSDefensiveDisguise GetDisguise() const { return Disguise; }

    /** The defense's disguise for this snap; it lines up again. */
    bool SetDisguise(const FPSDefensiveDisguise& InDisguise, bool bHuman);

    bool ToggleShellDisguise(bool bHuman);
    bool ToggleShowBlitz(bool bHuman);
    bool ToggleCreep(bool bHuman);

    /** Deep safeties the call plays, and the structure its alignment shows. */
    int32 GetCalledDeepSafeties();
    int32 GetShownDeepSafeties();

    /** "ZeroHigh", "SingleHigh" or "TwoHigh". */
    static FString DescribeStructure(int32 DeepSafeties);

    /** What the offense can read from where the defenders stand: the deep safeties, and
     *  whether linebackers or backs are walked up. False when there is no offensive line to
     *  read against. */
    bool ReadShownLook(int32& OutDeepSafeties, bool& bOutShowsBlitz);

    /** True while Defender is a blitzer lined up in a coverage look, to creep up late. */
    bool IsCreeper(const APSPlayerPawn* Defender) const;

    /** One pre-snap step: the defense lines up for a new call, and creeping blitzers walk up
     *  once CreepDelaySeconds has passed. */
    void TickPreSnap(float DeltaSeconds);

    // --- Audibles ---

    /** The other defensive plays of the defense's call's front, in playbook order. */
    TArray<FPSPlayDefinition> GetAudibles() const;

    bool Audible(FName PlayId, bool bHuman);

    /** The next play of the front after the current call (wrapping). */
    bool AudibleToNext(bool bHuman);

    // --- Matchups ---

    /** Defender (an AI defender) shadows Receiver (an offensive player) in man coverage from
     *  the next snap on, until cleared. */
    bool SetShadow(APSPlayerPawn* Defender, APSPlayerPawn* Receiver, bool bHuman);

    void ClearShadow(const APSPlayerPawn* Defender);

    UFUNCTION(BlueprintPure, Category = "PreSnap")
    APSPlayerPawn* GetShadow(const APSPlayerPawn* Defender) const;

    /** The defender shadowing Receiver, if one is. */
    APSPlayerPawn* GetShadowingDefender(const APSPlayerPawn* Receiver) const;

    /** At the snap (UPSPlayOrchestrator): a defender with a shadow plays man on him. */
    void ApplyMatchup(const APSPlayerPawn* Defender, EPSDefensiveAssignmentType& InOutType, AActor*& InOutTarget) const;

    // --- The CPU ---

    /** The CPU defense's coach (Epic 18), whose aggression sets how often it disguises. */
    void SetTendency(const FPSTendencyProfile& InTendency) { Tendency = InTendency; }

    /** Seeds the CPU's disguise decisions, so a snap can be replayed. */
    void SeedDecisions(int32 Seed) { DecisionStream.Initialize(Seed); }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    struct FShadow
    {
        TWeakObjectPtr<APSPlayerPawn> Receiver;
        bool bCpu = false;
    };

    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void HandlePersonnel(const FPSTelemetryPersonnelEvent& Event);
    void ResetDown();

    UPSPlayCallSubsystem* GetPlayCall() const;
    bool GetDefensePlay(FPSPlayDefinition& OutPlay) const;
    TArray<APSPlayerPawn*> GetFieldPawns() const;
    bool ReadLine(float& OutLineX, float& OutCentreY) const;
    int32 GetDeepSafetiesForShell(const FString& Shell);

    /** The kind of job the defense's call gives Defender (the same slot the orchestrator will
     *  hand him at the snap); false if none. */
    bool GetCalledKind(const APSPlayerPawn* Defender, const FPSPlayDefinition& Play, EPSAssignmentKind& OutKind) const;

    void PlanCpu(const FPSPlayDefinition& Play);
    void Align();
    void Publish(FName Action, const APSPlayerPawn* Player, FName Detail, bool bHuman);

    UPROPERTY(Transient)
    FPSDefensivePreSnapTuning Tuning;

    FPSTendencyProfile Tendency;
    FPSDefensiveDisguise Disguise;
    FRandomStream DecisionStream;
    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TMap<FObjectKey, FShadow> Shadows;
    /** Where each defender stood before the defense lined up this down. */
    TMap<FObjectKey, FVector> BaseSpots;
    TArray<TWeakObjectPtr<APSPlayerPawn>> Creepers;
    FName AlignedPlayId;
    float LineX = 0.f;
    float CentreY = 0.f;
    float CreepClock = 0.f;
    int32 ShownDeepSafeties = 1;
    bool bAlignDirty = false;
    bool bTuningLoaded = false;
};
