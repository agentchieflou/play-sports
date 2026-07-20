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
