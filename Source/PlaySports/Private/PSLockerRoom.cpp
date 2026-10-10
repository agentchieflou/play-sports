#include "PSLockerRoom.h"
#include "PSContractManager.h"
#include "PSContractNegotiation.h"
#include "PSDataIngestion.h"
#include "PSFranchiseSaveGame.h"
#include "PSPersonnelManager.h"
#include "PSRoster.h"
#include "Misc/Paths.h"

namespace PSLockerRoomPrivate
{
    static FString RoleName(EPlayerRole Role)
    {
        return StaticEnum<EPlayerRole>()->GetNameStringByValue(static_cast<int64>(Role));
    }

    static FString Signed(float Value)
    {
        return FString::Printf(TEXT("%s%.2f"), Value >= 0.f ? TEXT("+") : TEXT(""), Value);
    }

    static void AddFactor(FPSPlayerMorale& Morale, const TCHAR* Factor, float Delta, const FString& Description)
    {
        FPSMoraleFactor& Added = Morale.Factors.AddDefaulted_GetRef();
        Added.Factor = FName(Factor);
        Added.Delta = Delta;
        Added.Description = Description;
    }

    static bool SameLineup(TArray<FName> A, TArray<FName> B)
    {
        A.Sort([](const FName& X, const FName& Y) { return X.LexicalLess(Y); });
        B.Sort([](const FName& X, const FName& Y) { return X.LexicalLess(Y); });
        return A == B;
    }
}

FString UPSLockerRoom::GetDefaultTuningPath()
{
    return FPaths::ProjectDir() / TEXT("Data/morale.json");
}

bool UPSLockerRoom::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSMoraleTuning Loaded;
    if (!Ingestion->LoadMoraleTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLockerRoom: Could not load the morale tuning from %s."), *JsonFilePath);
        return false;
    }
    for (const FString& Problem : ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLockerRoom: %s"), *Problem);
    }
    Tuning = Loaded;
    return true;
}

bool UPSLockerRoom::LoadStartersFromPersonnel(const FString& PersonnelJsonPath)
{
    UPSPersonnelManager* Personnel = NewObject<UPSPersonnelManager>(this);
    if (!Personnel->LoadCatalogFromJson(PersonnelJsonPath))
    {
        return false;
    }

    StarterCounts.Reset();
    const UEnum* Roles = StaticEnum<EPlayerRole>();
    for (const FName PackageId : { Personnel->GetCatalog().DefaultOffensePackage, Personnel->GetCatalog().DefaultDefensePackage })
    {
        const FPSPersonnelPackage* Package = Personnel->FindPackage(PackageId);
        if (!Package)
        {
            continue;
        }
        for (const TPair<FString, int32>& Count : Package->RoleCounts)
        {
            const int64 Value = Roles->GetValueByNameString(Count.Key);
            if (Value != INDEX_NONE)
            {
                StarterCounts.Add(static_cast<EPlayerRole>(Value), Count.Value);
            }
        }
    }
    return StarterCounts.Num() > 0;
}

bool UPSLockerRoom::LoadDefaults()
{
    const bool bTuning = LoadTuningFromJson(GetDefaultTuningPath());
    const bool bStarters = LoadStartersFromPersonnel(UPSPersonnelManager::GetDefaultCatalogPath());
    return bTuning && bStarters;
}

