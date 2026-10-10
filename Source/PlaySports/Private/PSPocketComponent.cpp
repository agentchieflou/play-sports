#include "PSPocketComponent.h"
#include "PSDifficultySubsystem.h"
#include "PSBall.h"
#include "PSBallActionComponent.h"
#include "PSCarrierMoveComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldReads.h"
#include "PSHealthComponent.h"
#include "PSPlayerDNA.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSPocketPrivate
{
    /** Unit direction from From to To on the ground; zero when they coincide. */
    FVector GroundDirection(const FVector& From, const FVector& To)
    {
        FVector Direction = To - From;
        Direction.Z = 0.f;
        return Direction.GetSafeNormal();
    }

    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }

    bool IsEligibleReceiver(const APSPlayerPawn* Pawn, const APSPlayerPawn* Passer)
    {
        if (!Pawn || Pawn == Passer || Pawn->TeamSide != Passer->TeamSide)
        {
            return false;
        }
        const EPlayerRole Role = Pawn->GetAttributes().Role;
        return Role == EPlayerRole::WideReceiver || Role == EPlayerRole::TightEnd || Role == EPlayerRole::RunningBack;
    }
}

bool PSPocket::IsInsideTackleBox(const FVector& Location, const FVector& LineOfScrimmage, const FPocketTuningRow& Tuning)
{
    return FMath::Abs(Location.Y - LineOfScrimmage.Y) <= Tuning.TackleBoxHalfWidth && Location.X <= LineOfScrimmage.X;
}

float PSPocket::StripChance(const FPlayerAttributes& Rusher, const FPlayerAttributes& Passer, const FPocketTuningRow& Tuning)
{
    return FMath::Clamp(Tuning.StripBaseChance + (Rusher.Strength - Passer.Strength) * Tuning.StripStrengthWeight, 0.f, 1.f);
}

FVector PSPocket::ScrambleDrillSpot(const FVector& Receiver, const FVector& Passer, float ScrambleSide, const FVector2D& Jitter, const FPocketTuningRow& Tuning)
{
    const float Side = ScrambleSide < 0.f ? -1.f : 1.f;
    const FVector Spread(Jitter.X * Tuning.ScrambleDrillJitter, Jitter.Y * Tuning.ScrambleDrillJitter, 0.f);
    const float Across = Passer.Y + Side * Tuning.ScrambleDrillWidth;
    if (Receiver.X - Passer.X >= Tuning.ScrambleDeepDepth)
    {
        // Deep: keep going, working across to the scramble side.
        return FVector(Receiver.X + Tuning.ScrambleDeepRunOn, Across, Receiver.Z) + Spread;
    }
    // Short: come back to him, in front and toward his side, where he can throw it.
    return FVector(Passer.X + Tuning.ScrambleDrillDepth, Across, Receiver.Z) + Spread;
}

UPSPocketComponent::UPSPocketComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FString UPSPocketComponent::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/pocket_tuning.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPocketTuningRow& UPSPocketComponent::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSPocketComponent::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPocketTuningRow Loaded;
    if (!Ingestion->LoadPocketTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPocketComponent: Could not load pocket tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    BaseTuning = Loaded;
    Tuning = Loaded;
    return true;
}

