// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

class FDustFaunaModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDustFauna, Log, TEXT("DustFauna online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDustFauna, Log, TEXT("DustFauna offline."));
	}
};

IMPLEMENT_MODULE(FDustFaunaModule, DustFauna)
