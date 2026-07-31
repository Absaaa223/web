// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

class FDustNarrativeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDustNarrative, Log, TEXT("DustNarrative online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDustNarrative, Log, TEXT("DustNarrative offline."));
	}
};

IMPLEMENT_MODULE(FDustNarrativeModule, DustNarrative)
