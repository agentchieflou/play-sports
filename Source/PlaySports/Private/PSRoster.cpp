#include "PSRoster.h"

void UPSRoster::InitializeRoster(const TArray<FPlayerAttributes>& Players)
{
    FullRoster = Players;
    DepthChart.Empty();
}

void UPSRoster::BuildDefaultDepthChart()
{
    DepthChart.Empty();
    for (const FPlayerAttributes& Player : FullRoster)
    {
        FPSDepthChartEntry* Entry = FindOrAddEntry(Player.Role);
        if (Entry)
        {
            Entry->PlayerIdsByPriority.Add(Player.PlayerId);
        }
    }
}

void UPSRoster::SetDepthChartOrder(EPlayerRole Role, const TArray<FName>& PlayerIdsByPriority)
{
    FPSDepthChartEntry* Entry = FindOrAddEntry(Role);
    if (Entry)
    {
        Entry->PlayerIdsByPriority = PlayerIdsByPriority;
    }
}

FName UPSRoster::GetStarterId(EPlayerRole Role) const
{
    const FPSDepthChartEntry* Entry = FindEntry(Role);
    if (Entry && Entry->PlayerIdsByPriority.Num() > 0)
    {
        return Entry->PlayerIdsByPriority[0];
    }
    return NAME_None;
}

TArray<FName> UPSRoster::GetDepthChartForRole(EPlayerRole Role) const
{
    const FPSDepthChartEntry* Entry = FindEntry(Role);
    return Entry ? Entry->PlayerIdsByPriority : TArray<FName>();
}

FName UPSRoster::GetNextBackup(FName PlayerId) const
{
    for (const FPSDepthChartEntry& Entry : DepthChart)
    {
        const int32 Index = Entry.PlayerIdsByPriority.IndexOfByKey(PlayerId);
        if (Index != INDEX_NONE && Index + 1 < Entry.PlayerIdsByPriority.Num())
        {
            return Entry.PlayerIdsByPriority[Index + 1];
        }
    }
    return NAME_None;
}

bool UPSRoster::FindPlayerById(FName PlayerId, FPlayerAttributes& OutAttributes) const
{
    for (const FPlayerAttributes& Player : FullRoster)
    {
        if (Player.PlayerId == PlayerId)
        {
            OutAttributes = Player;
            return true;
        }
    }
    return false;
}

TMap<FName, FName> UPSRoster::EvaluateFatigueSubstitutions(const TMap<FName, float>& OnFieldStaminaRatioByPlayerId, float FatigueThreshold) const
{
    TMap<FName, FName> Substitutions;

    for (const TPair<FName, float>& Pair : OnFieldStaminaRatioByPlayerId)
    {
        if (Pair.Value >= FatigueThreshold)
        {
            continue;
        }

        const FName Backup = GetNextBackup(Pair.Key);
        if (!Backup.IsNone())
        {
            Substitutions.Add(Pair.Key, Backup);
        }
    }

    return Substitutions;
}

FPSDepthChartEntry* UPSRoster::FindOrAddEntry(EPlayerRole Role)
{
    for (FPSDepthChartEntry& Entry : DepthChart)
    {
        if (Entry.Role == Role)
        {
            return &Entry;
        }
    }

    FPSDepthChartEntry NewEntry;
    NewEntry.Role = Role;
    DepthChart.Add(NewEntry);
    return &DepthChart.Last();
}

const FPSDepthChartEntry* UPSRoster::FindEntry(EPlayerRole Role) const
{
    for (const FPSDepthChartEntry& Entry : DepthChart)
    {
        if (Entry.Role == Role)
        {
            return &Entry;
        }
    }
    return nullptr;
}

FPSPlayerLiveState& UPSRoster::FindOrAddLiveState(FName PlayerId)
{
    if (FPSPlayerLiveState* Existing = LiveStateByPlayerId.Find(PlayerId))
    {
        return *Existing;
    }

    FPSPlayerLiveState NewState;
    NewState.PlayerId = PlayerId;
    return LiveStateByPlayerId.Add(PlayerId, NewState);
}

void UPSRoster::MarkDownedForNextPlay(FName PlayerId, int32 CurrentPlayIndex)
{
    FPSPlayerLiveState& State = FindOrAddLiveState(PlayerId);
    State.bIsDowned = true;
    State.CurrentHitPoints = 0.f;
    State.UnavailableUntilPlayIndex = CurrentPlayIndex + 2; // sits out exactly the next play
}

void UPSRoster::MarkDownedForCurrentPlayOnly(FName PlayerId)
{
    FPSPlayerLiveState& State = FindOrAddLiveState(PlayerId);
    State.bIsDowned = true;
    State.CurrentHitPoints = 0.f;
    State.UnavailableUntilPlayIndex = INDEX_NONE;
}

void UPSRoster::RespawnForNewPlay(FName PlayerId, float MaxHitPoints)
{
    FPSPlayerLiveState& State = FindOrAddLiveState(PlayerId);
    State.bIsDowned = false;
    State.CurrentHitPoints = MaxHitPoints;
    State.UnavailableUntilPlayIndex = INDEX_NONE;
}

bool UPSRoster::IsAvailableForPlay(FName PlayerId, int32 CurrentPlayIndex) const
{
    const FPSPlayerLiveState* State = LiveStateByPlayerId.Find(PlayerId);
    if (!State)
    {
        return true;
    }
    return State->UnavailableUntilPlayIndex == INDEX_NONE || CurrentPlayIndex >= State->UnavailableUntilPlayIndex;
}

bool UPSRoster::FindLiveState(FName PlayerId, FPSPlayerLiveState& OutState) const
{
    if (const FPSPlayerLiveState* State = LiveStateByPlayerId.Find(PlayerId))
    {
        OutState = *State;
        return true;
    }
    return false;
}

void UPSRoster::AwardXp(FName PlayerId, float XpAmount)
{
    FPSPlayerLiveState& State = FindOrAddLiveState(PlayerId);
    State.CurrentXp += XpAmount;
}

void UPSRoster::SetLevel(FName PlayerId, int32 NewLevel, float RemainingXp)
{
    FPSPlayerLiveState& State = FindOrAddLiveState(PlayerId);
    State.Level = NewLevel;
    State.CurrentXp = RemainingXp;
}
