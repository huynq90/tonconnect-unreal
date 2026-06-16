using UnrealBuildTool;

public class TonConnectEditor : ModuleRules
{
    public TonConnectEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "Json",
            "TonConnect",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "Slate",
            "SlateCore",
            "EditorWidgets",
            "EditorStyle",
            "AssetTools",
            "ToolMenus",
            "InputCore",
            "LevelEditor",
            "EditorScriptingUtilities",
        });

        UndefinedIdentifierWarningLevel = WarningLevel.Off;
    }
}
