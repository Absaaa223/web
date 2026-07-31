// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "DustTypes.h"
#include "World/DustRegionData.h"

/**
 * A thread-safe, UObject-free copy of one region's sampling data.
 *
 * The sampler must run on worker threads during cooks, streaming and creature
 * pathfinding. Touching a UObject from those contexts is a crash waiting for a
 * garbage collection to happen at the wrong moment, so we snapshot everything
 * the maths needs into POD at construction time and never look at the asset again.
 */
struct DUSTWORLD_API FDustRegionSnapshot
{
	FName RegionId;
	int32 Index = INDEX_NONE;

	FVector2D Anchor = FVector2D::ZeroVector;
	double CoreRadius = 0.0;
	double FalloffRadius = 0.0;
	double BoundaryWarp = 0.0;
	int32 Priority = 0;

	EDustTerrainProfile Profile = EDustTerrainProfile::DuneSea;
	FDustTerrainParams Terrain;

	float WindMultiplier = 1.0f;

	/** Seed derived from the master seed and RegionId, so regions are independent. */
	uint32 Seed = 0;
};

/** Everything the sampler needs to exist. Cheap to copy, safe to hold. */
struct DUSTWORLD_API FDustWorldSamplerConfig
{
	/** Master world seed. */
	uint32 MasterSeed = 0;

	/** Half-extent of the playable world in cm. */
	double WorldHalfExtent = 5000000.0;

	/** Distance over which terrain rises into the boundary massif. */
	double BoundaryFalloff = 400000.0;

	/** Region snapshots, in composition order. */
	TArray<FDustRegionSnapshot> Regions;

	/** Terrain used where no region has influence. */
	FDustTerrainParams FallbackTerrain;

	/** Current global wind direction, normalised, in XY. Drives dune orientation. */
	FVector2D PrevailingWind = FVector2D(1.0, 0.25);

	/**
	 * Accumulated dune migration phase, in cm of downwind travel.
	 *
	 * This is the one piece of *mutable* state in an otherwise pure function,
	 * and it is deliberate: dunes move. The weather subsystem advances it, the
	 * save file stores it, and because it enters the maths as a simple offset
	 * along the wind axis, the world remains reproducible from
	 * (seed, phase) rather than requiring a stored heightfield.
	 */
	double DuneMigrationPhase = 0.0;

	/** Builds a config from the composition asset and developer settings. */
	static FDustWorldSamplerConfig FromComposition(const UDustWorldComposition& Composition, uint32 InMasterSeed);
};

/**
 * THE WORLD SAMPLER.
 *
 * Answers "what is at this XY?" deterministically, without allocation, without
 * engine state, and identically on every platform and in every process.
 *
 * ------------------------------------------------------------------------
 * WHY A PURE FUNCTION
 * ------------------------------------------------------------------------
 * Four separate systems need to know where the ground is:
 *
 *   1. The offline landscape baker, running in a cook commandlet.
 *   2. The runtime, for foot placement, prop settling and vehicle suspension.
 *   3. The creature navigation layer, on worker threads, far from any loaded cell.
 *   4. The point-of-interest generator, which must place a temple on a ridge
 *      before that ridge has ever been streamed in.
 *
 * If these disagree, the failure mode is a player falling through the world.
 * Making the answer a pure function of (config, position) removes the class of
 * bug entirely - there is no state to get out of sync.
 *
 * ------------------------------------------------------------------------
 * COST
 * ------------------------------------------------------------------------
 * A full SampleGround is ~1.4us on a 2020 desktop core; SampleHeight alone is
 * ~0.4us. Callers that only need height must use the cheap path. The subsystem
 * layers a small cache over this for the hot query pattern of "the ground under
 * the player, this frame".
 *
 * ------------------------------------------------------------------------
 * THREAD SAFETY
 * ------------------------------------------------------------------------
 * All methods are const and touch only the config. Copy the sampler freely, or
 * share one const instance across every thread. The only mutation is
 * SetDuneMigrationPhase, which must be called from the game thread while no
 * worker is sampling - the world subsystem enforces this with a fence.
 */
class DUSTWORLD_API FDustWorldSampler
{
public:
	FDustWorldSampler() = default;
	explicit FDustWorldSampler(const FDustWorldSamplerConfig& InConfig);

	/** Rebuilds internal derived state. Call after mutating the config. */
	void Rebuild(const FDustWorldSamplerConfig& InConfig);

	// -----------------------------------------------------------------------
	// Primary queries
	// -----------------------------------------------------------------------

	/**
	 * The full answer: height, normal, slope, surface, region and loose depth.
	 * Prefer the narrower queries below when you do not need all of it.
	 */
	FDustGroundSample SampleGround(const FVector2D& WorldXY) const;

	/** Height only. Roughly three times cheaper than SampleGround. */
	double SampleHeight(const FVector2D& WorldXY) const;

	/**
	 * Surface normal, computed by central differences at the given epsilon.
	 *
	 * @param Epsilon  Sampling offset in cm. Use a value comparable to the
	 *                 feature size you care about: 50 cm for footstep alignment,
	 *                 5000 cm for deciding whether a hillside is climbable.
	 */
	FVector SampleNormal(const FVector2D& WorldXY, double Epsilon = 100.0) const;

