// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

using UnrealBuildTool;

/// <summary>
/// Editor target for DUST. Adds the editor-only tooling module on top of the game.
/// </summary>
public class DustEditorTarget : TargetRules
{
	public DustEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;

		ExtraModuleNames.AddRange(new string[] { "Dust", "DustEditor" });

		bUseUnityBuild = true;
		bUseAdaptiveUnityBuild = true;
	}
}
