#include "PSPlayRecognitionSubsystem.h"
#include "PSAIFieldSnapshot.h"
#include "PSDataIngestion.h"
#include "PSDeceptionSubsystem.h"
#include "PSDefenderAIComponent.h"
#include "PSDefenseController.h"
#include "PSDifficultySubsystem.h"
#include "PSHealthComponent.h"
#include "PSPlayerDNA.h"
#include "PSPlayerPawn.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"
#include "Misc/Paths.h"

DECLARE_CYCLE_STAT(TEXT("Play recognition look"), STAT_PSAIRecognition, STATGROUP_PSAI);

namespace PSPlayRecognitionPrivate
{
    bool IsStanding(const APSPlayerPawn* Pawn)
    {
        const UPSHealthComponent* Health = Pawn ? Pawn->GetHealthComponent() : nullptr;
        return Pawn && !(Health && Health->IsDowned());
    }

    bool IsIn(FName Value, const TArray<FName>& Names)
    {
        return Names.Contains(Value);
    }

    void AddKey(TArray<FPSKeyRead>& Keys, const TCHAR* Key, EPSPlayRead Read, bool bFakeable)
    {
        FPSKeyRead& Entry = Keys.AddDefaulted_GetRef();
        Entry.Key = FName(Key);
        Entry.Read = Read;
        Entry.bFakeable = bFakeable;
    }
}

// --- The pure rules ---

FName PSPlayRecognition::UnknownClassId()
{
    return FName(TEXT("Unknown"));
}

bool PSPlayRecognition::MatchesClass(const FPSFormationClassDef& Def, const FPSFormationRead& Read)
{
    return (Def.QBAlignment.IsNone() || Def.QBAlignment == Read.QBAlignment)
        && (Def.Backfield.IsNone() || Def.Backfield == Read.Backfield)
        && Read.StrongSideReceivers >= Def.MinStrongSide
        && (Def.MaxWeakSide < 0 || Read.WeakSideReceivers <= Def.MaxWeakSide)
        && Read.TightEnds >= Def.MinTightEnds
        && Read.SplitReceivers >= Def.MinSplitReceivers;
}

