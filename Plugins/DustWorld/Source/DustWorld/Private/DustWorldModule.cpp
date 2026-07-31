// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustWorldModule.h"

#include "DustLog.h"

void FDustWorldModule::StartupModule()
{
	UE_LOG(LogDustWorld, Log, TEXT("DustWorld online."));
}

void FDustWorldModule::ShutdownModule()
{
	UE_LOG(LogDustWorld, Log, TEXT("DustWorld offline."));
}

IMPLEMENT_MODULE(FDustWorldModule, DustWorld)
