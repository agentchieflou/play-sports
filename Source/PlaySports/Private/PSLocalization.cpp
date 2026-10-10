#include "PSLocalization.h"
#include "PSSettingsSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/Paths.h"

const FName UPSLocalization::UITableId(TEXT("PSUI"));
const FName UPSLocalization::DataTableId(TEXT("PSUIData"));
const FName UPSLocalization::UnitsSettingId(TEXT("Units"));

namespace PSLocalizationPrivate
{
    static TAutoConsoleVariable<int32> CVarPseudo(
        TEXT("ps.Loc.Pseudo"),
        0,
        TEXT("1: pseudo-localize the UI (Epic 106). Text that came through the string tables shows accented, padded and bracketed; anything still plain bypassed them."),
        ECVF_Default);

    // Accented stand-ins for a-z and A-Z: still readable, never ASCII.
    static const TCHAR Lower[26] =
    {
        0x00E5, 0x0180, 0x00E7, 0x010F, 0x00E9, 0x0192, 0x011D, 0x0125, 0x00EE, 0x0135, 0x0137, 0x013C, 0x1E41,
        0x00F1, 0x00F6, 0x1E57, 0x01EB, 0x0155, 0x0161, 0x0163, 0x00FB, 0x1E7D, 0x0175, 0x1E8B, 0x00FD, 0x017E
    };
    static const TCHAR Upper[26] =
    {
        0x00C5, 0x1E02, 0x00C7, 0x010E, 0x00C9, 0x1E1E, 0x011C, 0x0124, 0x00CE, 0x0134, 0x0136, 0x013B, 0x1E40,
        0x00D1, 0x00D6, 0x1E56, 0x01EA, 0x0154, 0x0160, 0x0162, 0x00DB, 0x1E7C, 0x0174, 0x1E8A, 0x00DD, 0x017D
    };

    // Verbatim text's marks under pseudo-localization: single angle quotation marks.
    static const TCHAR VerbatimOpen = 0x2039;
    static const TCHAR VerbatimClose = 0x203A;

    // How much longer pseudo-localized text gets: translations run about a third longer.
    static const float PseudoGrowth = 0.3f;

    // Unit conversions.
    static const float PoundsPerKilogram = 2.20462262f;
    static const float CentimetersPerInch = 2.54f;
    static const int32 InchesPerFoot = 12;

    static bool bRegistered = false;

    static bool FindSource(FName TableId, const FString& Key, FString& OutSource)
    {
        UPSLocalization::RegisterStringTables();
        const FStringTableConstPtr Table = FStringTableRegistry::Get().FindStringTable(TableId);
        return Table.IsValid() && Table->GetSourceString(Key, OutSource);
    }

    static FText FromTable(FName TableId, const FString& Key)
    {
        const FText Text = FText::FromStringTable(TableId, Key, EStringTableLoadingPolicy::Find);
        return UPSLocalization::IsPseudoLocalization() ? FText::AsCultureInvariant(UPSLocalization::PseudoLocalize(Text.ToString())) : Text;
    }
}

void UPSLocalization::RegisterStringTables()
{
    if (PSLocalizationPrivate::bRegistered)
    {
        return;
    }
    PSLocalizationPrivate::bRegistered = true;

    // Paths are relative to the project's Content directory; the gather reads the same macros.
    LOCTABLE_FROMFILE_GAME("PSUI", "PSUI", "../Data/ui_text.csv");
    LOCTABLE_FROMFILE_GAME("PSUIData", "PSUIData", "../Data/ui_text_data.csv");
}

bool UPSLocalization::HasText(const FString& Key)
{
    FString Source;
    return PSLocalizationPrivate::FindSource(UITableId, Key, Source);
}

FText UPSLocalization::GetText(const FString& Key)
{
    if (!HasText(Key))
    {
        UE_LOG(LogTemp, Warning, TEXT("UPSLocalization: '%s' is not in Data/ui_text.csv."), *Key);
        return FText::AsCultureInvariant(FString::Printf(TEXT("<%s>"), *Key));
    }
    return PSLocalizationPrivate::FromTable(UITableId, Key);
}

