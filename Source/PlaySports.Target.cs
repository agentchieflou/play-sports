using UnrealBuildTool;

public class PlaySportsTarget : TargetRules
{
    public PlaySportsTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        ExtraModuleNames.Add("PlaySports");
    }
}
