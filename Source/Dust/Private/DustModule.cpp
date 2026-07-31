// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "CoreMinimal.h"
#include "DustLog.h"
#include "Modules/ModuleManager.h"

/**
 * The game module.
 *
 * Contains only what is specific to *this* game: the player, their traversal
 * moves, the camera, the interface and photo mode. Everything reusable lives in
 * the plugins, which is what would let a second project built on this
 * foundation ship without inheriting DUST's paraglider.
 */
class FDustGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDust, Log, TEXT("DUST // Beyond the Horizon - game module online."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDust, Log, TEXT("DUST game module offline."));
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDustGameModule, Dust, "Dust");
