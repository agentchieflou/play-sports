#include "PSCameraDirectorComponent.h"
#include "PSCameraAll22Component.h"
#include "PSCameraSkycamComponent.h"
#include "PSDataIngestion.h"
#include "PSPlayerPawn.h"
#include "PSTelemetrySamplingSubsystem.h"
#include "PSTelemetrySamplingTypes.h"
#include "PSUIAccessibilitySubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSCameraDirectorPrivate
{
    static const UEnum* ShotEnum()
    {
        return StaticEnum<EPSDirectorShot>();
    }

    static FString ShotName(EPSDirectorShot Shot)
    {
        return ShotEnum() ? ShotEnum()->GetNameStringByValue(static_cast<int64>(Shot)) : FString();
    }

    static bool UsesAll22Rig(EPSDirectorShot Shot)
    {
        return Shot == EPSDirectorShot::All22High || Shot == EPSDirectorShot::EndZone;
    }

    static int32 SafeSide(int32 InCameraSide)
    {
        return InCameraSide < 0 ? -1 : 1;
    }
}

UPSCameraDirectorComponent::UPSCameraDirectorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSCameraDirectorComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/camera_director.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSCameraDirectorTuning& UPSCameraDirectorComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSCameraDirectorComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSCameraDirectorTuning Loaded;
    if (!Ingestion->LoadCameraDirectorTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraDirectorComponent: Could not load %s; the director is off."), *JsonFilePath);
        Tuning = FPSCameraDirectorTuning();
        Tuning.bDirectorEnabled = false;
        return false;
    }
    UPSCameraAll22Component* All22 = GetAll22Component();
    for (const FString& Problem : ValidateTuning(Loaded, All22 ? &All22->GetTuning() : nullptr))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSCameraDirectorComponent: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

TArray<FString> UPSCameraDirectorComponent::ValidateTuning(const FPSCameraDirectorTuning& InTuning, const FPSAll22CameraTuning* All22)
{
    using namespace PSCameraDirectorPrivate;

    TArray<FString> Problems;
    TSet<EPSDirectorShot> Defined;
    for (int32 Index = 0; Index < InTuning.Shots.Num(); ++Index)
    {
        const FPSDirectorShotDef& Def = InTuning.Shots[Index];
        const FString Where = FString::Printf(TEXT("Shots[%d]"), Index);
        if (Def.Shot == EPSDirectorShot::None)
        {
            Problems.Add(Where + TEXT(": Shot is None"));
        }
        else if (Defined.Contains(Def.Shot))
        {
            Problems.Add(Where + TEXT(": ") + ShotName(Def.Shot) + TEXT(" is defined twice"));
        }
        Defined.Add(Def.Shot);
        if (UsesAll22Rig(Def.Shot))
        {
            if (Def.RigId.IsNone())
            {
                Problems.Add(Where + TEXT(": an all-22 shot needs a RigId"));
            }
            else if (All22 && !All22->All22Rigs.ContainsByPredicate([&Def](const FPSAll22RigDef& Rig) { return Rig.RigId == Def.RigId; }))
            {
                Problems.Add(Where + TEXT(": RigId ") + Def.RigId.ToString() + TEXT(" is not a rig in camera_all22.json"));
            }
        }
        else if (Def.HeightCm <= 0.f || Def.DistanceCm <= 0.f || Def.FieldOfView <= 0.f || Def.FieldOfView >= 170.f
            || Def.AimHeightCm < 0.f || Def.LeadSeconds < 0.f)
        {
            Problems.Add(Where + TEXT(": HeightCm and DistanceCm must be positive, FieldOfView in (0, 170), AimHeightCm and LeadSeconds 0 or more"));
        }
    }
    const EPSDirectorShot AllShots[] = { EPSDirectorShot::LosWide, EPSDirectorShot::All22High, EPSDirectorShot::TightFollow,
        EPSDirectorShot::EndZone, EPSDirectorShot::SidelineReaction, EPSDirectorShot::Skycam };
    for (const EPSDirectorShot Shot : AllShots)
    {
        if (!Defined.Contains(Shot))
        {
            Problems.Add(TEXT("Shots: ") + ShotName(Shot) + TEXT(" is not defined"));
        }
    }

    TSet<EPSDirectorTrigger> Ruled;
    for (int32 Index = 0; Index < InTuning.CutRules.Num(); ++Index)
    {
        const FPSDirectorCutRule& Rule = InTuning.CutRules[Index];
        const FString Where = FString::Printf(TEXT("CutRules[%d]"), Index);
        if (Ruled.Contains(Rule.Trigger))
        {
            Problems.Add(Where + TEXT(": the trigger has two rules"));
        }
        Ruled.Add(Rule.Trigger);
        if (Rule.Shot == EPSDirectorShot::None || !Defined.Contains(Rule.Shot))
        {
            Problems.Add(Where + TEXT(": asks for a shot that is not defined"));
        }
    }
    if (!Ruled.Contains(EPSDirectorTrigger::PreSnap))
    {
        Problems.Add(TEXT("CutRules: no rule for PreSnap, the director's opening shot"));
    }

    const FPSDirectorInterestTuning& Interest = InTuning.Interest;
    if (Interest.BallWeight < 0.f || Interest.ProximityWeight < 0.f || Interest.BreakawayWeight < 0.f || Interest.BigHitWeight < 0.f
        || Interest.SwitchMargin < 0.f || Interest.BreakawayClearanceCm < 0.f || Interest.BigHitDamage < 0.f)
    {
        Problems.Add(TEXT("Interest: weights, SwitchMargin, BreakawayClearanceCm and BigHitDamage must be 0 or more"));
    }
    if (Interest.ProximityRadiusCm <= 0.f || Interest.BreakawaySpeedCms <= 0.f || Interest.BigHitSeconds <= 0.f)
    {
        Problems.Add(TEXT("Interest: ProximityRadiusCm, BreakawaySpeedCms and BigHitSeconds must be positive"));
    }
    if (InTuning.MinShotSeconds < 0.f || InTuning.FollowInterpSpeed < 0.f || InTuning.NeutralBandCm < 0.f)
    {
        Problems.Add(TEXT("MinShotSeconds, FollowInterpSpeed and NeutralBandCm must be 0 or more"));
    }
    if (InTuning.CameraSide != -1 && InTuning.CameraSide != 1)
    {
        Problems.Add(TEXT("CameraSide must be -1 (the -Y sideline) or 1 (the +Y sideline)"));
    }
    return Problems;
}

