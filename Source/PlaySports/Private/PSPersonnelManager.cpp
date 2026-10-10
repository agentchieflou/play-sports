// PSPersonnelManager.cpp - Epic 19.5: who is on the field, by personnel package
#include "PSPersonnelManager.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSHealthComponent.h"
#include "PSPlayerPawn.h"
#include "PSRoster.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSPersonnel
{
    /** Players a side puts on the field: a rule of the game, not tuning. */
    constexpr int32 PlayersPerSide = 11;

    bool ParseRole(const FString& RoleName, EPlayerRole& OutRole)
    {
        const UEnum* RoleEnum = StaticEnum<EPlayerRole>();
        const int32 RoleIndex = RoleEnum ? RoleEnum->GetIndexByNameString(RoleName) : INDEX_NONE;
        if (RoleIndex == INDEX_NONE)
        {
            return false;
        }
        OutRole = static_cast<EPlayerRole>(RoleEnum->GetValueByIndex(RoleIndex));
        return true;
    }

    bool IsOffense(EPlayerRole Role)
    {
        return APSFieldGrid::GetSideForRole(Role) == EPSTeamSide::Offense;
    }

    EPSTeamSide ToSide(bool bOffense)
    {
        return bOffense ? EPSTeamSide::Offense : EPSTeamSide::Defense;
    }
}

FString UPSPersonnelManager::GetDefaultCatalogPath()
{
    return FPaths::ProjectDir() / TEXT("Data/personnel_packages.json");
}

void UPSPersonnelManager::Initialize(UPSRoster* InRoster)
{
    Roster = InRoster;
}

bool UPSPersonnelManager::LoadCatalogFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPersonnelCatalog Loaded;
    if (!Ingestion || !Ingestion->LoadPersonnelCatalogFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: Could not load personnel packages from %s."), *JsonFilePath);
        return false;
    }

    for (const FString& Problem : ValidateCatalog(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: %s"), *Problem);
    }
    Catalog = Loaded;
    UE_LOG(LogTemp, Display, TEXT("UPSPersonnelManager: Loaded %d personnel packages from %s."), Catalog.Packages.Num(), *JsonFilePath);
    return true;
}

