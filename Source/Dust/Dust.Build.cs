// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class Dust : ModuleRules
{
	public Dust(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",

			// DUST plugins
			"DustCore",
			"DustWorld",
			"DustWeather",
			"DustFauna",
			"DustNarrative",
			"DustAudio",

			"GameplayTags",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"RenderCore",
			"RHI",
			"PhysicsCore",
			"Chaos",
			"CinematicCamera",
			"ImageWrapper",
			"DeveloperSettings",
			"ApplicationCore",
			"Projects",
		});
	}
}
