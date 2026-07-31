// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class DustWorldEditor : ModuleRules
{
	public DustWorldEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DustCore",
			"DustWorld",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"InputCore",
			"AssetTools",
			"ImageWrapper",
			"RenderCore",
			"GameplayTags",
		});
	}
}