TArray<FString> UPSPersonnelManager::ValidateCatalog(const FPSPersonnelCatalog& InCatalog)
{
    TArray<FString> Problems;
    if (InCatalog.Packages.Num() == 0)
    {
        Problems.Add(TEXT("No personnel packages."));
    }
    if (InCatalog.FatigueSubstitutionThreshold < 0.f || InCatalog.FatigueSubstitutionThreshold > 1.f)
    {
        Problems.Add(FString::Printf(TEXT("FatigueSubstitutionThreshold %.2f must be between 0 and 1."), InCatalog.FatigueSubstitutionThreshold));
    }

    TSet<FName> SeenIds;
    TMap<FString, FName> OffenseFormationOwners;
    TMap<FString, FName> DefenseFormationOwners;
    for (const FPSPersonnelPackage& Package : InCatalog.Packages)
    {
        const FString Id = Package.PackageId.ToString();
        if (Package.PackageId.IsNone())
        {
            Problems.Add(TEXT("A package has no PackageId."));
        }
        else if (SeenIds.Contains(Package.PackageId))
        {
            Problems.Add(FString::Printf(TEXT("Package %s: duplicate PackageId."), *Id));
        }
        SeenIds.Add(Package.PackageId);

        if (Package.DisplayName.TrimStartAndEnd().IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("Package %s: no DisplayName."), *Id));
        }

        int32 Total = 0;
        int32 Quarterbacks = 0;
        int32 Linemen = 0;
        for (const TPair<FString, int32>& RoleCount : Package.RoleCounts)
        {
            EPlayerRole Role = EPlayerRole::Quarterback;
            if (!PSPersonnel::ParseRole(RoleCount.Key, Role))
            {
                Problems.Add(FString::Printf(TEXT("Package %s: '%s' is not an EPlayerRole."), *Id, *RoleCount.Key));
                continue;
            }
            if (PSPersonnel::IsOffense(Role) != Package.bOffense)
            {
                Problems.Add(FString::Printf(TEXT("Package %s: %s doesn't play on %s."), *Id, *RoleCount.Key, Package.bOffense ? TEXT("offense") : TEXT("defense")));
            }
            if (RoleCount.Value < 0)
            {
                Problems.Add(FString::Printf(TEXT("Package %s: %s count %d is negative."), *Id, *RoleCount.Key, RoleCount.Value));
            }
            Total += RoleCount.Value;
            if (Role == EPlayerRole::Quarterback)
            {
                Quarterbacks += RoleCount.Value;
            }
            else if (Role == EPlayerRole::OffensiveLineman)
            {
                Linemen += RoleCount.Value;
            }
        }
        if (Total != PSPersonnel::PlayersPerSide)
        {
            Problems.Add(FString::Printf(TEXT("Package %s fields %d players, not %d."), *Id, Total, PSPersonnel::PlayersPerSide));
        }
        if (Package.bOffense && (Quarterbacks < 1 || Linemen < 1))
        {
            Problems.Add(FString::Printf(TEXT("Package %s: an offense needs a Quarterback and an OffensiveLineman to snap to him."), *Id));
        }

        TMap<FString, FName>& Owners = Package.bOffense ? OffenseFormationOwners : DefenseFormationOwners;
        for (const FString& Formation : Package.Formations)
        {
            if (Formation.TrimStartAndEnd().IsEmpty())
            {
                Problems.Add(FString::Printf(TEXT("Package %s: an empty formation name."), *Id));
            }
            else if (const FName* Owner = Owners.Find(Formation))
            {
                Problems.Add(FString::Printf(TEXT("Formation '%s' brings on both %s and %s."), *Formation, *Owner->ToString(), *Id));
            }
            else
            {
                Owners.Add(Formation, Package.PackageId);
            }
        }
    }

    for (const bool bOffense : { true, false })
    {
        const FName DefaultId = bOffense ? InCatalog.DefaultOffensePackage : InCatalog.DefaultDefensePackage;
        const FPSPersonnelPackage* Default = InCatalog.Packages.FindByPredicate([DefaultId](const FPSPersonnelPackage& Candidate) { return Candidate.PackageId == DefaultId; });
        if (!Default || Default->bOffense != bOffense)
        {
            Problems.Add(FString::Printf(TEXT("%s '%s' is not %s package."),
                bOffense ? TEXT("DefaultOffensePackage") : TEXT("DefaultDefensePackage"), *DefaultId.ToString(), bOffense ? TEXT("an offensive") : TEXT("a defensive")));
        }
    }
    return Problems;
}

const FPSPersonnelPackage* UPSPersonnelManager::FindPackage(FName PackageId) const
{
    return Catalog.Packages.FindByPredicate([PackageId](const FPSPersonnelPackage& Candidate) { return Candidate.PackageId == PackageId; });
}

FName UPSPersonnelManager::GetPackageForFormation(const FString& Formation, bool bOffense) const
{
    for (const FPSPersonnelPackage& Package : Catalog.Packages)
    {
        if (Package.bOffense == bOffense && Package.Formations.Contains(Formation))
        {
            return Package.PackageId;
        }
    }
    return bOffense ? Catalog.DefaultOffensePackage : Catalog.DefaultDefensePackage;
}

