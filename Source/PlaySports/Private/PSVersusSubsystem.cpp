#include "PSVersusSubsystem.h"
#include "PSControlHandoffComponent.h"
#include "PSDataIngestion.h"
#include "PSFieldGrid.h"
#include "PSInputDeviceComponent.h"
#include "PSLocalization.h"
#include "PSMatchSetup.h"
#include "PSMenuComponent.h"
#include "PSPlayCallSubsystem.h"
#include "PSPlayerController.h"
#include "PSPlayerPawn.h"
#include "PSSessionTelemetry.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

namespace PSVersusPrivate
{
    /** The half a quarter falls in: 0 for the first, 1 for the second, 2 for overtime. */
    int32 HalfOfQuarter(int32 Quarter)
    {
        return Quarter <= 2 ? 0 : (Quarter <= 4 ? 1 : 2);
    }

    /** The side-select screen left to right: Away, nobody, Home. */
    int32 TeamColumn(EPSVersusTeam Team)
    {
        return Team == EPSVersusTeam::Away ? 0 : (Team == EPSVersusTeam::Home ? 2 : 1);
    }

    EPSVersusTeam ColumnTeam(int32 Column)
    {
        return Column <= 0 ? EPSVersusTeam::Away : (Column >= 2 ? EPSVersusTeam::Home : EPSVersusTeam::None);
    }

    /** The user whose devices drive Controller: its local player's controller ID, else Fallback. */
    int32 UserIndexOf(const APlayerController* Controller, int32 Fallback)
    {
        const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
        return LocalPlayer ? LocalPlayer->GetControllerId() : Fallback;
    }
}

void UPSVersusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Seats.SetNum(SeatCount);
    GetRules();

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnGameStateMC.AddUObject(this, &UPSVersusSubsystem::HandleGameState);
        Bus->OnSnapMC.AddUObject(this, &UPSVersusSubsystem::HandleSnap);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSVersusSubsystem::HandlePhaseChange);
        Bus->OnInputDeviceChangeMC.AddUObject(this, &UPSVersusSubsystem::HandleInputDevice);
        BoundBus = Bus;
    }
}

void UPSVersusSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnGameStateMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
        Bus->OnInputDeviceChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();

    Super::Deinitialize();
}

bool UPSVersusSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSVersusSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSVersusSubsystem, STATGROUP_Tickables);
}

void UPSVersusSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    if (IsVersusURL(InWorld.URL) && !BeginFromTravel(InWorld.URL))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSVersusSubsystem: A head-to-head game was asked for but two seats could not be filled."));
    }
}

void UPSVersusSubsystem::Tick(float DeltaTime)
{
    // Real time: the world's clock stands still while the game is paused.
    if (ResumeSecondsLeft >= 0.f)
    {
        AdvanceResume(static_cast<float>(FApp::GetDeltaTime()));
    }
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

FString UPSVersusSubsystem::GetDefaultRulesPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/versus_rules.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSVersusSubsystem::LoadRulesFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSVersusRules Loaded;
    if (!Ingestion || !Ingestion->LoadVersusRulesFromJson(JsonFilePath, Loaded))
    {
        return false;
    }
    const TArray<FString> Problems = ValidateRules(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSVersusSubsystem: %s: %s"), *JsonFilePath, *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Rules = Loaded;
    bRulesLoaded = true;
    return true;
}

const FPSVersusRules& UPSVersusSubsystem::GetRules()
{
    if (!bRulesLoaded)
    {
        // Loaded once; the defaults stand in when the file is missing or unsound.
        bRulesLoaded = true;
        if (!LoadRulesFromJson(GetDefaultRulesPath()))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSVersusSubsystem: Using the default head-to-head rules."));
        }
    }
    return Rules;
}

bool UPSVersusSubsystem::SetRules(const FPSVersusRules& InRules)
{
    if (ValidateRules(InRules).Num() > 0)
    {
        return false;
    }
    Rules = InRules;
    bRulesLoaded = true;
    return true;
}

