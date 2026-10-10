#include "PSOpponentModel.h"
#include "PSDataIngestion.h"
#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

namespace PSOpponentModelPrivate
{
    /** Index into the per-side arrays: [0] defense, [1] offense. */
    int32 SideIndex(bool bOffense)
    {
        return bOffense ? 1 : 0;
    }

    /** The categories some counter tracks for the human on that side, in data order. */
    TArray<FString> TrackedCategories(const FPSOpponentModelTuning& Tuning, bool bHumanOffense)
    {
        TArray<FString> Categories;
        for (const FPSOpponentCounterDef& Def : Tuning.Counters)
        {
            if (Def.bOffense == bHumanOffense && !Def.Observed.IsEmpty())
            {
                Categories.AddUnique(Def.Observed);
            }
        }
        return Categories;
    }

    /** Adds Weight * each matching cell's count to Totals by category. */
    void Tally(const TArray<FPSTendencyCell>& Cells, float Weight, bool bHumanOffense, int32 Down, int32 Bucket, FName Personnel,
        EPSTendencyBasis Basis, TMap<FString, float>& Totals)
    {
        if (Weight <= 0.f)
        {
            return;
        }
        for (const FPSTendencyCell& Cell : Cells)
        {
            const bool bMatches = Cell.bOffense == bHumanOffense
                && (Basis == EPSTendencyBasis::Overall || Cell.Down == Down)
                && (Basis == EPSTendencyBasis::Overall || Basis == EPSTendencyBasis::Down || Cell.DistanceBucket == Bucket)
                && (Basis != EPSTendencyBasis::Exact || Cell.Personnel == Personnel);
            if (bMatches && Cell.Count > 0)
            {
                Totals.FindOrAdd(Cell.Category) += Weight * Cell.Count;
            }
        }
    }
}

int32 PSOpponentModel::GetDistanceBucket(int32 Distance, const FPSOpponentModelTuning& Tuning)
{
    int32 Bucket = 0;
    while (Tuning.DistanceBuckets.IsValidIndex(Bucket) && Distance > Tuning.DistanceBuckets[Bucket])
    {
        ++Bucket;
    }
    return Bucket;
}

bool PSOpponentModel::IsTracked(const FPSOpponentModelTuning& Tuning, bool bHumanOffense, const FString& Category)
{
    return PSOpponentModelPrivate::TrackedCategories(Tuning, bHumanOffense).Contains(Category);
}

FPSTendencyRead PSOpponentModel::ReadTendency(const TArray<FPSTendencyCell>& GameCells, const TArray<FPSTendencyCell>& CareerCells,
    bool bHumanOffense, int32 Down, int32 Distance, FName Personnel, const FPSOpponentModelTuning& Tuning)
{
    using namespace PSOpponentModelPrivate;

    FPSTendencyRead Read;
    const int32 Bucket = GetDistanceBucket(Distance, Tuning);
    const TArray<FString> Tracked = TrackedCategories(Tuning, bHumanOffense);
    // The narrowest situation he has shown enough of.
    for (const EPSTendencyBasis Basis : { EPSTendencyBasis::Exact, EPSTendencyBasis::DownDistance, EPSTendencyBasis::Down, EPSTendencyBasis::Overall })
    {
        if (Basis == EPSTendencyBasis::Exact && Personnel.IsNone())
        {
            continue;
        }
        TMap<FString, float> Totals;
        Tally(GameCells, 1.f, bHumanOffense, Down, Bucket, Personnel, Basis, Totals);
        Tally(CareerCells, Tuning.PriorGameWeight, bHumanOffense, Down, Bucket, Personnel, Basis, Totals);
        float Samples = 0.f;
        for (const FString& Category : Tracked)
        {
            Samples += Totals.FindRef(Category);
        }
        if (Samples <= 0.f || Samples < Tuning.MinSamples)
        {
            continue;
        }
        Read.Basis = Basis;
        Read.Samples = Samples;
        for (const FString& Category : Tracked)
        {
            const float Share = Totals.FindRef(Category) / Samples;
            Read.Shares.Add(Category, Share);
            if (Share > Read.TopShare)
            {
                Read.TopShare = Share;
                Read.TopCategory = Category;
            }
        }
        return Read;
    }
    return Read;
}

