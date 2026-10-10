// PSDeceptionSubsystem.h - Epic 72: play-action, RPO and option football
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "PSPlaybookData.h"
#include "PSTelemetryBus.h"
#include "PSDeceptionSubsystem.generated.h"

class APSPlayerPawn;
class UPSPlayCallSubsystem;

/** The quarterback's choice at the mesh of a run option (Epic 72). */
UENUM(BlueprintType)
enum class EPSOptionChoice : uint8
{
    /** Hand it to the back. */
    Give,
    /** Keep it and run (and, on the triple option, read the pitch key). */
    Keep,
    /** Pull it and throw to the pass option (RPO). */
    Throw,
    /** Still riding the mesh: the read comes on a later look. */
    Ride
};

/** Deception football's tuning (Data/deception.json; Architecture rule 4). Distances in cm,
 *  chances 0-1, ratings 0-100. */
USTRUCT(BlueprintType)
struct FPSDeceptionTuning : public FTableRowBase
{
    GENERATED_BODY()

    // --- Play-action ---

    /** The quarterback carries out a play-action fake hand-off this long before his drop. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float FakeSeconds = 0.6f;

    /** A run-fit defender's chance to bite on the fake at Awareness 0 against an even run-pass
     *  mix ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteBaseChance = 0.5f;

    /** ... plus this times how much more than half of the offense's recent calls were runs
     *  (doubled: all runs adds it whole, all passes takes it away) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteRunTendencyWeight = 0.4f;

    /** ... minus this times his Awareness / 100. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteAwarenessWeight = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteMinChance = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteMaxChance = 0.9f;

    /** A defender who bites is frozen this long (holding on the run fake, not dropping). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    float BiteFreezeSeconds = 0.8f;

    /** The offense's tendency is read over its last this-many scrimmage calls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PlayAction")
    int32 TendencyWindow = 10;

    // --- The reads ---

    /** The quarterback rides the mesh this long after the snap before he reads his key, so the
     *  key has shown what he is doing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float MeshRideSeconds = 0.4f;

    /** A key moving at least this fast (cm/s) is read by where he is going; slower, by where he
     *  is. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float ReadMinSpeed = 100.f;

    /** A defender this close to the line of scrimmage is on the line: the end man on it (on
     *  the play side) is the zone read's key and the triple option's dive key. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float KeyLineDepth = 300.f;

    /** The triple option's quarterback, keeping it, pitches once the pitch key is this close to
     *  him (and closer to him than to the pitch man) ... */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float PitchReadRadius = 400.f;

    /** ... until he is this far past the line, where he keeps it for good. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reads")
    float PitchWindowDepth = 200.f;

    // --- Defensive integrity ---

    /** The quarterback with the ball this close to his back, behind the line, is a mesh: the
     *  defense sees the option and gives out its option jobs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Integrity")
    float MeshRecognizeRadius = 300.f;

    /** A defender this aware plays his option job: the read key takes the quarterback, the
     *  pitch key the pitch man. One less aware chases the ball: the read key crashes on the
     *  dive, the pitch key goes for the quarterback. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Integrity")
    float DisciplineAwareness = 70.f;

    /** When the read key crashes, an aware linebacker this close to him scrapes over to take
     *  the quarterback (the scrape exchange). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Integrity")
    float ScrapeRadius = 800.f;
};

/**
 * UPSDeceptionSubsystem runs deception football (Epic 72) for the play the offense called
 * (FPSPlayDefinition::Deception of the offense's latest PlayCall on the bus, looked up in
 * UPSPlayCallSubsystem's playbook):
 *
 *  - Play-action: the quarterback carries out a fake hand-off for FakeSeconds (UpdateFake), then
 *    sells it: every run-fit defender may bite -- his Awareness against how run-heavy the
 *    offense's recent calls have been (BiteChance) -- and one who bites freezes instead of
 *    dropping.
 *  - The run options' read at the mesh (ReadMesh), once the quarterback has ridden it for
 *    MeshRideSeconds: an RPO throws to its pass option when the conflict defender (the
 *    linebacker nearest the pass option, else a back) is playing the run, else gives; a zone read
 *    keeps it when the end man on the line on the play side is playing the back, else gives; the
 *    triple option reads the same end for the dive, and keeping it reads the pitch key
 *    (ReadPitch) until he is past the line. A key is read by where he is going (moving at least
 *    ReadMinSpeed), else by whom he is nearer.
 *  - Defensive integrity: when the defense sees the mesh of a zone read or triple option
 *    (UpdateDefense) it gives out option jobs: the read key takes the quarterback if he is
 *    disciplined (else he crashes on the dive, and an aware linebacker scrapes over to the
 *    quarterback), the nearest lineman takes the dive, the pitch key takes the pitch man if he
 *    is disciplined (else the quarterback). UPSDefenderAIComponent plays them (GetOptionTarget)
 *    while the quarterback has the ball near the line. Disciplined defenders make the reads give
 *    the ball to someone already covered; undisciplined ones get read and beaten.
 *
 * Every moment goes on UPSTelemetryBus (Deception). The quarterback (UPSSkillPlayerAIComponent)
 * asks for his fake and his reads; the defenders hear the bites. Rolls are seeded from the snap's
 * situation. It steps from Tick at the AI's decision rate; headless tests call UpdateDefense.
 * Tuning: Data/deception.json.
 */
UCLASS()
class PLAYSPORTS_API UPSDeceptionSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultTuningPath();