FPSFormationRead PSPlayRecognition::ClassifyFormation(const TArray<FPSAlignedPlayer>& Offense, const FVector& LineOfScrimmage, const FPSPlayRecognitionTuning& Tuning)
{
    FPSFormationRead Read;
    Read.ClassId = UnknownClassId();
    Read.RunLean = Tuning.DefaultRunLean;

    // Personnel: who is on the field, wherever he lines up.
    int32 RunningBacks = 0;
    const FPSAlignedPlayer* Passer = nullptr;
    for (const FPSAlignedPlayer& Player : Offense)
    {
        RunningBacks += Player.Role == EPlayerRole::RunningBack ? 1 : 0;
        Read.TightEnds += Player.Role == EPlayerRole::TightEnd ? 1 : 0;
        if (!Passer && Player.Role == EPlayerRole::Quarterback)
        {
            Passer = &Player;
        }
    }
    Read.Personnel = FString::Printf(TEXT("%d%d"), RunningBacks, Read.TightEnds);
    if (!Passer)
    {
        return Read;
    }
    Read.bValid = true;

    const float PasserDepth = LineOfScrimmage.X - Passer->Location.X;
    Read.QBAlignment = PasserDepth <= Tuning.UnderCenterMaxDepth ? FName(TEXT("UnderCenter"))
        : (PasserDepth <= Tuning.PistolMaxDepth ? FName(TEXT("Pistol")) : FName(TEXT("Shotgun")));

    // The backfield and the splits: everyone but the quarterback and the line is a back (deep, in
    // the box) or a receiver on his side of the ball.
    TArray<float> BackOffsets;
    int32 RightSide = 0;
    int32 LeftSide = 0;
    int32 InlineRight = 0;
    int32 InlineLeft = 0;
    for (const FPSAlignedPlayer& Player : Offense)
    {
        if (&Player == Passer || Player.Role == EPlayerRole::OffensiveLineman)
        {
            continue;
        }
        const float Depth = LineOfScrimmage.X - Player.Location.X;
        const float Across = Player.Location.Y - LineOfScrimmage.Y;
        if (Depth >= Tuning.BackfieldMinDepth && FMath::Abs(Across) <= Tuning.BoxHalfWidth)
        {
            BackOffsets.Add(Across);
            continue;
        }
        const bool bRight = Across >= 0.f;
        const bool bInline = FMath::Abs(Across) <= Tuning.InlineWidth;
        (bRight ? RightSide : LeftSide) += 1;
        if (bInline && Player.Role == EPlayerRole::TightEnd)
        {
            ++Read.InlineTightEnds;
            (bRight ? InlineRight : InlineLeft) += 1;
        }
        Read.SplitReceivers += bInline ? 0 : 1;
    }

    Read.Backs = BackOffsets.Num();
    if (Read.Backs == 0)
    {
        Read.Backfield = FName(TEXT("Empty"));
    }
    else if (Read.Backs == 1)
    {
        Read.Backfield = FMath::Abs(BackOffsets[0]) > Tuning.OffsetWidth ? FName(TEXT("Offset")) : FName(TEXT("Single"));
    }
    else if (Read.Backs == 2)
    {
        Read.Backfield = FMath::Abs(BackOffsets[0] - BackOffsets[1]) <= Tuning.StackWidth ? FName(TEXT("I")) : FName(TEXT("Split"));
    }
    else
    {
        Read.Backfield = FName(TEXT("Full"));
    }

    // The strength: more receivers, then more inline tight ends, then the right.
    Read.StrongSide = (RightSide > LeftSide || (RightSide == LeftSide && InlineRight >= InlineLeft)) ? 1 : -1;
    Read.StrongSideReceivers = FMath::Max(RightSide, LeftSide);
    Read.WeakSideReceivers = FMath::Min(RightSide, LeftSide);

    for (const FPSFormationClassDef& Def : Tuning.FormationClasses)
    {
        if (MatchesClass(Def, Read))
        {
            Read.ClassId = Def.ClassId;
            Read.RunLean = Def.RunLean;
            break;
        }
    }
    return Read;
}

TArray<FPSKeyRead> PSPlayRecognition::ReadKeys(const FPSKeyLook& Look, const FPSPlayRecognitionTuning& Tuning)
{
    using PSPlayRecognitionPrivate::AddKey;
    TArray<FPSKeyRead> Keys;
    if (Look.bHandedOff)
    {
        AddKey(Keys, TEXT("Handoff"), EPSPlayRead::Run, false);
    }
    if (Look.bPasserHasBall && Look.PasserDepth >= Tuning.DropKeyDepth && Look.PasserRetreat >= Tuning.DropKeyRetreat)
    {
        AddKey(Keys, TEXT("Drop"), EPSPlayRead::Pass, false);
    }
    if (Look.bHasLine && Tuning.LineKeyDistance > 0.f && Look.LineAdvance >= Tuning.LineKeyDistance)
    {
        AddKey(Keys, TEXT("LineFire"), EPSPlayRead::Run, false);
    }
    else if (Look.bHasLine && Tuning.LineKeyDistance > 0.f && Look.LineAdvance <= -Tuning.LineKeyDistance)
    {
        AddKey(Keys, TEXT("PassSet"), EPSPlayRead::Pass, false);
    }
    if (!Look.bHandedOff && Look.BackDownhillSpeed > 0.f && Look.BackDownhillSpeed >= Tuning.FlowMinSpeed)
    {
        AddKey(Keys, TEXT("Flow"), EPSPlayRead::Run, true);
    }
    return Keys;
}

