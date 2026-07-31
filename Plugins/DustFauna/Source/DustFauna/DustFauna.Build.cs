// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class DustFauna : ModuleRules
{
	public DustFauna(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DustCore",
			"DustWorld",
			"DustWeather",
			"GameplayTags",
			"AIModule",
			"NavigationSystem",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTasks",
			"DeveloperSettings",
		});
	}
}
