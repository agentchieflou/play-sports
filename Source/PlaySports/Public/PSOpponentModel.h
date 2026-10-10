// PSOpponentModel.h - Epic 78: the CPU notices the human's play-calling and counters it
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PSCoachingData.h"
#include "PSOpponentModelTypes.h"
#include "PSTelemetryBus.h"
#include "PSOpponentModel.generated.h"

class UPSSaveSubsystem;

/** The pure rules of the opponent model. */
namespace PSOpponentModel
{
    /** Which of Tuning's distance buckets Distance yards to go falls in (0 = the shortest). */
    PLAYSPORTS_API int32 GetDistanceBucket(int32 Distance, const FPSOpponentModelTuning& Tuning);

    /** True when some counter tracks Category for the human on that side. */
    PLAYSPORTS_API bool IsTracked(const FPSOpponentModelTuning& Tuning, bool bHumanOffense, const FString& Category);

    /** The human's tendency on his side in a situation: from GameCells, plus CareerCells at
     *  PriorGameWeight, in the narrowest situation (down, distance and personnel; down and
     *  distance; down; everything) with at least MinSamples calls. No read below that. */
    PLAYSPORTS_API FPSTendencyRead ReadTendency(const TArray<FPSTendencyCell>& GameCells, const TArray<FPSTendencyCell>& CareerCells,
        bool bHumanOffense, int32 Down, int32 Distance, FName Personnel, const FPSOpponentModelTuning& Tuning);

    /** The CPU's multiplier for each category it counters with on the side opposite the
     *  human's: 1 + Strength * the sum, over the categories the human is tracked on, of (their
     *  share - an even share) * the counter's weight, kept within MinMultiplier..MaxMultiplier.
     *  An even mix of calls, no read or no strength leaves every category at 1. */
    PLAYSPORTS_API TMap<FString, float> ComputeCounterWeights(const FPSTendencyRead& Read, bool bHumanOffense, float Strength, const FPSOpponentModelTuning& Tuning);

    /** Folds From's counts into Into, cell by cell. */
    PLAYSPORTS_API void MergeCells(TArray<FPSTendencyCell>& Into, const TArray<FPSTendencyCell>& From);

    /** Problems with Tuning, one line each (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateTuning(const FPSOpponentModelTuning& Tuning);
}

/**
 * UPSOpponentModel is the CPU's memory of how the human calls plays (Epic 78), within a game
 * and across games, and what the CPU's calls do about it.
 *
 *  - Tendency tracker: every play the human calls and runs is counted by his side, the down,
 *    the distance bucket, the offense's personnel and the play's category. A call is counted at
 *    its snap, never sooner, so the CPU's own call for a play can't have seen it: the model only
 *    knows what the human has already shown (never psychic). A call he changes before the snap
 *    counts as the one he ran.
 *  - Counter-selection: when UPSPlayCallSubsystem calls for the CPU against a human, the side's
 *    tendency gets CounterWeights from the human's read in the situation (CounterTendency):
 *    against a team that keeps running on early downs the defense stays in base, against one
 *    that keeps throwing deep it plays prevent (Data/opponent_model.json's Counters).
 *  - Halftime adjustments: the CPU leans on its read at FirstHalfStrength until HalftimeQuarter
 *    and at SecondHalfStrength from then on. The first call that leans harder announces it on
 *    UPSTelemetryBus (OpponentAdjustment).
 *  - Guardrails: everything is scaled by the adaptation dial (0 never adapts; the difficulty
 *    sets it, Epic 84), a read needs MinSamples calls, and no counter moves a weight outside
 *    MinMultiplier..MaxMultiplier.
 *  - Across games: earlier games' calls count at PriorGameWeight. EndGame folds this game's into
 *    them and saves them in the player's profile (UPSProfileSaveGame), which the next game reads.
 *
 * Headless tests drive it through the bus; worlds without a game instance keep it in memory.
 */
UCLASS()
class PLAYSPORTS_API UPSOpponentModel : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSOpponentModelTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (tests, or a mode with its own). */
    void SetTuning(const FPSOpponentModelTuning& InTuning);

    /** How far the CPU adapts at all, 0-1. Unset, the player's difficulty tier gives it
     *  (UPSDifficultySubsystem, Epic 84), else the tuning's DefaultAdaptationDial. */
    UFUNCTION(BlueprintCallable, Category = "OpponentModel")
    void SetAdaptationDial(float Dial);

    UFUNCTION(BlueprintPure, Category = "OpponentModel")
    float GetAdaptationDial();

    /** How hard the CPU leans on its read in Quarter: the dial times the half's strength. */
    UFUNCTION(BlueprintPure, Category = "OpponentModel")
    float GetAdaptationStrength(int32 Quarter);

    /** The human's tendency on his side in a situation (PSOpponentModel::ReadTendency). */
    UFUNCTION(BlueprintPure, Category = "OpponentModel")
    FPSTendencyRead ReadTendency(bool bHumanOffense, int32 Down, int32 Distance, FName Personnel);

    /** Tendency for the CPU's call on its side against the human in Situation, with
     *  CounterWeights from what it has seen him call. The first call after the half that leans
     *  harder announces the CPU's adjustment on the bus. */
    FPSTendencyProfile CounterTendency(bool bCpuOffense, const FPSSituationContext& Situation, const FPSTendencyProfile& Tendency);

    /** The offense's personnel package on the field, as last announced on the bus. */
    UFUNCTION(BlueprintPure, Category = "OpponentModel")
    FName GetOffensePersonnel() const { return OffensePersonnel; }

    /** This game's counted calls, and the earlier games'. */
    const TArray<FPSTendencyCell>& GetGameCells() const { return GameCells; }
    const TArray<FPSTendencyCell>& GetCareerCells();

    /** Replaces the earlier games' calls (tests; a game reads them from the profile). */
    void SetCareerCells(const TArray<FPSTendencyCell>& Cells);

    /** The game is over: its calls join the earlier games' (saved to the profile when there is
     *  one) and a new game starts with none, and with the first half's strength. */
    UFUNCTION(BlueprintCallable, Category = "OpponentModel")
    void EndGame();

    /** Listens for calls, snaps and personnel. Initialize binds the world's bus. */
    void BindToBus(UPSTelemetryBus* Bus);

    void UnbindFromBus();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePersonnel(const FPSTelemetryPersonnelEvent& Event);

    UPSSaveSubsystem* GetSaveSubsystem() const;
    void EnsureCareerLoaded();
    void SaveCareer();

    UPROPERTY(Transient)
    FPSOpponentModelTuning Tuning;

    UPROPERTY(Transient)
    TArray<FPSTendencyCell> GameCells;

    UPROPERTY(Transient)
    TArray<FPSTendencyCell> CareerCells;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** The human's call on each side waiting for its snap ([0] defense, [1] offense). */
    FString PendingCategory[2];
    FName OffensePersonnel;
    float AdaptationDial = -1.f;
    /** The strength each CPU side last leaned with ([0] defense, [1] offense); -1 before. */
    float LastStrength[2] = { -1.f, -1.f };
    bool bTuningLoaded = false;
    bool bCareerLoaded = false;
};
