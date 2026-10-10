// PSGameStateSerializer.cpp - Epic 82: the game state an outside model reads, sized for its context
#include "PSGameStateSerializer.h"
#include "PSGameStateEvents.h"
#include "PSOpponentModel.h"
#include "PSPersonnelManager.h"
#include "PSStatsEngine.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PSGameStateSerializerPrivate
{
    /** Free text from the game (a phase, a drive's result) is clipped to this. */
    constexpr int32 MaxTextChars = 40;

    const TCHAR* const StateContract = TEXT("play-sports.game-state/1");
    const TCHAR* const AnalysisContract = TEXT("play-sports.post-game/1");

    FString Clip(const FString& Text)
    {
        return Text.Left(MaxTextChars);
    }

    const TCHAR* TeamName(bool bHome)
    {
        return bHome ? TEXT("home") : TEXT("away");
    }

    template <typename EnumType>
    FString EnumName(EnumType Value)
    {
        return StaticEnum<EnumType>()->GetNameStringByValue(static_cast<int64>(Value));
    }

    TSharedRef<FJsonObject> MakeHomeAway(int32 Home, int32 Away)
    {
        TSharedRef<FJsonObject> Pair = MakeShared<FJsonObject>();
        Pair->SetNumberField(TEXT("home"), Home);
        Pair->SetNumberField(TEXT("away"), Away);
        return Pair;
    }

    TSharedRef<FJsonObject> MakeSituation(const FPSTelemetryGameStateEvent& State)
    {
        TSharedRef<FJsonObject> Situation = MakeShared<FJsonObject>();
        Situation->SetStringField(TEXT("phase"), Clip(State.Phase));
        Situation->SetNumberField(TEXT("quarter"), State.Quarter);
        Situation->SetStringField(TEXT("clock"), PSGameStateEvents::ClockText(State.GameClockSeconds));
        Situation->SetBoolField(TEXT("clockRunning"), State.bGameClockRunning);
        Situation->SetNumberField(TEXT("playClock"), FMath::Max(0, FMath::RoundToInt(State.PlayClockSeconds)));
        Situation->SetStringField(TEXT("offense"), TeamName(State.bHomeHasPossession));
        Situation->SetBoolField(TEXT("kickoff"), State.bKickoff);
        Situation->SetNumberField(TEXT("down"), State.Down);
        Situation->SetNumberField(TEXT("distance"), State.Distance);
        Situation->SetNumberField(TEXT("yardLine"), State.YardLine);
        Situation->SetNumberField(TEXT("yardsToGoal"), 100 - State.YardLine);
        Situation->SetObjectField(TEXT("score"), MakeHomeAway(State.HomeScore, State.AwayScore));
        const int32 Margin = State.bHomeHasPossession ? State.HomeScore - State.AwayScore : State.AwayScore - State.HomeScore;
        Situation->SetNumberField(TEXT("offenseMargin"), Margin);
        Situation->SetObjectField(TEXT("timeouts"), MakeHomeAway(State.HomeTimeoutsRemaining, State.AwayTimeoutsRemaining));
        Situation->SetNumberField(TEXT("drives"), State.CompletedDrives);
        if (State.CompletedDrives > 0)
        {
            TSharedRef<FJsonObject> LastDrive = MakeShared<FJsonObject>();
            LastDrive->SetNumberField(TEXT("plays"), State.LastDrivePlays);
            LastDrive->SetNumberField(TEXT("yards"), State.LastDriveYards);
            LastDrive->SetStringField(TEXT("result"), Clip(State.LastDriveResult));
            Situation->SetObjectField(TEXT("lastDrive"), LastDrive);
        }
        return Situation;
    }

    /** The human's tendency on a side in this situation, or null with no read. */
    TSharedPtr<FJsonObject> MakeTendency(UPSOpponentModel& Opponent, bool bHumanOffense, const FPSTelemetryGameStateEvent& State, bool bShares)
    {
        const FPSTendencyRead Read = Opponent.ReadTendency(bHumanOffense, State.Down, State.Distance, Opponent.GetOffensePersonnel());
        if (Read.Basis == EPSTendencyBasis::None || Read.Samples <= 0.f)
        {
            return nullptr;
        }
        TSharedPtr<FJsonObject> Tendency = MakeShared<FJsonObject>();
        Tendency->SetStringField(TEXT("side"), bHumanOffense ? TEXT("offense") : TEXT("defense"));
        Tendency->SetStringField(TEXT("basis"), EnumName(Read.Basis));
        Tendency->SetNumberField(TEXT("samples"), FMath::RoundToInt(Read.Samples));
        Tendency->SetStringField(TEXT("top"), Clip(Read.TopCategory));
        Tendency->SetNumberField(TEXT("topPct"), FMath::RoundToInt(Read.TopShare * 100.f));
        if (bShares)
        {
            TArray<TPair<FString, float>> Shares;
            for (const TPair<FString, float>& Share : Read.Shares)
            {
                Shares.Add(Share);
            }
            Shares.Sort([](const TPair<FString, float>& A, const TPair<FString, float>& B)
            {
                return A.Value != B.Value ? A.Value > B.Value : A.Key < B.Key;
            });
            TSharedRef<FJsonObject> Percents = MakeShared<FJsonObject>();
            for (const TPair<FString, float>& Share : Shares)
            {
                Percents->SetNumberField(Clip(Share.Key), FMath::RoundToInt(Share.Value * 100.f));
            }
            Tendency->SetObjectField(TEXT("pct"), Percents);
        }
        return Tendency;
    }

    TSharedRef<FJsonObject> MakePersonnel(const UPSPersonnelManager& Personnel, bool bPlayers)
    {
        TSharedRef<FJsonObject> Sides = MakeShared<FJsonObject>();
        for (const bool bOffense : { true, false })
        {
            TSharedRef<FJsonObject> Side = MakeShared<FJsonObject>();
            Side->SetStringField(TEXT("package"), Clip(Personnel.GetCurrentPackage(bOffense).ToString()));
            if (bPlayers)
            {
                TArray<TSharedPtr<FJsonValue>> Players;
                for (const FName PlayerId : Personnel.GetOnFieldPlayerIds(bOffense))
                {
                    Players.Add(MakeShared<FJsonValueString>(Clip(PlayerId.ToString())));
                }
                Side->SetArrayField(TEXT("players"), Players);
            }
            Sides->SetObjectField(bOffense ? TEXT("offense") : TEXT("defense"), Side);
        }
        return Sides;
    }

    TSharedRef<FJsonObject> MakeTeamLine(const FPSTeamStatLine& Line)
    {
        TSharedRef<FJsonObject> Team = MakeShared<FJsonObject>();
        Team->SetStringField(TEXT("team"), Clip(Line.TeamId.ToString()));
        Team->SetNumberField(TEXT("points"), Line.Points);
        Team->SetNumberField(TEXT("plays"), Line.Plays);
        Team->SetNumberField(TEXT("yards"), Line.TotalYards);
        Team->SetNumberField(TEXT("passYards"), Line.PassingYards);
        Team->SetNumberField(TEXT("rushYards"), Line.RushingYards);
        Team->SetNumberField(TEXT("firstDowns"), Line.FirstDowns);
        Team->SetNumberField(TEXT("turnovers"), Line.Turnovers);
        Team->SetNumberField(TEXT("takeaways"), Line.Takeaways);
        return Team;
    }

    /** The game's best PerCategory in each of the categories a model reads first. */
    TArray<TSharedPtr<FJsonValue>> MakeLeaders(const FPSBoxScore& Game, int32 PerCategory)
    {
        static const EPSStatCategory Categories[] = {
            EPSStatCategory::PassingYards, EPSStatCategory::RushingYards, EPSStatCategory::ReceivingYards,
            EPSStatCategory::Tackles, EPSStatCategory::Sacks, EPSStatCategory::Interceptions };
        TArray<TSharedPtr<FJsonValue>> Leaders;
        for (const EPSStatCategory Category : Categories)
        {
            TArray<const FPSPlayerStatLine*> Lines;
            for (const FPSPlayerStatLine& Line : Game.Players)
            {
                if (Line.GetValue(Category) > 0)
                {
                    Lines.Add(&Line);
                }
            }
            Lines.Sort([Category](const FPSPlayerStatLine& A, const FPSPlayerStatLine& B)
            {
                const int32 ValueA = A.GetValue(Category);
                const int32 ValueB = B.GetValue(Category);
                return ValueA != ValueB ? ValueA > ValueB : A.PlayerId.LexicalLess(B.PlayerId);
            });
            for (int32 Index = 0; Index < FMath::Min(PerCategory, Lines.Num()); ++Index)
            {
                TSharedRef<FJsonObject> Leader = MakeShared<FJsonObject>();
                Leader->SetStringField(TEXT("cat"), EnumName(Category));
                Leader->SetStringField(TEXT("id"), Clip(Lines[Index]->PlayerId.ToString()));
                Leader->SetStringField(TEXT("team"), Clip(Lines[Index]->TeamId.ToString()));
                Leader->SetNumberField(TEXT("v"), Lines[Index]->GetValue(Category));
                Leaders.Add(MakeShared<FJsonValueObject>(Leader));
            }
        }
        return Leaders;
    }

    /** How much of each section the state carries: 2 everything, 1 the outline, 0 none. */
    struct FSectionDetail
    {
        int32 Stats = 0;
        int32 Personnel = 0;
        int32 Tendencies = 0;
    };

    FString BuildState(const FPSGameStateSources& Sources, const FSectionDetail& Detail, const TArray<FString>& Trimmed)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("contract"), StateContract);
        Root->SetObjectField(TEXT("situation"), MakeSituation(Sources.State));
        TSharedRef<FJsonObject> Humans = MakeShared<FJsonObject>();
        Humans->SetBoolField(TEXT("offense"), Sources.bHumanOffense);
        Humans->SetBoolField(TEXT("defense"), Sources.bHumanDefense);
        Root->SetObjectField(TEXT("humans"), Humans);

        if (Detail.Tendencies > 0 && Sources.Opponent)
        {
            TArray<TSharedPtr<FJsonValue>> Tendencies;
            for (const bool bOffense : { true, false })
            {
                const bool bHuman = bOffense ? Sources.bHumanOffense : Sources.bHumanDefense;
                const TSharedPtr<FJsonObject> Tendency = bHuman ? MakeTendency(*Sources.Opponent, bOffense, Sources.State, Detail.Tendencies > 1) : TSharedPtr<FJsonObject>();
                if (Tendency.IsValid())
                {
                    Tendencies.Add(MakeShared<FJsonValueObject>(Tendency));
                }
            }
            Root->SetArrayField(TEXT("tendencies"), Tendencies);
        }
        if (Detail.Personnel > 0 && Sources.Personnel)
        {
            Root->SetObjectField(TEXT("personnel"), MakePersonnel(*Sources.Personnel, Detail.Personnel > 1));
        }
        if (Detail.Stats > 0 && Sources.Stats)
        {
            const FPSBoxScore& Game = Sources.Stats->GetCurrentGame();
            TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
            Stats->SetObjectField(TEXT("home"), MakeTeamLine(Game.Home));
            Stats->SetObjectField(TEXT("away"), MakeTeamLine(Game.Away));
            if (Detail.Stats > 1)
            {
                Stats->SetArrayField(TEXT("leaders"), MakeLeaders(Game, FMath::Max(0, Sources.LeadersPerCategory)));
            }
            Root->SetObjectField(TEXT("stats"), Stats);
        }
        if (Trimmed.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> Names;
            for (const FString& Name : Trimmed)
            {
                Names.Add(MakeShared<FJsonValueString>(Name));
            }
            Root->SetArrayField(TEXT("trimmed"), Names);
        }
        return PSGameStateSerializer::ToCompactJson(Root);
    }

    TSharedRef<FJsonObject> MakeDrive(const FPSDriveRecap& Drive)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("team"), TeamName(Drive.bHomeTeam));
        Object->SetNumberField(TEXT("quarter"), Drive.Quarter);
        Object->SetNumberField(TEXT("plays"), Drive.Plays);
        Object->SetNumberField(TEXT("yards"), Drive.Yards);
        Object->SetStringField(TEXT("result"), Clip(Drive.Result));
        return Object;
    }

    TSharedRef<FJsonObject> MakeKeyPlay(const FPSKeyPlay& Play)
    {
        TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("play"), Play.PlayNumber);
        Object->SetStringField(TEXT("kind"), EnumName(Play.Kind));
        Object->SetNumberField(TEXT("importance"), FMath::RoundToInt(Play.Importance));
        Object->SetNumberField(TEXT("yards"), Play.Yards);
        Object->SetNumberField(TEXT("points"), Play.Points);
        Object->SetBoolField(TEXT("turnover"), Play.bTurnover);
        Object->SetNumberField(TEXT("brokenTackles"), Play.BrokenTackles);
        Object->SetNumberField(TEXT("quarter"), Play.Quarter);
        Object->SetNumberField(TEXT("down"), Play.Down);
        Object->SetNumberField(TEXT("distance"), Play.Distance);
        Object->SetNumberField(TEXT("yardLine"), Play.YardLine);
        Object->SetStringField(TEXT("offense"), TeamName(Play.bHomeOffense));
        return Object;
    }

    /** {"drives":n,"keyPlays":n}: what an analysis left out to fit. */
    TSharedRef<FJsonObject> MakeOmitted(int32 Drives, int32 KeyPlays)
    {
        TSharedRef<FJsonObject> Omitted = MakeShared<FJsonObject>();
        Omitted->SetNumberField(TEXT("drives"), Drives);
        Omitted->SetNumberField(TEXT("keyPlays"), KeyPlays);
        return Omitted;
    }

    /** The analysis with its drives from FirstDrive on and its first KeyPlayCount key plays. */
    FString BuildAnalysis(const FPSGameAnalysis& Analysis, int32 FirstDrive, int32 KeyPlayCount)
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("contract"), AnalysisContract);
        TSharedRef<FJsonObject> Final = MakeHomeAway(Analysis.HomeScore, Analysis.AwayScore);
        Final->SetStringField(TEXT("winner"), Analysis.HomeScore == Analysis.AwayScore ? TEXT("tie") : TeamName(Analysis.HomeScore > Analysis.AwayScore));
        Final->SetBoolField(TEXT("over"), Analysis.bFinal);
        Root->SetObjectField(TEXT("final"), Final);

        TArray<TSharedPtr<FJsonValue>> Drives;
        for (int32 Index = FirstDrive; Index < Analysis.Drives.Num(); ++Index)
        {
            Drives.Add(MakeShared<FJsonValueObject>(MakeDrive(Analysis.Drives[Index])));
        }
        Root->SetArrayField(TEXT("drives"), Drives);

        TArray<TSharedPtr<FJsonValue>> KeyPlays;
        for (int32 Index = 0; Index < KeyPlayCount; ++Index)
        {
            KeyPlays.Add(MakeShared<FJsonValueObject>(MakeKeyPlay(Analysis.KeyPlays[Index])));
        }
        Root->SetArrayField(TEXT("keyPlays"), KeyPlays);

        if (FirstDrive > 0 || KeyPlayCount < Analysis.KeyPlays.Num())
        {
            Root->SetObjectField(TEXT("omitted"), MakeOmitted(FirstDrive, Analysis.KeyPlays.Num() - KeyPlayCount));
        }
        return PSGameStateSerializer::ToCompactJson(Root);
    }
}

