// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

class FDustWeatherModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDustWeather, Log, TEXT("DustWeather online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDustWeather, Log, TEXT("DustWeather offline."));
	}
};

IMPLEMENT_MODULE(FDustWeatherModule, DustWeather)
