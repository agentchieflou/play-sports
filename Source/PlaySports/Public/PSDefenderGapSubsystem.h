// PSDefenderGapSubsystem.h - Epic 81: run defense as a coordinated gap-accounting system
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Stats/Stats.h"
#include "PSPlayerAttributes.h"
#include "PSTelemetryBus.h"
#include "PSDefenderGapSubsystem.generated.h"

class APSPlayerPawn;

/** A run gap: the spaces between and outside the offensive linemen, left (-Y) and right (+Y)
 *  of the ball. A sits beside the center, B outside the guard, C outside the tackle (inside an
 *  inline tight end), D outside that. */
UENUM(BlueprintType)
enum class EPSRunGap : uint8
{
    None,
    DLeft,
    CLeft,
    BLeft,
    ALeft,
    ARight,
    BRight,
    CRight,
    DRight
};

/** How a fitter plays his gap. */
UENUM(BlueprintType)
enum class EPSFitTechnique : uint8
{
    None,
    /** Squeezes his gap from the inside so the run bounces outside, to the force player. */
    Spill,
    /** The force player, the outermost fitter on his side: keeps outside leverage and turns the
     *  run back inside. */
    Box
};

/** One role's gaps in a front, given to that role's defenders left to right across the field. */
USTRUCT(BlueprintType)
struct FPSRunFitRoleGaps
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    EPlayerRole Role = EPlayerRole::DefensiveLineman;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    TArray<EPSRunGap> Gaps;
};

/** A defensive front's gap map (the play's Front, e.g. "4-3"). */
USTRUCT(BlueprintType)
struct FPSRunFitFront
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    FString Front;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    TArray<FPSRunFitRoleGaps> Fits;
};

/** Run-fit data (Data/run_fits.json; Architecture rule 4). Distances in cm. */
USTRUCT(BlueprintType)
struct FPSRunFitCatalog
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    TArray<FPSRunFitFront> Fronts;

    /** The front for a call whose front isn't listed, or with no call. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    FString DefaultFront;

    /** A gap outside the last lineman (or inline tight end) is this wide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float GapWidth = 150.f;

    /** A tight end this close outside the end lineman is inline: he extends the line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float InlineTightEndWidth = 250.f;

    /** A defensive lineman fits his gap this far past the line of scrimmage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float FitDepth = 50.f;

    /** Everyone else (the second level) fits this far past it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float SecondLevelDepth = 350.f;

    /** A spill fitter plays this far inside his gap, the force player this far outside it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float LeverageOffset = 40.f;

    /** A second-level fitter flows this fraction of the way from his gap toward the carrier,
     *  across the field. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float FlowWeight = 0.4f;

    /** A carrier this close to a fitter's gap, across the field, is coming through it: the
     *  fitter attacks him. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float AttackRadius = 120.f;

    /** A fitter this close to his gap's spot, across the field, fills it (blocked or not). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RunFit")
    float FillRadius = 120.f;
};

/** One defender's run fit this play. */
USTRUCT(BlueprintType)
struct FPSDefenderGapFit
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    EPSRunGap Gap = EPSRunGap::None;

    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    EPSFitTechnique Technique = EPSFitTechnique::None;

    /** A defensive lineman fits at the line; everyone else at the second level. */
    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    bool bFirstLevel = false;

    /** He took this gap in a scrape exchange. */
    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    bool bExchanged = false;
};

/** One gap's state: who owns it, and whether he is in it. */
USTRUCT(BlueprintType)
struct FPSGapStatus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    EPSRunGap Gap = EPSRunGap::None;

    /** The defender who owns the gap, if any. */
    TWeakObjectPtr<APSPlayerPawn> Owner;

    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    bool bFilled = false;

    UPROPERTY(BlueprintReadOnly, Category = "RunFit")
    bool bOwnerBlocked = false;
};

/** The pure rules of the gap model. */
namespace PSDefenderGaps
{
    /** Gap index from the ball outward (A 0 .. D 3), and its side. */
    PLAYSPORTS_API int32 GetGapLevel(EPSRunGap Gap);
    PLAYSPORTS_API bool IsLeftGap(EPSRunGap Gap);

    /** The gap Level steps out from the ball on one side. */
    PLAYSPORTS_API EPSRunGap MakeGap(int32 Level, bool bLeft);

    /** The eight gaps' spots from the offensive line where it stands: OutSpots is indexed by
     *  EPSRunGap (None unused). The center is the lineman nearest BallY; an inline tight end
     *  extends the line; past the last man each gap is GapWidth wide. False with no linemen.
     *  Roles, when given, are the pawns' roles in the same order (UPSAIFieldSnapshot's). */
    PLAYSPORTS_API bool ComputeGapSpots(const TArray<APSPlayerPawn*>& Pawns, float BallY, const FPSRunFitCatalog& Catalog, TArray<FVector>& OutSpots,
        const TArray<EPlayerRole>* Roles = nullptr);