TArray<FString> UPSVersusSubsystem::ValidateRules(const FPSVersusRules& Candidate)
{
    TArray<FString> Problems;
    if (APSFieldGrid::GetSideForRole(Candidate.OffenseControlRole) != EPSTeamSide::Offense)
    {
        Problems.Add(FString::Printf(TEXT("OffenseControlRole %s is not an offensive role"), *UEnum::GetValueAsString(Candidate.OffenseControlRole)));
    }
    if (APSFieldGrid::GetSideForRole(Candidate.DefenseControlRole) != EPSTeamSide::Defense)
    {
        Problems.Add(FString::Printf(TEXT("DefenseControlRole %s is not a defensive role"), *UEnum::GetValueAsString(Candidate.DefenseControlRole)));
    }
    if (Candidate.PausesPerHalf < -1)
    {
        Problems.Add(TEXT("PausesPerHalf must be -1 (no limit) or 0 or more"));
    }
    if (Candidate.ResumeCountdownSeconds < 0.f)
    {
        Problems.Add(TEXT("ResumeCountdownSeconds must not be negative"));
    }
    return Problems;
}

// ---------------------------------------------------------------------------
// Seats and side select
// ---------------------------------------------------------------------------

int32 UPSVersusSubsystem::ClaimSeat(APSPlayerController* Controller, int32 UserIndex)
{
    if (!Controller || (Phase != EPSVersusPhase::Inactive && Phase != EPSVersusPhase::SideSelect) || FindSeat(Controller) != INDEX_NONE)
    {
        return INDEX_NONE;
    }
    for (const FPSVersusSeat& Taken : Seats)
    {
        if (Taken.IsClaimed() && Taken.UserIndex == UserIndex)
        {
            return INDEX_NONE;
        }
    }

    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        if (Seats[Index].IsClaimed())
        {
            continue;
        }
        Seats[Index] = FPSVersusSeat();
        Seats[Index].Controller = Controller;
        Seats[Index].UserIndex = UserIndex;
        // The controller is this human now: its events say so, and its device component
        // counts only this user's input.
        Controller->HumanIndex = Index;
        if (UPSInputDeviceComponent* Devices = Controller->GetInputDeviceComponent())
        {
            Devices->OwnerUserIndex = UserIndex;
        }
        Phase = EPSVersusPhase::SideSelect;
        UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: User %d sits in seat %d."), UserIndex, Index);
        return Index;
    }
    return INDEX_NONE;
}

bool UPSVersusSubsystem::ReleaseSeat(int32 Seat)
{
    if (!IsValidSeat(Seat) || !Seats[Seat].IsClaimed() || IsSessionActive())
    {
        return false;
    }
    if (APSPlayerController* Controller = Seats[Seat].Controller.Get())
    {
        Controller->HumanIndex = 0;
        if (UPSInputDeviceComponent* Devices = Controller->GetInputDeviceComponent())
        {
            Devices->OwnerUserIndex = INDEX_NONE;
        }
    }
    Seats[Seat] = FPSVersusSeat();
    return true;
}

const FPSVersusSeat& UPSVersusSubsystem::GetSeat(int32 Seat) const
{
    static const FPSVersusSeat NoSeat;
    return IsValidSeat(Seat) && Seats.IsValidIndex(Seat) ? Seats[Seat] : NoSeat;
}

APSPlayerController* UPSVersusSubsystem::GetSeatController(int32 Seat) const
{
    return IsValidSeat(Seat) && Seats.IsValidIndex(Seat) ? Seats[Seat].Controller.Get() : nullptr;
}