TArray<FString> UPSLockerRoom::ValidateTuning(const FPSMoraleTuning& InTuning)
{
    TArray<FString> Problems;
    for (const float Fraction : { InTuning.UnderpaidRatio, InTuning.MoraleInertia, InTuning.TradeRequestMorale, InTuning.HoldoutPayRatio,
        InTuning.HoldoutMorale, InTuning.LeaderWinPercentage, InTuning.LeaderMorale })
    {
        if (Fraction < 0.f || Fraction > 1.f)
        {
            Problems.Add(TEXT("UnderpaidRatio, MoraleInertia, TradeRequestMorale, HoldoutPayRatio, HoldoutMorale, LeaderWinPercentage and LeaderMorale are 0-1"));
            break;
        }
    }
    if (InTuning.MoraleInertia >= 1.f)
    {
        Problems.Add(TEXT("MoraleInertia must be below 1, or morale never moves"));
    }
    if (InTuning.PerformanceSwing < 0.f || InTuning.PerformanceSwing >= 1.f)
    {
        Problems.Add(TEXT("PerformanceSwing must be in [0, 1)"));
    }
    if (InTuning.TradeRequestWeeks < 1 || InTuning.MaxLeaders < 0)
    {
        Problems.Add(TEXT("TradeRequestWeeks must be at least 1 and MaxLeaders 0 or more"));
    }
    TSet<FName> Seen;
    for (const FPSChemistryUnit& Unit : InTuning.Units)
    {
        if (Unit.Unit.IsNone() || Seen.Contains(Unit.Unit))
        {
            Problems.Add(FString::Printf(TEXT("Units: empty or duplicate unit '%s'"), *Unit.Unit.ToString()));
        }
        Seen.Add(Unit.Unit);
        if (Unit.FullCohesionGames < 1 || Unit.MaxBonus < 0.f || Unit.MaxBonus >= 1.f)
        {
            Problems.Add(FString::Printf(TEXT("Units: %s needs FullCohesionGames of 1 or more and a MaxBonus in [0, 1)"), *Unit.Unit.ToString()));
        }
    }
    return Problems;
}

int32 UPSLockerRoom::GetStarterCount(EPlayerRole Role) const
{
    const int32* Count = StarterCounts.Find(Role);
    return Count ? *Count : 1;
}

TArray<FName> UPSLockerRoom::GetStarters(const UPSRoster* Roster, EPlayerRole Role) const
{
    TArray<FName> Order = Roster ? Roster->GetDepthChartForRole(Role) : TArray<FName>();
    if (Order.Num() == 0 && Roster)
    {
        for (const FPlayerAttributes& Player : Roster->GetFullRoster())
        {
            if (Player.Role == Role)
            {
                Order.Add(Player.PlayerId);
            }
        }
    }
    if (Order.Num() > GetStarterCount(Role))
    {
        Order.SetNum(GetStarterCount(Role));
    }
    return Order;
}

FPSPlayerMorale& UPSLockerRoom::FindOrAddPlayer(FName PlayerId)
{
    if (FPSPlayerMorale* Found = State.Players.FindByPredicate([PlayerId](const FPSPlayerMorale& Entry) { return Entry.PlayerId == PlayerId; }))
    {
        return *Found;
    }
    FPSPlayerMorale& Added = State.Players.AddDefaulted_GetRef();
    Added.PlayerId = PlayerId;
    return Added;
}

const FPSPlayerMorale* UPSLockerRoom::FindPlayer(FName PlayerId) const
{
    return State.Players.FindByPredicate([PlayerId](const FPSPlayerMorale& Entry) { return Entry.PlayerId == PlayerId; });
}

const FPSUnitChemistry* UPSLockerRoom::FindUnit(FName TeamId, FName Unit) const
{
    return State.Units.FindByPredicate([TeamId, Unit](const FPSUnitChemistry& Entry) { return Entry.TeamId == TeamId && Entry.Unit == Unit; });
}

const FPSChemistryUnit* UPSLockerRoom::FindUnitTuning(FName Unit) const
{
    return Tuning.Units.FindByPredicate([Unit](const FPSChemistryUnit& Entry) { return Entry.Unit == Unit; });
}