FString PSGameStateSerializer::Serialize(const FPSGameStateSources& Sources, int32 MaxChars, TArray<FString>* OutTrimmed)
{
    using namespace PSGameStateSerializerPrivate;

    FSectionDetail Detail;
    Detail.Stats = Sources.Stats ? 2 : 0;
    Detail.Personnel = Sources.Personnel ? 2 : 0;
    Detail.Tendencies = Sources.Opponent && (Sources.bHumanOffense || Sources.bHumanDefense) ? 2 : 0;

    // What goes first when the state runs over: the least a model needs to call a play.
    struct FTrimStep
    {
        int32 FSectionDetail::* Section;
        int32 Level;
        const TCHAR* Name;
    };
    static const FTrimStep Steps[] = {
        { &FSectionDetail::Stats, 1, TEXT("stats.leaders") },
        { &FSectionDetail::Personnel, 1, TEXT("personnel.players") },
        { &FSectionDetail::Tendencies, 1, TEXT("tendencies.pct") },
        { &FSectionDetail::Stats, 0, TEXT("stats") },
        { &FSectionDetail::Personnel, 0, TEXT("personnel") },
        { &FSectionDetail::Tendencies, 0, TEXT("tendencies") } };

    TArray<FString> Trimmed;
    FString Json = BuildState(Sources, Detail, Trimmed);
    for (const FTrimStep& Step : Steps)
    {
        if (Json.Len() <= MaxChars)
        {
            break;
        }
        int32& Level = Detail.*(Step.Section);
        if (Level <= Step.Level)
        {
            continue;
        }
        Level = Step.Level;
        Trimmed.Add(Step.Name);
        Json = BuildState(Sources, Detail, Trimmed);
    }
    if (OutTrimmed)
    {
        *OutTrimmed = Trimmed;
    }
    return Json;
}

FString PSGameStateSerializer::SerializeAnalysis(const FPSGameAnalysis& Analysis, int32 MaxChars)
{
    using namespace PSGameStateSerializerPrivate;

    int32 FirstDrive = 0;
    int32 KeyPlayCount = Analysis.KeyPlays.Num();
    FString Json = BuildAnalysis(Analysis, FirstDrive, KeyPlayCount);
    while (Json.Len() > MaxChars && (FirstDrive < Analysis.Drives.Num() || KeyPlayCount > 0))
    {
        if (FirstDrive < Analysis.Drives.Num())
        {
            ++FirstDrive;
        }
        else
        {
            --KeyPlayCount;
        }
        Json = BuildAnalysis(Analysis, FirstDrive, KeyPlayCount);
    }
    return Json;
}

FString PSGameStateSerializer::ToCompactJson(const TSharedRef<FJsonObject>& Object)
{
    FString Out;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
    FJsonSerializer::Serialize(Object, Writer);
    return Out;
}

TSharedPtr<FJsonObject> PSGameStateSerializer::ParseObject(const FString& Json)
{
    TSharedPtr<FJsonObject> Object;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Object))
    {
        return nullptr;
    }
    return Object;
}