FPSPlayDiagnosis PSPlayRecognition::Diagnose(const TArray<FPSKeySighting>& Sightings, float PassSeconds, float RunSeconds, float Now)
{
    FPSPlayDiagnosis Diagnosis;
    const FPSKeySighting* Chosen = nullptr;
    float ChosenLatency = 0.f;
    for (const FPSKeySighting& Sighting : Sightings)
    {
        if (Sighting.Key.Read == EPSPlayRead::None)
        {
            continue;
        }
        const float Latency = FMath::Max(0.f, Sighting.Key.Read == EPSPlayRead::Pass ? PassSeconds : RunSeconds);
        if (Now < Sighting.ShownAt + Latency)
        {
            continue;
        }
        // A true key beats the backfield's flow; between keys alike the latest shown wins (the
        // first listed, shown together).
        const bool bBetter = !Chosen
            || (Chosen->Key.bFakeable && !Sighting.Key.bFakeable)
            || (Chosen->Key.bFakeable == Sighting.Key.bFakeable && Sighting.ShownAt > Chosen->ShownAt);
        if (bBetter)
        {
            Chosen = &Sighting;
            ChosenLatency = Latency;
        }
    }
    if (Chosen)
    {
        Diagnosis.Read = Chosen->Key.Read;
        Diagnosis.Key = Chosen->Key.Key;
        Diagnosis.bFakeable = Chosen->Key.bFakeable;
        Diagnosis.ShownAt = Chosen->ShownAt;
        Diagnosis.ReadAt = Chosen->ShownAt + ChosenLatency;
        Diagnosis.Player = Chosen->Player;
    }
    return Diagnosis;
}

float PSPlayRecognition::ExpectedRunShare(float FormationRunLean, float RunShare, const FPSPlayRecognitionTuning& Tuning)
{
    return FMath::Lerp(FMath::Clamp(FormationRunLean, 0.f, 1.f), FMath::Clamp(RunShare, 0.f, 1.f), FMath::Clamp(Tuning.TendencyWeight, 0.f, 1.f));
}

FPSDefenderReadTimes PSPlayRecognition::ReadTimes(float Reaction, float ExpectedRun, float Jitter, const FPSPlayRecognitionTuning& Tuning)
{
    // Lean: -1 expecting the pass outright, +1 the run.
    const float Base = FMath::Max(0.f, Reaction);
    const float Lean = (FMath::Clamp(ExpectedRun, 0.f, 1.f) - 0.5f) * 2.f;
    const float Weight = FMath::Max(0.f, Tuning.ExpectationWeight);
    FPSDefenderReadTimes Times;
    Times.PassSeconds = Base * FMath::Max(0.f, Tuning.PassReadScale) * (1.f + Weight * FMath::Max(0.f, Lean));
    Times.RunSeconds = Base * FMath::Max(0.f, Tuning.RunReadScale) * (1.f + Weight * FMath::Max(0.f, -Lean));
    Times.ThrowSeconds = Base * FMath::Max(0.f, Tuning.ThrowReadScale);
    Times.FakeSeconds = FMath::Max(0.f, Base * FMath::Max(0.f, Tuning.FakeReadScale) * (1.f + Weight * Lean)
        * (1.f + FMath::Max(0.f, Tuning.LatencyJitter) * FMath::Clamp(Jitter, -1.f, 1.f)));
    return Times;
}

float PSPlayRecognition::BiteSeconds(float FakeReadSeconds, float FakeSeconds, const FPSPlayRecognitionTuning& Tuning)
{
    return FakeReadSeconds > FakeSeconds ? FMath::Min(FakeReadSeconds - FakeSeconds, FMath::Max(0.f, Tuning.MaxBiteSeconds)) : 0.f;
}

FName PSPlayRecognition::ReadName(EPSPlayRead Read)
{
    switch (Read)
    {
    case EPSPlayRead::Run:
        return FName(TEXT("Run"));
    case EPSPlayRead::Pass:
        return FName(TEXT("Pass"));
    default:
        return FName(TEXT("None"));
    }
}

