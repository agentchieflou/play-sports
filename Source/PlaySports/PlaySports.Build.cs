using UnrealBuildTool;

public class PlaySports : ModuleRules
{
    public PlaySports(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "EnhancedInput",
            "UMG",
            "AIModule",
            "NavigationSystem",
            "Json",
            "JsonUtilities",
            "FunctionalTesting"
        });

        // Slate's input pre-processor and the platform device mapper feed active-device
        // tracking (Epic 127, UPSInputDeviceComponent); EngineSettings gives the front end
        // the default map to travel to (Epic 101, UPSMenuComponent).
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "ApplicationCore",
            "EngineSettings"
        });

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });
    }
}