int32 UPSVersusSubsystem::FindSeat(const AController* Controller) const
{
    if (!Controller)
    {
        return INDEX_NONE;
    }
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        if (Seats[Index].Controller.Get() == Controller)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

bool UPSVersusSubsystem::SelectTeam(int32 Seat, EPSVersusTeam Team)
{
    if (!IsValidSeat(Seat) || !Seats[Seat].IsClaimed() || Phase != EPSVersusPhase::SideSelect)
    {
        return false;
    }
    if (Team != EPSVersusTeam::None && Seats[GetOtherSeat(Seat)].Team == Team)
    {
        return false;
    }
    if (Seats[Seat].Team != Team)
    {
        Seats[Seat].Team = Team;
        Seats[Seat].bReady = false;
    }
    return true;
}

EPSVersusTeam UPSVersusSubsystem::StepTeam(int32 Seat, int32 Direction)
{
    if (!IsValidSeat(Seat))
    {
        return EPSVersusTeam::None;
    }
    const int32 Column = PSVersusPrivate::TeamColumn(Seats[Seat].Team) + FMath::Clamp(Direction, -1, 1);
    SelectTeam(Seat, PSVersusPrivate::ColumnTeam(Column));
    return Seats[Seat].Team;
}

bool UPSVersusSubsystem::SetReady(int32 Seat, bool bReady)
{
    if (!IsValidSeat(Seat) || !Seats[Seat].IsClaimed() || Phase != EPSVersusPhase::SideSelect)
    {
        return false;
    }
    if (bReady && Seats[Seat].Team == EPSVersusTeam::None)
    {
        return false;
    }
    Seats[Seat].bReady = bReady;
    return true;
}

bool UPSVersusSubsystem::CanStart() const
{
    if (Phase != EPSVersusPhase::SideSelect)
    {
        return false;
    }
    for (const FPSVersusSeat& Each : Seats)
    {
        if (!Each.IsClaimed() || Each.Team == EPSVersusTeam::None || !Each.bReady)
        {
            return false;
        }
    }
    return Seats[0].Team != Seats[1].Team;
}

bool UPSVersusSubsystem::StartSession()
{
    if (!CanStart())
    {
        return false;
    }

    const FPSVersusRules& Active = GetRules();
    for (FPSVersusSeat& Each : Seats)
    {
        Each.PausesUsed = 0;
        Each.bResumeConfirmed = false;
        Each.bDisconnected = false;
        Each.Tempo = EPSTempo::Huddle;
    }
    PausedBySeat = INDEX_NONE;
    ResumeSecondsLeft = -1.f;
    WinnerSeat = INDEX_NONE;
    Phase = EPSVersusPhase::Playing;

    // One view for both, or one each (the visual side of a split screen is an editor pass).
    UWorld* World = GetWorld();
    if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
    {
        Viewport->SetForceDisableSplitscreen(Active.Screen == EPSVersusScreen::Shared);
    }

    ApplySides(true);
    if (UPSPlayCallSubsystem* PlayCall = GetPlayCall())
    {
        PlayCall->SetHumanTempo(EPSTempo::Huddle);
    }
    UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Head-to-head game on: seat %d is home."), Seats[0].Team == EPSVersusTeam::Home ? 0 : 1);
    Publish(EPSVersusEventKind::Started, INDEX_NONE);
    return true;
}

APSPlayerController* UPSVersusSubsystem::CreateLocalSeatPlayer(int32 ControllerId)
{
    UWorld* World = GetWorld();
    if (!World || !World->GetGameInstance())
    {
        return nullptr;
    }
    return Cast<APSPlayerController>(UGameplayStatics::CreatePlayer(World, ControllerId, true));
}

void UPSVersusSubsystem::SetMatchSetup(const UPSMatchSetup* InMatchSetup)
{
    MatchSetup = InMatchSetup;
}

FName UPSVersusSubsystem::GetSeatTeamId(int32 Seat) const
{
    const UPSMatchSetup* Match = MatchSetup.Get();
    if (!Match || !IsValidSeat(Seat) || !Seats.IsValidIndex(Seat) || Seats[Seat].Team == EPSVersusTeam::None)
    {
        return NAME_None;
    }
    return Match->GetTeamId(Seats[Seat].Team == EPSVersusTeam::Home);
}

bool UPSVersusSubsystem::IsVersusURL(const FURL& URL)
{
    return UPSSessionTelemetrySubsystem::ModeFromURL(URL) == TEXT("Versus");
}

int32 UPSVersusSubsystem::GetHomeSeatFromURL(const FURL& URL)
{
    const TCHAR* HomeOption = URL.GetOption(TEXT("homeseat="), nullptr);
    return HomeOption && FCString::Atoi(HomeOption) == 1 ? 1 : 0;
}

bool UPSVersusSubsystem::BeginFromTravel(const FURL& URL)
{
    UWorld* World = GetWorld();
    if (!IsVersusURL(URL) || !World || !World->GetGameInstance())
    {
        return false;
    }

    // The first local player is already in the game; the second joins on the next controller.
    APSPlayerController* First = Cast<APSPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
    APSPlayerController* Second = Cast<APSPlayerController>(UGameplayStatics::GetPlayerController(World, 1));
    if (!Second)
    {
        Second = CreateLocalSeatPlayer(PSVersusPrivate::UserIndexOf(First, 0) + 1);
    }
    if (!First || !Second)
    {
        return false;
    }

    const int32 FirstSeat = ClaimSeat(First, PSVersusPrivate::UserIndexOf(First, 0));
    const int32 SecondSeat = ClaimSeat(Second, PSVersusPrivate::UserIndexOf(Second, 1));
    if (FirstSeat == INDEX_NONE || SecondSeat == INDEX_NONE)
    {
        return false;
    }
    const int32 HomeSeat = GetHomeSeatFromURL(URL);
    SelectTeam(HomeSeat, EPSVersusTeam::Home);
    SelectTeam(GetOtherSeat(HomeSeat), EPSVersusTeam::Away);
    SetReady(0, true);
    SetReady(1, true);
    return StartSession();
}

// ---------------------------------------------------------------------------
// Sides
// ---------------------------------------------------------------------------

EPSTeamSide UPSVersusSubsystem::GetSeatSide(int32 Seat) const
{
    const bool bHome = IsValidSeat(Seat) && Seats[Seat].Team == EPSVersusTeam::Home;
    return bHome == bHomeHasPossession ? EPSTeamSide::Offense : EPSTeamSide::Defense;
}

int32 UPSVersusSubsystem::GetSeatOnSide(EPSTeamSide Side) const
{
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        if (Seats[Index].IsClaimed() && Seats[Index].Team != EPSVersusTeam::None && GetSeatSide(Index) == Side)
        {
            return Index;
        }
    }
    return INDEX_NONE;
}