TArray<FPSLockerRoomEvent> UPSLockerRoom::EvaluateTeam(FName TeamId, const UPSRoster* Roster, float WinPercentage, const UPSContractManager* Contracts, bool bNewLeagueYear)
{
    using namespace PSLockerRoomPrivate;

    TArray<FPSLockerRoomEvent> Events;
    if (!Roster)
    {
        return Events;
    }
    const auto Announce = [&Events, TeamId](EPSLockerRoomEventKind Kind, FName PlayerId, const FString& Description)
    {
        FPSLockerRoomEvent& Event = Events.AddDefaulted_GetRef();
        Event.Kind = Kind;
        Event.PlayerId = PlayerId;
        Event.TeamId = TeamId;
        Event.Description = Description;
        UE_LOG(LogTemp, Display, TEXT("UPSLockerRoom: %s"), *Description);
    };

    // The leaders already in the room lift everyone else.
    TSet<FName> Leaders;
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        const FPSPlayerMorale* Known = FindPlayer(Player.PlayerId);
        if (Known && Known->bLeader)
        {
            Leaders.Add(Player.PlayerId);
        }
    }

    const float WinShare = FMath::Clamp(WinPercentage, 0.f, 1.f);
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        const FName PlayerId = Player.PlayerId;
        FPSPlayerMorale& Morale = FindOrAddPlayer(PlayerId);
        Morale.TeamId = TeamId;
        Morale.Factors.Reset();
        const float Rating = UPSContractNegotiation::RatePlayer(Player);

        // Playing time.
        const TArray<FName> Starters = GetStarters(Roster, Player.Role);
        const bool bStarter = Starters.Contains(PlayerId);
        if (bStarter)
        {
            AddFactor(Morale, TEXT("PlayingTime"), Tuning.StarterBonus, FString::Printf(TEXT("Starting at %s"), *RoleName(Player.Role)));
        }
        else
        {
            AddFactor(Morale, TEXT("PlayingTime"), -Tuning.BackupPenalty, FString::Printf(TEXT("Backing up at %s"), *RoleName(Player.Role)));
            const bool bBetter = Starters.ContainsByPredicate([Roster, Rating](FName StarterId)
            {
                const FPlayerAttributes* Starter = Roster->FindPlayerPtr(StarterId);
                return Starter && UPSContractNegotiation::RatePlayer(*Starter) < Rating;
            });
            if (bBetter)
            {
                AddFactor(Morale, TEXT("PlayingTime"), -Tuning.BetterThanStarterPenalty, TEXT("Rated above a starter ahead of him"));
            }
        }

        // Team success.
        AddFactor(Morale, TEXT("TeamSuccess"), Tuning.TeamSuccessWeight * (WinShare - 0.5f), FString::Printf(TEXT("Team winning %d%%"), FMath::RoundToInt(WinShare * 100.f)));

        // Contract: pay against his worth, and a deal's last year.
        float PayRatio = -1.f;
        const FPSContract* Contract = Contracts ? Contracts->FindContract(PlayerId) : nullptr;
        if (Contract && Contract->TeamId == TeamId)
        {
            const int32 Worth = Contracts->GetDemand(Contracts->MakeNegotiationContext(Player, Contracts->GetTuning().DefaultPlayerAge, Morale.Morale, TeamId)).AnnualValue;
            PayRatio = Worth > 0 ? static_cast<float>(Contracts->GetCapHit(PlayerId, Contracts->GetLeagueYear())) / Worth : 1.f;
            if (PayRatio < Tuning.UnderpaidRatio)
            {
                const float Shortfall = Tuning.UnderpaidRatio > 0.f ? (Tuning.UnderpaidRatio - PayRatio) / Tuning.UnderpaidRatio : 1.f;
                AddFactor(Morale, TEXT("Contract"), -Tuning.UnderpaidPenalty * FMath::Clamp(Shortfall, 0.f, 1.f), FString::Printf(TEXT("Paid %d%% of his worth"), FMath::RoundToInt(PayRatio * 100.f)));
            }
            else if (PayRatio >= 1.f)
            {
                AddFactor(Morale, TEXT("Contract"), Tuning.WellPaidBonus, TEXT("Paid his worth"));
            }
            if (Contract->GetLastYear() == Contracts->GetLeagueYear())
            {
                AddFactor(Morale, TEXT("Contract"), -Tuning.ContractYearPenalty, TEXT("In the last year of his deal"));
            }
        }

        // Leadership: the room's leaders, himself aside.
        const int32 Lifting = FMath::Min(Leaders.Num() - (Leaders.Contains(PlayerId) ? 1 : 0), FMath::Max(0, Tuning.MaxLeaders));
        if (Lifting > 0)
        {
            AddFactor(Morale, TEXT("Leadership"), Lifting * Tuning.LeaderBoost, FString::Printf(TEXT("%d leader(s) in the room"), Lifting));
        }

        float Target = 0.5f;
        for (const FPSMoraleFactor& Factor : Morale.Factors)
        {
            Target += Factor.Delta;
        }
        Morale.TargetMorale = FMath::Clamp(Target, 0.f, 1.f);
        Morale.Morale = FMath::Clamp(Tuning.MoraleInertia * Morale.Morale + (1.f - Tuning.MoraleInertia) * Morale.TargetMorale, 0.f, 1.f);

        // Events: a trade request after weeks of misery (withdrawn once he is happier).
        if (Morale.Morale < Tuning.TradeRequestMorale)
        {
            ++Morale.LowMoraleWeeks;
            if (Morale.LowMoraleWeeks >= Tuning.TradeRequestWeeks && !Morale.bTradeRequested)
            {
                Morale.bTradeRequested = true;
                Announce(EPSLockerRoomEventKind::TradeRequest, PlayerId, FString::Printf(TEXT("%s requests a trade from the %s"), *PlayerId.ToString(), *TeamId.ToString()));
            }
        }
        else
        {
            Morale.LowMoraleWeeks = 0;
            Morale.bTradeRequested = false;
        }

        // A leader emerges on a winning team.
        if (!Morale.bLeader && bStarter && Player.Awareness >= Tuning.LeaderAwareness && WinShare >= Tuning.LeaderWinPercentage && Morale.Morale >= Tuning.LeaderMorale)
        {
            Morale.bLeader = true;
            Announce(EPSLockerRoomEventKind::LeaderEmerged, PlayerId, FString::Printf(TEXT("%s has become a leader of the %s"), *PlayerId.ToString(), *TeamId.ToString()));
        }

        // A holdout ends once he is paid (or his deal is gone); one begins at a new league year.
        if (Morale.bHoldingOut && (PayRatio < 0.f || PayRatio >= Tuning.HoldoutPayRatio))
        {
            Morale.bHoldingOut = false;
            Announce(EPSLockerRoomEventKind::HoldoutEnded, PlayerId, FString::Printf(TEXT("%s ends his holdout"), *PlayerId.ToString()));
        }
        else if (bNewLeagueYear && !Morale.bHoldingOut && PayRatio >= 0.f && Rating >= Tuning.StarRating && PayRatio < Tuning.HoldoutPayRatio && Morale.Morale < Tuning.HoldoutMorale)
        {
            Morale.bHoldingOut = true;
            Announce(EPSLockerRoomEventKind::Holdout, PlayerId, FString::Printf(TEXT("%s holds out for a new deal (paid %d%% of his worth)"), *PlayerId.ToString(), FMath::RoundToInt(PayRatio * 100.f)));
        }
    }
    return Events;
}