void UPSCameraDirectorComponent::BeginPlay()
{
    Super::BeginPlay();
    BindToBus();
}

void UPSCameraDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UnbindFromBus();
    Super::EndPlay(EndPlayReason);
}

void UPSCameraDirectorComponent::BindToBus()
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnSnapMC.AddUObject(this, &UPSCameraDirectorComponent::HandleSnap);
    Bus->OnPhaseChangeMC.AddUObject(this, &UPSCameraDirectorComponent::HandlePhaseChange);
    Bus->OnThrowMC.AddUObject(this, &UPSCameraDirectorComponent::HandleThrow);
    Bus->OnCatchMC.AddUObject(this, &UPSCameraDirectorComponent::HandleCatch);
    Bus->OnTackleMC.AddUObject(this, &UPSCameraDirectorComponent::HandleTackle);
    Bus->OnFumbleMC.AddUObject(this, &UPSCameraDirectorComponent::HandleFumble);
    Bus->OnScoreMC.AddUObject(this, &UPSCameraDirectorComponent::HandleScore);
    Bus->OnDamageMC.AddUObject(this, &UPSCameraDirectorComponent::HandleDamage);
    BoundBus = Bus;
}

void UPSCameraDirectorComponent::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnCatchMC.RemoveAll(this);
        Bus->OnTackleMC.RemoveAll(this);
        Bus->OnFumbleMC.RemoveAll(this);
        Bus->OnScoreMC.RemoveAll(this);
        Bus->OnDamageMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSCameraDirectorComponent::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    HandleTrigger(EPSDirectorTrigger::Snap);
}

void UPSCameraDirectorComponent::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap"))
    {
        HandleTrigger(EPSDirectorTrigger::PreSnap);
    }
    else if (Event.NewPhase == TEXT("Scoring"))
    {
        HandleTrigger(EPSDirectorTrigger::PlayEnd);
    }
}