void UPSVersusSubsystem::ApplySides(bool bMoveControl)
{
    if (!IsSessionActive())
    {
        return;
    }
    const FPSVersusRules& Active = GetRules();

    // First every seat's side, role and switching rules; a seat holding a player of the other
    // side lets go first, so the second pass never reaches for the other human's player.
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        APSPlayerController* Controller = Seats[Index].Controller.Get();
        if (!Controller)
        {
            continue;
        }
        const EPSTeamSide Side = GetSeatSide(Index);
        const bool bOffense = Side == EPSTeamSide::Offense;
        Controller->HumanSide = Side;
        Controller->DefaultControlRole = bOffense ? Active.OffenseControlRole : Active.DefenseControlRole;
        if (UPSControlHandoffComponent* Handoff = Controller->GetControlHandoffComponent())
        {
            Handoff->bSwitchDuringPlay = bOffense || Active.bDefenseSwitchDuringPlay;
            Handoff->bPreSnapPicks = bOffense || Active.bDefensePreSnapPicks;
        }
        const APSPlayerPawn* Controlled = Cast<APSPlayerPawn>(Controller->GetPawn());
        if (Controlled && Controlled->TeamSide != Side)
        {
            CloseCallScreens(Index);
            Controller->ReleaseControl();
        }
    }

    // Then each takes its role's player: always when it has none, and every down when the
    // rules reset control.
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        APSPlayerController* Controller = Seats[Index].Controller.Get();
        if (!Controller)
        {
            continue;
        }
        const APSPlayerPawn* Controlled = Cast<APSPlayerPawn>(Controller->GetPawn());
        const bool bOnRole = Controlled && Controlled->GetAttributes().Role == Controller->DefaultControlRole;
        if (!Controlled || (bMoveControl && !bOnRole))
        {
            Controller->TakeDefaultControl();
        }
    }
}

void UPSVersusSubsystem::CloseCallScreens(int32 Seat)
{
    APSPlayerController* Controller = GetSeatController(Seat);
    UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
    if (Menu && Menu->IsPlayCallScreenOpen())
    {
        Menu->Resume();
    }
}