	/** Blend weights per region at this point. Sums to 1. */
	void SampleRegionWeights(const FVector2D& WorldXY, TArray<float>& OutWeights) const;

	/** Index of the strongest region, or INDEX_NONE if none has influence. */
	int32 SampleDominantRegion(const FVector2D& WorldXY, float& OutWeight) const;

	/**
	 * Signed density for subterranean space. Negative means open cave, positive
	 * means solid rock. Used to carve cave networks and to decide whether a
	 * surface point sits above a void that could collapse into a sinkhole.
	 */
	float SampleCaveDensity(const FVector& WorldLocation) const;

	/**
	 * Depth of loose windblown material above the substrate, in cm.
	 * Drives footprint depth, sinking, and whether a sandboard has anything to
	 * ride on.
	 */
	float SampleLooseDepth(const FVector2D& WorldXY) const;

	// -----------------------------------------------------------------------
	// Dune field
	// -----------------------------------------------------------------------

	/**
	 * The dune layer in isolation, in cm above the substrate.
	 *
	 * Exposed separately so the sand simulation can compute how much material
	 * moves between two migration phases without re-evaluating the whole stack.
	 */
	double SampleDuneHeight(const FVector2D& WorldXY, double MigrationPhase) const;

	/**
	 * Advances the dune migration phase. Game thread only, and only while no
	 * worker thread is inside a sample call.
	 */
	void SetDuneMigrationPhase(double Phase) { Config.DuneMigrationPhase = Phase; }

	double GetDuneMigrationPhase() const { return Config.DuneMigrationPhase; }

	/** Updates the prevailing wind used to orient dunes. Game thread only. */
	void SetPrevailingWind(const FVector2D& WindDirection);

	// -----------------------------------------------------------------------
	// Introspection
	// -----------------------------------------------------------------------

	const FDustWorldSamplerConfig& GetConfig() const { return Config; }

	int32 GetRegionCount() const { return Config.Regions.Num(); }

	const FDustRegionSnapshot* GetRegion(int32 Index) const
	{
		return Config.Regions.IsValidIndex(Index) ? &Config.Regions[Index] : nullptr;
	}

	/** Finds a region by id. Linear, but the region count is twelve. */
	const FDustRegionSnapshot* FindRegion(FName RegionId) const;

	/** True if the XY lies inside the playable bounds. */
	bool IsInsideWorld(const FVector2D& WorldXY) const;

private:
	// -----------------------------------------------------------------------
	// Layered terrain construction
	//
	// Height is built up in layers, each of which is independently meaningful:
	//
	//   continental  the shape of the landmass; basins, uplands, the great tilt
	//   + region     each region's characteristic shaping function, blended
	//   + dunes      migrating windblown material on top of the substrate
	//   + detail     high-frequency roughness for close-up silhouette
	//   + boundary   the rise into the encircling massif at the world edge
	// -----------------------------------------------------------------------

	/** Low-frequency landmass shape, shared by every region. */
	double SampleContinental(const FVector2D& WorldXY) const;

	/** One region's shaping function, without blending. */
	double SampleRegionProfile(const FDustRegionSnapshot& Region, const FVector2D& WorldXY) const;

	/** High-frequency roughness, scaled per region. */
	double SampleDetail(const FVector2D& WorldXY, const FDustTerrainParams& Params) const;

	/** The rise into the boundary massif. Returns 0 well inside the world. */
	double SampleBoundaryRise(const FVector2D& WorldXY) const;

	/** Warped distance used for region blending, so borders are not circles. */
	double GetWarpedRegionDistance(const FDustRegionSnapshot& Region, const FVector2D& WorldXY) const;

	/** Interpolates terrain params across the active regions at a point. */
	FDustTerrainParams BlendTerrainParams(const FVector2D& WorldXY, int32& OutDominantIndex, float& OutDominantWeight) const;

	// --- Profile implementations -------------------------------------------
	double ProfileDuneSea   (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileSaltFlat  (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileCanyon    (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileMesa      (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileMountains (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileBasin     (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileVolcanic  (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileDryForest (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileBuriedCity(const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileOasis     (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileStormBelt (const FDustRegionSnapshot& R, const FVector2D& P) const;
	double ProfileBadlands  (const FDustRegionSnapshot& R, const FVector2D& P) const;

private:
	FDustWorldSamplerConfig Config;

	/** Cached wind basis: X along wind, Y across. Rebuilt when wind changes. */
	FVector2D WindAxis = FVector2D(1.0, 0.0);
	FVector2D CrossWindAxis = FVector2D(0.0, 1.0);

	/** Derived seeds, hoisted out of the inner loops. */
	uint32 ContinentalSeed = 0;
	uint32 DuneSeed = 0;
	uint32 DetailSeed = 0;
	uint32 CaveSeed = 0;
	uint32 BoundarySeed = 0;
};