TMap<FString, float> PSOpponentModel::ComputeCounterWeights(const FPSTendencyRead& Read, bool bHumanOffense, float Strength, const FPSOpponentModelTuning& Tuning)
{
    TMap<FString, float> Weights;
    const TArray<FString> Tracked = PSOpponentModelPrivate::TrackedCategories(Tuning, bHumanOffense);
    if (Read.Basis == EPSTendencyBasis::None || Strength <= 0.f || Tracked.Num() == 0)
    {
        return Weights;
    }
    // How far each of his categories is over (or under) an even share, toward its counters.
    const float EvenShare = 1.f / Tracked.Num();
    for (const FPSOpponentCounterDef& Def : Tuning.Counters)
    {
        if (Def.bOffense != bHumanOffense || Def.Counter.IsEmpty())
        {
            continue;
        }
        Weights.FindOrAdd(Def.Counter, 1.f) += Strength * (Read.Shares.FindRef(Def.Observed) - EvenShare) * Def.Weight;
    }
    for (TPair<FString, float>& Entry : Weights)
    {
        Entry.Value = FMath::Clamp(Entry.Value, Tuning.MinMultiplier, Tuning.MaxMultiplier);
    }
    return Weights;
}

void PSOpponentModel::MergeCells(TArray<FPSTendencyCell>& Into, const TArray<FPSTendencyCell>& From)
{
    for (const FPSTendencyCell& Cell : From)
    {
        FPSTendencyCell* Existing = Into.FindByPredicate([&Cell](const FPSTendencyCell& Other)
        {
            return Other.bOffense == Cell.bOffense && Other.Down == Cell.Down && Other.DistanceBucket == Cell.DistanceBucket
                && Other.Personnel == Cell.Personnel && Other.Category == Cell.Category;
        });
        if (Existing)
        {
            Existing->Count += Cell.Count;
        }
        else
        {
            Into.Add(Cell);
        }
    }
}

TArray<FString> PSOpponentModel::ValidateTuning(const FPSOpponentModelTuning& Tuning)
{
    TArray<FString> Problems;
    for (int32 Index = 1; Index < Tuning.DistanceBuckets.Num(); ++Index)
    {
        if (Tuning.DistanceBuckets[Index] <= Tuning.DistanceBuckets[Index - 1])
        {
            Problems.Add(TEXT("DistanceBuckets: must rise"));
            break;
        }
    }
    if (Tuning.MinSamples < 1.f)
    {
        Problems.Add(TEXT("MinSamples: at least 1"));
    }
    if (Tuning.PriorGameWeight < 0.f || Tuning.PriorGameWeight > 1.f)
    {
        Problems.Add(TEXT("PriorGameWeight: 0 to 1"));
    }
    for (const float Strength : { Tuning.FirstHalfStrength, Tuning.SecondHalfStrength, Tuning.DefaultAdaptationDial })
    {
        if (Strength < 0.f || Strength > 1.f)
        {
            Problems.Add(TEXT("FirstHalfStrength, SecondHalfStrength and DefaultAdaptationDial: 0 to 1"));
            break;
        }
    }
    if (Tuning.HalftimeQuarter < 2)
    {
        Problems.Add(TEXT("HalftimeQuarter: 2 or later"));
    }
    if (Tuning.MinMultiplier <= 0.f || Tuning.MinMultiplier > 1.f || Tuning.MaxMultiplier < 1.f)
    {
        Problems.Add(TEXT("MinMultiplier must be above 0 and at most 1, MaxMultiplier at least 1"));
    }
    TSet<FString> Seen;
    for (const FPSOpponentCounterDef& Def : Tuning.Counters)
    {
        const FString Key = FString::Printf(TEXT("%s %s -> %s"), Def.bOffense ? TEXT("offense") : TEXT("defense"), *Def.Observed, *Def.Counter);
        if (Def.Observed.IsEmpty() || Def.Counter.IsEmpty())
        {
            Problems.Add(FString::Printf(TEXT("Counters '%s': needs an Observed and a Counter category"), *Key));
        }
        else if (Seen.Contains(Key))
        {
            Problems.Add(FString::Printf(TEXT("Counters '%s': listed twice"), *Key));
        }
        Seen.Add(Key);
    }
    return Problems;
}

void UPSOpponentModel::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    BindToBus(Collection.InitializeDependency<UPSTelemetryBus>());
}

void UPSOpponentModel::Deinitialize()
{
    UnbindFromBus();
    Super::Deinitialize();
}

bool UPSOpponentModel::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