TArray<FString> PSPlayRecognition::ValidateTuning(const FPSPlayRecognitionTuning& Tuning)
{
    using PSPlayRecognitionPrivate::IsIn;
    TArray<FString> Problems;
    const float Numbers[] = { Tuning.UnderCenterMaxDepth, Tuning.PistolMaxDepth, Tuning.BackfieldMinDepth, Tuning.BoxHalfWidth, Tuning.StackWidth,
        Tuning.OffsetWidth, Tuning.InlineWidth, Tuning.DropKeyDepth, Tuning.DropKeyRetreat, Tuning.PassReadScale, Tuning.RunReadScale,
        Tuning.ThrowReadScale, Tuning.FakeReadScale, Tuning.ExpectationWeight, Tuning.MaxBiteSeconds };
    for (const float Number : Numbers)
    {
        if (Number < 0.f)
        {
            Problems.Add(TEXT("Distances, read scales, weights and times must be 0 or more"));
            break;
        }
    }
    if (Tuning.FlowMinSpeed <= 0.f || Tuning.LineKeyDistance <= 0.f)
    {
        Problems.Add(TEXT("FlowMinSpeed and LineKeyDistance must be above 0"));
    }
    if (Tuning.UnderCenterMaxDepth > Tuning.PistolMaxDepth)
    {
        Problems.Add(TEXT("PistolMaxDepth must not be inside UnderCenterMaxDepth"));
    }
    if (Tuning.DefaultRunLean < 0.f || Tuning.DefaultRunLean > 1.f || Tuning.TendencyWeight < 0.f || Tuning.TendencyWeight > 1.f)
    {
        Problems.Add(TEXT("DefaultRunLean and TendencyWeight run from 0 to 1"));
    }
    if (Tuning.LatencyJitter < 0.f || Tuning.LatencyJitter >= 1.f)
    {
        Problems.Add(TEXT("LatencyJitter is a fraction, 0 to below 1"));
    }
    if (Tuning.FormationClasses.Num() == 0)
    {
        Problems.Add(TEXT("No formation classes: every look would be Unknown"));
    }
    TSet<FName> Seen;
    for (int32 Index = 0; Index < Tuning.FormationClasses.Num(); ++Index)
    {
        const FPSFormationClassDef& Def = Tuning.FormationClasses[Index];
        const FString Where = FString::Printf(TEXT("FormationClasses[%d] (%s)"), Index, *Def.ClassId.ToString());
        if (Def.ClassId.IsNone() || Def.ClassId == UnknownClassId() || Seen.Contains(Def.ClassId))
        {
            Problems.Add(FString::Printf(TEXT("%s: needs a ClassId of its own (not Unknown)"), *Where));
        }
        Seen.Add(Def.ClassId);
        if (!Def.QBAlignment.IsNone() && !IsIn(Def.QBAlignment, { FName(TEXT("UnderCenter")), FName(TEXT("Pistol")), FName(TEXT("Shotgun")) }))
        {
            Problems.Add(FString::Printf(TEXT("%s: QBAlignment is UnderCenter, Pistol or Shotgun"), *Where));
        }
        if (!Def.Backfield.IsNone() && !IsIn(Def.Backfield, { FName(TEXT("Empty")), FName(TEXT("Single")), FName(TEXT("Offset")), FName(TEXT("I")), FName(TEXT("Split")), FName(TEXT("Full")) }))
        {
            Problems.Add(FString::Printf(TEXT("%s: Backfield is Empty, Single, Offset, I, Split or Full"), *Where));
        }
        if (Def.MinStrongSide < 0 || Def.MaxWeakSide < -1 || Def.MinTightEnds < 0 || Def.MinSplitReceivers < 0)
        {
            Problems.Add(FString::Printf(TEXT("%s: counts are 0 or more (MaxWeakSide -1 for any)"), *Where));
        }
        if (Def.RunLean < 0.f || Def.RunLean > 1.f)
        {
            Problems.Add(FString::Printf(TEXT("%s: RunLean runs from 0 to 1"), *Where));
        }
    }
    return Problems;
}

