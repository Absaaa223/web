// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

/**
 * Editor tooling for the world plugin.
 *
 * The heavy lifting lives in UDustWorldBakeCommandlet so that the same code
 * runs from the toolbar and from CI. This module only wires the menu entries.
 */
class FDustWorldEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDustWorld, Log, TEXT("DustWorldEditor online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDustWorld, Log, TEXT("DustWorldEditor offline."));
	}
};

IMPLEMENT_MODULE(FDustWorldEditorModule, DustWorldEditor)
