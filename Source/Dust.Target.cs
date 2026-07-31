// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

/// <summary>
/// Game (cooked runtime) target for DUST.
/// </summary>
public class DustTarget : TargetRules
{
	public DustTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

		ExtraModuleNames.Add("Dust");

		// The desert is one contiguous streamed world; unity builds keep iteration
		// times sane across the seven modules.
		bUseUnityBuild = true;
		bUseAdaptiveUnityBuild = true;

		// Shipping builds strip the developer overlays but keep the performance
		// governor, which is a gameplay-visible system rather than a debug tool.
		if (Target.Configuration == UnrealTargetConfiguration.Shipping)
		{
			bUseLoggingInShipping = false;
		}
	}
}