void UPSVersusSubsystem::HandleGameState(const FPSTelemetryGameStateEvent& Event)
{
    // A new half gives every seat its pauses back.
    const int32 EventHalf = PSVersusPrivate::HalfOfQuarter(Event.Quarter);
    if (EventHalf != Half)
    {
        Half = EventHalf;
        for (FPSVersusSeat& Each : Seats)
        {
            Each.PausesUsed = 0;
        }
    }
    if (Event.bHomeHasPossession == bHomeHasPossession)
    {
        return;
    }

    // The ball changed hands: the seats swap sides. The seat going to defense keeps its tempo
    // for when it has the ball again; the one coming on offense brings its own.
    UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    const int32 LeavingOffense = IsSessionActive() ? GetSeatOnSide(EPSTeamSide::Offense) : INDEX_NONE;
    if (PlayCall && LeavingOffense != INDEX_NONE)
    {
        Seats[LeavingOffense].Tempo = PlayCall->GetHumanTempo();
    }
    bHomeHasPossession = Event.bHomeHasPossession;
    if (!IsSessionActive())
    {
        return;
    }

    ApplySides(GetRules().bResetControlEachDown);
    const int32 TakingOffense = GetSeatOnSide(EPSTeamSide::Offense);
    if (PlayCall && TakingOffense != INDEX_NONE)
    {
        PlayCall->SetHumanTempo(Seats[TakingOffense].Tempo);
    }
    Publish(EPSVersusEventKind::SidesChanged, INDEX_NONE);
}

void UPSVersusSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bPlayLive = true;
}

void UPSVersusSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("PreSnap") || Event.NewPhase == TEXT("Scoring"))
    {
        bPlayLive = false;
    }
    // A new down: both seats back on their side's control role (the rules permitting).
    if (Event.NewPhase == TEXT("PreSnap") && IsSessionActive())
    {
        ApplySides(GetRules().bResetControlEachDown);
    }
}

// ---------------------------------------------------------------------------
// Hidden picks and competitive integrity
// ---------------------------------------------------------------------------

UPSPlayCallSubsystem* UPSVersusSubsystem::GetPlayCall() const
{
    const UWorld* World = GetWorld();
    return World ? World->GetSubsystem<UPSPlayCallSubsystem>() : nullptr;
}

EPSVersusCallState UPSVersusSubsystem::GetCallState(int32 Seat) const
{
    const UPSPlayCallSubsystem* PlayCall = GetPlayCall();
    if (!PlayCall || !IsValidSeat(Seat) || !PlayCall->IsCallWindowOpen())
    {
        return EPSVersusCallState::Closed;
    }
    return PlayCall->GetCall(GetSeatSide(Seat) == EPSTeamSide::Offense).IsSet() ? EPSVersusCallState::Called : EPSVersusCallState::Choosing;
}

EPSVersusCallState UPSVersusSubsystem::GetOpponentCallState(int32 Seat) const
{
    return IsValidSeat(Seat) ? GetCallState(GetOtherSeat(Seat)) : EPSVersusCallState::Closed;
}

