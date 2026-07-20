// PSBallActionComponent.cpp - Epic C3: extracted ball-action logic from APSPlayerPawn
#include "PSBallActionComponent.h"
#include "PSPlayerPawn.h"
#include "PSBall.h"
#include "PSGameMode.h"
#include "PSPlaySimulation.h"
#include "PSHealthComponent.h"
#include "PSCombatRulesModel.h"
#include "PSTelemetryBus.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "Engine/World.h"

UPSBallActionComponent::UPSBallActionComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

bool UPSBallActionComponent::ThrowPass(APSBall* Ball, const FVector& TargetLocation, bool bHighArc, APSPlayerPawn* IntendedTarget)
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
    float LaunchSpeed = 1500.f + (OwnerPawn->GetAttributes().Strength * 15.f);

    // Apply accuracy scatter to the target point based on Awareness (lower awareness = more error)
    FVector ScatterTarget = TargetLocation;
    if (OwnerPawn->GetAttributes().Awareness < 100.f)
    {
        float AccuracyError = (100.f - OwnerPawn->GetAttributes().Awareness) * 2.f; // Max error up to 200cm
        FVector ErrorOffset = FMath::VRand() * FMath::FRandRange(0.f, AccuracyError);
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

    if (bSuccess)
    {
        Ball->Launch(OutVelocity);
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

    APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (GM && GM->ActiveBall)
    {
        if (OwnerPawn->TransferPossessionTo(TargetPlayer))
        {
            GM->ActiveBall->AttachToCarrier(TargetPlayer, TEXT("HandSocket"));
            UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Executed handoff from %s to %s."), *OwnerPawn->GetAttributes().DisplayName, *TargetPlayer->GetAttributes().DisplayName);
            return true;
        }
    }
    return false;
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

    APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (!GM || !GM->ActiveBall)
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
        GM->ActiveBall->Launch(OutVelocity);
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

    APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (GM && GM->ActiveBall)
    {
        FVector FumbleVelocity = OwnerPawn->GetActorForwardVector() * 300.f + FVector(0.f, 0.f, 200.f);
        FumbleVelocity += FMath::VRand() * 100.f;
        FumbleVelocity.Z = FMath::Max(50.f, FumbleVelocity.Z);

        GM->ActiveBall->Fumble(FumbleVelocity);
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

    APSGameMode* GM = Cast<APSGameMode>(UGameplayStatics::GetGameMode(this));
    if (!GM)
    {
        return false;
    }

    FPlayerAttributes CarrierAttr = OwnerPawn->GetAttributes();
    FPlayerAttributes DefenderAttr = Defender->GetAttributes();

    float CarrierSpeed = OwnerPawn->GetVelocity().Size();
    float DefenderSpeed = Defender->GetVelocity().Size();

    float DefenderPower = DefenderAttr.Strength * 0.5f + (DefenderSpeed * 0.1f);
    float CarrierPower = CarrierAttr.Strength * 0.3f + CarrierAttr.Agility * 0.3f + (CarrierSpeed * 0.05f);

    float TackleChance = 0.50f + (DefenderPower - CarrierPower) * 0.005f;
    TackleChance = FMath::Clamp(TackleChance, 0.10f, 0.95f);

    float Roll = FMath::FRand();
    if (Roll <= TackleChance)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSBallActionComponent: Tackle SUCCESS! Defender %s tackled carrier %s (Roll: %.2f <= Chance: %.2f)"), 
            *DefenderAttr.DisplayName, *CarrierAttr.DisplayName, Roll, TackleChance);

        // Fumble chance check
        float FumbleChance = 0.02f + (DefenderSpeed * 0.0001f);
        FumbleChance = FMath::Clamp(FumbleChance, 0.01f, 0.25f);
        if (FMath::FRand() <= FumbleChance)
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
        if (CarrierHealth)
        {
            UPSCombatRulesModel* CombatRules = NewObject<UPSCombatRulesModel>(this);
            const float Damage = CombatRules->ResolveTackleDamage(CarrierAttr, DefenderAttr, GM->ArchetypeTuningSettings);
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
            if (GM->PlaySimulation)
            {
                GM->PlaySimulation->RecordTackle(YardsGained);
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
