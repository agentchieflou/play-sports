// PSBallActionComponent.cpp - Epic C3: extracted ball-action logic from APSPlayerPawn
#include "PSBallActionComponent.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "PSGameMode.h"
#include "PSHealthComponent.h"
#include "PSCombatRulesModel.h"
#include "PSBallResolutionHelpers.h"
#include "PSCarrierMoveComponent.h"
#include "PSDefenderTechniqueComponent.h"
#include "PSDifficultySubsystem.h"
#include "PSNetRandomStreams.h"
#include "PSTelemetryBus.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"

UPSBallActionComponent::UPSBallActionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UPSBallActionComponent::ThrowPass(APSBall* Ball, const FVector& TargetLocation, bool bHighArc, APSPlayerPawn* IntendedTarget, float SpeedScale)
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn)
    {
        return false;
    }

    if (!Ball)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ThrowPass failed - Ball is null."));
        return false;
    }

    if (!OwnerPawn->HasPossession())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ThrowPass failed - Player %s does not have the ball."), *OwnerPawn->GetAttributes().DisplayName);
        return false;
    }

    // LaunchSpeed is scaled by Strength (0-100 rating -> 1500 to 3000 cm/s launch speed)
    const float FullSpeed = 1500.f + (OwnerPawn->GetAttributes().Strength * 15.f);
    float LaunchSpeed = FullSpeed * FMath::Clamp(SpeedScale, 0.1f, 1.f);

    // Apply accuracy scatter to the target point based on Awareness (lower awareness = more error)
    FVector ScatterTarget = TargetLocation;
    if (OwnerPawn->GetAttributes().Awareness < 100.f)
    {
        float AccuracyError = (100.f - OwnerPawn->GetAttributes().Awareness) * 2.f; // Max error up to 200cm
        // A CPU passer's execution varies with the difficulty tier (Epic 84); his rating doesn't.
        if (UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(GetWorld()))
        {
            AccuracyError *= Difficulty->GetThrowScatterScale(OwnerPawn);
        }
        // The miss is the passer's own roll on the play's seeded stream (Epic 108): the same match
        // seed and snap throw the same ball. Direction, then distance, as two statements: one
        // stream drawn twice in one expression is drawn in whichever order the compiler picks.
        FVector ErrorDirection;
        float ErrorDistance = 0.f;
        if (UPSNetRandomStreams* Streams = UPSNetRandomStreams::Get(this))
        {
            const FName PasserId = OwnerPawn->GetAttributes().PlayerId;
            ErrorDirection = Streams->RollUnitVector(TEXT("ThrowScatter"), PasserId);
            ErrorDistance = Streams->RollRange(TEXT("ThrowScatter"), 0.f, AccuracyError, PasserId);
        }
        else
        {
            ErrorDirection = FMath::VRand();
            ErrorDistance = FMath::FRandRange(0.f, AccuracyError);
        }
        FVector ErrorOffset = ErrorDirection * ErrorDistance;
        ErrorOffset.Z = 0.f; // Keep error on 2D plane
        ScatterTarget += ErrorOffset;
    }

    FVector OutVelocity = FVector::ZeroVector;
    FVector StartLocation = OwnerPawn->GetActorLocation() + FVector(0.f, 0.f, 50.f); // Throw from hand/chest height

    bool bSuccess = UGameplayStatics::SuggestProjectileVelocity(
        this,
        OutVelocity,
        StartLocation,
        ScatterTarget,
        LaunchSpeed,
        bHighArc,
        0.f,
        0.f,
        ESuggestProjVelocityTraceOption::DoNotTrace
    );
    if (!bSuccess && LaunchSpeed < FullSpeed)
    {
        // Too far for a touch pass: it goes on a line instead.
        LaunchSpeed = FullSpeed;
        bSuccess = UGameplayStatics::SuggestProjectileVelocity(this, OutVelocity, StartLocation, ScatterTarget, LaunchSpeed,
            bHighArc, 0.f, 0.f, ESuggestProjVelocityTraceOption::DoNotTrace);
    }

    if (bSuccess)
    {
        Ball->Launch(OutVelocity);
        // The velocity was worked out from hand height; the carried ball sits at the pawn's
        // center (the stand-in mesh has no hand socket), so it leaves from where the throw
        // was aimed from (Epic 32: otherwise it comes down short of the target).
        Ball->SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
        OwnerPawn->LosePossession();
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Player %s (ID: %s) threw a pass to %s. Launch velocity: %s"),
            *OwnerPawn->GetAttributes().DisplayName,
            *OwnerPawn->GetAttributes().PlayerId.ToString(),
            *TargetLocation.ToString(),
            *OutVelocity.ToString());

        if (IntendedTarget)
        {
            if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
            {
                FPSTelemetryThrowEvent ThrowEvt;
                ThrowEvt.PasserName = OwnerPawn->GetAttributes().DisplayName;
                ThrowEvt.TargetReceiverName = IntendedTarget->GetAttributes().DisplayName;
                ThrowEvt.StartLocation = StartLocation;
                ThrowEvt.TargetLocation = TargetLocation;
                ThrowEvt.LandingLocation = ScatterTarget;
                ThrowEvt.LaunchSpeed = LaunchSpeed;
                Bus->PublishThrow(ThrowEvt);
            }
        }

        return true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ThrowPass failed - Target is out of range for launch speed %.1f."), LaunchSpeed);
        return false;
    }
}