bool UPSVersusSubsystem::ShouldShowOverlay(EPSVersusOverlay Overlay, const APlayerController* Viewer) const
{
    if (!IsSessionActive())
    {
        return true;
    }
    const EPSVersusAudience Audience = Overlay == EPSVersusOverlay::RouteArt ? Rules.RouteArtAudience : Rules.DefensiveIconsAudience;
    switch (Audience)
    {
    case EPSVersusAudience::Everyone:
        return true;
    case EPSVersusAudience::OwnerOnly:
    {
        // On one shared view the opponent would see it too.
        if (Rules.Screen == EPSVersusScreen::Shared)
        {
            return false;
        }
        const int32 ViewerSeat = FindSeat(Viewer);
        const EPSTeamSide OwnerSide = Overlay == EPSVersusOverlay::RouteArt ? EPSTeamSide::Offense : EPSTeamSide::Defense;
        return ViewerSeat != INDEX_NONE && GetSeatSide(ViewerSeat) == OwnerSide;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Pause etiquette
// ---------------------------------------------------------------------------

int32 UPSVersusSubsystem::GetPausesLeft(int32 Seat) const
{
    if (Rules.PausesPerHalf < 0 || !IsValidSeat(Seat))
    {
        return -1;
    }
    return FMath::Max(Rules.PausesPerHalf - Seats[Seat].PausesUsed, 0);
}

bool UPSVersusSubsystem::CanPause(int32 Seat, FString& OutReason) const
{
    OutReason.Reset();
    if (!IsValidSeat(Seat) || !Seats[Seat].IsClaimed() || Phase != EPSVersusPhase::Playing)
    {
        OutReason = TEXT("Versus.Refused.NoSession");
        return false;
    }
    if (Rules.bPauseOnlyBetweenPlays && bPlayLive)
    {
        OutReason = TEXT("Versus.Refused.PlayLive");
        return false;
    }
    if (GetPausesLeft(Seat) == 0)
    {
        OutReason = TEXT("Versus.Refused.NoPausesLeft");
        return false;
    }
    return true;
}

bool UPSVersusSubsystem::RequestPause(int32 Seat)
{
    FString Reason;
    if (!CanPause(Seat, Reason))
    {
        if (IsValidSeat(Seat) && IsSessionActive())
        {
            UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Seat %d may not pause (%s)."), Seat, *Reason);
            Publish(EPSVersusEventKind::PauseRefused, Seat, Reason);
        }
        return false;
    }
    ++Seats[Seat].PausesUsed;
    EnterPause(Seat);
    return true;
}

void UPSVersusSubsystem::EnterPause(int32 Seat)
{
    Phase = EPSVersusPhase::Paused;
    PausedBySeat = Seat;
    ResumeSecondsLeft = -1.f;
    for (FPSVersusSeat& Each : Seats)
    {
        Each.bResumeConfirmed = false;
    }

    // The game stops through the pausing seat's controller; both players get the pause screen.
    if (APSPlayerController* Pauser = GetSeatController(Seat))
    {
        Pauser->SetPause(true);
    }
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        APSPlayerController* Controller = Seats[Index].Controller.Get();
        UPSMenuComponent* Menu = Controller ? Controller->GetMenuComponent() : nullptr;
        if (Menu)
        {
            Menu->OpenScreen(Menu->GetCatalog().PauseScreen);
        }
    }
    UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Seat %d paused (%d left this half)."), Seat, GetPausesLeft(Seat));
    Publish(EPSVersusEventKind::Paused, Seat);
}

bool UPSVersusSubsystem::ConfirmResume(int32 Seat)
{
    if (Phase != EPSVersusPhase::Paused || !IsValidSeat(Seat) || !Seats[Seat].IsClaimed())
    {
        return false;
    }
    // Without the both-ready rule, the player who paused resumes alone.
    if (!Rules.bResumeNeedsBoth && Seat != PausedBySeat)
    {
        return false;
    }
    if (!Seats[Seat].bResumeConfirmed)
    {
        Seats[Seat].bResumeConfirmed = true;
        Publish(EPSVersusEventKind::ResumeConfirmed, Seat);
    }
    TryStartCountdown();
    return true;
}

void UPSVersusSubsystem::TryStartCountdown()
{
    if (Phase != EPSVersusPhase::Paused || ResumeSecondsLeft >= 0.f)
    {
        return;
    }
    bool bEveryoneReady = true;
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        const FPSVersusSeat& Each = Seats[Index];
        if (!Each.IsClaimed())
        {
            continue;
        }
        if (Rules.bPauseOnDisconnect && Each.bDisconnected)
        {
            return;
        }
        const bool bMustConfirm = Rules.bResumeNeedsBoth || Index == PausedBySeat;
        bEveryoneReady &= !bMustConfirm || Each.bResumeConfirmed;
    }
    if (!bEveryoneReady)
    {
        return;
    }

    ResumeSecondsLeft = FMath::Max(Rules.ResumeCountdownSeconds, 0.f);
    Publish(EPSVersusEventKind::ResumeCountdown, PausedBySeat);
    if (ResumeSecondsLeft <= 0.f)
    {
        FinishResume();
    }
}

void UPSVersusSubsystem::AdvanceResume(float DeltaSeconds)
{
    if (Phase != EPSVersusPhase::Paused || ResumeSecondsLeft < 0.f)
    {
        return;
    }
    ResumeSecondsLeft -= FMath::Max(DeltaSeconds, 0.f);
    if (ResumeSecondsLeft <= 0.f)
    {
        FinishResume();
    }
}

void UPSVersusSubsystem::FinishResume()
{
    const int32 Pauser = PausedBySeat;
    Phase = EPSVersusPhase::Playing;
    PausedBySeat = INDEX_NONE;
    ResumeSecondsLeft = -1.f;

    APSPlayerController* Unpauser = GetSeatController(Pauser);
    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        Seats[Index].bResumeConfirmed = false;
        APSPlayerController* Controller = Seats[Index].Controller.Get();
        if (!Controller)
        {
            continue;
        }
        Unpauser = Unpauser ? Unpauser : Controller;
        if (UPSMenuComponent* Menu = Controller->GetMenuComponent())
        {
            Menu->Resume();
        }
    }
    if (Unpauser)
    {
        Unpauser->SetPause(false);
    }
    UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Play resumes."));
    Publish(EPSVersusEventKind::Resumed, Pauser);
}

