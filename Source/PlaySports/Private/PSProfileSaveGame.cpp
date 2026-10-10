#include "PSProfileSaveGame.h"
#include "PSSaveSubsystem.h"

FString UPSProfileSaveGame::GetDefaultSlotName()
{
    return UPSSaveSubsystem::MakeSlotName(EPSSaveCategory::Profile, TEXT("Player"));
}
