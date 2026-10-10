// PSPlatformTypes.h - Epic 152: the platform services' shared types
#pragma once

#include "CoreMinimal.h"
#include "PSPlatformTypes.generated.h"

/** A user the platform knows (UPSPlatformServices::GetSignedInUser): a signed-in account on a
 *  console, the local player on a PC. */
USTRUCT(BlueprintType)
struct FPSPlatformUser
{
    GENERATED_BODY()

    /** The local player slot the user plays in: 0 for the first, 1 for the second (Epic 107). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    int32 LocalUserIndex = INDEX_NONE;

    /** The platform's stable ID for the user, the same every run ("Local0" on a PC). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FString UserId;

    /** The name to show: the account's name on a console, "Player 1" on a PC. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform")
    bool bSignedIn = false;

    bool IsSignedIn() const { return bSignedIn && !UserId.IsEmpty(); }
};
