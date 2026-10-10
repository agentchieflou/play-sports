// PSPenaltyModel.h - which flags a snap draws, at per-play rates from data
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PSPlaySimulation.h"
#include "PSPenaltyModel.generated.h"

/** How often the simulation's own flags fly (Data/penalties.json; Architecture rule 4). Defaults
 *  equal the file. The other flags come from the players: an offside jump (Epic 104.5) and pass
 *  interference (Epic 69). */
USTRUCT(BlueprintType)
struct FPSPenaltyTuning
{
    GENERATED_BODY()

    /** The chance a scrimmage play draws offensive holding, once per play however long it runs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    float HoldingChancePerPlay = 0.15f;

    /** The chance any snap draws a defensive offside. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Penalties")
    float OffsidesChancePerSnap = 0.05f;
};

/**
 * UPSPenaltyModel decides the flags a snap draws, for UPSPlaySimulation (which throws, rules and
 * announces them). Each is rolled once, at the snap, on the process-global random stream the
 * rest of the simulation rolls on (so a seeded game stays repeatable):
 *
 *  - A defensive offside on any snap, at OffsidesChancePerSnap.
 *  - Failing that, offensive holding on a scrimmage play (not a kick, a kneel or a spike), at
 *    HoldingChancePerPlay. A per-play chance holds for any pace: quick sim's 6-second steps and
 *    a live game's frames see the same rate. (A chance per second drew holding on about 45% of
 *    quick-sim plays against about 15% live, because quick sim spends a whole step in each of a
 *    play's three live phases.)
 */
UCLASS()
class PLAYSPORTS_API UPSPenaltyModel : public UObject
{
    GENERATED_BODY()

public:
    static FString GetDefaultTuningPath();

    /** Replaces the tuning with JsonFilePath's, read through UPSDataIngestion; tuning with
     *  problems is refused. */
    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning; refused when ValidateTuning finds problems. */
    bool SetTuning(const FPSPenaltyTuning& InTuning);

    const FPSPenaltyTuning& GetTuning() const { return Tuning; }

    /** Problems with Candidate, one line each (empty when sound): each chance from 0 to 1. */
    static TArray<FString> ValidateTuning(const FPSPenaltyTuning& Candidate);

    /** The flag a snap draws (None for none): an offside first, else holding on a scrimmage
     *  play. */
    EPSPenaltyType RollSnapFlag(bool bScrimmagePlay) const;

private:
    FPSPenaltyTuning Tuning;
};
