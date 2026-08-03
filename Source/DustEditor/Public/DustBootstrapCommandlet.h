// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DustBootstrapCommandlet.generated.h"

class UDustRegionData;
class UDustWeatherPreset;

/**
 * Generates the minimum content set the C++ runtime expects.
 *
 * ------------------------------------------------------------------------
 * WHY THIS EXISTS
 * ------------------------------------------------------------------------
 * DUST's runtime modules deliberately hard-reference nothing in /Game. That is
 * correct architecture, and it has one bad consequence: a fresh clone will boot
 * into a flat plane with a wall of "asset not found" errors, because the
 * composition, the region data, the weather presets, the material parameter
 * collections and the input assets are all content.
 *
 * This commandlet closes that gap. After running it, a fresh clone launches into
 * a real, playable, twelve-region world.
 *
 * Everything it produces is a *starting point* with tuned, defensible defaults,
 * intended to be hand-authored from there. It is not a content generator and it
 * is not run again after the first time - once a designer has touched a region
 * asset, this tool will refuse to overwrite it unless -force is passed.
 *
 * Usage:
 *   UnrealEditor-Cmd Dust.uproject -run=DustBootstrap [-force] [-only=regions]
 */
UCLASS()
class DUSTEDITOR_API UDustBootstrapCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDustBootstrapCommandlet();

	//~ Begin UCommandlet
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet

private:
	/** Creates the twelve region assets and the composition that lists them. */
	int32 GenerateWorld(bool bForce);

	/** Creates the six weather presets and their transition graph. */
	int32 GenerateWeather(bool bForce);

	/** Creates MPC_DustSky and MPC_DustWeather with the documented parameters. */
	int32 GenerateMaterialCollections(bool bForce);

	/** Creates the Enhanced Input mapping context and actions. */
	int32 GenerateInput(bool bForce);

	/** Creates the default bestiary. */
	int32 GenerateFauna(bool bForce);

	/** Builds one region asset with the design-authored defaults for its profile. */
	UDustRegionData* MakeRegion(
		const FString& AssetName,
		FName RegionId,
		const FText& DisplayName,
		const FString& DesignIntent,
		const FVector2D& Anchor,
		int32 Priority,
		enum class EDustTerrainProfile Profile,
		bool bForce);

	/** Saves a newly created package to disk. */
	static bool SavePackage(UObject* Asset);
};