void UPSCameraDirectorComponent::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    HandleTrigger(EPSDirectorTrigger::Throw);
}

void UPSCameraDirectorComponent::HandleCatch(const FPSTelemetryCatchEvent& Event)
{
    HandleTrigger(EPSDirectorTrigger::Catch);
}

void UPSCameraDirectorComponent::HandleTackle(const FPSTelemetryTackleEvent& Event)
{
    // A tackle is a big hit for both men in it.
    BigHitTimes.Add(Event.TacklerName, Clock);
    BigHitTimes.Add(Event.BallCarrierName, Clock);
    HandleTrigger(EPSDirectorTrigger::Tackle);
}

void UPSCameraDirectorComponent::HandleFumble(const FPSTelemetryFumbleEvent& Event)
{
    HandleTrigger(EPSDirectorTrigger::Fumble);
}

void UPSCameraDirectorComponent::HandleScore(const FPSTelemetryScoreEvent& Event)
{
    HandleTrigger(EPSDirectorTrigger::Score);
}

void UPSCameraDirectorComponent::HandleDamage(const FPSTelemetryDamageEvent& Event)
{
    if (Event.Amount >= GetTuning().Interest.BigHitDamage)
    {
        BigHitTimes.Add(Event.TargetName, Clock);
    }
}

bool UPSCameraDirectorComponent::IsDirectorEnabled()
{
    return !bDisabledAtRuntime && GetTuning().bDirectorEnabled;
}

void UPSCameraDirectorComponent::SetDirectorEnabled(bool bEnabled)
{
    bDisabledAtRuntime = !bEnabled;
    if (bEnabled)
    {
        bSnapNextStep = true;
    }
}

void UPSCameraDirectorComponent::HandleTrigger(EPSDirectorTrigger Trigger)
{
    const FPSDirectorCutRule* Rule = GetTuning().CutRules.FindByPredicate(
        [Trigger](const FPSDirectorCutRule& Candidate) { return Candidate.Trigger == Trigger; });
    if (Rule)
    {
        RequestCut(Rule->Shot);
    }
}

bool UPSCameraDirectorComponent::RequestCut(EPSDirectorShot Shot)
{
    GetTuning();
    if (Shot == EPSDirectorShot::None || !FindShotDef(Shot))
    {
        return false;
    }
    if (Shot == CurrentShot)
    {
        // Already live: a waiting ask for something else is overruled by this newer one.
        PendingShot = EPSDirectorShot::None;
        return false;
    }
    if (CurrentShot != EPSDirectorShot::None && Clock - ShotStartTime < GetTuning().MinShotSeconds)
    {
        PendingShot = Shot;
        return false;
    }
    CutTo(Shot);
    return true;
}

bool UPSCameraDirectorComponent::CutNow(EPSDirectorShot Shot)
{
    GetTuning();
    if (Shot == EPSDirectorShot::None || !FindShotDef(Shot))
    {
        return false;
    }
    CutTo(Shot);
    return true;
}

void UPSCameraDirectorComponent::CutTo(EPSDirectorShot Shot)
{
    CurrentShot = Shot;
    PendingShot = EPSDirectorShot::None;
    ShotStartTime = Clock;
    ++CutCount;
    bSnapNextStep = true;
    OnShotChanged.Broadcast(CurrentShot, SubjectId);
}

