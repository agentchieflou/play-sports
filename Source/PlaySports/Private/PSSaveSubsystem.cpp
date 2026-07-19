#include "PSSaveSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Crc.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Async/Async.h"

namespace
{
    constexpr uint32 SaveFileMagic = 0x50534156; // 'PSAV'
    constexpr int32 SaveFormatVersion = 1;
    constexpr int32 HeaderBytes = sizeof(uint32) + sizeof(int32) + sizeof(uint32) + sizeof(int32);
}

FString UPSSaveSubsystem::MakeSlotName(EPSSaveCategory Category, const FString& Id)
{
    const UEnum* CategoryEnum = StaticEnum<EPSSaveCategory>();
    return FString::Printf(TEXT("%s_%s"), *CategoryEnum->GetNameStringByValue(static_cast<int64>(Category)), *Id);
}

FString UPSSaveSubsystem::GetSlotPath(const FString& SlotName)
{
    return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".psav"));
}

bool UPSSaveSubsystem::DoesSlotExist(const FString& SlotName) const
{
    return FPaths::FileExists(GetSlotPath(SlotName));
}

bool UPSSaveSubsystem::SerializeToFileData(UPSSaveGame* SaveObject, TArray<uint8>& OutFileData) const
{
    if (!SaveObject)
    {
        return false;
    }

    SaveObject->SaveVersion = UPSSaveGame::CurrentSaveVersion;
    SaveObject->SavedAtUtc = FDateTime::UtcNow();

    TArray<uint8> Payload;
    if (!UGameplayStatics::SaveGameToMemory(SaveObject, Payload) || Payload.Num() == 0)
    {
        return false;
    }

    uint32 Magic = SaveFileMagic;
    int32 FormatVersion = SaveFormatVersion;
    uint32 PayloadCrc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
    int32 PayloadSize = Payload.Num();

    FMemoryWriter Writer(OutFileData);
    Writer << Magic;
    Writer << FormatVersion;
    Writer << PayloadCrc;
    Writer << PayloadSize;
    OutFileData.Append(Payload);
    return true;
}

bool UPSSaveSubsystem::ValidateAndExtractPayload(const TArray<uint8>& FileData, TArray<uint8>& OutPayload)
{
    if (FileData.Num() <= HeaderBytes)
    {
        return false;
    }

    FMemoryReader Reader(FileData);
    uint32 Magic = 0;
    int32 FormatVersion = 0;
    uint32 PayloadCrc = 0;
    int32 PayloadSize = 0;
    Reader << Magic;
    Reader << FormatVersion;
    Reader << PayloadCrc;
    Reader << PayloadSize;

    if (Magic != SaveFileMagic || FormatVersion != SaveFormatVersion)
    {
        return false;
    }
    if (PayloadSize <= 0 || FileData.Num() != HeaderBytes + PayloadSize)
    {
        return false;
    }

    OutPayload.Reset(PayloadSize);
    OutPayload.Append(FileData.GetData() + HeaderBytes, PayloadSize);

    return FCrc::MemCrc32(OutPayload.GetData(), OutPayload.Num()) == PayloadCrc;
}

bool UPSSaveSubsystem::WriteFileWithBackup(const FString& Path, const TArray<uint8>& FileData)
{
    IPlatformFile& FileSystem = FPlatformFileManager::Get().GetPlatformFile();
    FileSystem.CreateDirectoryTree(*FPaths::GetPath(Path));

    if (FPaths::FileExists(Path))
    {
        const FString BackupPath = Path + TEXT(".bak");
        FileSystem.DeleteFile(*BackupPath);
        FileSystem.CopyFile(*BackupPath, *Path);
    }

    return FFileHelper::SaveArrayToFile(FileData, *Path);
}

bool UPSSaveSubsystem::ReadFileWithFallback(const FString& Path, TArray<uint8>& OutFileData, TArray<uint8>& OutPayload)
{
    if (FFileHelper::LoadFileToArray(OutFileData, *Path) && ValidateAndExtractPayload(OutFileData, OutPayload))
    {
        return true;
    }

    const FString BackupPath = Path + TEXT(".bak");
    OutFileData.Reset();
    OutPayload.Reset();
    if (FFileHelper::LoadFileToArray(OutFileData, *BackupPath) && ValidateAndExtractPayload(OutFileData, OutPayload))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSSaveSubsystem: primary save corrupt, loaded backup for %s"), *Path);
        return true;
    }
    return false;
}

