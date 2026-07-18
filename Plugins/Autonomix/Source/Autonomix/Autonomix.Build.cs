using UnrealBuildTool;

public class Autonomix : ModuleRules
{
    public Autonomix(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Projects",
            "EditorScriptingUtilities",
            "PythonScriptPlugin"
        });
    }
}
