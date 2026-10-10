#include "PSJsonWriting.h"
#include "Misc/FileHelper.h"

FString PSJsonWriting::Quote(const FString& Value)
{
    FString Out = TEXT("\"");
    for (const TCHAR Char : Value)
    {
        if (Char == TEXT('"') || Char == TEXT('\\'))
        {
            Out.AppendChar(TEXT('\\'));
            Out.AppendChar(Char);
        }
        else if (Char < 0x20)
        {
            Out += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Char));
        }
        else
        {
            Out.AppendChar(Char);
        }
    }
    Out.AppendChar(TEXT('"'));
    return Out;
}

FString PSJsonWriting::Number(float Value)
{
    return FMath::IsNearlyEqual(Value, FMath::RoundToFloat(Value), 1e-4f) ? FString::Printf(TEXT("%d"), FMath::RoundToInt(Value)) : FString::SanitizeFloat(Value);
}

FString PSJsonWriting::Vector(const FVector& Value)
{
    return FString::Printf(TEXT("{ \"X\": %s, \"Y\": %s, \"Z\": %s }"),
        *Number(static_cast<float>(Value.X)), *Number(static_cast<float>(Value.Y)), *Number(static_cast<float>(Value.Z)));
}

FString PSJsonWriting::ArrayFile(const FString& Field, const TArray<FString>& Rows)
{
    return FString::Printf(TEXT("{\n  \"%s\": [\n    %s\n  ]\n}\n"), *Field, *FString::Join(Rows, TEXT(",\n    ")));
}

bool PSJsonWriting::SaveText(const FString& Path, const FString& Text)
{
    if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Warning, TEXT("PSJsonWriting: Could not write %s."), *Path);
        return false;
    }
    return true;
}