bool UPSPersonnelManager::SelectPlayers(const FPSPersonnelPackage& Package, const TSet<FName>& Excluded, TArray<FName>& OutPlayerIds) const
{
    OutPlayerIds.Reset();
    if (!Roster)
    {
        return false;
    }

    for (const TPair<FString, int32>& RoleCount : Package.RoleCounts)
    {
        EPlayerRole Role = EPlayerRole::Quarterback;
        if (!PSPersonnel::ParseRole(RoleCount.Key, Role))
        {
            return false;
        }

        const TArray<FName> DepthChart = Roster->GetDepthChartForRole(Role);
        int32 Picked = 0;
        // Pass 0 takes rested players; pass 1 takes tired ones, only while the role is short.
        for (int32 Pass = 0; Pass < 2 && Picked < RoleCount.Value; ++Pass)
        {
            for (const FName& PlayerId : DepthChart)
            {
                if (Picked >= RoleCount.Value)
                {
                    break;
                }
                const bool bResting = RestingPlayerIds.Contains(PlayerId);
                if ((Pass == 0) == bResting || Excluded.Contains(PlayerId) || OutPlayerIds.Contains(PlayerId)
                    || !Roster->IsAvailableForPlay(PlayerId, CurrentPlayIndex))
                {
                    continue;
                }
                OutPlayerIds.Add(PlayerId);
                ++Picked;
            }
        }
        if (Picked < RoleCount.Value)
        {
            return false;
        }
    }
    return true;
}

TArray<const FPlayerAttributes*> UPSPersonnelManager::GetStartingLineup() const
{
    TArray<const FPlayerAttributes*> Lineup;
    if (!Roster)
    {
        return Lineup;
    }

    for (const bool bOffense : { true, false })
    {
        const FName DefaultId = bOffense ? Catalog.DefaultOffensePackage : Catalog.DefaultDefensePackage;
        const FPSPersonnelPackage* Default = FindPackage(DefaultId);
        TArray<FName> Starters;
        const bool bFilled = Default && Default->bOffense == bOffense && SelectPlayers(*Default, TSet<FName>(), Starters);
        if (!bFilled)
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: The roster can't field the %s default package %s; every %s player starts."),
                bOffense ? TEXT("offense's") : TEXT("defense's"), *DefaultId.ToString(), bOffense ? TEXT("offensive") : TEXT("defensive"));
        }

        // Short of the package, every player on the side's depth chart starts (with both teams
        // on the roster, only the side's own team: UPSFieldSides).
        for (const FPlayerAttributes& Player : Roster->GetFullRoster())
        {
            if (PSPersonnel::IsOffense(Player.Role) == bOffense
                && (bFilled ? Starters.Contains(Player.PlayerId) : Roster->GetDepthChartForRole(Player.Role).Contains(Player.PlayerId)))
            {
                Lineup.Add(&Player);
            }
        }
    }
    return Lineup;
}

void UPSPersonnelManager::BindPawns(const TArray<APSPlayerPawn*>& Pawns)
{
    BoundPawns.Reset();
    for (APSPlayerPawn* Pawn : Pawns)
    {
        if (Pawn)
        {
            BoundPawns.Add(Pawn);
        }
    }

    for (const bool bOffense : { true, false })
    {
        const FName DefaultId = bOffense ? Catalog.DefaultOffensePackage : Catalog.DefaultDefensePackage;
        const FPSPersonnelPackage* Default = FindPackage(DefaultId);
        TArray<FName> Starters;
        FName& SidePackage = bOffense ? OffensePackage : DefensePackage;
        SidePackage = NAME_None;
        if (Default && Default->bOffense == bOffense && SelectPlayers(*Default, TSet<FName>(), Starters))
        {
            SidePackage = DefaultId;
        }
    }
}

void UPSPersonnelManager::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    BoundBus = Bus;
    PlayCallHandle = Bus->OnPlayCallMC.AddUObject(this, &UPSPersonnelManager::HandlePlayCall);
}

void UPSPersonnelManager::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.Remove(PlayCallHandle);
    }
    PlayCallHandle.Reset();
    BoundBus.Reset();
}

