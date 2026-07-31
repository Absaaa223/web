// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class DustWeather : ModuleRules
{
	public DustWeather(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DustCore",
			"DustWorld",
			"GameplayTags",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"DeveloperSettings",
		});
	}
}
