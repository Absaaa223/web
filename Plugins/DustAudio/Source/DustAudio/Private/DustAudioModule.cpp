// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

class FDustAudioModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDustAudio, Log, TEXT("DustAudio online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDustAudio, Log, TEXT("DustAudio offline."));
	}
};

IMPLEMENT_MODULE(FDustAudioModule, DustAudio)