void UPSVersusSubsystem::HandleInputDevice(const FPSTelemetryInputDeviceEvent& Event)
{
    if (!Event.bFromConnectionChange || !IsSessionActive() || !IsValidSeat(Event.HumanIndex) || !Seats[Event.HumanIndex].IsClaimed())
    {
        return;
    }
    const int32 Seat = Event.HumanIndex;
    if (!Event.bConnected)
    {
        if (Seats[Seat].bDisconnected)
        {
            return;
        }
        Seats[Seat].bDisconnected = true;
        UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Seat %d's controller disconnected."), Seat);
        Publish(EPSVersusEventKind::Disconnected, Seat);
        if (!Rules.bPauseOnDisconnect)
        {
            return;
        }
        // Mid-play too, and not counted against the seat's pauses.
        if (Phase == EPSVersusPhase::Playing)
        {
            EnterPause(Seat);
        }
        else
        {
            ResumeSecondsLeft = -1.f;
        }
    }
    else if (Seats[Seat].bDisconnected)
    {
        Seats[Seat].bDisconnected = false;
        UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Seat %d's controller is back."), Seat);
        Publish(EPSVersusEventKind::Reconnected, Seat);
        TryStartCountdown();
    }
}

bool UPSVersusSubsystem::Forfeit(int32 Seat)
{
    if (!IsSessionActive() || !IsValidSeat(Seat) || !Seats[Seat].IsClaimed())
    {
        return false;
    }
    if (Phase == EPSVersusPhase::Paused)
    {
        if (APSPlayerController* Controller = GetSeatController(Seat))
        {
            Controller->SetPause(false);
        }
    }
    Phase = EPSVersusPhase::Ended;
    PausedBySeat = INDEX_NONE;
    ResumeSecondsLeft = -1.f;
    if (Rules.bQuitForfeits)
    {
        WinnerSeat = GetOtherSeat(Seat);
        UE_LOG(LogTemp, Display, TEXT("UPSVersusSubsystem: Seat %d quits; seat %d wins by forfeit."), Seat, WinnerSeat);
        Publish(EPSVersusEventKind::Forfeit, Seat);
    }
    return true;
}

bool UPSVersusSubsystem::HandlePausePressed(APSPlayerController* Controller)
{
    const int32 Seat = FindSeat(Controller);
    if (Seat == INDEX_NONE || !IsSessionActive())
    {
        return false;
    }
    if (Phase == EPSVersusPhase::Paused)
    {
        ConfirmResume(Seat);
    }
    else
    {
        RequestPause(Seat);
    }
    return true;
}