bool UPSBallActionComponent::ExecuteHandoff(APSPlayerPawn* TargetPlayer)
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn)
    {
        return false;
    }

    if (!TargetPlayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecuteHandoff failed - TargetPlayer is null."));
        return false;
    }

    if (!OwnerPawn->HasPossession())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecuteHandoff failed - Player %s does not have the ball."), *OwnerPawn->GetAttributes().DisplayName);
        return false;
    }

    float Distance = FVector::Dist(OwnerPawn->GetActorLocation(), TargetPlayer->GetActorLocation());
    if (Distance > 200.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecuteHandoff failed - TargetPlayer %s is out of range (%.1f > 200 cm)."), *TargetPlayer->GetAttributes().DisplayName, Distance);
        return false;
    }

    // The ball this pawn carries, not the game mode's (rule 5: no reach-through).
    APSBall* Ball = GetCarriedBall();
    if (Ball && OwnerPawn->TransferPossessionTo(TargetPlayer))
    {
        Ball->AttachToCarrier(TargetPlayer, TEXT("HandSocket"));
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Executed handoff from %s to %s."), *OwnerPawn->GetAttributes().DisplayName, *TargetPlayer->GetAttributes().DisplayName);
        return true;
    }
    return false;
}

APSBall* UPSBallActionComponent::GetCarriedBall() const
{
    const AActor* OwnerActor = GetOwner();
    if (!OwnerActor)
    {
        return nullptr;
    }
    TArray<AActor*> Attached;
    OwnerActor->GetAttachedActors(Attached);
    for (AActor* Actor : Attached)
    {
        if (APSBall* Ball = Cast<APSBall>(Actor))
        {
            return Ball;
        }
    }
    return nullptr;
}

bool UPSBallActionComponent::ExecutePitch(APSPlayerPawn* TargetPlayer)
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn)
    {
        return false;
    }

    if (!TargetPlayer)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecutePitch failed - TargetPlayer is null."));
        return false;
    }

    if (!OwnerPawn->HasPossession())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecutePitch failed - Player %s does not have the ball."), *OwnerPawn->GetAttributes().DisplayName);
        return false;
    }

    APSBall* Ball = GetCarriedBall();
    if (!Ball)
    {
        return false;
    }

    FVector OutVelocity = FVector::ZeroVector;
    FVector StartLocation = OwnerPawn->GetActorLocation() + FVector(0.f, 0.f, 50.f);
    FVector TargetLocation = TargetPlayer->GetActorLocation() + FVector(0.f, 0.f, 50.f);

    float PitchSpeed = 1000.f;

    bool bSuccess = UGameplayStatics::SuggestProjectileVelocity(
        this,
        OutVelocity,
        StartLocation,
        TargetLocation,
        PitchSpeed,
        false,
        0.f,
        0.f,
        ESuggestProjVelocityTraceOption::DoNotTrace
    );

    if (bSuccess)
    {
        Ball->Launch(OutVelocity);
        // From where the pitch was aimed from, as for a pass.
        Ball->SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
        OwnerPawn->LosePossession();
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Executed lateral pitch from %s to %s. Launch velocity: %s"), 
            *OwnerPawn->GetAttributes().DisplayName, 
            *TargetPlayer->GetAttributes().DisplayName, 
            *OutVelocity.ToString());
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecutePitch failed - Target is out of range for pitch speed %.1f."), PitchSpeed);
        return false;
    }
}