void UPSCameraDirectorComponent::AdvanceTime(float DeltaSeconds)
{
    Clock += FMath::Max(0.f, DeltaSeconds);
    if (!IsDirectorEnabled())
    {
        return;
    }

    // The director's eyes are Epic 26's snapshots: positions, velocities and the ball.
    UWorld* World = GetWorld();
    UPSTelemetrySamplingSubsystem* Sampler = World ? World->GetSubsystem<UPSTelemetrySamplingSubsystem>() : nullptr;
    FPSSnapshotFrame Frame;
    if (!Sampler || !Sampler->GetLatestFrame(Frame))
    {
        return;
    }
    FPSDirectorView View;
    bool bSubjectBreakaway = false;
    if (!BuildView(Frame, View, bSubjectBreakaway))
    {
        return;
    }

    // The opening shot is the pre-snap rule's; afterwards a waiting cut lands once the live
    // shot has run its minimum.
    if (CurrentShot == EPSDirectorShot::None)
    {
        HandleTrigger(EPSDirectorTrigger::PreSnap);
        if (CurrentShot == EPSDirectorShot::None && Tuning.Shots.Num() > 0)
        {
            CutTo(Tuning.Shots[0].Shot);
        }
    }
    else if (PendingShot != EPSDirectorShot::None && Clock - ShotStartTime >= Tuning.MinShotSeconds)
    {
        CutTo(PendingShot);
    }

    // The subject breaking into the clear is a trigger of its own.
    if (bSubjectBreakaway && !bSubjectWasBreakaway)
    {
        HandleTrigger(EPSDirectorTrigger::Breakaway);
    }
    bSubjectWasBreakaway = bSubjectBreakaway;

    const FPSDirectorShotDef* Def = FindShotDef(CurrentShot);
    if (!Def)
    {
        return;
    }

    // The skycam (Epic 39) flies itself: its shot is handed over as it is, mass and all, and it
    // is exempt from the camera side, flying over the line of action.
    UPSCameraSkycamComponent* Skycam = GetSkycamComponent();
    if (CurrentShot == EPSDirectorShot::Skycam && Skycam && Skycam->IsFlying())
    {
        TargetShot = Skycam->GetShot();
        LineOfActionY = View.BallLocation.Y;
        ApplyToCamera(true, DeltaSeconds);
        bSnapNextStep = false;
        return;
    }

    UPSCameraAll22Component* All22 = GetAll22Component();
    FVector Target;
    TargetShot = ComputeShot(*Def, View, All22 ? &All22->GetTuning() : nullptr, Tuning.CameraSide, Target);
    LineOfActionY = View.BallLocation.Y;
    EnforceCameraSide(TargetShot, Target, LineOfActionY, Tuning.CameraSide, Tuning.NeutralBandCm);
    ApplyToCamera(bSnapNextStep, DeltaSeconds);
    bSnapNextStep = false;
}

bool UPSCameraDirectorComponent::BuildView(const FPSSnapshotFrame& Frame, FPSDirectorView& OutView, bool& bOutSubjectBreakaway)
{
    bOutSubjectBreakaway = false;
    if (Frame.Pawns.Num() == 0)
    {
        return false;
    }

    // The ball: sampled, else its carrier, else the middle of the players.
    const FPSPawnSnapshot* Carrier = Frame.Pawns.FindByPredicate([](const FPSPawnSnapshot& Snapshot) { return Snapshot.bHasBall; });
    FVector Centroid = FVector::ZeroVector;
    for (const FPSPawnSnapshot& Snapshot : Frame.Pawns)
    {
        Centroid += Snapshot.Location;
        OutView.PlayerLocations.Add(Snapshot.Location);
    }
    Centroid /= Frame.Pawns.Num();
    OutView.BallLocation = Frame.bBallSampled ? Frame.BallLocation : (Carrier ? Carrier->Location : Centroid);

    // The film component (Epic 40) already reads which way the offense attacks at each snap,
    // and the frame's shape.
    if (const UPSCameraAll22Component* All22 = GetAll22Component())
    {
        OutView.AttackDirection = All22->GetAttackDirection();
        OutView.AspectRatio = All22->GetCurrentShot().AspectRatio;
    }

    // Score everyone and keep or change the subject.
    TArray<float> Scores;
    TArray<bool> Breakaways;
    Scores.Reserve(Frame.Pawns.Num());
    Breakaways.Reserve(Frame.Pawns.Num());
    int32 CurrentIndex = INDEX_NONE;
    for (int32 Index = 0; Index < Frame.Pawns.Num(); ++Index)
    {
        const FPSPawnSnapshot& Snapshot = Frame.Pawns[Index];
        FPSDirectorInterestInput Input;
        Input.bHasBall = Snapshot.bHasBall;
        Input.DistanceToBallCm = static_cast<float>(FVector::Dist2D(Snapshot.Location, OutView.BallLocation));
        Input.SpeedCms = static_cast<float>(Snapshot.Velocity.Size2D());
        double Nearest = TNumericLimits<double>::Max();
        for (const FPSPawnSnapshot& Other : Frame.Pawns)
        {
            if (Other.TeamSide != Snapshot.TeamSide)
            {
                Nearest = FMath::Min(Nearest, FVector::Dist2D(Snapshot.Location, Other.Location));
            }
        }
        Input.NearestOpponentCm = static_cast<float>(FMath::Min(Nearest, static_cast<double>(TNumericLimits<float>::Max())));
        const APSPlayerPawn* Pawn = Snapshot.Pawn.Get();
        const double* HitTime = Pawn ? BigHitTimes.Find(Pawn->GetAttributes().DisplayName) : nullptr;
        Input.SecondsSinceBigHit = HitTime ? static_cast<float>(Clock - *HitTime) : -1.f;
        Scores.Add(ScoreInterest(Input, Tuning.Interest));
        Breakaways.Add(Input.SpeedCms >= Tuning.Interest.BreakawaySpeedCms && Input.NearestOpponentCm >= Tuning.Interest.BreakawayClearanceCm);
        if (Snapshot.PlayerId == SubjectId)
        {
            CurrentIndex = Index;
        }
    }
    const int32 SubjectIndex = PickSubject(Scores, CurrentIndex, Tuning.Interest.SwitchMargin);
    const FPSPawnSnapshot& Subject = Frame.Pawns[SubjectIndex];
    SubjectId = Subject.PlayerId;
    bOutSubjectBreakaway = Breakaways[SubjectIndex];
    OutView.SubjectLocation = Subject.Location;
    OutView.SubjectVelocity = Subject.Velocity;
    return true;
}

