// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustCoreModule.h"

#include "DustLog.h"
#include "DustGameplayTags.h"

DEFINE_LOG_CATEGORY(LogDust);
DEFINE_LOG_CATEGORY(LogDustWorld);
DEFINE_LOG_CATEGORY(LogDustWeather);
DEFINE_LOG_CATEGORY(LogDustFauna);
DEFINE_LOG_CATEGORY(LogDustNarrative);
DEFINE_LOG_CATEGORY(LogDustAudio);
DEFINE_LOG_CATEGORY(LogDustTraversal);
DEFINE_LOG_CATEGORY(LogDustSave);
DEFINE_LOG_CATEGORY(LogDustPerf);

#define LOCTEXT_NAMESPACE "FDustCoreModule"

void FDustCoreModule::StartupModule()
{
	// Native tags must be registered before any subsystem or data asset that
	// references them is loaded, which is why this module is PreDefault.
	FDustGameplayTags::InitialiseNativeTags();

	UE_LOG(LogDust, Log, TEXT("DustCore online."));
}

void FDustCoreModule::ShutdownModule()
{
	UE_LOG(LogDust, Log, TEXT("DustCore offline."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDustCoreModule, DustCore)