bool UPSBallActionComponent::ExecuteKick(APSBall* Ball, float KickPower, float LaunchAngle)
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn)
    {
        return false;
    }

    if (!Ball)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecuteKick failed - Ball is null."));
        return false;
    }

    if (!OwnerPawn->HasPossession())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: ExecuteKick failed - Player does not have possession."));
        return false;
    }

    FVector Direction = OwnerPawn->GetActorForwardVector();
    Direction.Z = FMath::Sin(FMath::DegreesToRadians(LaunchAngle));
    if (!Direction.IsNearlyZero())
    {
        Direction.Normalize();
    }
    else
    {
        Direction = FVector(1.f, 0.f, 0.f);
    }

    FVector LaunchVelocity = Direction * KickPower;

    Ball->Launch(LaunchVelocity);
    OwnerPawn->LosePossession();

    UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Executed kick with power %.1f at angle %.1f degrees. Velocity: %s"), 
        KickPower, LaunchAngle, *LaunchVelocity.ToString());

    return true;
}

void UPSBallActionComponent::FumbleBall()
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn)
    {
        return;
    }

    if (!OwnerPawn->HasPossession())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSBallActionComponent: FumbleBall failed - Player %s does not have the ball."), *OwnerPawn->GetAttributes().DisplayName);
        return;
    }

    // The ball this pawn carries (rule 5: no reach-through), else the game's.
    APSBall* Ball = GetCarriedBall();
    if (!Ball)
    {
        const APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
        Ball = GM ? GM->ActiveBall : nullptr;
    }
    if (Ball)
    {
        FVector FumbleVelocity = OwnerPawn->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 200.f);
        // Which way it squirts is the fumbler's roll on the play's seeded streams (Epic 108).
        FumbleVelocity += UPSNetRandomStreams::RollUnitVectorFor(this, TEXT("FumbleBounce"), OwnerPawn->GetAttributes().PlayerId) * 100.f;
        FumbleVelocity.Z = FMath::Max(50.f, FumbleVelocity.Z);

        Ball->Fumble(FumbleVelocity);
        OwnerPawn->LosePossession();
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Player %s (ID: %s) fumbled the ball!"), *OwnerPawn->GetAttributes().DisplayName, *OwnerPawn->GetAttributes().PlayerId.ToString());
    }
}