FString UPSOpponentModel::GetDefaultTuningPath()
{
    FString Path = FPaths::ProjectDir() / TEXT("Data/opponent_model.json");
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

const FPSOpponentModelTuning& UPSOpponentModel::GetTuning()
{
    if (!bTuningLoaded)
    {
        LoadTuningFromJson(GetDefaultTuningPath());
    }
    return Tuning;
}

bool UPSOpponentModel::LoadTuningFromJson(const FString& JsonFilePath)
{
    bTuningLoaded = true;
    UPSDataIngestion* Ingestion = NewObject<UPSDataIngestion>(this);
    FPSOpponentModelTuning Loaded;
    if (!Ingestion->LoadOpponentModelTuningFromJson(JsonFilePath, Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOpponentModel: Could not load the opponent model from %s; the CPU won't adapt."), *JsonFilePath);
        Tuning = FPSOpponentModelTuning();
        Tuning.DefaultAdaptationDial = 0.f;
        return false;
    }
    for (const FString& Problem : PSOpponentModel::ValidateTuning(Loaded))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOpponentModel: %s: %s"), *JsonFilePath, *Problem);
    }
    Tuning = Loaded;
    return true;
}

void UPSOpponentModel::SetTuning(const FPSOpponentModelTuning& InTuning)
{
    Tuning = InTuning;
    bTuningLoaded = true;
}

void UPSOpponentModel::SetAdaptationDial(float Dial)
{
    AdaptationDial = FMath::Clamp(Dial, 0.f, 1.f);
}

float UPSOpponentModel::GetAdaptationDial()
{
    return AdaptationDial >= 0.f ? AdaptationDial : FMath::Clamp(GetTuning().DefaultAdaptationDial, 0.f, 1.f);
}

float UPSOpponentModel::GetAdaptationStrength(int32 Quarter)
{
    const FPSOpponentModelTuning& Settings = GetTuning();
    const float HalfStrength = Quarter >= Settings.HalftimeQuarter ? Settings.SecondHalfStrength : Settings.FirstHalfStrength;
    return GetAdaptationDial() * FMath::Clamp(HalfStrength, 0.f, 1.f);
}

FPSTendencyRead UPSOpponentModel::ReadTendency(bool bHumanOffense, int32 Down, int32 Distance, FName Personnel)
{
    return PSOpponentModel::ReadTendency(GameCells, GetCareerCells(), bHumanOffense, Down, Distance, Personnel, GetTuning());
}

FPSTendencyProfile UPSOpponentModel::CounterTendency(bool bCpuOffense, const FPSSituationContext& Situation, const FPSTendencyProfile& InTendency)
{
    FPSTendencyProfile Countered = InTendency;
    const bool bHumanOffense = !bCpuOffense;
    const float Strength = GetAdaptationStrength(Situation.Quarter);
    const FPSTendencyRead Read = ReadTendency(bHumanOffense, Situation.Down, Situation.Distance, OffensePersonnel);
    Countered.CounterWeights = PSOpponentModel::ComputeCounterWeights(Read, bHumanOffense, Strength, GetTuning());

    // The halftime adjustment: the first call that leans harder on a read says so. Without a
    // read yet, it waits for one.
    float& Last = LastStrength[PSOpponentModelPrivate::SideIndex(bCpuOffense)];
    const bool bStepUp = Last >= 0.f && Strength > Last + KINDA_SMALL_NUMBER;
    if (!bStepUp || Read.Basis != EPSTendencyBasis::None)
    {
        UPSTelemetryBus* Bus = BoundBus.Get();
        if (bStepUp && Bus)
        {
            FPSTelemetryOpponentAdjustmentEvent Event;
            Event.bCpuOffense = bCpuOffense;
            Event.Quarter = Situation.Quarter;
            Event.PreviousStrength = Last;
            Event.Strength = Strength;
            Event.TopCategory = Read.TopCategory;
            Event.TopShare = Read.TopShare;
            Event.Samples = Read.Samples;
            Bus->PublishOpponentAdjustment(Event);
        }
        Last = Strength;
    }
    return Countered;
}

const TArray<FPSTendencyCell>& UPSOpponentModel::GetCareerCells()
{
    EnsureCareerLoaded();
    return CareerCells;
}

void UPSOpponentModel::SetCareerCells(const TArray<FPSTendencyCell>& Cells)
{
    CareerCells = Cells;
    bCareerLoaded = true;
}

void UPSOpponentModel::EndGame()
{
    EnsureCareerLoaded();
    PSOpponentModel::MergeCells(CareerCells, GameCells);
    GameCells.Reset();
    PendingCategory[0].Reset();
    PendingCategory[1].Reset();
    LastStrength[0] = -1.f;
    LastStrength[1] = -1.f;
    SaveCareer();
}

void UPSOpponentModel::BindToBus(UPSTelemetryBus* Bus)
{
    if (!Bus || BoundBus.Get() == Bus)
    {
        return;
    }
    UnbindFromBus();
    Bus->OnPlayCallMC.AddUObject(this, &UPSOpponentModel::HandlePlayCall);
    Bus->OnSnapMC.AddUObject(this, &UPSOpponentModel::HandleSnap);
    Bus->OnPersonnelMC.AddUObject(this, &UPSOpponentModel::HandlePersonnel);
    BoundBus = Bus;
}

void UPSOpponentModel::UnbindFromBus()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnPlayCallMC.RemoveAll(this);
        Bus->OnSnapMC.RemoveAll(this);
        Bus->OnPersonnelMC.RemoveAll(this);
    }
    BoundBus.Reset();
}