FText UPSLocalization::Format(const FString& Key, const FFormatNamedArguments& Arguments)
{
    return FText::Format(FTextFormat(GetText(Key)), Arguments);
}

FText UPSLocalization::GetDataText(const FString& Key, const FString& Source)
{
    if (Source.IsEmpty())
    {
        return FText::GetEmpty();
    }
    FString TableSource;
    if (!PSLocalizationPrivate::FindSource(DataTableId, Key, TableSource) || !TableSource.Equals(Source, ESearchCase::CaseSensitive))
    {
        // Missing from Data/ui_text_data.csv, or changed since it was generated: shown as
        // written. tools/ui_text.py --write brings the table up to date.
        return FText::AsCultureInvariant(Source);
    }
    return PSLocalizationPrivate::FromTable(DataTableId, Key);
}

FText UPSLocalization::Verbatim(const FString& Text)
{
    if (IsPseudoLocalization() && !Text.IsEmpty())
    {
        FString Marked;
        Marked.AppendChar(PSLocalizationPrivate::VerbatimOpen);
        Marked += Text;
        Marked.AppendChar(PSLocalizationPrivate::VerbatimClose);
        return FText::AsCultureInvariant(Marked);
    }
    return FText::AsCultureInvariant(Text);
}

FText UPSLocalization::FromLocalized(const FString& Localized)
{
    return FText::AsCultureInvariant(Localized);
}

FText UPSLocalization::JoinLines(const TArray<FText>& Lines)
{
    return FText::Join(FText::AsCultureInvariant(TEXT("\n")), Lines);
}

FString UPSLocalization::MenuKey(FName ScreenId, const FString& Field)
{
    return FString::Printf(TEXT("Menu.%s.%s"), *ScreenId.ToString(), *Field);
}

FString UPSLocalization::MenuOptionKey(FName ScreenId, FName OptionId, const FString& Field)
{
    return FString::Printf(TEXT("Menu.%s.%s.%s"), *ScreenId.ToString(), *OptionId.ToString(), *Field);
}

FString UPSLocalization::SettingCategoryKey(FName CategoryId)
{
    return FString::Printf(TEXT("Setting.Category.%s"), *CategoryId.ToString());
}

FString UPSLocalization::SettingKey(FName SettingId, const FString& Field)
{
    return FString::Printf(TEXT("Setting.%s.%s"), *SettingId.ToString(), *Field);
}

FString UPSLocalization::SettingChoiceKey(FName SettingId, int32 Index)
{
    return FString::Printf(TEXT("Setting.%s.Choice%d"), *SettingId.ToString(), Index);
}

FString UPSLocalization::TipKey(FName TipId)
{
    return FString::Printf(TEXT("Tip.%s"), *TipId.ToString());
}

FString UPSLocalization::AdjustmentKey(FName AdjustmentId, const FString& Field)
{
    return FString::Printf(TEXT("Adjustment.%s.%s"), *AdjustmentId.ToString(), *Field);
}

void UPSLocalization::SetPseudoLocalization(bool bOn)
{
    PSLocalizationPrivate::CVarPseudo->Set(bOn ? 1 : 0, ECVF_SetByCode);
}

bool UPSLocalization::IsPseudoLocalization()
{
    return PSLocalizationPrivate::CVarPseudo.GetValueOnAnyThread() != 0;
}

FString UPSLocalization::PseudoLocalize(const FString& Text)
{
    FString Out;
    Out.Reserve(Text.Len() * 2 + 2);
    Out.AppendChar(TEXT('['));
    int32 PlaceholderDepth = 0;
    for (const TCHAR Char : Text)
    {
        if (Char == TEXT('{'))
        {
            ++PlaceholderDepth;
        }
        else if (Char == TEXT('}') && PlaceholderDepth > 0)
        {
            --PlaceholderDepth;
        }

        if (PlaceholderDepth == 0 && Char >= TEXT('a') && Char <= TEXT('z'))
        {
            Out.AppendChar(PSLocalizationPrivate::Lower[Char - TEXT('a')]);
        }
        else if (PlaceholderDepth == 0 && Char >= TEXT('A') && Char <= TEXT('Z'))
        {
            Out.AppendChar(PSLocalizationPrivate::Upper[Char - TEXT('A')]);
        }
        else
        {
            Out.AppendChar(Char);
        }
    }
    const int32 Padding = FMath::CeilToInt(Text.Len() * PSLocalizationPrivate::PseudoGrowth);
    for (int32 Index = 0; Index < Padding; ++Index)
    {
        Out.AppendChar(TEXT('~'));
    }
    Out.AppendChar(TEXT(']'));
    return Out;
}