// --- The subsystem ---

void UPSPlayRecognitionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSPlayRecognitionSubsystem::HandleSnap);
        Bus->OnThrowMC.AddUObject(this, &UPSPlayRecognitionSubsystem::HandleThrow);
        Bus->OnPhaseChangeMC.AddUObject(this, &UPSPlayRecognitionSubsystem::HandlePhaseChange);
        BoundBus = Bus;
    }
}

void UPSPlayRecognitionSubsystem::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnThrowMC.RemoveAll(this);
        Bus->OnPhaseChangeMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Super::Deinitialize();
}

bool UPSPlayRecognitionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPSPlayRecognitionSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UPSPlayRecognitionSubsystem, STATGROUP_Tickables);
}

void UPSPlayRecognitionSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // The snap's formation read goes out the frame after the snap, never inside its broadcast.
    AnnounceFormation();
}

FString UPSPlayRecognitionSubsystem::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/play_recognition.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

UPSPlayRecognitionSubsystem* UPSPlayRecognitionSubsystem::Get(const UWorld* World)
{
    return World ? World->GetSubsystem<UPSPlayRecognitionSubsystem>() : nullptr;
}

const FPSPlayRecognitionTuning& UPSPlayRecognitionSubsystem::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSPlayRecognitionSubsystem::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    ReadTimesByDefender.Reset();
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSPlayRecognitionTuning Loaded;
    if (!Ingestion->LoadPlayRecognitionTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayRecognitionSubsystem: Could not load recognition tuning from %s; keeping defaults."), *JsonFilePath);
        return false;
    }
    const TArray<FString> Problems = PSPlayRecognition::ValidateTuning(Loaded);
    for (const FString& Problem : Problems)
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSPlayRecognitionSubsystem: %s"), *Problem);
    }
    Tuning = Loaded;
    return Problems.Num() == 0;
}

void UPSPlayRecognitionSubsystem::SetTuning(const FPSPlayRecognitionTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
    ReadTimesByDefender.Reset();
}

// --- The formation ---

FPSFormationRead UPSPlayRecognitionSubsystem::ReadFormation()
{
    TArray<FPSAlignedPlayer> Offense;
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (Field)
    {
        const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
        const TArray<EPlayerRole>& Roles = Field->GetRoles();
        for (int32 Index = 0; Index < Pawns.Num() && Index < Roles.Num(); ++Index)
        {
            const APSPlayerPawn* Pawn = Pawns[Index];
            if (Pawn && Pawn->TeamSide == EPSTeamSide::Offense)
            {
                FPSAlignedPlayer& Player = Offense.AddDefaulted_GetRef();
                Player.Role = Roles[Index];
                Player.Location = Pawn->GetActorLocation();
            }
        }
    }
    return PSPlayRecognition::ClassifyFormation(Offense, LineOfScrimmage, GetTuning());
}

float UPSPlayRecognitionSubsystem::GetExpectedRunShare()
{
    const FPSPlayRecognitionTuning& Settings = GetTuning();
    const UPSDeceptionSubsystem* Deception = GetWorld() ? GetWorld()->GetSubsystem<UPSDeceptionSubsystem>() : nullptr;
    const float Lean = FormationRead.bValid ? FormationRead.RunLean : Settings.DefaultRunLean;
    return PSPlayRecognition::ExpectedRunShare(Lean, Deception ? Deception->GetRunShare() : 0.5f, Settings);
}

// --- The keys ---