void UPSLockerRoom::RecordLineup(FName TeamId, const UPSRoster* Roster)
{
    using namespace PSLockerRoomPrivate;

    for (const FPSChemistryUnit& UnitTuning : Tuning.Units)
    {
        const TArray<FName> Starters = GetStarters(Roster, UnitTuning.Role);
        FPSUnitChemistry* Chemistry = State.Units.FindByPredicate([TeamId, &UnitTuning](const FPSUnitChemistry& Entry)
        {
            return Entry.TeamId == TeamId && Entry.Unit == UnitTuning.Unit;
        });
        if (!Chemistry)
        {
            Chemistry = &State.Units.AddDefaulted_GetRef();
            Chemistry->TeamId = TeamId;
            Chemistry->Unit = UnitTuning.Unit;
        }
        if (Chemistry->GamesTogether > 0 && SameLineup(Chemistry->Starters, Starters))
        {
            ++Chemistry->GamesTogether;
        }
        else
        {
            Chemistry->Starters = Starters;
            Chemistry->GamesTogether = Starters.Num() > 0 ? 1 : 0;
        }
    }
}

float UPSLockerRoom::GetMorale(FName PlayerId) const
{
    const FPSPlayerMorale* Morale = FindPlayer(PlayerId);
    return Morale ? Morale->Morale : 0.5f;
}

bool UPSLockerRoom::GetPlayerMorale(FName PlayerId, FPSPlayerMorale& OutMorale) const
{
    const FPSPlayerMorale* Morale = FindPlayer(PlayerId);
    if (Morale)
    {
        OutMorale = *Morale;
    }
    return Morale != nullptr;
}

float UPSLockerRoom::GetUnitCohesion(FName TeamId, FName Unit) const
{
    const FPSUnitChemistry* Chemistry = FindUnit(TeamId, Unit);
    const FPSChemistryUnit* UnitTuning = FindUnitTuning(Unit);
    if (!Chemistry || !UnitTuning)
    {
        return 0.f;
    }
    return FMath::Clamp(static_cast<float>(Chemistry->GamesTogether) / FMath::Max(1, UnitTuning->FullCohesionGames), 0.f, 1.f);
}

