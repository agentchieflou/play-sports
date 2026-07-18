using UnrealBuildTool;

public class PlaySportsEditorTarget : TargetRules
{
    public PlaySportsEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        ExtraModuleNames.Add("PlaySports");
    }
}
