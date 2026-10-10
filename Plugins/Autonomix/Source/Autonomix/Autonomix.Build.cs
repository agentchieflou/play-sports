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

        // Its tools are served by AgenticLink's MCP server (Core 25.1, 25.2).
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Json",
            "AgenticLink"
        });

        // Undoable edits (FScopedTransaction) exist only in the editor.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
    }
}