bool UPSLockerRoom::IsHoldingOut(FName PlayerId) const
{
    const FPSPlayerMorale* Morale = FindPlayer(PlayerId);
    return Morale && Morale->bHoldingOut;
}

float UPSLockerRoom::GetEffectMultiplier(FName TeamId, const FPlayerAttributes& Player) const
{
    float Multiplier = 1.f + Tuning.PerformanceSwing * (GetMorale(Player.PlayerId) - 0.5f) * 2.f;
    for (const FPSChemistryUnit& UnitTuning : Tuning.Units)
    {
        const FPSUnitChemistry* Chemistry = FindUnit(TeamId, UnitTuning.Unit);
        if (UnitTuning.Role == Player.Role && Chemistry && Chemistry->Starters.Contains(Player.PlayerId))
        {
            Multiplier *= 1.f + UnitTuning.MaxBonus * GetUnitCohesion(TeamId, UnitTuning.Unit);
        }
    }
    return Multiplier;
}

FPlayerAttributes UPSLockerRoom::ApplyEffects(FName TeamId, const FPlayerAttributes& Player) const
{
    const float Multiplier = GetEffectMultiplier(TeamId, Player);
    FPlayerAttributes Affected = Player;
    Affected.Speed *= Multiplier;
    Affected.Agility *= Multiplier;
    Affected.Strength *= Multiplier;
    Affected.Acceleration *= Multiplier;
    Affected.Awareness *= Multiplier;
    return Affected;
}

TArray<FString> UPSLockerRoom::DescribePlayer(FName PlayerId) const
{
    using namespace PSLockerRoomPrivate;

    TArray<FString> Lines;
    const FPSPlayerMorale* Morale = FindPlayer(PlayerId);
    if (!Morale)
    {
        Lines.Add(FString::Printf(TEXT("%s: morale 0.50 (not yet in a locker room)"), *PlayerId.ToString()));
        return Lines;
    }
    Lines.Add(FString::Printf(TEXT("%s: morale %.2f, heading to %.2f"), *PlayerId.ToString(), Morale->Morale, Morale->TargetMorale));
    for (const FPSMoraleFactor& Factor : Morale->Factors)
    {
        Lines.Add(FString::Printf(TEXT("  %s %s"), *Signed(Factor.Delta), *Factor.Description));
    }
    const float Swing = Tuning.PerformanceSwing * (Morale->Morale - 0.5f) * 2.f * 100.f;
    Lines.Add(FString::Printf(TEXT("  Plays %s%.1f%% for his morale"), Swing >= 0.f ? TEXT("+") : TEXT(""), Swing));
    if (Morale->bLeader)
    {
        Lines.Add(TEXT("  A leader in the room"));
    }
    if (Morale->bTradeRequested)
    {
        Lines.Add(TEXT("  Has asked to be traded"));
    }
    if (Morale->bHoldingOut)
    {
        Lines.Add(TEXT("  Holding out for a new deal"));
    }
    return Lines;
}

TArray<FString> UPSLockerRoom::DescribeTeamChemistry(FName TeamId) const
{
    TArray<FString> Lines;
    for (const FPSChemistryUnit& UnitTuning : Tuning.Units)
    {
        const FPSUnitChemistry* Chemistry = FindUnit(TeamId, UnitTuning.Unit);
        const float Cohesion = GetUnitCohesion(TeamId, UnitTuning.Unit);
        Lines.Add(FString::Printf(TEXT("%s: %d game(s) together, cohesion %d%%, +%.1f%%"), *UnitTuning.Unit.ToString(),
            Chemistry ? Chemistry->GamesTogether : 0, FMath::RoundToInt(Cohesion * 100.f), UnitTuning.MaxBonus * Cohesion * 100.f));
    }
    return Lines;
}

void UPSLockerRoom::SaveTo(UPSFranchiseSaveGame* Save) const
{
    if (Save)
    {
        Save->LockerRoom = State;
    }
}

bool UPSLockerRoom::LoadFrom(const UPSFranchiseSaveGame* Save)
{
    if (!Save || (Save->LockerRoom.Players.Num() == 0 && Save->LockerRoom.Units.Num() == 0))
    {
        return false;
    }
    State = Save->LockerRoom;
    return true;
}