void UPSOpponentModel::HandlePlayCall(const FPSTelemetryPlayCallEvent& Event)
{
    // Held until the snap: the call counts as shown once the play runs, so the CPU's call for
    // this play never sees it.
    FString& Pending = PendingCategory[PSOpponentModelPrivate::SideIndex(Event.bOffense)];
    if (Event.bHumanCall && PSOpponentModel::IsTracked(GetTuning(), Event.bOffense, Event.PlayCategory))
    {
        Pending = Event.PlayCategory;
    }
    else
    {
        // The CPU (or a quick call) made this side's call: nothing of his to count.
        Pending.Reset();
    }
}

void UPSOpponentModel::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    bool bCounted = false;
    for (const bool bOffense : { true, false })
    {
        FString& Pending = PendingCategory[PSOpponentModelPrivate::SideIndex(bOffense)];
        if (Pending.IsEmpty())
        {
            continue;
        }
        FPSTendencyCell Cell;
        Cell.bOffense = bOffense;
        Cell.Down = Event.Down;
        Cell.DistanceBucket = PSOpponentModel::GetDistanceBucket(Event.Distance, GetTuning());
        Cell.Personnel = OffensePersonnel;
        Cell.Category = Pending;
        Cell.Count = 1;
        PSOpponentModel::MergeCells(GameCells, { Cell });
        Pending.Reset();
        bCounted = true;
    }
    if (bCounted)
    {
        SaveCareer();
    }
}

void UPSOpponentModel::HandlePersonnel(const FPSTelemetryPersonnelEvent& Event)
{
    if (Event.bOffense)
    {
        OffensePersonnel = Event.PackageId;
    }
}

UPSSaveSubsystem* UPSOpponentModel::GetSaveSubsystem() const
{
    const UWorld* World = GetWorld();
    UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
    return GameInstance ? GameInstance->GetSubsystem<UPSSaveSubsystem>() : nullptr;
}

void UPSOpponentModel::EnsureCareerLoaded()
{
    if (bCareerLoaded)
    {
        return;
    }
    bCareerLoaded = true;
    UPSSaveSubsystem* Saves = GetSaveSubsystem();
    const FString Slot = UPSProfileSaveGame::GetDefaultSlotName();
    if (Saves && Saves->DoesSlotExist(Slot))
    {
        if (const UPSProfileSaveGame* Profile = Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot)))
        {
            CareerCells = Profile->OpponentTendencies;
        }
    }
}

void UPSOpponentModel::SaveCareer()
{
    UPSSaveSubsystem* Saves = GetSaveSubsystem();
    if (!Saves)
    {
        return;
    }
    // The profile keeps every game's calls, this one's so far included, so a game that ends
    // without EndGame still counts next time. Whatever else the profile holds is kept.
    EnsureCareerLoaded();
    const FString Slot = UPSProfileSaveGame::GetDefaultSlotName();
    UPSProfileSaveGame* Profile = Saves->DoesSlotExist(Slot) ? Cast<UPSProfileSaveGame>(Saves->LoadFromSlot(Slot)) : nullptr;
    if (!Profile)
    {
        Profile = NewObject<UPSProfileSaveGame>(this);
    }
    TArray<FPSTendencyCell> Everything = CareerCells;
    PSOpponentModel::MergeCells(Everything, GameCells);
    Profile->OpponentTendencies = MoveTemp(Everything);
    if (!Saves->SaveToSlot(Profile, Slot))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSOpponentModel: Could not save the play-calling history to %s."), *Slot);
    }
}