void UPSPocketComponent::SetTuning(const FPocketTuningRow& InTuning)
{
    BaseTuning = InTuning;
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSPocketComponent::ApplyPlayTuning(const APSPlayerPawn* Passer)
{
    GetTuning();
    Tuning = BaseTuning;
    if (!Passer)
    {
        return;
    }
    if (UPSPlayerDNASubsystem* DNA = UPSPlayerDNASubsystem::Get(GetWorld()))
    {
        DNA->ApplyTo(Passer->GetAttributes(), TEXT("Pocket"), Tuning);
    }
    if (UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(GetWorld()))
    {
        Difficulty->ApplyTo(Passer, TEXT("Pocket"), Tuning);
    }
}

void UPSPocketComponent::ResetPlay(int32 Seed)
{
    StripTried.Reset();
    Rolls.Initialize(Seed);
    ScrambleSide = 1.f;
    ScrambleStartedAt = 0.f;
    bScrambling = false;
}

FPSPocketRead UPSPocketComponent::ReadPocket(const APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage)
{
    FPSPocketRead Read;
    if (!Passer)
    {
        return Read;
    }
    const FPocketTuningRow& Settings = GetTuning();
    const FVector Location = Passer->GetActorLocation();

    // Each rusher near him presses, harder the closer and the freer he is: pushing him away
    // from that rusher, off the edge or up the middle.
    FVector Push = FVector::ZeroVector;
    float EdgePressure = 0.f;
    float InsidePressure = 0.f;
    Read.NearestFreeDistance = TNumericLimits<float>::Max();
    for (APSPlayerPawn* Rusher : Pawns)
    {
        if (!Rusher || Rusher->TeamSide == Passer->TeamSide || !PSPocketPrivate::IsStanding(Rusher))
        {
            continue;
        }
        const float Distance = FVector::Dist2D(Rusher->GetActorLocation(), Location);
        if (!Rusher->bIsEngaged && Distance < Read.NearestFreeDistance)
        {
            Read.NearestFreeDistance = Distance;
            Read.NearestFreeRusher = Rusher;
        }
        if (Distance >= Settings.PocketRadius || Settings.PocketRadius <= 0.f)
        {
            continue;
        }
        const float Weight = (Rusher->bIsEngaged ? Settings.EngagedPressureWeight : 1.f) * (1.f - Distance / Settings.PocketRadius);
        Push += PSPocketPrivate::GroundDirection(Rusher->GetActorLocation(), Location) * Weight;
        Read.Pressure += Weight;
        if (FMath::Abs(Rusher->GetActorLocation().Y - Location.Y) > Settings.EdgeWidth)
        {
            EdgePressure += Weight;
        }
        else
        {
            InsidePressure += Weight;
        }
    }
    Read.LateralPush = Push.Y;
    Read.bCollapsed = Read.NearestFreeDistance <= Settings.EscapeRadius || Read.Pressure >= Settings.CollapsePressure;
    if (Read.Pressure < Settings.MinPressure)
    {
        return Read;
    }

    if (EdgePressure >= InsidePressure)
    {
        // Off the edges: step up into the pocket, though never onto the line.
        if (Location.X < LineOfScrimmage.X - Settings.ClimbStopDistance)
        {
            Read.Move = EPSPocketMove::Climb;
            Read.Direction = FVector(1.f, 0.f, 0.f);
        }
    }
    else
    {
        // Up the middle from one side: slide away from it.
        const float Side = Push.Y < 0.f ? -1.f : 1.f;
        Read.Move = Side < 0.f ? EPSPocketMove::SlideLeft : EPSPocketMove::SlideRight;
        Read.Direction = FVector(0.f, Side, 0.f);
    }
    return Read;
}

bool UPSPocketComponent::ResolveImminentSack(APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage)
{
    UPSBallActionComponent* BallAction = Passer ? Passer->GetBallActionComponent() : nullptr;
    APSBall* Ball = BallAction ? BallAction->GetCarriedBall() : nullptr;
    if (!Ball || !Passer->HasPossession())
    {
        return false;
    }
    const FPocketTuningRow& Settings = GetTuning();
    const FPSPocketRead Read = ReadPocket(Passer, Pawns, LineOfScrimmage);
    APSPlayerPawn* Rusher = Read.NearestFreeRusher;
    if (!Rusher || Read.NearestFreeDistance > Settings.SackImminentRadius)
    {
        return false;
    }
    const FVector Location = Passer->GetActorLocation();

    // From his blind side (behind him) he never sees it: the rusher goes for the ball, once.
    if (Rusher->GetActorLocation().X < Location.X)
    {
        const FObjectKey Key(Rusher);
        if (StripTried.Contains(Key))
        {
            return false;
        }
        StripTried.Add(Key);
        const bool bStripped = Rolls.FRand() < PSPocket::StripChance(Rusher->GetAttributes(), Passer->GetAttributes(), Settings);
        if (bStripped)
        {
            Passer->FumbleBall();
        }
        Publish(EPSPocketEventKind::StripAttempt, Passer, Rusher, bStripped);
        return bStripped && !Passer->HasPossession();
    }

    // He sees it coming: an aware quarterback throws it away rather than take the sack.
    const float Awareness = FMath::Clamp(Passer->GetAttributes().Awareness, 0.f, 100.f);
    if (Awareness < Settings.ThrowawayMinAwareness)
    {
        return false;
    }
    const APSPlayerPawn* Receiver = nullptr;
    float ReceiverDistance = Settings.ThrowawayReceiverRange;
    for (const APSPlayerPawn* Candidate : Pawns)
    {
        const float CandidateDistance = Candidate ? FVector::Dist2D(Candidate->GetActorLocation(), Location) : 0.f;
        if (PSPocketPrivate::IsEligibleReceiver(Candidate, Passer) && CandidateDistance <= ReceiverDistance)
        {
            ReceiverDistance = CandidateDistance;
            Receiver = Candidate;
        }
    }
    FVector Target = FVector::ZeroVector;
    if (Receiver)
    {
        // At his feet, short of him.
        Target = Receiver->GetActorLocation() - PSPocketPrivate::GroundDirection(Location, Receiver->GetActorLocation()) * Settings.ThrowawayShort;
    }
    else
    {
        // Nobody to throw at: away past the line, out toward his sideline.
        const float Side = Location.Y < LineOfScrimmage.Y ? -1.f : 1.f;
        Target = FVector(FMath::Max(Location.X, LineOfScrimmage.X) + Settings.ThrowawayDepth, Location.Y + Side * Settings.ThrowawayWidth, Location.Z);
    }
    const bool bGrounding = !Receiver && PSPocket::IsInsideTackleBox(Location, LineOfScrimmage, Settings);
    if (bGrounding && Awareness >= Settings.GroundingAvoidAwareness)
    {
        // He knows that would be grounding: he takes the sack.
        return false;
    }
    if (!Passer->ThrowPass(Ball, Target, true))
    {
        return false;
    }
    Publish(bGrounding ? EPSPocketEventKind::IntentionalGrounding : EPSPocketEventKind::Throwaway, Passer, Rusher, false);
    return true;
}

void UPSPocketComponent::BeginScramble(const APSPlayerPawn* Passer, const FPSPocketRead& Read, float TimeSinceSnap)
{
    if (bScrambling || !Passer)
    {
        return;
    }
    bScrambling = true;
    ScrambleStartedAt = TimeSinceSnap;

    // To the side with room: away from the pressure, or from the free rusher on him.
    float Side = Read.LateralPush;
    if (FMath::IsNearlyZero(Side) && Read.NearestFreeRusher)
    {
        Side = Passer->GetActorLocation().Y - Read.NearestFreeRusher->GetActorLocation().Y;
    }
    ScrambleSide = Side < 0.f ? -1.f : 1.f;
    Publish(EPSPocketEventKind::Escape, Passer, Read.NearestFreeRusher, false);
}

bool UPSPocketComponent::CanThrowOnTheRun(float TimeSinceSnap)
{
    return bScrambling && TimeSinceSnap - ScrambleStartedAt < GetTuning().ScrambleMaxSeconds;
}

FVector UPSPocketComponent::SteerScramble(const APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns)
{
    if (!Passer)
    {
        return FVector::ZeroVector;
    }
    const FPocketTuningRow& Settings = GetTuning();
    const FVector Location = Passer->GetActorLocation();

    // A clear lane ahead: tuck it and go.
    bool bLaneClear = true;
    for (const APSPlayerPawn* Defender : Pawns)
    {
        if (!Defender || Defender->TeamSide == Passer->TeamSide || !PSPocketPrivate::IsStanding(Defender))
        {
            continue;
        }
        const FVector Offset = Defender->GetActorLocation() - Location;
        if (Offset.X > 0.f && Offset.X <= Settings.RunLaneClearance && FMath::Abs(Offset.Y) <= Settings.RunLaneWidth)
        {
            bLaneClear = false;
            break;
        }
    }
    if (bLaneClear)
    {
        return FVector(1.f, 0.f, 0.f);
    }

    // Else across to his side, a little upfield, veering from the nearest defender.
    FVector Heading(Settings.ScrambleForwardBias, ScrambleSide, 0.f);
    float Distance = TNumericLimits<float>::Max();
    const APSPlayerPawn* Nearest = PSFieldReads::NearestOpponent(Pawns, Passer->TeamSide, Location, &Distance);
    if (Nearest && Distance < Settings.PocketRadius && Settings.PocketRadius > 0.f)
    {
        const float AwayY = Location.Y - Nearest->GetActorLocation().Y;
        Heading.Y += (AwayY < 0.f ? -1.f : 1.f) * (1.f - Distance / Settings.PocketRadius);
    }
    Heading.Z = 0.f;
    return Heading.GetSafeNormal();
}

bool UPSPocketComponent::MaybeSlide(APSPlayerPawn* Passer, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage)
{
    UPSCarrierMoveComponent* Moves = Passer ? Passer->GetCarrierMoveComponent() : nullptr;
    if (!Moves || Moves->HasGivenUp() || !Passer->HasPossession())
    {
        return false;
    }
    const APSPlayerPawn* Tackler = FindSlideThreat(GetTuning(), Passer, Pawns, LineOfScrimmage);
    if (!Tackler || !Moves->TryMove(EPSCarrierMove::Slide, FVector2D::ZeroVector))
    {
        return false;
    }
    Publish(EPSPocketEventKind::Slide, Passer, Tackler, true);
    return true;
}

const APSPlayerPawn* UPSPocketComponent::FindSlideThreat(const FPocketTuningRow& Settings, const APSPlayerPawn* Carrier, const TArray<APSPlayerPawn*>& Pawns, const FVector& LineOfScrimmage)
{
    if (!Carrier)
    {
        return nullptr;
    }
    const FVector Location = Carrier->GetActorLocation();
    if (Location.X - LineOfScrimmage.X < Settings.SlideMinGain)
    {
        return nullptr;
    }
    float Distance = TNumericLimits<float>::Max();
    const APSPlayerPawn* Tackler = PSFieldReads::NearestOpponent(Pawns, Carrier->TeamSide, Location, &Distance);
    if (!Tackler || Distance > Settings.SlideTriggerRadius || Tackler->GetActorLocation().X < Location.X)
    {
        return nullptr;
    }
    return Tackler;
}

void UPSPocketComponent::Publish(EPSPocketEventKind Kind, const APSPlayerPawn* Passer, const APSPlayerPawn* Defender, bool bSuccess)
{
    UWorld* World = GetWorld();
    UPSTelemetryBus* Bus = World ? World->GetSubsystem<UPSTelemetryBus>() : nullptr;
    if (!Bus)
    {
        return;
    }
    FPSTelemetryPocketEvent Event;
    Event.Kind = Kind;
    Event.PasserName = Passer ? Passer->GetAttributes().DisplayName : FString();
    Event.DefenderName = Defender ? Defender->GetAttributes().DisplayName : FString();
    Event.Location = Passer ? Passer->GetActorLocation() : FVector::ZeroVector;
    Event.ScrambleSide = ScrambleSide;
    Event.bSuccess = bSuccess;
    Bus->PublishPocket(Event);
}