void UPSPersonnelManager::BeginNewPlay(float ScrimmageX, int32 PlayIndex)
{
    LineOfScrimmageX = ScrimmageX;
    CurrentPlayIndex = PlayIndex;
    RestingPlayerIds.Reset();
    if (!Roster)
    {
        return;
    }

    // Epic 19.3's hook: a tired player with someone behind him on the depth chart rests a play.
    TMap<FName, float> StaminaRatios;
    for (const APSPlayerPawn* Pawn : GetAllBoundPawns())
    {
        if (Pawn->MaxStamina > 0.f)
        {
            StaminaRatios.Add(Pawn->GetAttributes().PlayerId, Pawn->CurrentStamina / Pawn->MaxStamina);
        }
    }
    for (const TPair<FName, FName>& Substitution : Roster->EvaluateFatigueSubstitutions(StaminaRatios, Catalog.FatigueSubstitutionThreshold))
    {
        RestingPlayerIds.Add(Substitution.Key);
    }

    for (const bool bOffense : { true, false })
    {
        const FName SidePackage = GetCurrentPackage(bOffense);
        if (!SidePackage.IsNone())
        {
            ApplyPackageToSide(bOffense, SidePackage);
        }
    }

    // A player due to sit out whom nobody could replace plays, and starts the play fresh like
    // everyone else on the field.
    for (APSPlayerPawn* Pawn : GetAllBoundPawns())
    {
        const FName PlayerId = Pawn->GetAttributes().PlayerId;
        if (!Roster->IsAvailableForPlay(PlayerId, CurrentPlayIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: %s should sit out, but nobody on the roster can take his place."), *PlayerId.ToString());
            if (const UPSHealthComponent* Health = Pawn->GetHealthComponent())
            {
                Roster->RespawnForNewPlay(PlayerId, Health->GetMaxHitPoints());
            }
        }
    }
}

bool UPSPersonnelManager::ApplyPackage(FName PackageId)
{
    const FPSPersonnelPackage* Package = FindPackage(PackageId);
    return Package && ApplyPackageToSide(Package->bOffense, PackageId);
}

TArray<FName> UPSPersonnelManager::GetOnFieldPlayerIds(bool bOffense) const
{
    TArray<FName> PlayerIds;
    for (const APSPlayerPawn* Pawn : GetBoundPawns(bOffense))
    {
        PlayerIds.Add(Pawn->GetAttributes().PlayerId);
    }
    return PlayerIds;
}

void UPSPersonnelManager::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    if (BoundPawns.Num() > 0)
    {
        ApplyPackageToSide(Event.bOffense, GetPackageForFormation(Event.Formation, Event.bOffense));
    }
}

