#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PSTelemetryBus.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelemetryBusRoundTripTest,
    "PlaySports.TelemetryBus.RoundTripPublishSubscribe",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetryBusRoundTripTest::RunTest(const FString& Parameters)
{
    UPSTelemetryBus* Bus = NewObject<UPSTelemetryBus>();
    TestNotNull(TEXT("Subsystem is instantiated"), Bus);
    if (!Bus)
    {
        return false;
    }

    struct FHelper
    {
        static void HandleSnap(const FPSTelemetrySnapEvent& Event, bool& bOutReceived, FPSTelemetrySnapEvent& OutEvent)
        {
            bOutReceived = true;
            OutEvent = Event;
        }
    };

    bool bReceived = false;
    FPSTelemetrySnapEvent ReceivedEvent;
    
    Bus->OnSnap.AddStatic(&FHelper::HandleSnap, bReceived, ReceivedEvent);

    FPSTelemetrySnapEvent PublishEvent;
    PublishEvent.YardLine = 42;
    PublishEvent.Down = 2;
    PublishEvent.Distance = 8;
    PublishEvent.GameClockSeconds = 120.f;

    Bus->PublishSnap(PublishEvent);

    TestTrue(TEXT("Subscriber received event"), bReceived);
    TestEqual(TEXT("YardLine matches"), ReceivedEvent.YardLine, 42);
    TestEqual(TEXT("Down matches"), ReceivedEvent.Down, 2);
    TestEqual(TEXT("Distance matches"), ReceivedEvent.Distance, 8);
    TestEqual(TEXT("Clock matches"), ReceivedEvent.GameClockSeconds, 120.f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPSTelemetryBusHistoryTest,
    "PlaySports.TelemetryBus.HistoryRingBuffer",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPSTelemetryBusHistoryTest::RunTest(const FString& Parameters)
{
    UPSTelemetryBus* Bus = NewObject<UPSTelemetryBus>();
    TestNotNull(TEXT("Subsystem is instantiated"), Bus);
    if (!Bus)
    {
        return false;
    }

    Bus->ClearHistory();
    TestEqual(TEXT("History is empty initially"), Bus->GetEventHistory().Num(), 0);

    for (int32 i = 0; i < 105; ++i)
    {
        FPSTelemetrySnapEvent Snap;
        Snap.YardLine = i;
        Bus->PublishSnap(Snap);
    }

    TArray<FPSTelemetryEvent> History = Bus->GetEventHistory();
    TestEqual(TEXT("History is capped at MaxHistorySize"), History.Num(), 100);

    TestTrue(TEXT("First event is correct"), History[0].Description.Contains(TEXT("YardLine=5")));
    TestTrue(TEXT("Last event is correct"), History[99].Description.Contains(TEXT("YardLine=104")));

    TestFalse(TEXT("Payload JSON is not empty"), History[0].PayloadJson.IsEmpty());

    return true;
}

#endif
