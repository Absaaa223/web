// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * DustCore module.
 *
 * Loads at PreDefault so that developer settings, gameplay tags and the console
 * command registry exist before any other DUST module or subsystem initialises.
 */
class FDustCoreModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/** Convenience accessor. Asserts if the module is not loaded. */
	static FDustCoreModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FDustCoreModule>(TEXT("DustCore"));
	}
};