bool UPSPersonnelManager::ApplyPackageToSide(bool bOffense, FName PackageId)
{
    if (!Roster || BoundPawns.Num() == 0)
    {
        return false;
    }

    const TSet<FName> Excluded = GetPlayersOnOtherPawns();
    const FPSPersonnelPackage* Package = FindPackage(PackageId);
    TArray<FName> Selected;
    if (!Package || Package->bOffense != bOffense || !SelectPlayers(*Package, Excluded, Selected))
    {
        const FName DefaultId = bOffense ? Catalog.DefaultOffensePackage : Catalog.DefaultDefensePackage;
        const FPSPersonnelPackage* Fallback = FindPackage(DefaultId);
        if (!Fallback || Fallback == Package || Fallback->bOffense != bOffense || !SelectPlayers(*Fallback, Excluded, Selected))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: The roster can't field %s or the %s default %s; personnel unchanged."),
                *PackageId.ToString(), bOffense ? TEXT("offense's") : TEXT("defense's"), *DefaultId.ToString());
            return false;
        }
        UE_LOG(LogTemp, Display, TEXT("UPSPersonnelManager: The roster can't field %s; %s comes on instead."), *PackageId.ToString(), *DefaultId.ToString());
        Package = Fallback;
    }

    // Players already on the field keep their pawns; the side's other pawns take the rest.
    TArray<FName> Incoming = Selected;
    TArray<APSPlayerPawn*> Outgoing;
    for (APSPlayerPawn* Pawn : GetBoundPawns(bOffense))
    {
        if (Incoming.Remove(Pawn->GetAttributes().PlayerId) == 0)
        {
            Outgoing.Add(Pawn);
        }
    }
    if (Outgoing.Num() != Incoming.Num())
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPersonnelManager: %s has %d pawns to change for %d incoming players."),
            *Package->PackageId.ToString(), Outgoing.Num(), Incoming.Num());
    }

    // An incoming player takes the pawn of an outgoing player of his own role when there is
    // one: the pawn keeps its spot, and the snapper the ball, through a like-for-like swap (a
    // tired player's backup; the other team coming on after a change of possession).
    TArray<APSPlayerPawn*> IncomingPawns;
    IncomingPawns.Init(nullptr, Incoming.Num());
    for (int32 IncomingIndex = 0; IncomingIndex < Incoming.Num(); ++IncomingIndex)
    {
        const FPlayerAttributes* Row = Roster->FindPlayerPtr(Incoming[IncomingIndex]);
        const int32 SameRole = Row ? Outgoing.IndexOfByPredicate([Row](const APSPlayerPawn* Pawn) { return Pawn->GetAttributes().Role == Row->Role; }) : INDEX_NONE;
        if (SameRole != INDEX_NONE)
        {
            IncomingPawns[IncomingIndex] = Outgoing[SameRole];
            Outgoing.RemoveAt(SameRole);
        }
    }
    for (int32 IncomingIndex = 0; IncomingIndex < Incoming.Num() && Outgoing.Num() > 0; ++IncomingIndex)
    {
        if (!IncomingPawns[IncomingIndex])
        {
            IncomingPawns[IncomingIndex] = Outgoing[0];
            Outgoing.RemoveAt(0);
        }
    }

    FPSTelemetryPersonnelEvent Change;
    Change.bOffense = bOffense;
    Change.PackageId = Package->PackageId;
    Change.PackageName = Package->DisplayName;
    for (int32 IncomingIndex = 0; IncomingIndex < Incoming.Num(); ++IncomingIndex)
    {
        APSPlayerPawn* Pawn = IncomingPawns[IncomingIndex];
        const FName Leaving = Pawn ? Pawn->GetAttributes().PlayerId : NAME_None;
        if (Pawn && PutPlayerOnPawn(*Pawn, Incoming[IncomingIndex]))
        {
            Change.PlayersOut.Add(Leaving);
            Change.PlayersIn.Add(Incoming[IncomingIndex]);
        }
    }

    FName& SidePackage = bOffense ? OffensePackage : DefensePackage;
    const bool bPackageChanged = SidePackage != Package->PackageId;
    SidePackage = Package->PackageId;

    if (Change.PlayersIn.Num() > 0)
    {
        LineUpSide(bOffense);
    }
    if (bPackageChanged || Change.PlayersIn.Num() > 0)
    {
        UE_LOG(LogTemp, Display, TEXT("UPSPersonnelManager: %s in %s (%d substitutions)."),
            bOffense ? TEXT("Offense") : TEXT("Defense"), *Package->DisplayName, Change.PlayersIn.Num());
        if (UPSTelemetryBus* Bus = BoundBus.Get())
        {
            Bus->PublishPersonnel(Change);
        }
    }
    return true;
}

bool UPSPersonnelManager::PutPlayerOnPawn(APSPlayerPawn& Pawn, FName PlayerId)
{
    const FPlayerAttributes* Row = Roster ? Roster->FindPlayerPtr(PlayerId) : nullptr;
    if (!Row)
    {
        return false;
    }

    const FPlayerAttributes Leaving = Pawn.GetAttributes();
    const bool bHadBall = Pawn.HasPossession();
    const bool bHumanControlled = Pawn.IsUserControlled();
    if (bHumanControlled)
    {
        PublishControlChange(Leaving, false);
    }

    Pawn.InitializePlayerPointer(Row);
    // Initializing clears the pawn's possession flag; the ball is still in its hands.
    if (bHadBall)
    {
        Pawn.GainPossession();
    }
    // The incoming player starts the play fresh, like everyone lined up for it.
    if (const UPSHealthComponent* Health = Pawn.GetHealthComponent())
    {
        Roster->RespawnForNewPlay(PlayerId, Health->GetMaxHitPoints());
    }
    if (bHumanControlled)
    {
        PublishControlChange(*Row, true);
    }

    UE_LOG(LogTemp, Display, TEXT("UPSPersonnelManager: %s (%s) comes on for %s (%s)."),
        *Row->DisplayName, *PlayerId.ToString(), *Leaving.DisplayName, *Leaving.PlayerId.ToString());
    return true;
}

