// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

/**
 * Game-side editor module.
 *
 * Holds the bootstrap commandlet and any tooling specific to DUST rather than
 * to one of the plugins. Plugin-specific tools live in that plugin's editor
 * module (DustWorldEditor owns the terrain bake, for instance) so a plugin
 * stays self-contained.
 */
class FDustEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDust, Log, TEXT("DustEditor online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDust, Log, TEXT("DustEditor offline."));
	}
};

IMPLEMENT_MODULE(FDustEditorModule, DustEditor)
