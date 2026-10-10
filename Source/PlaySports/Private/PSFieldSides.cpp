#include "PSFieldSides.h"
#include "PSFieldGrid.h"
#include "PSRoster.h"

void UPSFieldSides::Initialize(UPSRoster* InRoster, const TArray<FName>& InHomePlayerIds)
{
    Roster = InRoster;
    HomePlayerIds = TSet<FName>(InHomePlayerIds);
    ArrangeForPossession(true);
}

void UPSFieldSides::ArrangeForPossession(bool bHomeHasBall)
{
    bHomeOnOffense = bHomeHasBall;
    if (!Roster || HomePlayerIds.Num() == 0)
    {
        return;
    }

    // The offense is the team with the ball, the defense the other team: a player is on the
    // depth chart when his team is the one his role's side belongs to this possession.
    TMap<EPlayerRole, TArray<FName>> DepthByRole;
    for (const FPlayerAttributes& Player : Roster->GetFullRoster())
    {
        TArray<FName>& Depth = DepthByRole.FindOrAdd(Player.Role);
        const bool bOffenseRole = APSFieldGrid::GetSideForRole(Player.Role) == EPSTeamSide::Offense;
        const bool bHomePlayer = HomePlayerIds.Contains(Player.PlayerId);
        if (bHomePlayer == (bOffenseRole == bHomeHasBall))
        {
            Depth.Add(Player.PlayerId);
        }
    }
    for (const TPair<EPlayerRole, TArray<FName>>& Depth : DepthByRole)
    {
        Roster->SetDepthChartOrder(Depth.Key, Depth.Value);
    }
    UE_LOG(LogTemp, Display, TEXT("UPSFieldSides: The %s team has the ball and lines up on offense."), bHomeHasBall ? TEXT("home") : TEXT("away"));
}

void UPSFieldSides::BindToBus(UPSTelemetryBus* Bus)
{
    UnbindFromBus();
    if (!Bus)
    {
        return;
    }
    BoundBus = Bus;
    GameStateHandle = Bus->OnGameStateMC.AddUObject(this, &UPSFieldSides::HandleGameState);
}

void UPSFieldSides::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.Remove(GameStateHandle);
    }
    GameStateHandle.Reset();
    BoundBus.Reset();
}

void UPSFieldSides::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    if (Event.bHomeHasPossession != bHomeOnOffense)
    {
        ArrangeForPossession(Event.bHomeHasPossession);
    }
}