bool UPSVersusSubsystem::HandleResumePressed(APSPlayerController* Controller)
{
    const int32 Seat = FindSeat(Controller);
    if (Seat == INDEX_NONE || Phase != EPSVersusPhase::Paused)
    {
        return false;
    }
    // The player is ready: their pause screen closes while the other's stays until they are.
    if (ConfirmResume(Seat) && Phase == EPSVersusPhase::Paused)
    {
        if (UPSMenuComponent* Menu = Controller->GetMenuComponent())
        {
            Menu->Resume();
        }
    }
    return true;
}

bool UPSVersusSubsystem::HandleQuitPressed(APSPlayerController* Controller)
{
    return Forfeit(FindSeat(Controller));
}

// ---------------------------------------------------------------------------
// Status text
// ---------------------------------------------------------------------------

FText UPSVersusSubsystem::DescribeSeat(int32 Seat)
{
    FFormatNamedArguments Arguments;
    Arguments.Add(TEXT("Number"), Seat + 1);
    return UPSLocalization::Format(TEXT("Versus.Player"), Arguments);
}

FText UPSVersusSubsystem::DescribeStatus() const
{
    FFormatNamedArguments Arguments;
    if (Phase == EPSVersusPhase::Ended)
    {
        if (!IsValidSeat(WinnerSeat))
        {
            return FText::GetEmpty();
        }
        Arguments.Add(TEXT("Player"), DescribeSeat(WinnerSeat));
        return UPSLocalization::Format(TEXT("Versus.Forfeit"), Arguments);
    }
    if (Phase != EPSVersusPhase::Paused)
    {
        return FText::GetEmpty();
    }

    for (int32 Index = 0; Index < Seats.Num(); ++Index)
    {
        if (Seats[Index].IsClaimed() && Seats[Index].bDisconnected)
        {
            Arguments.Add(TEXT("Player"), DescribeSeat(Index));
            return UPSLocalization::Format(TEXT("Versus.Reconnect"), Arguments);
        }
    }
    if (ResumeSecondsLeft >= 0.f)
    {
        Arguments.Add(TEXT("Seconds"), FMath::CeilToInt(ResumeSecondsLeft));
        return UPSLocalization::Format(TEXT("Versus.ResumingIn"), Arguments);
    }
    // Someone is ready: name who the game waits for.
    const bool bAnyoneReady = Seats.ContainsByPredicate([](const FPSVersusSeat& Each) { return Each.bResumeConfirmed; });
    for (int32 Index = 0; bAnyoneReady && Index < Seats.Num(); ++Index)
    {
        const bool bMustConfirm = Rules.bResumeNeedsBoth || Index == PausedBySeat;
        if (Seats[Index].IsClaimed() && bMustConfirm && !Seats[Index].bResumeConfirmed)
        {
            Arguments.Add(TEXT("Player"), DescribeSeat(Index));
            return UPSLocalization::Format(TEXT("Versus.WaitingFor"), Arguments);
        }
    }
    Arguments.Add(TEXT("Player"), DescribeSeat(PausedBySeat));
    const int32 PausesLeft = GetPausesLeft(PausedBySeat);
    if (PausesLeft < 0)
    {
        return UPSLocalization::Format(TEXT("Versus.PausedNoLimit"), Arguments);
    }
    Arguments.Add(TEXT("Pauses"), PausesLeft);
    return UPSLocalization::Format(TEXT("Versus.Paused"), Arguments);
}

FText UPSVersusSubsystem::DescribeOpponentCall(int32 Seat) const
{
    switch (GetOpponentCallState(Seat))
    {
    case EPSVersusCallState::Choosing:
        return UPSLocalization::GetText(TEXT("Versus.Call.Choosing"));
    case EPSVersusCallState::Called:
        return UPSLocalization::GetText(TEXT("Versus.Call.Called"));
    default:
        return FText::GetEmpty();
    }
}

void UPSVersusSubsystem::Publish(EPSVersusEventKind Kind, int32 Seat, const FString& Reason)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryVersusEvent Event;
    Event.Kind = Kind;
    Event.Seat = Seat;
    Event.bHomeOnOffense = bHomeHasPossession;
    Event.PausesLeft = GetPausesLeft(Seat);
    Event.CountdownSeconds = FMath::Max(ResumeSecondsLeft, 0.f);
    Event.Reason = Reason;
    Bus->PublishVersus(Event);
}