void UPSPlayRecognitionSubsystem::Observe(float TimeSinceSnap)
{
    SCOPE_CYCLE_COUNTER(STAT_PSAIRecognition);
    if (!bLive || TimeSinceSnap <= LastLookTime)
    {
        return;
    }
    LastLookTime = TimeSinceSnap;
    AnnounceFormation();

    TMap<FName, TWeakObjectPtr<APSPlayerPawn>> KeyPlayers;
    const FPSKeyLook Look = LookAtField(KeyPlayers);
    for (const FPSKeyRead& Shown : PSPlayRecognition::ReadKeys(Look, GetTuning()))
    {
        if (Sightings.ContainsByPredicate([&Shown](const FPSKeySighting& Earlier) { return Earlier.Key.Key == Shown.Key; }))
        {
            continue;
        }
        FPSKeySighting& Sighting = Sightings.AddDefaulted_GetRef();
        Sighting.Key = Shown;
        Sighting.ShownAt = TimeSinceSnap;
        if (const TWeakObjectPtr<APSPlayerPawn>* KeyPlayer = KeyPlayers.Find(Shown.Key))
        {
            Sighting.Player = *KeyPlayer;
        }
    }
}

FPSKeyLook UPSPlayRecognitionSubsystem::LookAtField(TMap<FName, TWeakObjectPtr<APSPlayerPawn>>& OutPlayers)
{
    FPSKeyLook Look;
    const FPSPlayRecognitionTuning& Settings = GetTuning();
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (!Field)
    {
        return Look;
    }

    const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
    const TArray<EPlayerRole>& Roles = Field->GetRoles();
    APSPlayerPawn* Passer = nullptr;
    TArray<APSPlayerPawn*> Backs;
    float LineSum = 0.f;
    int32 LineCount = 0;
    for (int32 Index = 0; Index < Pawns.Num() && Index < Roles.Num(); ++Index)
    {
        APSPlayerPawn* Pawn = Pawns[Index];
        if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense || !PSPlayRecognitionPrivate::IsStanding(Pawn))
        {
            continue;
        }
        if (Roles[Index] == EPlayerRole::Quarterback && !Passer)
        {
            Passer = Pawn;
        }
        else if (Roles[Index] == EPlayerRole::RunningBack)
        {
            Backs.Add(Pawn);
        }
        else if (const float* Stance = Roles[Index] == EPlayerRole::OffensiveLineman ? LineStanceX.Find(FObjectKey(Pawn)) : nullptr)
        {
            // The line: how far its linemen have come off their stance, on average.
            LineSum += Pawn->GetActorLocation().X - *Stance;
            ++LineCount;
        }
    }
    Look.bHasLine = LineCount > 0;
    Look.LineAdvance = LineCount > 0 ? LineSum / LineCount : 0.f;

    // The quarterback: his depth, and how far he has dropped from where he lined up.
    if (Passer)
    {
        const FVector Spot = Passer->GetActorLocation();
        Look.bPasserHasBall = Passer->HasPossession();
        Look.PasserDepth = LineOfScrimmage.X - Spot.X;
        Look.PasserRetreat = bHasPasserStance ? PasserStanceX - Spot.X : 0.f;
        OutPlayers.Add(FName(TEXT("Drop")), Passer);
        OutPlayers.Add(FName(TEXT("PassSet")), Passer);
    }

    // The ball in another offensive player's hands, not thrown there: a hand-off (or a pitch).
    APSPlayerPawn* Carrier = Field->FindBallCarrier();
    if (Carrier && Carrier->TeamSide == EPSTeamSide::Offense && Carrier->GetAttributes().Role != EPlayerRole::Quarterback && !bThrown)
    {
        Look.bHandedOff = true;
        OutPlayers.Add(FName(TEXT("Handoff")), Carrier);
    }

    // The backfield's flow: a back in the box behind the line heading more downhill than across.
    // (A quarterback heading for his back is no key here: his straight drop does that too.)
    APSPlayerPawn* FlowBack = nullptr;
    for (APSPlayerPawn* Back : Backs)
    {
        const FVector Spot = Back->GetActorLocation();
        const FVector Velocity = Back->GetVelocity();
        if (Spot.X >= LineOfScrimmage.X || FMath::Abs(Spot.Y - LineOfScrimmage.Y) > Settings.BoxHalfWidth || Velocity.X <= FMath::Abs(Velocity.Y))
        {
            continue;
        }
        if (Velocity.X > Look.BackDownhillSpeed)
        {
            Look.BackDownhillSpeed = Velocity.X;
            FlowBack = Back;
        }
    }
    if (FlowBack)
    {
        OutPlayers.Add(FName(TEXT("Flow")), FlowBack);
    }
    if (FlowBack || Backs.Num() > 0)
    {
        OutPlayers.Add(FName(TEXT("LineFire")), FlowBack ? FlowBack : Backs[0]);
    }
    return Look;
}