bool UPSLocalization::IsFullyLocalized(const FString& Shown)
{
    int32 VerbatimDepth = 0;
    for (const TCHAR Char : Shown)
    {
        if (Char == PSLocalizationPrivate::VerbatimOpen)
        {
            ++VerbatimDepth;
        }
        else if (Char == PSLocalizationPrivate::VerbatimClose && VerbatimDepth > 0)
        {
            --VerbatimDepth;
        }
        else if (VerbatimDepth == 0 && ((Char >= TEXT('a') && Char <= TEXT('z')) || (Char >= TEXT('A') && Char <= TEXT('Z'))))
        {
            return false;
        }
    }
    return true;
}

FText UPSLocalization::FormatNumber(float Value, int32 MaxFractionalDigits)
{
    return FormatNumberIn(Value, MaxFractionalDigits, nullptr);
}

FText UPSLocalization::FormatNumberIn(float Value, int32 MaxFractionalDigits, const FCulturePtr& Culture)
{
    FNumberFormattingOptions Options;
    Options.SetUseGrouping(true);
    Options.SetMinimumFractionalDigits(0);
    Options.SetMaximumFractionalDigits(FMath::Max(0, MaxFractionalDigits));
    return FText::AsNumber(Value, &Options, Culture);
}

FText UPSLocalization::FormatPercent(float Fraction)
{
    FNumberFormattingOptions Whole;
    Whole.SetMaximumFractionalDigits(0);
    return FText::AsPercent(Fraction, &Whole);
}

FText UPSLocalization::FormatDate(const FDateTime& Date)
{
    return FText::AsDate(Date, EDateTimeStyle::Medium, FText::GetInvariantTimeZone());
}

FText UPSLocalization::FormatWeight(float WeightKg, EPSUnitSystem Units)
{
    FFormatNamedArguments Arguments;
    if (Units == EPSUnitSystem::Metric)
    {
        Arguments.Add(TEXT("Value"), FText::AsNumber(FMath::RoundToInt(WeightKg)));
        return Format(TEXT("Units.Kilograms"), Arguments);
    }
    Arguments.Add(TEXT("Value"), FText::AsNumber(FMath::RoundToInt(WeightKg * PSLocalizationPrivate::PoundsPerKilogram)));
    return Format(TEXT("Units.Pounds"), Arguments);
}

FText UPSLocalization::FormatHeight(float HeightCm, EPSUnitSystem Units)
{
    FFormatNamedArguments Arguments;
    if (Units == EPSUnitSystem::Metric)
    {
        Arguments.Add(TEXT("Value"), FText::AsNumber(FMath::RoundToInt(HeightCm)));
        return Format(TEXT("Units.Centimeters"), Arguments);
    }
    const int32 TotalInches = FMath::RoundToInt(HeightCm / PSLocalizationPrivate::CentimetersPerInch);
    Arguments.Add(TEXT("Feet"), FText::AsNumber(TotalInches / PSLocalizationPrivate::InchesPerFoot));
    Arguments.Add(TEXT("Inches"), FText::AsNumber(TotalInches % PSLocalizationPrivate::InchesPerFoot));
    return Format(TEXT("Units.FeetInches"), Arguments);
}

EPSUnitSystem UPSLocalization::GetUnitSystem(UPSSettingsSubsystem* Settings)
{
    if (!Settings || !Settings->GetCatalog().FindSetting(UnitsSettingId))
    {
        return EPSUnitSystem::Imperial;
    }
    return FMath::RoundToInt(Settings->GetValue(UnitsSettingId)) == int32(EPSUnitSystem::Metric) ? EPSUnitSystem::Metric : EPSUnitSystem::Imperial;
}