float UPSCameraDirectorComponent::ScoreInterest(const FPSDirectorInterestInput& Input, const FPSDirectorInterestTuning& InterestTuning)
{
    float Score = 0.f;
    if (Input.bHasBall)
    {
        Score += InterestTuning.BallWeight;
    }
    if (InterestTuning.ProximityRadiusCm > 0.f)
    {
        Score += InterestTuning.ProximityWeight * FMath::Max(0.f, 1.f - Input.DistanceToBallCm / InterestTuning.ProximityRadiusCm);
    }
    if (Input.SpeedCms >= InterestTuning.BreakawaySpeedCms && Input.NearestOpponentCm >= InterestTuning.BreakawayClearanceCm)
    {
        Score += InterestTuning.BreakawayWeight;
    }
    if (Input.SecondsSinceBigHit >= 0.f && InterestTuning.BigHitSeconds > 0.f)
    {
        Score += InterestTuning.BigHitWeight * FMath::Max(0.f, 1.f - Input.SecondsSinceBigHit / InterestTuning.BigHitSeconds);
    }
    return Score;
}

int32 UPSCameraDirectorComponent::PickSubject(const TArray<float>& Scores, int32 CurrentIndex, float SwitchMargin)
{
    int32 Best = INDEX_NONE;
    for (int32 Index = 0; Index < Scores.Num(); ++Index)
    {
        if (Best == INDEX_NONE || Scores[Index] > Scores[Best])
        {
            Best = Index;
        }
    }
    if (Best != INDEX_NONE && Scores.IsValidIndex(CurrentIndex) && Scores[Best] < Scores[CurrentIndex] + SwitchMargin)
    {
        return CurrentIndex;
    }
    return Best;
}