// --- The defenders ---

float UPSPlayRecognitionSubsystem::GetReaction(const APSPlayerPawn* Defender) const
{
    if (!Defender)
    {
        return 0.f;
    }
    const APSDefenseController* Controller = Cast<APSDefenseController>(Defender->GetController());
    if (UPSDefenderAIComponent* DefenseAI = Controller ? Controller->GetDefenderAI() : nullptr)
    {
        return DefenseAI->GetReactionSeconds();
    }
    const float Awareness = FMath::Clamp(Defender->GetAttributes().Awareness, 0.f, 100.f);
    return FDefenderAITuningRow().MaxReactionSeconds * (1.f - Awareness / 100.f);
}

FPSDefenderReadTimes UPSPlayRecognitionSubsystem::GetReadTimes(const APSPlayerPawn* Defender)
{
    if (!Defender)
    {
        return FPSDefenderReadTimes();
    }
    if (const FPSDefenderReadTimes* Known = ReadTimesByDefender.Find(FObjectKey(Defender)))
    {
        return *Known;
    }
    // His own tuning: his style (Epic 79), then the CPU's difficulty (Epic 84).
    FPSPlayRecognitionTuning Personal = GetTuning();
    if (UPSPlayerDNASubsystem* DNA = UPSPlayerDNASubsystem::Get(GetWorld()))
    {
        DNA->ApplyTo(Defender->GetAttributes(), TEXT("Recognition"), Personal);
    }
    if (UPSDifficultySubsystem* Difficulty = UPSDifficultySubsystem::Get(GetWorld()))
    {
        Difficulty->ApplyTo(Defender, TEXT("Recognition"), Personal);
    }
    // His fake read varies from snap to snap, the same way for the same snap.
    FRandomStream Variation(static_cast<int32>(HashCombine(SnapSeed, GetTypeHash(Defender->GetAttributes().PlayerId))));
    const float Jitter = Variation.FRandRange(-1.f, 1.f);
    const FPSDefenderReadTimes Times = PSPlayRecognition::ReadTimes(GetReaction(Defender), GetExpectedRunShare(), Jitter, Personal);
    ReadTimesByDefender.Add(FObjectKey(Defender), Times);
    return Times;
}

FPSPlayDiagnosis UPSPlayRecognitionSubsystem::GetDiagnosis(const APSPlayerPawn* Defender, float TimeSinceSnap)
{
    if (!Defender || !bLive)
    {
        return FPSPlayDiagnosis();
    }
    Observe(TimeSinceSnap);
    const FPSDefenderReadTimes Times = GetReadTimes(Defender);
    const FPSPlayDiagnosis Diagnosis = PSPlayRecognition::Diagnose(Sightings, Times.PassSeconds, Times.RunSeconds, TimeSinceSnap);
    if (Diagnosis.Read != EPSPlayRead::None)
    {
        FName& LastKey = Announced.FindOrAdd(FObjectKey(Defender));
        if (LastKey != Diagnosis.Key)
        {
            LastKey = Diagnosis.Key;
            Publish(EPSRecognitionEventKind::Diagnosis, Defender, &Diagnosis);
        }
    }
    return Diagnosis;
}

