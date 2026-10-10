// PSJsonWriting.h - the pieces the content generators write Data/ files with (Epics 121, 122)
#pragma once

#include "CoreMinimal.h"

/**
 * Text for JSON the generators write, field by field, so every field name is exactly the
 * struct's (as tools/content_contracts.py and validate_data.py require). Reading stays
 * UPSDataIngestion's (Architecture rule 4).
 */
namespace PSJsonWriting
{
    /** Value as a JSON string literal, quotes and backslashes escaped. */
    PLAYSPORTS_API FString Quote(const FString& Value);

    /** A number as JSON: a whole number without a fraction (so an int32 field reads as one),
     *  others to at most six places. */
    PLAYSPORTS_API FString Number(float Value);

    /** An FVector as { "X": ..., "Y": ..., "Z": ... }. */
    PLAYSPORTS_API FString Vector(const FVector& Value);

    /** A file holding one array: { "<Field>": [ <Rows, one per line> ] }. */
    PLAYSPORTS_API FString ArrayFile(const FString& Field, const TArray<FString>& Rows);

    /** Writes Text to Path as UTF-8 without a byte-order mark, making its directory. False, with
     *  a warning, when it can't. */
    PLAYSPORTS_API bool SaveText(const FString& Path, const FString& Text);
}