FPSCameraShot UPSCameraDirectorComponent::ComputeShot(const FPSDirectorShotDef& Def, const FPSDirectorView& View,
    const FPSAll22CameraTuning* All22, int32 InCameraSide, FVector& OutTarget)
{
    using namespace PSCameraDirectorPrivate;

    if (UsesAll22Rig(Def.Shot) && All22)
    {
        const FPSAll22RigDef* Rig = All22->All22Rigs.FindByPredicate([&Def](const FPSAll22RigDef& Candidate) { return Candidate.RigId == Def.RigId; });
        if (Rig)
        {
            const FBox Box = UPSCameraFraming::ComputePlayerBox(View.PlayerLocations, *All22);
            OutTarget = Box.IsValid ? Box.GetCenter() : View.BallLocation;
            return UPSCameraFraming::FrameBox(*Rig, Box, View.AttackDirection, View.AspectRatio);
        }
    }

    // The ground-level shots stand their distance toward the camera side of their target: the
    // ball for the wide shot, the subject (led along his run) for the others.
    FVector Target = View.SubjectLocation;
    if (Def.Shot == EPSDirectorShot::LosWide || UsesAll22Rig(Def.Shot))
    {
        Target = View.BallLocation;
    }
    else if (Def.Shot == EPSDirectorShot::TightFollow)
    {
        Target += FVector(View.SubjectVelocity.X, View.SubjectVelocity.Y, 0.0) * Def.LeadSeconds;
    }
    Target.Z = Def.AimHeightCm;
    OutTarget = Target;

    FPSCameraShot Shot;
    Shot.Location = FVector(Target.X, Target.Y + SafeSide(InCameraSide) * Def.DistanceCm, Def.HeightCm);
    Shot.Rotation = (Target - Shot.Location).Rotation();
    Shot.FieldOfView = Def.FieldOfView;
    Shot.AspectRatio = View.AspectRatio;
    return Shot;
}

bool UPSCameraDirectorComponent::IsOnCameraSide(const FPSCameraShot& Shot, double LineY, int32 InCameraSide, float NeutralBandCm)
{
    const double Offset = (Shot.Location.Y - LineY) * PSCameraDirectorPrivate::SafeSide(InCameraSide);
    return Offset >= -FMath::Max(0.f, NeutralBandCm);
}

bool UPSCameraDirectorComponent::EnforceCameraSide(FPSCameraShot& Shot, const FVector& Target, double LineY, int32 InCameraSide, float NeutralBandCm)
{
    if (IsOnCameraSide(Shot, LineY, InCameraSide, NeutralBandCm))
    {
        return false;
    }
    Shot.Location.Y = 2.0 * LineY - Shot.Location.Y;
    const FVector Look = Target - Shot.Location;
    if (!Look.IsNearlyZero())
    {
        Shot.Rotation = Look.Rotation();
    }
    return true;
}

void UPSCameraDirectorComponent::ApplyToCamera(bool bSnap, float DeltaSeconds)
{
    ACameraActor* Camera = GetCamera();
    UCameraComponent* CameraComponent = Camera ? Camera->GetCameraComponent() : nullptr;
    if (!Camera || !CameraComponent)
    {
        return;
    }
    // With reduced motion (Epic 103.5) the follow speed is 0, so the camera stays on its target.
    const float FollowSpeed = UPSUIAccessibilitySubsystem::GetCameraFollowSpeedIn(this, Tuning.FollowInterpSpeed);
    if (bSnap || FollowSpeed <= 0.f)
    {
        Camera->SetActorLocationAndRotation(TargetShot.Location, TargetShot.Rotation);
        CameraComponent->SetFieldOfView(TargetShot.FieldOfView);
        return;
    }
    const double Alpha = FMath::Clamp<double>(DeltaSeconds * FollowSpeed, 0.0, 1.0);
    const FVector Location = FMath::Lerp(Camera->GetActorLocation(), TargetShot.Location, Alpha);
    const FQuat Rotation = FQuat::Slerp(Camera->GetActorQuat(), TargetShot.Rotation.Quaternion(), Alpha);
    Camera->SetActorLocationAndRotation(Location, Rotation);
    CameraComponent->SetFieldOfView(FMath::Lerp(CameraComponent->FieldOfView, TargetShot.FieldOfView, static_cast<float>(Alpha)));
}

ACameraActor* UPSCameraDirectorComponent::GetCamera() const
{
    return Cast<ACameraActor>(GetOwner());
}

UPSCameraAll22Component* UPSCameraDirectorComponent::GetAll22Component() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UPSCameraAll22Component>() : nullptr;
}

UPSCameraSkycamComponent* UPSCameraDirectorComponent::GetSkycamComponent() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UPSCameraSkycamComponent>() : nullptr;
}

const FPSDirectorShotDef* UPSCameraDirectorComponent::FindShotDef(EPSDirectorShot Shot) const
{
    return Tuning.Shots.FindByPredicate([Shot](const FPSDirectorShotDef& Def) { return Def.Shot == Shot; });
}
