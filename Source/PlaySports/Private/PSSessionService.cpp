// PSSessionService.cpp - Epic 108.5: the platform-agnostic session and matchmaking service
#include "PSSessionService.h"
#include "PSDataIngestion.h"
#include "PSNetRandomStreams.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

FString UPSSessionService::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/session_matchmaking.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool UPSSessionService::LoadTuningFromJson(const FString& JsonFilePath)
{
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSSessionMatchmakingTuning Loaded;
    if (!Ingestion || !Ingestion->LoadSessionMatchmakingFromJson(JsonFilePath, Loaded))
    {
        return false;
    }
    const TArray<FString> Problems = ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSSessionService: %s: %s"), *JsonFilePath, *Problem);
    }
    if (Problems.Num() > 0)
    {
        return false;
    }
    Tuning = Loaded;
    bTuningLoaded = true;
    return true;
}

const FPSSessionMatchmakingTuning& UPSSessionService::GetTuning()
{
    if (!bTuningLoaded)
    {
        bTuningLoaded = true;
        if (!LoadTuningFromJson(GetDefaultTuningPath()))
        {
            UE_LOG(LogTemp, Warning, TEXT("UPSSessionService: Using the default matchmaking tuning."));
        }
    }
    return Tuning;
}

bool UPSSessionService::SetTuning(const FPSSessionMatchmakingTuning& InTuning)
{
    if (ValidateTuning(InTuning).Num() > 0)
    {
        return false;
    }
    Tuning = InTuning;
    bTuningLoaded = true;
    return true;
}

TArray<FString> UPSSessionService::ValidateTuning(const FPSSessionMatchmakingTuning& Candidate)
{
    TArray<FString> Problems;
    if (Candidate.ProtocolVersion < 1)
    {
        Problems.Add(TEXT("ProtocolVersion must be 1 or more"));
    }
    if (Candidate.InitialSkillWindow < 0.f || Candidate.SkillWindowGrowthPerSecond < 0.f || Candidate.MaxSkillWindow < Candidate.InitialSkillWindow)
    {
        Problems.Add(TEXT("InitialSkillWindow and SkillWindowGrowthPerSecond must be 0 or more, and MaxSkillWindow at least InitialSkillWindow"));
    }
    if (Candidate.RegionRelaxSeconds < 0.f || !(Candidate.MaxWaitSeconds > 0.f))
    {
        Problems.Add(TEXT("RegionRelaxSeconds must be 0 or more and MaxWaitSeconds above 0"));
    }
    if (Candidate.HostScoreDesktop < 0.f || Candidate.HostScoreOnPower < 0.f || Candidate.HostScoreUnmetered < 0.f)
    {
        Problems.Add(TEXT("Host scores must be 0 or more"));
    }
    return Problems;
}

FPSMatchRequest UPSSessionService::MakeRequest(const FPSSessionPlayer& Player, FName Mode)
{
    const FPSSessionMatchmakingTuning& Active = GetTuning();
    FPSMatchRequest Request;
    Request.Mode = Mode;
    Request.ProtocolVersion = Active.ProtocolVersion;
    Request.Player = Player;
    Request.CrossPlay = Player.Input == EPSSessionInput::Touch ? Active.TouchDefaultCrossPlay : Active.DefaultCrossPlay;
    return Request;
}

EPSSessionFailure UPSSessionService::CheckCompatible(const FPSMatchRequest& A, const FPSMatchRequest& B, float LongestWaitSeconds,
    const FPSSessionMatchmakingTuning& InTuning, bool bForMatchmaking)
{
    if (A.Mode != B.Mode)
    {
        return EPSSessionFailure::ModeMismatch;
    }
    if (A.ProtocolVersion != B.ProtocolVersion)
    {
        return EPSSessionFailure::ProtocolMismatch;
    }

    // Each player's policy must accept the other player.
    const auto Accepts = [](const FPSMatchRequest& Chooser, const FPSSessionPlayer& Other)
    {
        switch (Chooser.CrossPlay)
        {
        case EPSCrossPlayPolicy::SameInput:
            return Chooser.Player.Input == Other.Input;
        case EPSCrossPlayPolicy::SamePlatform:
            return Chooser.Player.Platform == Other.Platform;
        default:
            return true;
        }
    };
    if (!Accepts(A, B.Player) || !Accepts(B, A.Player))
    {
        return EPSSessionFailure::CrossPlayRefused;
    }

    if (bForMatchmaking)
    {
        if (!A.Region.IsEmpty() && !B.Region.IsEmpty() && !A.Region.Equals(B.Region, ESearchCase::IgnoreCase)
            && LongestWaitSeconds < InTuning.RegionRelaxSeconds)
        {
            return EPSSessionFailure::RegionMismatch;
        }
        if (FMath::Abs(A.Player.SkillRating - B.Player.SkillRating) > GetSkillWindow(LongestWaitSeconds, InTuning))
        {
            return EPSSessionFailure::SkillGap;
        }
    }
    return EPSSessionFailure::None;
}