    /** The tuning in use, loaded from the default path on first use. */
    const FPSDeceptionTuning& GetTuning();

    bool LoadTuningFromJson(const FString& JsonFilePath);

    /** Replaces the tuning (headless tests). */
    void SetTuning(const FPSDeceptionTuning& InTuning);

    /** Problems with InTuning (empty when sound). */
    static TArray<FString> ValidateTuning(const FPSDeceptionTuning& InTuning);

    /** A run-fit defender's chance to bite on a play-action fake: his Awareness against the share
     *  of runs in the offense's recent calls (0-1). */
    static float BiteChance(float Awareness, float RunShare, const FPSDeceptionTuning& InTuning);

    /** The share of runs in the offense's last TendencyWindow scrimmage calls (0.5 with none). */
    float GetRunShare() const;

    /** The deception of the play the offense is running: its latest call's, as the quarterback
     *  runs it (a call the playbook doesn't know has none). */
    const FPSDeceptionDef& GetPlayDeception() const { return PlayDeception; }

    // --- The quarterback ---

    /** Play-action: true while the quarterback carries out the fake (the first FakeSeconds of
     *  the play); the first call after sells it and rolls the bites. False on any other play. */
    bool UpdateFake(APSPlayerPawn* Passer, float TimeSinceSnap);

    /** The run options' read at the mesh, made once a play: Ride until the quarterback has ridden
     *  the mesh for MeshRideSeconds, then the choice. Give on any other play. */
    EPSOptionChoice ReadMesh(APSPlayerPawn* Passer, APSPlayerPawn* Back, float TimeSinceSnap);

    /** The RPO's pass option: the first player of the play's PassRole on a route. */
    APSPlayerPawn* GetPassOption() const;

    /** The triple option, the quarterback keeping it: the pitch man once the pitch key has taken
     *  the quarterback, while he is behind (or just past) the line; null otherwise. */
    APSPlayerPawn* ReadPitch(APSPlayerPawn* Passer);

    // --- The defense ---

    /** One look: the defense sees an option's mesh and gives out its option jobs. */
    void UpdateDefense();

    /** True once the defense has seen this play's option mesh. */
    UFUNCTION(BlueprintPure, Category = "Deception")
    bool HasRecognizedOption() const { return bRecognized; }

    /** The man Defender's option job puts him on, if he has one. */
    APSPlayerPawn* GetOptionMan(const APSPlayerPawn* Defender) const;

    /** Where Defender plays now: his option man, while the quarterback has the ball behind (or
     *  just past) the line. False when he has no option job or the option is over. */
    bool GetOptionTarget(const APSPlayerPawn* Defender, FVector& OutTarget) const;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePlayCall(const FPSTelemetryPlayCallEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);

    /** The first offensive player of Role (on a route when bOnRoute). */
    APSPlayerPawn* FindOffense(EPlayerRole Role, bool bOnRoute = false) const;

    /** The end man on the line on the play side: the zone read's key, the triple's dive key. */
    APSPlayerPawn* FindEndManOnLine() const;

    /** True when Defender is playing Back rather than Passer: heading more toward him when moving
     *  at least MinSpeed, else nearer him. */
    static bool IsPlayingTheBack(const APSPlayerPawn* Defender, const APSPlayerPawn* Back, const APSPlayerPawn* Passer, float MinSpeed);

    void SellFake(APSPlayerPawn* Passer);
    void Assign(APSPlayerPawn* Defender, APSPlayerPawn* Man, FName Job);
    void Publish(EPSDeceptionEventKind Kind, const APSPlayerPawn* Player, const APSPlayerPawn* Other, FName Outcome, float Seconds = 0.f);

    UPSPlayCallSubsystem* GetPlayCall() const;

    UPROPERTY(Transient)
    FPSDeceptionTuning Tuning;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    /** The offense's recent scrimmage calls, newest last: true for a run. */
    TArray<bool> RecentRuns;
    /** By defender: the man his option job puts him on. */
    TMap<FObjectKey, TWeakObjectPtr<APSPlayerPawn>> OptionJobs;
    TWeakObjectPtr<APSPlayerPawn> PitchKey;

    FPSDeceptionDef PlayDeception;
    FRandomStream Rolls;
    FVector LineOfScrimmage = FVector::ZeroVector;
    float SinceUpdate = 0.f;
    EPSOptionChoice MeshChoice = EPSOptionChoice::Give;
    bool bLive = false;
    /** The offense has called since the last snap: an audible replaces that call's tendency entry. */
    bool bCallSinceSnap = false;
    bool bFakeSold = false;
    bool bMeshRead = false;
    bool bPitched = false;
    bool bRecognized = false;
    bool bTuningLoaded = false;
};
