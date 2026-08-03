// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class DustEditor : ModuleRules
{
	public DustEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Dust",
			"DustCore",
			"DustWorld",
			"DustWeather",
			"DustFauna",
			"DustNarrative",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"InputCore",
			"EnhancedInput",
			"GameplayTags",
			"MaterialEditor",
			"DeveloperSettings",
		});
	}
}
