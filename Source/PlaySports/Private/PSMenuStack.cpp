#include "PSMenuStack.h"

bool UPSMenuStack::Push(FName ScreenId)
{
    if (ScreenId.IsNone() || Top() == ScreenId)
    {
        return false;
    }

    const FName Previous = Top();
    Screens.Add(ScreenId);
    OnChanged.Broadcast(Previous, ScreenId, EPSMenuTransition::Push);
    return true;
}

bool UPSMenuStack::Pop()
{
    if (Screens.Num() == 0)
    {
        return false;
    }

    const FName Previous = Screens.Pop();
    OnChanged.Broadcast(Previous, Top(), EPSMenuTransition::Pop);
    return true;
}

void UPSMenuStack::Replace(FName ScreenId)
{
    const FName Previous = Top();
    if (Screens.Num() > 0)
    {
        Screens.Last() = ScreenId;
    }
    else
    {
        Screens.Add(ScreenId);
    }
    OnChanged.Broadcast(Previous, ScreenId, EPSMenuTransition::Replace);
}

void UPSMenuStack::Clear()
{
    if (Screens.Num() == 0)
    {
        return;
    }

    const FName Previous = Top();
    Screens.Reset();
    OnChanged.Broadcast(Previous, NAME_None, EPSMenuTransition::Clear);
}
