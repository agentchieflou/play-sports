#include "PSSessionTelemetrySave.h"
#include "PSSaveSubsystem.h"
#include "JsonObjectConverter.h"

FString UPSSessionTelemetrySave::GetDefaultSlotName()
{
    return UPSSaveSubsystem::MakeSlotName(EPSSaveCategory::Telemetry, TEXT("Sessions"));
}

void UPSSessionTelemetrySave::UpsertSession(const FPSSessionSummary& Summary, int32 MaxSessions)
{
    const FGuid Id = Summary.SessionId;
    const int32 Existing = Sessions.IndexOfByPredicate([Id](const FPSSessionSummary& Stored) { return Stored.SessionId == Id; });
    if (Existing != INDEX_NONE)
    {
        Sessions[Existing] = Summary;
    }
    else
    {
        Sessions.Add(Summary);
    }

    const int32 Excess = Sessions.Num() - FMath::Max(MaxSessions, 1);
    if (Excess > 0)
    {
        Sessions.RemoveAt(0, Excess);
    }
}

void UPSSessionTelemetrySave::RemoveSession(const FGuid& SessionId)
{
    Sessions.RemoveAll([&SessionId](const FPSSessionSummary& Stored) { return Stored.SessionId == SessionId; });
}

FPSSessionTelemetryReport UPSSessionTelemetrySave::BuildReport() const
{
    FPSSessionTelemetryReport Report;
    for (const FPSSessionSummary& Session : Sessions)
    {
        ++Report.Sessions;
        if (!Session.bEndedCleanly)
        {
            ++Report.UncleanSessions;
        }

        FPSModeUsage* Usage = Report.Modes.FindByPredicate([&Session](const FPSModeUsage& Mode) { return Mode.Mode == Session.Mode; });
        if (!Usage)
        {
            Usage = &Report.Modes.AddDefaulted_GetRef();
            Usage->Mode = Session.Mode;
        }
        ++Usage->Sessions;
        Usage->Plays += Session.PlayCount;
        Usage->Seconds += Session.DurationSeconds;
    }
    return Report;
}

FString UPSSessionTelemetrySave::ReportToJson(const FPSSessionTelemetryReport& Report)
{
    FString Json;
    FJsonObjectConverter::UStructToJsonObjectString(Report, Json);
    return Json;
}

UPSSessionTelemetrySave* UPSSessionTelemetrySave::LoadStore(UPSSaveSubsystem* Saves, const FString& SlotName)
{
    if (!Saves || !Saves->DoesSlotExist(SlotName))
    {
        return nullptr;
    }
    return Cast<UPSSessionTelemetrySave>(Saves->LoadFromSlot(SlotName));
}

EPSTelemetryConsent UPSSessionTelemetrySave::GetConsentInSlot(UPSSaveSubsystem* Saves, const FString& SlotName)
{
    const UPSSessionTelemetrySave* Store = LoadStore(Saves, SlotName);
    if (!Store)
    {
        return EPSTelemetryConsent::NotAsked;
    }
    return Store->bOptedIn ? EPSTelemetryConsent::OptedIn : EPSTelemetryConsent::OptedOut;
}

bool UPSSessionTelemetrySave::SetConsentInSlot(UPSSaveSubsystem* Saves, const FString& SlotName, bool bOptIn)
{
    if (!Saves)
    {
        return false;
    }

    UPSSessionTelemetrySave* Store = bOptIn ? LoadStore(Saves, SlotName) : nullptr;
    if (!bOptIn)
    {
        // Erase first: a plain save would copy the old sessions into the backup.
        if (!Saves->DeleteSlot(SlotName))
        {
            return false;
        }
    }
    if (!Store)
    {
        Store = NewObject<UPSSessionTelemetrySave>();
    }
    Store->bOptedIn = bOptIn;
    return Saves->SaveToSlot(Store, SlotName);
}

EPSTelemetryConsent UPSSessionTelemetrySave::GetConsent(UPSSaveSubsystem* Saves)
{
    return GetConsentInSlot(Saves, GetDefaultSlotName());
}

bool UPSSessionTelemetrySave::SetConsent(UPSSaveSubsystem* Saves, bool bOptIn)
{
    return SetConsentInSlot(Saves, GetDefaultSlotName(), bOptIn);
}
