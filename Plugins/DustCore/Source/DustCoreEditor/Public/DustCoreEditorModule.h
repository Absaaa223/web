// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Editor-side companion to DustCore.
 *
 * Owns the DUST tool menu, the asset validation hooks and the shared editor
 * style. Anything that needs UnrealEd lives behind this module so the runtime
 * never picks up an editor dependency.
 */
class FDustCoreEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Builds the "DUST" section in the level editor toolbar. */
	void RegisterToolMenus();

	/** Registers content validators that run on save and in CI. */
	void RegisterValidators();

	FDelegateHandle ToolMenusHandle;
};
