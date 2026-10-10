// PSNetRandomStreams.cpp - Epic 108: the match's seeded random streams
#include "PSNetRandomStreams.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/Crc.h"

void UPSNetRandomStreams::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UPSTelemetryBus* Bus = Collection.InitializeDependency<UPSTelemetryBus>();
    if (Bus)
    {
        Bus->OnSnapMC.AddUObject(this, &UPSNetRandomStreams::HandleSnap);
        BoundBus = Bus;
    }

    // An unseeded match is a different game every time. The default comes from the clock, not
    // from FMath::Rand: drawing from the global stream here would shift the rolls of anything
    // that seeded it (the franchise quick sims, their tests) whenever a world is made.
    const uint64 Cycles = FPlatformTime::Cycles64();
    SetMatchSeed(MixSeed(static_cast<int32>(Cycles & 0xffffffffu), static_cast<uint32>(Cycles >> 32)));
}

void UPSNetRandomStreams::Deinitialize()
{
    if (UPSTelemetryBus* Bus = BoundBus.Get())
    {
        Bus->OnSnapMC.RemoveAll(this);
    }
    BoundBus.Reset();
    Streams.Reset();
    Super::Deinitialize();
}

bool UPSNetRandomStreams::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UPSNetRandomStreams* UPSNetRandomStreams::Get(const UObject* WorldContext)
{
    const UWorld* World = WorldContext && GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
    return World ? World->GetSubsystem<UPSNetRandomStreams>() : nullptr;
}

void UPSNetRandomStreams::SetMatchSeed(int32 Seed)
{
    MatchSeed = Seed;
    PlayIndex = 0;
    PlaySeed = MixSeed(MatchSeed, 0u);
    Streams.Reset();
}

void UPSNetRandomStreams::BeginPlayStreams(const FPSTelemetrySnapEvent& Snap)
{
    ++PlayIndex;
    PlaySeed = MixSeed(MixSeed(MatchSeed, static_cast<uint32>(PlayIndex)), HashSnap(Snap));
    Streams.Reset();
}

void UPSNetRandomStreams::HandleSnap(const FPSTelemetrySnapEvent& Event)
{
    BeginPlayStreams(Event);
}

float UPSNetRandomStreams::Roll(const TCHAR* Domain, FName Key)
{
    return FindStream(Domain, Key).FRand();
}

float UPSNetRandomStreams::RollRange(const TCHAR* Domain, float Min, float Max, FName Key)
{
    return FindStream(Domain, Key).FRandRange(Min, Max);
}

FVector UPSNetRandomStreams::RollUnitVector(const TCHAR* Domain, FName Key)
{
    return FindStream(Domain, Key).VRand();
}

int32 UPSNetRandomStreams::RollSeed(const TCHAR* Domain, FName Key)
{
    return static_cast<int32>(FindStream(Domain, Key).GetUnsignedInt());
}

int32 UPSNetRandomStreams::MakeMatchSeed(const TCHAR* Domain) const
{
    return MixSeed(MatchSeed, HashText(Domain));
}

int32 UPSNetRandomStreams::MixSeed(int32 Seed, uint32 Salt)
{
    // Golden-ratio combine, then MurmurHash3's 32-bit finalizer: every input bit reaches every
    // output bit, with unsigned (wrapping) arithmetic only.
    const uint32 Base = static_cast<uint32>(Seed);
    uint32 Mixed = Base ^ (Salt + 0x9E3779B9u + (Base << 6) + (Base >> 2));
    Mixed ^= Mixed >> 16;
    Mixed *= 0x85EBCA6Bu;
    Mixed ^= Mixed >> 13;
    Mixed *= 0xC2B2AE35u;
    Mixed ^= Mixed >> 16;
    return static_cast<int32>(Mixed);
}

uint32 UPSNetRandomStreams::HashText(const FString& Text)
{
    return FCrc::StrCrc32(*Text.ToLower());
}

uint32 UPSNetRandomStreams::HashSnap(const FPSTelemetrySnapEvent& Snap)
{
    uint32 ClockBits = 0;
    static_assert(sizeof(uint32) == sizeof(float), "The game clock is a 32-bit float");
    FMemory::Memcpy(&ClockBits, &Snap.GameClockSeconds, sizeof(ClockBits));

    uint32 Hash = static_cast<uint32>(MixSeed(Snap.Down, static_cast<uint32>(Snap.Distance)));
    Hash = static_cast<uint32>(MixSeed(static_cast<int32>(Hash), static_cast<uint32>(Snap.YardLine)));
    return static_cast<uint32>(MixSeed(static_cast<int32>(Hash), ClockBits));
}

FRandomStream& UPSNetRandomStreams::FindStream(const TCHAR* Domain, FName Key)
{
    const FString Name = Key.IsNone() ? FString(Domain) : FString::Printf(TEXT("%s|%s"), Domain, *Key.ToString());
    if (FRandomStream* Found = Streams.Find(Name))
    {
        return *Found;
    }
    return Streams.Add(Name, FRandomStream(MixSeed(PlaySeed, HashText(Name))));
}
