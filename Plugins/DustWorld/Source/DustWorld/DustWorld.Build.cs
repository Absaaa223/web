// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

public class DustWorld : ModuleRules
{
	public DustWorld(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DustCore",
			"GameplayTags",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"Renderer",
			"PhysicsCore",
			"Chaos",
		});

		// The sampler is called hundreds of thousands of times per second across
		// worker threads. Unoptimised it would make the editor unusable.
		if (Target.Configuration == UnrealTargetConfiguration.Debug ||
			Target.Configuration == UnrealTargetConfiguration.DebugGame)
		{
			OptimizeCode = CodeOptimization.Always;
		}
	}
}
