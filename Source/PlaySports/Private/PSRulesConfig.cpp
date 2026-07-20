#include "PSRulesConfig.h"

int32 GetDefensivePersonnelCount(int32 Down, const UPSRulesConfig* RulesConfig)
{
    static const int32 BaseDefensivePersonnel = 11;

    const bool bOverloadEnabled = RulesConfig ? RulesConfig->bFourthDownDefensiveOverload : true;
    const int32 ExtraDefenders = RulesConfig ? RulesConfig->NumExtraDefendersOnFourthDown : 1;

    if (Down == 4 && bOverloadEnabled)
    {
        return BaseDefensivePersonnel + FMath::Max(0, ExtraDefenders);
    }

    return BaseDefensivePersonnel;
}
