// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DustWorldBakeCommandlet.generated.h"

/**
 * Offline world baking and validation.
 *
 * Runs the same sampler the game uses, headless, over the whole world. Three
 * jobs, all of which are things you want a build machine doing rather than a
 * designer waiting on:
 *
 *   1. HEIGHTMAP EXPORT
 *      Writes 16-bit PNG heightmap tiles suitable for landscape import. The
 *      procedural terrain becomes a real landscape actor that Nanite, virtual
 *      shadow maps and the physics system can all use directly, while the
 *      runtime sampler remains available for the queries a landscape cannot
 *      cheaply answer (height 3 km away, in an unstreamed cell).
 *
 *   2. VALIDATION
 *      Sweeps the world for the failure modes that only show up at scale:
 *      unreachable regions, slopes no traversal move can climb out of, POI
 *      deserts where a player could walk for kilometres with nothing to find,
 *      and water surfaces that ended up above their basin rim.
 *
 *   3. STATISTICS
 *      Region area, elevation histograms, POI counts by category, mean
 *      distance to the nearest point of interest. This is the report that
 *      answers "is the dune sea actually as empty as we intended?" with a
 *      number instead of an opinion.
 *
 * Usage:
 *   UnrealEditor-Cmd Dust.uproject -run=DustWorldBake -mode=all -resolution=4033 -out=Saved/WorldBake
 */
UCLASS()
class DUSTWORLDEDITOR_API UDustWorldBakeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDustWorldBakeCommandlet();

	//~ Begin UCommandlet
	virtual int32 Main(const FString& Params) override;
	//~ End UCommandlet

private:
	/** Writes 16-bit heightmap tiles for landscape import. */
	int32 RunHeightmapExport(const FString& OutputDirectory, int32 TileResolution);

	/** Sweeps for design-rule violations. Returns the number of failures. */
	int32 RunValidation(const FString& OutputDirectory);

	/** Writes a statistics report. */
	int32 RunStatistics(const FString& OutputDirectory);

	/** Builds a sampler from the project's world composition. */
	bool BuildSampler(struct FDustWorldSamplerConfig& OutConfig) const;
};
