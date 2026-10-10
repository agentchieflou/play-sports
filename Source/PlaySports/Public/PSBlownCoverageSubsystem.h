// PSBlownCoverageSubsystem.h - Epic 17.4: the defense reacts to a blown coverage
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSTelemetryBus.h"
#include "PSBlownCoverageSubsystem.generated.h"

class APSPlayerPawn;
class UPSDefenderAIComponent;

/** When a coverage counts as blown, and who may help (Data/blown_coverage.json; Architecture
 *  rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FBlownCoverageTuningRow : public FTableRowBase
{
    GENERATED_BODY()

    /** How often the defense looks for a free receiver while the quarterback holds the ball. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlownCoverage")
    float CheckIntervalSeconds = 0.1f;

    /** A receiver this far from every defender is running free: the coverage is blown ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlownCoverage")
    float UncoveredSeparation = 1000.f;

    /** ... once he is this far past the line of scrimmage (a back or receiver behind it, or
     *  on it, is not yet a threat). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlownCoverage")
    float MinDepthPastLine = 300.f;

    /** A zone defender within this distance of him may leave his zone to pick him up. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlownCoverage")
    float HelpRadius = 2500.f;
};

/**
 * UPSBlownCoverageSubsystem is the defense's reaction to a blown coverage, Epic 17's
 * broken-play adaptation. While the offense's quarterback holds the ball behind the line, it
 * watches for a receiver running free downfield: past the line, with every defender at least
 * UncoveredSeparation from him, because a man defender bit on a double move or a zone left a
 * hole. The nearest defender playing a zone (or one in man with nobody left to cover), not
 * frozen and within HelpRadius, leaves his zone to cover him. Each receiver is spotted, and
 * each defender sent, once per play.
 *
 * It decides; the defenders act. Each spotting goes on UPSTelemetryBus as a BlownCoverage
 * event naming the receiver and the helper, and the named defender's UPSDefenderAIComponent
 * takes the receiver in man coverage. The quarterback's own read of a free receiver is his
 * (UPSSkillPlayerAIComponent).
 *
 * It looks every CheckIntervalSeconds from Tick; headless tests call CheckCoverage. Tuning:
 * Data/blown_coverage.json.
 */
UCLASS()
class PLAYSPORTS_API UPSBlownCoverageSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FBlownCoverageTuningRow& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FBlownCoverageTuningRow& InTuning);

    /** True from the snap until the ball leaves the quarterback's hands or the play ends. */
    UFUNCTION(BlueprintPure, Category = "BlownCoverage")
    bool IsWatching() const { return bWatching; }

    /** One look: every receiver running free gets the nearest zone defender who can help.
     *  Returns how many were spotted. Only while the quarterback holds the ball behind the
     *  line. */
    int32 CheckCoverage();

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandleThrow(const FPSTelemetryThrowEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    TArray<APSPlayerPawn*> GetFieldPawns() const;

    /** The defender who can pick Receiver up: the nearest one in a zone (or in man with
     *  nobody to cover), not frozen, not already sent, within HelpRadius. */
    APSPlayerPawn* FindHelper(const APSPlayerPawn* Receiver, const TArray<APSPlayerPawn*>& Pawns) const;

    UPROPERTY(Transient)
    FBlownCoverageTuningRow Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;

    /** Receivers spotted and defenders sent this play. */
    TSet<FObjectKey> Spotted;
    TSet<FObjectKey> Sent;

    FVector LineOfScrimmage = FVector::ZeroVector;
    float SinceCheck = 0.f;
    bool bWatching = false;
    bool bTuningLoaded = false;
};