float UPSSessionService::GetSkillWindow(float WaitedSeconds, const FPSSessionMatchmakingTuning& InTuning)
{
    return FMath::Min(InTuning.MaxSkillWindow, InTuning.InitialSkillWindow + InTuning.SkillWindowGrowthPerSecond * FMath::Max(0.f, WaitedSeconds));
}

float UPSSessionService::GetHostScore(const FPSSessionPlayer& Player, const FPSSessionMatchmakingTuning& InTuning)
{
    float Score = 0.f;
    if (IsDesktop(Player.Platform))
    {
        Score += InTuning.HostScoreDesktop;
    }
    if (!Player.bOnBattery)
    {
        Score += InTuning.HostScoreOnPower;
    }
    if (!Player.bOnMeteredNetwork)
    {
        Score += InTuning.HostScoreUnmetered;
    }
    return Score;
}

int32 UPSSessionService::ChooseHostSeat(const TArray<FPSSessionPlayer>& Players, const FPSSessionMatchmakingTuning& InTuning)
{
    int32 Best = INDEX_NONE;
    float BestScore = 0.f;
    for (int32 Seat = 0; Seat < Players.Num(); ++Seat)
    {
        const float Score = GetHostScore(Players[Seat], InTuning);
        if (Best == INDEX_NONE || Score > BestScore)
        {
            Best = Seat;
            BestScore = Score;
        }
    }
    return Best;
}

bool UPSSessionService::IsDesktop(EPSSessionPlatform Platform)
{
    return Platform == EPSSessionPlatform::Windows || Platform == EPSSessionPlatform::Mac || Platform == EPSSessionPlatform::Linux;
}

EPSSessionPlatform UPSSessionService::GetCurrentPlatform()
{
#if PLATFORM_WINDOWS
    return EPSSessionPlatform::Windows;
#elif PLATFORM_MAC
    return EPSSessionPlatform::Mac;
#elif PLATFORM_LINUX
    return EPSSessionPlatform::Linux;
#elif PLATFORM_IOS
    return EPSSessionPlatform::IOS;
#elif PLATFORM_ANDROID
    return EPSSessionPlatform::Android;
#else
    return EPSSessionPlatform::Unknown;
#endif
}

bool UPSSessionService::ApplyMatchToWorld(const FPSSessionMatch& InMatch, UWorld* World)
{
    UPSNetRandomStreams* Streams = World ? World->GetSubsystem<UPSNetRandomStreams>() : nullptr;
    if (!InMatch.IsValid() || !Streams)
    {
        return false;
    }
    Streams->SetMatchSeed(InMatch.MatchSeed);
    return true;
}

FString UPSSessionService::GetTravelOptions(const FPSSessionMatch& InMatch)
{
    if (!InMatch.IsValid())
    {
        return FString();
    }
    return FString::Printf(TEXT("mode=%s?homeseat=%d?matchseed=%d"), *InMatch.Mode.ToString(), InMatch.HomeSeat, InMatch.MatchSeed);
}

void UPSSessionService::SetState(EPSSessionState NewState)
{
    if (State == NewState)
    {
        return;
    }
    State = NewState;
    OnStateChanged.Broadcast(State);
}

void UPSSessionService::CompleteMatch(const FPSSessionMatch& InMatch)
{
    Match = InMatch;
    LastFailure = EPSSessionFailure::None;
    SetState(EPSSessionState::Matched);
    OnMatched.Broadcast(Match);
}

void UPSSessionService::Fail(EPSSessionFailure Failure)
{
    Match = FPSSessionMatch();
    LastFailure = Failure;
    SetState(EPSSessionState::Failed);
    OnFailed.Broadcast(Failure);
}

void UPSSessionService::Refuse(EPSSessionFailure Failure)
{
    LastFailure = Failure;
}

void UPSSessionService::ResetToIdle()
{
    Match = FPSSessionMatch();
    LastFailure = EPSSessionFailure::None;
    SetState(EPSSessionState::Idle);
}

FPSMatchRequest UPSSessionService::Resolve(const FPSMatchRequest& Request)
{
    FPSMatchRequest Resolved = Request;
    if (Resolved.ProtocolVersion == 0)
    {
        Resolved.ProtocolVersion = GetTuning().ProtocolVersion;
    }
    return Resolved;
}