UPSSaveGame* UPSSaveSubsystem::LoadAndMigrate(const TArray<uint8>& Payload)
{
    USaveGame* Raw = UGameplayStatics::LoadGameFromMemory(Payload);
    UPSSaveGame* Save = Cast<UPSSaveGame>(Raw);
    if (!Save)
    {
        return nullptr;
    }

    for (int32 Version = Save->SaveVersion; Version < UPSSaveGame::CurrentSaveVersion; ++Version)
    {
        if (!Save->MigrateFrom(Version))
        {
            UE_LOG(LogTemp, Warning, TEXT("PSSaveSubsystem: migration from version %d failed"), Version);
            return nullptr;
        }
    }
    Save->SaveVersion = UPSSaveGame::CurrentSaveVersion;
    return Save;
}

bool UPSSaveSubsystem::SaveToSlot(UPSSaveGame* SaveObject, const FString& SlotName)
{
    TArray<uint8> FileData;
    if (!SerializeToFileData(SaveObject, FileData))
    {
        return false;
    }
    return WriteFileWithBackup(GetSlotPath(SlotName), FileData);
}

UPSSaveGame* UPSSaveSubsystem::LoadFromSlot(const FString& SlotName)
{
    TArray<uint8> FileData;
    TArray<uint8> Payload;
    if (!ReadFileWithFallback(GetSlotPath(SlotName), FileData, Payload))
    {
        return nullptr;
    }
    return LoadAndMigrate(Payload);
}

void UPSSaveSubsystem::SaveToSlotAsync(UPSSaveGame* SaveObject, const FString& SlotName, FPSSaveOpComplete OnComplete)
{
    TArray<uint8> FileData;
    if (!SerializeToFileData(SaveObject, FileData))
    {
        OnComplete.ExecuteIfBound(SlotName, false);
        return;
    }

    const FString Path = GetSlotPath(SlotName);
    Async(EAsyncExecution::ThreadPool, [FileData = MoveTemp(FileData), Path, SlotName, OnComplete]()
    {
        const bool bWritten = WriteFileWithBackup(Path, FileData);
        AsyncTask(ENamedThreads::GameThread, [SlotName, OnComplete, bWritten]()
        {
            OnComplete.ExecuteIfBound(SlotName, bWritten);
        });
    });
}

int32 UPSSaveSubsystem::LoadFromSlotAsync(const FString& SlotName, FPSSaveOpComplete OnComplete)
{
    const int32 RequestId = NextAsyncLoadRequestId++;
    const FString Path = GetSlotPath(SlotName);
    TWeakObjectPtr<UPSSaveSubsystem> WeakThis(this);

    Async(EAsyncExecution::ThreadPool, [WeakThis, Path, SlotName, OnComplete, RequestId]()
    {
        TArray<uint8> FileData;
        TArray<uint8> Payload;
        const bool bRead = ReadFileWithFallback(Path, FileData, Payload);

        AsyncTask(ENamedThreads::GameThread, [WeakThis, Payload = MoveTemp(Payload), SlotName, OnComplete, bRead, RequestId]()
        {
            UPSSaveSubsystem* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            // UObject deserialization stays on the game thread by design.
            UPSSaveGame* Result = bRead ? Self->LoadAndMigrate(Payload) : nullptr;
            Self->PendingLoadResults.Add(RequestId, Result);
            OnComplete.ExecuteIfBound(SlotName, Result != nullptr);
        });
    });

    return RequestId;
}

UPSSaveGame* UPSSaveSubsystem::GetAsyncLoadResult(int32 RequestId)
{
    if (UPSSaveGame** Found = PendingLoadResults.Find(RequestId))
    {
        UPSSaveGame* Result = *Found;
        PendingLoadResults.Remove(RequestId);
        return Result;
    }
    return nullptr;
}