bool UPSBallActionComponent::ResolveTackle(APSPlayerPawn* Defender)
{
    APSPlayerPawn* OwnerPawn = Cast<APSPlayerPawn>(GetOwner());
    if (!OwnerPawn || !Defender)
    {
        return false;
    }

    // The archetype tuning is the game mode's when there is one; a world without one (headless
    // tests) resolves the hit with the defaults, as the pawn's hitpoints do.
    const APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    static const FPSArchetypeTuning DefaultArchetypeTuning;
    const FPSArchetypeTuning& ArchetypeTuning = GM ? GM->ArchetypeTuningSettings : DefaultArchetypeTuning;

    FPlayerAttributes CarrierAttr = OwnerPawn->GetAttributes();
    FPlayerAttributes DefenderAttr = Defender->GetAttributes();

    float CarrierSpeed = OwnerPawn->GetVelocity().Size();
    float DefenderSpeed = Defender->GetVelocity().Size();

    // The carrier's move (Epic 104.2) and the tackler's strip attempt (Epic 104.5) change the
    // odds; a carrier who slid is simply down.
    const UPSCarrierMoveComponent* Moves = OwnerPawn->GetCarrierMoveComponent();
    const UPSDefenderTechniqueComponent* Technique = Defender->GetDefenderTechniqueComponent();
    const bool bGaveUp = Moves && Moves->HasGivenUp();
    const float OddsMultiplier = (Moves ? Moves->GetTackleChanceMultiplier() : 1.f) * (Technique ? Technique->GetTackleChanceScale() : 1.f);
    const float TackleChance = bGaveUp ? 1.f
        : PSBallResolutionHelpers::ComputeTackleChance(CarrierAttr, DefenderAttr, CarrierSpeed, DefenderSpeed, OddsMultiplier);

    // The contest's rolls are the carrier's, on the play's seeded streams (Epic 108).
    const FName CarrierId = CarrierAttr.PlayerId;
    float Roll = UPSNetRandomStreams::RollFor(this, TEXT("Tackle"), CarrierId);
    if (Roll <= TackleChance)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Tackle SUCCESS! Defender %s tackled carrier %s (Roll: %.2f <= Chance: %.2f)"), 
            *DefenderAttr.DisplayName, *CarrierAttr.DisplayName, Roll, TackleChance);

        // Fumble chance check (a slide protects the ball; a strip attempt rips at it)
        const float FumbleChance = PSBallResolutionHelpers::ComputeFumbleChance(DefenderSpeed, Technique ? Technique->GetFumbleChanceBonus() : 0.f);
        if (!bGaveUp && UPSNetRandomStreams::RollFor(this, TEXT("TackleFumble"), CarrierId) <= FumbleChance)
        {
            FumbleBall();
            return true;
        }

        // Apply physics impulse (knockback)
        FVector KnockbackDir = OwnerPawn->GetActorLocation() - Defender->GetActorLocation();
        KnockbackDir.Z = 0.f;
        if (!KnockbackDir.IsNearlyZero())
        {
            KnockbackDir.Normalize();
        }
        else
        {
            KnockbackDir = -OwnerPawn->GetActorForwardVector();
        }

        if (OwnerPawn->GetFloatingMovementComponent())
        {
            OwnerPawn->GetFloatingMovementComponent()->Velocity = KnockbackDir * 150.f;
        }

        // Down by contact
        if (OwnerPawn->GetFloatingMovementComponent())
        {
            OwnerPawn->GetFloatingMovementComponent()->Velocity = FVector::ZeroVector;
            OwnerPawn->GetFloatingMovementComponent()->StopActiveMovement();
        }

        int32 YardsGained = FMath::RoundToInt((OwnerPawn->GetActorLocation().X - OwnerPawn->GetStartingLocation().X) / 100.f);

        // Hitpoint resolution (Epic 139): a successful tackle deals damage rather than
        // automatically ending the play -- the snap isn't over until the carrier is
        // downed (hitpoints reach 0). A carrier who survives the hit has broken the
        // tackle and keeps the play alive.
        bool bCarrierDowned = true;
        UPSHealthComponent* CarrierHealth = OwnerPawn->GetHealthComponent();
        if (CarrierHealth && !bGaveUp)
        {
            // A fresh model's stream starts at seed 0, so unseeded every hit drew the same spread;
            // seeded from the carrier's stream, each hit draws its own, the same on a replay.
            UPSCombatRulesModel* CombatRules = NewObject<UPSCombatRulesModel>(this);
            CombatRules->SeedDeterminism(UPSNetRandomStreams::RollSeedFor(this, TEXT("TackleDamage"), CarrierId));
            const float Damage = CombatRules->ResolveTackleDamage(CarrierAttr, DefenderAttr, ArchetypeTuning);
            bCarrierDowned = CarrierHealth->ApplyDamage(Damage);

            if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
            {
                FPSTelemetryDamageEvent DamageEvt;
                DamageEvt.TargetName = CarrierAttr.DisplayName;
                DamageEvt.Amount = Damage;
                DamageEvt.RemainingHitPoints = CarrierHealth->GetCurrentHitPoints();
                Bus->PublishDamage(DamageEvt);

                if (bCarrierDowned)
                {
                    FPSTelemetryDeathEvent DeathEvt;
                    DeathEvt.PlayerName = CarrierAttr.DisplayName;
                    DeathEvt.Cause = EPSDeathCause::TackleDamage;
                    Bus->PublishDeath(DeathEvt);
                }
            }
        }

        if (bCarrierDowned)
        {
            // The tackle goes out on the bus (rule 5): the play simulation, the outcome authority,
            // records it from there (one path, rule 6), and the stats, cameras, rumble, overlays
            // and controllers hear the same event. The spot is the yard line he went down on
            // (the game mode places the line of scrimmage at YardLine * 100 cm).
            if (UPSTelemetryBus* Bus = GetWorld() ? GetWorld()->GetSubsystem<UPSTelemetryBus>() : nullptr)
            {
                FPSTelemetryTackleEvent TackleEvt;
                TackleEvt.TacklerName = DefenderAttr.DisplayName;
                TackleEvt.BallCarrierName = CarrierAttr.DisplayName;
                TackleEvt.YardLine = FMath::Clamp(FMath::RoundToInt(OwnerPawn->GetActorLocation().X / 100.f), 0, 100);
                TackleEvt.YardsGained = YardsGained;
                // A quarterback still holding the ball, brought down behind where he lined up
                // (itself behind the line), was sacked.
                TackleEvt.bIsSack = CarrierAttr.Role == EPlayerRole::Quarterback && YardsGained < 0;
                Bus->PublishTackle(TackleEvt);
            }
        }
        else
        {
            UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Carrier %s survived the hit (%.1f HP remaining) -- play continues."),
                *CarrierAttr.DisplayName, CarrierHealth ? CarrierHealth->GetCurrentHitPoints() : 0.f);
        }
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Tackle BROKEN! Carrier %s broke tackle from defender %s (Roll: %.2f > Chance: %.2f)"), 
            *CarrierAttr.DisplayName, *DefenderAttr.DisplayName, Roll, TackleChance);

        if (OwnerPawn->GetFloatingMovementComponent())
        {
            OwnerPawn->GetFloatingMovementComponent()->Velocity *= 0.5f;
        }
        return false;
    }
}
