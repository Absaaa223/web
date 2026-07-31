// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "DustTypes.h"
#include "World/DustWorldSampler.h"

/**
 * Deterministic point-of-interest generation.
 *
 * ------------------------------------------------------------------------
 * THE PROBLEM
 * ------------------------------------------------------------------------
 * A 100 km2 world needs thousands of things worth finding, but we cannot
 * generate them all up front (memory, load time) and we cannot generate them
 * lazily in a way that depends on where the player has been (the same ridge
 * must hold the same ruin whether you approach it from the north on your first
 * day or from the south on your fortieth hour).
 *
 * ------------------------------------------------------------------------
 * THE APPROACH
 * ------------------------------------------------------------------------
 * The world is divided into sectors. Each sector's contents are a pure
 * function of (master seed, sector coordinate). Generating a sector is cheap -
 * a few dozen candidate positions, each rejected or accepted by terrain rules -
 * so any query for "what is within 3 km of here" simply generates the
 * overlapping sectors on demand and discards the rest.
 *
 * Because generation is pure, the same sector generated on a worker thread
 * during streaming, in an editor tool, and in the offline world validator all
 * produce byte-identical results.
 *
 * ------------------------------------------------------------------------
 * PLACEMENT IS NOT RANDOM
 * ------------------------------------------------------------------------
 * A uniformly random scatter reads as noise, not as a world. Each category has
 * *placement rules* that consult the terrain: a landmark wants topographic
 * prominence, a cache wants concealment, an inscription wants a rock face, an
 * oasis ruin wants to be near water. The rules are what make a found object
 * feel like it was put there rather than sprinkled.
 */
class DUSTWORLD_API FDustPoiGenerator
{
public:
	FDustPoiGenerator() = default;
	explicit FDustPoiGenerator(const FDustWorldSampler* InSampler, uint32 InMasterSeed);

	/** Sector edge length in cm. 512 m keeps per-sector cost around 40us. */
	static constexpr double SectorSize = 51200.0;

	/**
	 * Returns every point of interest whose anchor lies within Radius of Centre.
	 *
	 * Cost scales with the number of overlapping sectors, so a 3 km query
	 * touches ~150 sectors and costs a few milliseconds. Callers doing this
	 * every frame should use the interest registry in DustNarrative, which
	 * caches the result and updates it incrementally as the player moves.
	 */
	void QueryRadius(const FVector2D& Centre, double Radius, TArray<FDustPointOfInterest>& OutPois) const;

	/** Generates the contents of a single sector. Pure and cacheable. */
	void GenerateSector(const FIntPoint& SectorCoord, TArray<FDustPointOfInterest>& OutPois) const;

	/** Converts a world XY to the sector that contains it. */
	static FIntPoint WorldToSector(const FVector2D& WorldXY)
	{
		return FIntPoint(
			FMath::FloorToInt32(WorldXY.X / SectorSize),
			FMath::FloorToInt32(WorldXY.Y / SectorSize));
	}

	/**
	 * Topographic prominence at a point: how far this location stands above the
	 * surrounding terrain, in cm, sampled over the given radius.
	 *
	 * Prominence is the single most useful placement heuristic in the game.
	 * It is why landmarks end up on ridges the player can actually see, and why
	 * caches end up in hollows they have to look for.
	 */
	static double ComputeProminence(const FDustWorldSampler& Sampler, const FVector2D& WorldXY, double Radius, int32 SampleCount = 12);

	/**
	 * Registers a hand-authored point of interest that must always exist.
	 *
	 * Hand-placed content takes priority over generated content: the generator
	 * suppresses anything it would otherwise place within the exclusion radius,
	 * so a designer can drop a temple down and trust that no procedural rubble
	 * pile will appear in its courtyard.
	 */
	void RegisterHandAuthored(const FDustPointOfInterest& Poi, double ExclusionRadius);

	/** Clears all hand-authored registrations. */
	void ClearHandAuthored();

private:
	/** Terrain constraints a category imposes on its placement. */
	struct FPlacementRules
	{
		float MinSlopeDegrees = 0.0f;
		float MaxSlopeDegrees = 90.0f;

		/** Minimum prominence in cm. Landmarks demand a lot; caches want none. */
		double MinProminence = -TNumericLimits<double>::Max();
		double MaxProminence = TNumericLimits<double>::Max();

		/** Surfaces this category may be placed on. Empty means any. */
		TArray<EDustSurface> AllowedSurfaces;

		/** How far this reads as interesting, in cm. */
		double SilhouetteRadius = 5000.0;

		/** Base authored significance, 0..1. */
		float Significance = 0.5f;

		/** Radius over which prominence is evaluated. */
		double ProminenceRadius = 40000.0;

		/** Must sit within this distance of standing water. 0 = no requirement. */
		double MaxWaterDistance = 0.0;
	};

	/** Returns the placement rules for a discovery category tag. */
	static FPlacementRules GetRulesForCategory(const FGameplayTag& Category);

	/** Tests a candidate position against a category's rules. */
	bool PassesRules(const FVector2D& Candidate, const FPlacementRules& Rules) const;

	/** Picks a category for a sector slot from the region's weighted mix. */
	FGameplayTag PickCategory(const FDustRegionSnapshot& Region, uint32 SlotSeed) const;

	/** True if a hand-authored POI suppresses generation at this point. */
	bool IsSuppressed(const FVector2D& Candidate) const;

	/** Derives a stable GUID from position and category. */
	static FGuid MakeStableId(const FVector2D& Location, const FGameplayTag& Category, uint32 Seed);

private:
	const FDustWorldSampler* Sampler = nullptr;
	uint32 MasterSeed = 0;

	/** Seed for the POI field, independent of terrain. */
	uint32 PoiSeed = 0;

	struct FHandAuthoredEntry
	{
		FDustPointOfInterest Poi;
		double ExclusionRadius = 0.0;
	};

	TArray<FHandAuthoredEntry> HandAuthored;
};
