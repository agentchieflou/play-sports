#include "PSDeterminism.h"

namespace PSDeterminismPrivate
{
    FPSReplayDivergence Make(int32 EventIndex, int32 TickIndex, const TCHAR* Field, const FString& Expected, const FString& Actual)
    {
        FPSReplayDivergence Divergence;
        Divergence.bDiverged = true;
        Divergence.EventIndex = EventIndex;
        Divergence.TickIndex = TickIndex;
        Divergence.Field = Field;
        Divergence.Expected = Expected;
        Divergence.Actual = Actual;
        return Divergence;
    }
}

FPSReplayDivergence UPSDeterminism::FindFirstDivergence(const FPSReplayRecording& Expected, const FPSReplayRecording& Actual)
{
    using namespace PSDeterminismPrivate;

    if (Expected.Header.RandomSeed != Actual.Header.RandomSeed)
    {
        return Make(INDEX_NONE, INDEX_NONE, TEXT("RandomSeed"), FString::FromInt(Expected.Header.RandomSeed), FString::FromInt(Actual.Header.RandomSeed));
    }
    if (Expected.Header.FixedDeltaSeconds != Actual.Header.FixedDeltaSeconds)
    {
        return Make(INDEX_NONE, INDEX_NONE, TEXT("FixedDeltaSeconds"),
            FString::SanitizeFloat(Expected.Header.FixedDeltaSeconds), FString::SanitizeFloat(Actual.Header.FixedDeltaSeconds));
    }

    const int32 Shared = FMath::Min(Expected.Events.Num(), Actual.Events.Num());
    for (int32 Index = 0; Index < Shared; ++Index)
    {
        const FPSReplayEventRecord& Want = Expected.Events[Index];
        const FPSReplayEventRecord& Got = Actual.Events[Index];
        if (Want.TickIndex != Got.TickIndex)
        {
            return Make(Index, Want.TickIndex, TEXT("TickIndex"), FString::FromInt(Want.TickIndex), FString::FromInt(Got.TickIndex));
        }
        if (Want.EventType != Got.EventType)
        {
            return Make(Index, Want.TickIndex, TEXT("EventType"), Want.EventType, Got.EventType);
        }
        if (Want.PayloadJson != Got.PayloadJson)
        {
            return Make(Index, Want.TickIndex, TEXT("Payload"), Want.PayloadJson, Got.PayloadJson);
        }
    }

    if (Expected.Events.Num() != Actual.Events.Num())
    {
        const FPSReplayRecording& Longer = Expected.Events.Num() > Actual.Events.Num() ? Expected : Actual;
        return Make(Shared, Longer.Events[Shared].TickIndex, TEXT("EventCount"),
            FString::FromInt(Expected.Events.Num()), FString::FromInt(Actual.Events.Num()));
    }
    return FPSReplayDivergence();
}

FString UPSDeterminism::DescribeDivergence(const FPSReplayDivergence& Divergence)
{
    if (!Divergence.bDiverged)
    {
        return TEXT("identical");
    }
    if (Divergence.EventIndex == INDEX_NONE)
    {
        return FString::Printf(TEXT("headers differ: %s expected %s, got %s"), *Divergence.Field, *Divergence.Expected, *Divergence.Actual);
    }
    return FString::Printf(TEXT("diverged at event %d (tick %d): %s expected %s, got %s"),
        Divergence.EventIndex, Divergence.TickIndex, *Divergence.Field, *Divergence.Expected, *Divergence.Actual);
}