float UPSPlayRecognitionSubsystem::GetBiteSeconds(const APSPlayerPawn* Defender, float FakeSeconds)
{
    return Defender ? PSPlayRecognition::BiteSeconds(GetReadTimes(Defender).FakeSeconds, FakeSeconds, GetTuning()) : 0.f;
}

// --- The bus ---

void UPSPlayRecognitionSubsystem::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bLive = true;
    bThrown = false;
    LastLookTime = -1.f;
    Sightings.Reset();
    Announced.Reset();
    ReadTimesByDefender.Reset();
    LineStanceX.Reset();
    bHasPasserStance = false;
    LineOfScrimmage = Event.LineOfScrimmage;
    // Seeded from the snap's situation, so a replayed snap reads the same way.
    SnapSeed = HashCombine(HashCombine(GetTypeHash(Event.YardLine), GetTypeHash(Event.Down)), GetTypeHash(Event.GameClockSeconds));

    // Where the offense lined up: the quarterback's drop and the line's first step are read
    // against it.
    UPSAIFieldSnapshot* Field = GetWorld() ? GetWorld()->GetSubsystem<UPSAIFieldSnapshot>() : nullptr;
    if (Field)
    {
        const TArray<APSPlayerPawn*>& Pawns = Field->GetPawns();
        const TArray<EPlayerRole>& Roles = Field->GetRoles();
        for (int32 Index = 0; Index < Pawns.Num() && Index < Roles.Num(); ++Index)
        {
            const APSPlayerPawn* Pawn = Pawns[Index];
            if (!Pawn || Pawn->TeamSide != EPSTeamSide::Offense)
            {
                continue;
            }
            if (Roles[Index] == EPlayerRole::Quarterback && !bHasPasserStance)
            {
                PasserStanceX = Pawn->GetActorLocation().X;
                bHasPasserStance = true;
            }
            else if (Roles[Index] == EPlayerRole::OffensiveLineman)
            {
                LineStanceX.Add(FObjectKey(Pawn), Pawn->GetActorLocation().X);
            }
        }
    }
    FormationRead = ReadFormation();
    bFormationPending = true;
}

void UPSPlayRecognitionSubsystem::HandleThrow(const FPSTelemetryThrowEvent& Event)
{
    bThrown = true;
}

void UPSPlayRecognitionSubsystem::HandlePhaseChange(const FPSTelemetryPhaseChangeEvent& Event)
{
    if (Event.NewPhase == TEXT("Scoring") || Event.NewPhase == TEXT("PreSnap"))
    {
        bLive = false;
        bFormationPending = false;
    }
}

void UPSPlayRecognitionSubsystem::AnnounceFormation()
{
    if (!bFormationPending)
    {
        return;
    }
    bFormationPending = false;
    if (FormationRead.bValid)
    {
        Publish(EPSRecognitionEventKind::Formation, nullptr, nullptr);
    }
}

void UPSPlayRecognitionSubsystem::Publish(EPSRecognitionEventKind Kind, const APSPlayerPawn* Defender, const FPSPlayDiagnosis* Diagnosis)
{
    UPSTelemetryBus* Bus = BoundBus.Get();
    if (!Bus)
    {
        return;
    }
    FPSTelemetryRecognitionEvent Event;
    Event.Kind = Kind;
    Event.Formation = FormationRead.ClassId;
    Event.Personnel = FormationRead.Personnel;
    Event.RunLean = Kind == EPSRecognitionEventKind::Formation ? FormationRead.RunLean : GetExpectedRunShare();
    Event.PlayerName = Defender ? Defender->GetAttributes().DisplayName : FString();
    if (Diagnosis)
    {
        Event.Read = PSPlayRecognition::ReadName(Diagnosis->Read);
        Event.Key = Diagnosis->Key;
        Event.Seconds = Diagnosis->ReadAt;
    }
    Bus->PublishRecognition(Event);
}
