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
        // the default map to travel to and MoviePlayer the loading screen (Epic 101).
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "ApplicationCore",
            "EngineSettings",
            "MoviePlayer"
        });

        PublicIncludePaths.AddRange(new string[] { });
        PrivateIncludePaths.AddRange(new string[] { });

        // Epic 145.2: the loaders read their data by path, FPaths::ProjectDir() / "Data/...". A
        // packaged build stages the whole Data/ folder as loose files at the same place under its
        // own project directory, so the same paths resolve there, and a tester can still edit the
        // tuning. PSDataPaths lists every default data file; the packaged smoke test
        // (-PSSmokeTest) checks the build carries each one.
        RuntimeDependencies.Add("$(ProjectDir)/Data/...", StagedFileType.NonUFS);
    }
}
