// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

/// <summary>
/// Headless target. DUST is single-player, but a headless build lets the world
/// validation and soak-test tooling run the real simulation on build machines
/// without a GPU. See Tools/RunWorldSoak.sh.
/// </summary>
public class DustServerTarget : TargetRules
{
	public DustServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

		ExtraModuleNames.Add("Dust");

		bUsesSteam = false;
	}
}
