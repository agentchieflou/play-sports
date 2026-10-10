#include "PSCameraSkycamComponent.h"
#include "PSCameraAll22Component.h"
#include "PSDataIngestion.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSTelemetrySamplingTypes.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSCameraSkycamPrivate
{
    static double Cosh(double Value)
    {
        return 0.5 * (FMath::Exp(Value) + FMath::Exp(-Value));
    }

    /** A catenary's sag below its anchors at Offset from the middle of a span of HalfSpan
     *  either side, with parameter A. */
    static double Sag(double HalfSpan, double Offset, double A)
    {
        const double Clamped = FMath::Min(FMath::Abs(Offset), HalfSpan);
        return A * (Cosh(HalfSpan / A) - Cosh(Clamped / A));
    }

    /** Below this speed a carrier counts as standing; the chase then looks along the attack. */
    static constexpr double StandingSpeedCms = 100.0;

    /** The height the camera looks at above the field. */
    static constexpr double LookHeightCm = 100.0;
}

UPSCameraSkycamComponent::UPSCameraSkycamComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSCameraSkycamComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/camera_skycam.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSSkycamTuning& UPSCameraSkycamComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCameraSkycamComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSkycamTuning Loaded;
    if (!Ingestion->LoadSkycamTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraSkycamComponent: Could not load %s; using the default rig."), *JsonFilePath);
        Tuning = FPSSkycamTuning();
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraSkycamComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSCameraSkycamComponent::ValidateTuning(const FPSSkycamTuning& InTuning)
{
    TArray<FString> Problems;
    if (InTuning.AnchorHalfLengthCm <= 0.f || InTuning.AnchorHalfWidthCm <= 0.f || InTuning.AnchorHeightCm <= 0.f)
    {
        Problems.Add(TEXT("AnchorHalfLengthCm, AnchorHalfWidthCm and AnchorHeightCm must be positive"));
    }
    if (InTuning.CatenaryParameterCm <= 0.f)
    {
        Problems.Add(TEXT("CatenaryParameterCm must be positive"));
    }
    if (InTuning.EdgeMarginCm < 0.f || InTuning.EdgeMarginCm >= FMath::Min(InTuning.AnchorHalfLengthCm, InTuning.AnchorHalfWidthCm))
    {
        Problems.Add(TEXT("EdgeMarginCm must be 0 or more and leave room inside the towers"));
    }
    if (InTuning.StiffnessPerSecSq <= 0.f || InTuning.DampingPerSec < 0.f || InTuning.MaxSpeedCms <= 0.f || InTuning.MaxAccelerationCms2 <= 0.f)
    {
        Problems.Add(TEXT("StiffnessPerSecSq, MaxSpeedCms and MaxAccelerationCms2 must be positive, DampingPerSec 0 or more"));
    }
    if (InTuning.BehindQuarterbackDistanceCm < 0.f || InTuning.ChaseDistanceCm < 0.f || InTuning.LookAheadCm < 0.f)
    {
        Problems.Add(TEXT("BehindQuarterbackDistanceCm, ChaseDistanceCm and LookAheadCm must be 0 or more"));
    }
    if (InTuning.FieldOfView <= 0.f || InTuning.FieldOfView >= 170.f)
    {
        Problems.Add(TEXT("FieldOfView must be in (0, 170)"));
    }
    if (InTuning.CatenaryParameterCm > 0.f && InTuning.MinHeightCm >= CeilingAt(InTuning, 0.0, 0.0))
    {
        Problems.Add(TEXT("MinHeightCm must be below the cables' ceiling over midfield"));
    }
    return Problems;
}

double UPSCameraSkycamComponent::CeilingAt(const FPSSkycamTuning& InTuning, double X, double Y)
{
    using namespace PSCameraSkycamPrivate;
    const double A = FMath::Max(1.0, static_cast<double>(InTuning.CatenaryParameterCm));
    return InTuning.AnchorHeightCm
        - Sag(InTuning.AnchorHalfLengthCm, X, A)
        - Sag(InTuning.AnchorHalfWidthCm, Y, A);
}

FVector UPSCameraSkycamComponent::ConstrainToEnvelope(const FPSSkycamTuning& InTuning, const FVector& Location)
{
    const double MaxX = FMath::Max(0.0, static_cast<double>(InTuning.AnchorHalfLengthCm - InTuning.EdgeMarginCm));
    const double MaxY = FMath::Max(0.0, static_cast<double>(InTuning.AnchorHalfWidthCm - InTuning.EdgeMarginCm));
    FVector Result(FMath::Clamp<double>(Location.X, -MaxX, MaxX), FMath::Clamp<double>(Location.Y, -MaxY, MaxY), Location.Z);
    const double Ceiling = CeilingAt(InTuning, Result.X, Result.Y);
    const double Floor = FMath::Min(static_cast<double>(InTuning.MinHeightCm), Ceiling);
    Result.Z = FMath::Clamp<double>(Result.Z, Floor, Ceiling);
    return Result;
}

bool UPSCameraSkycamComponent::IsInsideEnvelope(const FPSSkycamTuning& InTuning, const FVector& Location, double Tolerance)
{
    return FVector::Dist(ConstrainToEnvelope(InTuning, Location), Location) <= Tolerance;
}

void UPSCameraSkycamComponent::StepRig(const FPSSkycamTuning& InTuning, FVector& InOutLocation, FVector& InOutVelocity, const FVector& Desired, float DeltaSeconds)
{
    if (DeltaSeconds <= 0.f)
    {
        return;
    }
    const FVector Acceleration = (InTuning.StiffnessPerSecSq * (Desired - InOutLocation) - InTuning.DampingPerSec * InOutVelocity)
        .GetClampedToMaxSize(InTuning.MaxAccelerationCms2);
    InOutVelocity = (InOutVelocity + Acceleration * DeltaSeconds).GetClampedToMaxSize(InTuning.MaxSpeedCms);
    const FVector Unconstrained = InOutLocation + InOutVelocity * DeltaSeconds;
    const FVector Constrained = ConstrainToEnvelope(InTuning, Unconstrained);
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (!FMath::IsNearlyEqual(Constrained[Axis], Unconstrained[Axis], 0.001))
        {
            InOutVelocity[Axis] = 0.0;
        }
    }
    InOutLocation = Constrained;
}

void UPSCameraSkycamComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToBus();
}

void UPSCameraSkycamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSCameraSkycamComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSCameraSkycamComponent::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSCameraSkycamComponent::HandlePhaseChange);
    BoundBus = Bus;
}

void UPSCameraSkycamComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCameraSkycamComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    Mode = EPSSkycamMode::Chase;
}

void UPSCameraSkycamComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        Mode = EPSSkycamMode::BehindQuarterback;
    }
}

void UPSCameraSkycamComponent::AdvanceTime(float DeltaSeconds)
{
    GetTuning();
    UWorld* World = GetWorld();
    UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
    FPSSnapshotFrame Frame;
    if (!Sampler || !Sampler->GetLatestFrame(Frame))
    {
        return;
    }
    if (!PickTargets(Frame, DesiredLocation, LookTarget))
    {
        return;
    }
    if (!bFlying)
    {
        // The rig starts parked where it is wanted.
        RigLocation = DesiredLocation;
        RigVelocity = FVector::ZeroVector;
        bFlying = true;
        return;
    }
    StepRig(Tuning, RigLocation, RigVelocity, DesiredLocation, DeltaSeconds);
}

bool UPSCameraSkycamComponent::PickTargets(const FPSSnapshotFrame& Frame, FVector& OutDesired, FVector& OutLook) const
{
    using namespace PSCameraSkycamPrivate;

    if (Frame.Pawns.Num() == 0)
    {
        return false;
    }
    const FPSPawnSnapshot* Quarterback = Frame.Pawns.FindByPredicate([](const FPSPawnSnapshot& Snapshot)
    {
        return Snapshot.TeamSide == EPSTeamSide::Offense && Snapshot.Role == EPlayerRole::Quarterback;
    });
    const FPSPawnSnapshot* Carrier = Frame.Pawns.FindByPredicate([](const FPSPawnSnapshot& Snapshot) { return Snapshot.bHasBall; });
    FVector Centroid = FVector::ZeroVector;
    for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
    {
        Centroid += Snapshot.Location;
    }
    Centroid /= Frame.Pawns.Num();
    const FVector Ball = Frame.bBallSampled ? Frame.BallLocation : (Carrier ? Carrier->Location : Centroid);

    const UPSCameraAll22Component* All22 = GetAll22Component();
    const FVector Attack(All22 && All22->GetAttackDirection() < 0.f ? -1.0 : 1.0, 0.0, 0.0);

    FVector Anchor;
    FVector Along;
    double Behind;
    double Height;
    if (Mode == EPSSkycamMode::BehindQuarterback)
    {
        Anchor = Quarterback ? Quarterback->Location : Ball;
        Along = Attack;
        Behind = Tuning.BehindQuarterbackDistanceCm;
        Height = Tuning.BehindQuarterbackHeightCm;
    }
    else
    {
        Anchor = Carrier ? Carrier->Location : Ball;
        const FVector Run = Carrier ? FVector(Carrier->Velocity.X, Carrier->Velocity.Y, 0.0) : FVector::ZeroVector;
        Along = Run.Size() >= StandingSpeedCms ? Run.GetSafeNormal() : Attack;
        Behind = Tuning.ChaseDistanceCm;
        Height = Tuning.ChaseHeightCm;
    }

    FVector Desired = Anchor - Along * Behind;
    Desired.Z = Height;
    OutDesired = ConstrainToEnvelope(Tuning, Desired);
    OutLook = Anchor + Along * Tuning.LookAheadCm;
    OutLook.Z = LookHeightCm;
    return true;
}

FPSCameraShot UPSCameraSkycamComponent::GetShot() const
{
    FPSCameraShot Shot;
    Shot.Location = RigLocation;
    const FVector Look = LookTarget - RigLocation;
    Shot.Rotation = Look.IsNearlyZero() ? FRotator::ZeroRotator : Look.Rotation();
    Shot.FieldOfView = Tuning.FieldOfView;
    if (const UPSCameraAll22Component* All22 = GetAll22Component())
    {
        Shot.AspectRatio = All22->GetCurrentShot().AspectRatio;
    }
    return Shot;
}

UPSCameraAll22Component* UPSCameraSkycamComponent::GetAll22Component() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UPSCameraAll22Component>() : nullptr;
}