    /** Front's gaps for Defenders: each role's defenders, left to right, take that role's gaps
     *  in order; the rest get None. OutGaps parallels Defenders. */
    PLAYSPORTS_API void AssignFrontGaps(const FPSRunFitFront& Front, const TArray<APSPlayerPawn*>& Defenders, TArray<EPSRunGap>& OutGaps);

    /** Problems with Catalog (empty when sound). */
    PLAYSPORTS_API TArray<FString> ValidateCatalog(const FPSRunFitCatalog& Catalog);
}

/**
 * UPSDefenderGapSubsystem is the one authority for who owns which run gap this play (Epic 81).
 *
 * At the snap it maps the defensive call's front (Data/run_fits.json) onto the front
 * defenders: those rushing or in a run fit get a gap, those in coverage none, so a call that
 * drops linebackers leaves gaps open. The outermost fitter on each side is the force player
 * (box technique); the rest spill. Gap spots follow the offensive line as it moves.
 *
 * UPSDefenderAIComponent asks GetFitTarget on a run read: a fitter plays his gap (the second
 * level flowing with the carrier) until the carrier comes to it or crosses the line, then
 * attacks. On a run, when the gap the carrier is heading for has no free owner, the nearest
 * free linebacker scrapes into it and the two exchange gaps. Gap integrity -- which gaps are
 * filled and which are open -- is published on UPSTelemetryBus (GapIntegrity) whenever it
 * changes, or an exchange happens.
 *
 * It ticks with the world at the platform tier's AI decision rate, reading the field through
 * UPSAIFieldSnapshot; headless tests call AssignGaps and UpdateFits.
 */
UCLASS()
class PLAYSPORTS_API UPSDefenderGapSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    static FString GetDefaultCatalogPath();

    /** The run-fit data in use, loaded from the default path on first use. */
    const FPSRunFitCatalog& GetCatalog();

    bool LoadCatalogFromJson(const FString& JsonFilePath);

    /** Maps Front (or the default front, if it isn't listed) onto the defenders on the field. The
     *  snap does this on the first update after it, with the defensive call's front. */
    void AssignGaps(const FString& Front);

    /** One step: the scrape exchange on a run, then the gaps' integrity, published when it
     *  changed. */
    void UpdateFits(float DeltaSeconds);

    /** Defender's fit this play (Gap None without one). */
    FPSDefenderGapFit GetFit(const APSPlayerPawn* Defender);

    /** Who owns Gap this play, if anyone. */
    APSPlayerPawn* GetGapOwner(EPSRunGap Gap);

    /** Gap's spot on the line now; zero when the line can't be read. */
    FVector GetGapSpot(EPSRunGap Gap);

    /** Every gap's spot on the line now, indexed by EPSRunGap (None unused); false when the line
     *  can't be read. */
    bool GetGapSpots(TArray<FVector>& OutSpots) { return ReadGapSpots(OutSpots); }

    /** Where Defender should be to fit his gap against Carrier, or false when he should just
     *  attack: no gap, no line to read, the carrier past the line or coming through his gap. */
    bool GetFitTarget(const APSPlayerPawn* Defender, const APSPlayerPawn* Carrier, FVector& OutTarget);

    /** Every gap's state as of the last update, D left to D right. */
    const TArray<FPSGapStatus>& GetIntegrity() const { return Integrity; }

    /** How many gaps were open at the last update. */
    int32 GetOpenGapCount() const;

    /** The front the gaps were assigned from. */
    const FString& GetFront() const { return AssignedFront; }

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    void HandleSnap(const FPSTelemetrySnapEvent& Event);
    void HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event);
    void EnsureAssigned();
    void AssignTechniques();
    void TryScrapeExchange(const APSPlayerPawn* Carrier, const TArray<FVector>& Spots);
    const TArray<APSPlayerPawn*>& GetFieldPawns() const;
    APSPlayerPawn* FindRunCarrier(const TArray<APSPlayerPawn*>& Pawns) const;
    bool ReadGapSpots(TArray<FVector>& OutSpots);

    UPROPERTY(Transient)
    FPSRunFitCatalog Catalog;

    TWeakObjectPtr<UPSTelemetryBus> BoundBus;
    TMap<TWeakObjectPtr<APSPlayerPawn>, FPSDefenderGapFit> Fits;
    TSet<EPSRunGap> ExchangedGaps;
    TArray<FPSGapStatus> Integrity;
    FString AssignedFront;
    FVector LineOfScrimmage = FVector::ZeroVector;
    /** The open gaps last published, as a bit per EPSRunGap; -1 before the first. */
    int32 PublishedOpenMask = -1;
    int32 PublishedExchangeCount = 0;
    int32 ExchangeCount = 0;
    /** Time since the last update; updates come at the platform tier's AI decision rate. */
    float UpdateClock = 0.f;
    bool bAssignPending = false;
    bool bPlayLive = false;
    bool bCatalogLoaded = false;
};