void UPSPersonnelManager::LineUpSide(bool bOffense)
{
    TArray<APSPlayerPawn*> SidePawns = GetBoundPawns(bOffense);
    if (UWorld* World = GetPawnWorld())
    {
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            if (!IsBound(*It) && It->TeamSide == PSPersonnel::ToSide(bOffense))
            {
                SidePawns.Add(*It);
            }
        }
    }

    TArray<EPlayerRole> Roles;
    for (const APSPlayerPawn* Pawn : SidePawns)
    {
        Roles.Add(Pawn->GetAttributes().Role);
    }
    const TArray<FVector> Lineup = APSFieldGrid::ComputeLineup(Roles, LineOfScrimmageX);
    for (int32 PawnIndex = 0; PawnIndex < SidePawns.Num() && PawnIndex < Lineup.Num(); ++PawnIndex)
    {
        SidePawns[PawnIndex]->SetActorLocation(Lineup[PawnIndex], false, nullptr, ETeleportType::TeleportPhysics);
        SidePawns[PawnIndex]->SetStartingLocation(Lineup[PawnIndex]);
    }
}

TArray<APSPlayerPawn*> UPSPersonnelManager::GetAllBoundPawns() const
{
    TArray<APSPlayerPawn*> Pawns;
    for (const TWeakObjectPtr<APSPlayerPawn>& Bound : BoundPawns)
    {
        if (APSPlayerPawn* Pawn = Bound.Get())
        {
            Pawns.Add(Pawn);
        }
    }
    return Pawns;
}

TArray<APSPlayerPawn*> UPSPersonnelManager::GetBoundPawns(bool bOffense) const
{
    TArray<APSPlayerPawn*> Pawns = GetAllBoundPawns();
    Pawns.RemoveAll([bOffense](const APSPlayerPawn* Pawn) { return Pawn->TeamSide != PSPersonnel::ToSide(bOffense); });
    return Pawns;
}

bool UPSPersonnelManager::IsBound(const APSPlayerPawn* Pawn) const
{
    for (const TWeakObjectPtr<APSPlayerPawn>& Bound : BoundPawns)
    {
        if (Bound.Get() == Pawn)
        {
            return true;
        }
    }
    return false;
}

UWorld* UPSPersonnelManager::GetPawnWorld() const
{
    for (const TWeakObjectPtr<APSPlayerPawn>& Bound : BoundPawns)
    {
        if (const APSPlayerPawn* Pawn = Bound.Get())
        {
            return Pawn->GetWorld();
        }
    }
    return nullptr;
}

TSet<FName> UPSPersonnelManager::GetPlayersOnOtherPawns() const
{
    TSet<FName> PlayerIds;
    if (UWorld* World = GetPawnWorld())
    {
        for (TActorIterator<APSPlayerPawn> It(World); It; ++It)
        {
            if (!IsBound(*It))
            {
                PlayerIds.Add(It->GetAttributes().PlayerId);
            }
        }
    }
    return PlayerIds;
}

void UPSPersonnelManager::PublishControlChange(const FPlayerAttributes& Player, bool bHumanControlled) const
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryControlChangeEvent ControlChange;
    ControlChange.PlayerName = Player.DisplayName;
    ControlChange.PlayerId = Player.PlayerId;
    ControlChange.bHumanControlled = bHumanControlled;
    Bus->PublishControlChange(ControlChange);
}
