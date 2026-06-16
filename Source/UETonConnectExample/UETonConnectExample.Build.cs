// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UETonConnectExample : ModuleRules
{
	public UETonConnectExample(ReadOnlyTargetRules Target) : base(Target)
	{
		// PrivateDependencyModuleNames.AddRange(new string[] { "Bignumber" });
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
			{ "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "TonConnect" });
		// PrivateDependencyModuleNames.Add("HTTP"); 
	}
}