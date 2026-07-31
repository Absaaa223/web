// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "World/DustWorldSampler.h"

#include "DustLog.h"
#include "Math/DustNoise.h"

namespace DustSampler
{
	/**
	 * Upper bound on regions. A fixed cap lets every hot path use stack arrays
	 * instead of allocating, which matters when the sampler is called a hundred
	 * thousand times in a landscape bake.
	 */
	static constexpr int32 MaxRegions = 32;

	/**
	 * Converts a world position (cm, potentially in the millions) into noise
	 * space at the given feature scale.
	 *
	 * The division happens in double precision *before* the cast to float.
	 * Casting first would throw away centimetres at the world edge and produce
	 * visible quantisation banding 5 km from the origin - a bug that only shows
	 * up at the far corners of the map and is miserable to track down.
	 */
	FORCEINLINE FVector2f ToNoiseSpace(const FVector2D& WorldXY, double Scale)
	{
		checkSlow(Scale > 0.0);
		return FVector2f(
			static_cast<float>(WorldXY.X / Scale),
			static_cast<float>(WorldXY.Y / Scale));
	}

	/**
	 * The cross-section of a wind-formed dune, normalised to [0,1] over one
	 * wavelength and to [0,1] in height.
	 *
	 * Real dunes are strongly asymmetric: a long shallow windward face where
	 * grains are pushed up by saltation, then an abrupt brink and a straight
	 * slip face sitting exactly at the angle of repose (~34 degrees for dry
	 * sand). Symmetric sine dunes are the single most common tell of a
	 * procedural desert, so the profile is asymmetric by construction rather
	 * than by adding noise to a sine wave.
	 *
	 * @param T                 Position through the wavelength, 0..1, downwind.
	 * @param WindwardFraction  Portion of the wavelength on the windward side.
	 */
	FORCEINLINE float DuneProfile(float T, float WindwardFraction)
	{
		T = FMath::Frac(T);

		if (T < WindwardFraction)
		{
			// Windward: smooth accelerating rise, C2 continuous into the brink.
			return FDustNoise::Quintic(T / FMath::Max(WindwardFraction, KINDA_SMALL_NUMBER));
		}

		const float S = (T - WindwardFraction) / FMath::Max(1.0f - WindwardFraction, KINDA_SMALL_NUMBER);

		// Slip face: essentially straight, with the brink rounded over the top
		// eighth so the crest silhouette does not read as a knife edge at range.
		const float Brink = 1.0f - 0.09f * FDustNoise::Quintic(FMath::Min(S / 0.125f, 1.0f));
		return Brink * (1.0f - S);
	}

	/** Angle of repose for dry sand. Slip faces are clamped to this. */
	static constexpr float AngleOfReposeDegrees = 34.0f;
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

FDustWorldSamplerConfig FDustWorldSamplerConfig::FromComposition(
	const UDustWorldComposition& Composition, uint32 InMasterSeed)
{
	FDustWorldSamplerConfig Result;
	Result.MasterSeed = InMasterSeed;
	Result.FallbackTerrain = Composition.FallbackTerrain;
	Result.Regions.Reserve(Composition.Regions.Num());

	int32 Index = 0;
	for (const TObjectPtr<UDustRegionData>& RegionAsset : Composition.Regions)
	{
		if (!RegionAsset)
		{
			UE_LOG(LogDustWorld, Warning, TEXT("Null region at composition index %d; skipped."), Index);
			continue;
		}

		if (Index >= DustSampler::MaxRegions)
		{
			UE_LOG(LogDustWorld, Error,
				TEXT("World composition exceeds the %d region cap; '%s' and beyond ignored."),
				DustSampler::MaxRegions, *RegionAsset->RegionId.ToString());
			break;
		}

		FDustRegionSnapshot Snapshot;
		Snapshot.RegionId       = RegionAsset->RegionId;
		Snapshot.Index          = Index;
		Snapshot.Anchor         = RegionAsset->AnchorLocation;
		Snapshot.CoreRadius     = RegionAsset->CoreRadius;
		Snapshot.FalloffRadius  = RegionAsset->FalloffRadius;
		Snapshot.BoundaryWarp   = RegionAsset->BoundaryWarp;
		Snapshot.Priority       = RegionAsset->Priority;
		Snapshot.Profile        = RegionAsset->Profile;
		Snapshot.Terrain        = RegionAsset->TerrainParams;
		Snapshot.WindMultiplier = RegionAsset->WindMultiplier;

		// Each region gets an independent noise field derived from its stable
		// id, so reordering the composition array cannot reshape the terrain.
		Snapshot.Seed = FDustNoise::DeriveSeed(InMasterSeed, *RegionAsset->RegionId.ToString());

		Result.Regions.Add(MoveTemp(Snapshot));
		++Index;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

FDustWorldSampler::FDustWorldSampler(const FDustWorldSamplerConfig& InConfig)
{
	Rebuild(InConfig);
}

void FDustWorldSampler::Rebuild(const FDustWorldSamplerConfig& InConfig)
{
	Config = InConfig;

	ContinentalSeed = FDustNoise::DeriveSeed(Config.MasterSeed, TEXT("Continental"));
	DuneSeed        = FDustNoise::DeriveSeed(Config.MasterSeed, TEXT("Dunes"));
	DetailSeed      = FDustNoise::DeriveSeed(Config.MasterSeed, TEXT("Detail"));
	CaveSeed        = FDustNoise::DeriveSeed(Config.MasterSeed, TEXT("Caves"));
	BoundarySeed    = FDustNoise::DeriveSeed(Config.MasterSeed, TEXT("Boundary"));

	SetPrevailingWind(Config.PrevailingWind);

	UE_LOG(LogDustWorld, Log,
		TEXT("World sampler built: seed %u, %d regions, %.1f km half-extent, terrain algorithm v%d."),
		Config.MasterSeed, Config.Regions.Num(),
		Config.WorldHalfExtent / 100000.0, FDustNoise::AlgorithmVersion);
}

void FDustWorldSampler::SetPrevailingWind(const FVector2D& WindDirection)
{
	FVector2D Normalised = WindDirection;
	if (!Normalised.Normalize())
	{
		Normalised = FVector2D(1.0, 0.0);
	}

	Config.PrevailingWind = Normalised;
	WindAxis = Normalised;
	CrossWindAxis = FVector2D(-Normalised.Y, Normalised.X);
}

const FDustRegionSnapshot* FDustWorldSampler::FindRegion(FName RegionId) const
{
	return Config.Regions.FindByPredicate(
		[RegionId](const FDustRegionSnapshot& R) { return R.RegionId == RegionId; });
}

bool FDustWorldSampler::IsInsideWorld(const FVector2D& WorldXY) const
{
	return FMath::Abs(WorldXY.X) <= Config.WorldHalfExtent
		&& FMath::Abs(WorldXY.Y) <= Config.WorldHalfExtent;
}

// ---------------------------------------------------------------------------
// Region blending
// ---------------------------------------------------------------------------

double FDustWorldSampler::GetWarpedRegionDistance(const FDustRegionSnapshot& Region, const FVector2D& WorldXY) const
{
	const double RawDistance = FVector2D::Distance(WorldXY, Region.Anchor);

	if (Region.BoundaryWarp <= 0.0)
	{
		return RawDistance;
	}

	// Displace the boundary by low-frequency noise. Without this every region
	// is a visible circle from the air, which is the difference between "a
	// generated map" and "a place".
	//
	// The warp is sampled in world space rather than in region-local space so
	// that neighbouring regions warp *coherently* along their shared border -
	// otherwise the two sides of a boundary wobble independently and produce
	// thin slivers where neither region has influence.
	FDustNoise::FFractalParams WarpParams;
	WarpParams.Octaves    = 3;
	WarpParams.Frequency  = 1.0f;
	WarpParams.Lacunarity = 2.07f;
	WarpParams.Gain       = 0.55f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(WorldXY, Region.BoundaryWarp * 4.0);
	const float Warp = FDustNoise::FBM2D(NoiseP, ContinentalSeed, WarpParams);

	return RawDistance + static_cast<double>(Warp) * Region.BoundaryWarp;
}

void FDustWorldSampler::SampleRegionWeights(const FVector2D& WorldXY, TArray<float>& OutWeights) const
{
	const int32 Count = Config.Regions.Num();
	OutWeights.SetNumZeroed(Count, EAllowShrinking::No);

	float Total = 0.0f;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDustRegionSnapshot& Region = Config.Regions[Index];
		const double Distance = GetWarpedRegionDistance(Region, WorldXY);

		if (Distance >= Region.CoreRadius + Region.FalloffRadius)
		{
			continue;
		}

		// Smooth falloff from full strength at CoreRadius to zero at the edge.
		double Weight = 1.0;
		if (Distance > Region.CoreRadius)
		{
			const double T = (Distance - Region.CoreRadius) / FMath::Max(Region.FalloffRadius, 1.0);
			Weight = 1.0 - FDustNoise::Quintic(static_cast<float>(FMath::Clamp(T, 0.0, 1.0)));
		}

		// Priority acts as a doubling per level. A priority-3 region overwhelms
		// a priority-0 neighbour in the overlap but still blends at the very
		// edge, so the Buried City cuts through the dune sea without a seam.
		Weight *= FMath::Pow(2.0, static_cast<double>(Region.Priority));

		OutWeights[Index] = static_cast<float>(Weight);
		Total += OutWeights[Index];
	}

	if (Total > KINDA_SMALL_NUMBER)
	{
		const float InvTotal = 1.0f / Total;
		for (float& Weight : OutWeights)
		{
			Weight *= InvTotal;
		}
	}
}

int32 FDustWorldSampler::SampleDominantRegion(const FVector2D& WorldXY, float& OutWeight) const
{
	// Stack array: this runs inside the height path and must not allocate.
	float Weights[DustSampler::MaxRegions] = {};
	const int32 Count = FMath::Min(Config.Regions.Num(), DustSampler::MaxRegions);

	float Total = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDustRegionSnapshot& Region = Config.Regions[Index];
		const double Distance = GetWarpedRegionDistance(Region, WorldXY);

		if (Distance >= Region.CoreRadius + Region.FalloffRadius)
		{
			continue;
		}

		double Weight = 1.0;
		if (Distance > Region.CoreRadius)
		{
			const double T = (Distance - Region.CoreRadius) / FMath::Max(Region.FalloffRadius, 1.0);
			Weight = 1.0 - FDustNoise::Quintic(static_cast<float>(FMath::Clamp(T, 0.0, 1.0)));
		}
		Weight *= FMath::Pow(2.0, static_cast<double>(Region.Priority));

		Weights[Index] = static_cast<float>(Weight);
		Total += Weights[Index];
	}

	if (Total <= KINDA_SMALL_NUMBER)
	{
		OutWeight = 0.0f;
		return INDEX_NONE;
	}

	int32 Best = INDEX_NONE;
	float BestWeight = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Weights[Index] > BestWeight)
		{
			BestWeight = Weights[Index];
			Best = Index;
		}
	}

	OutWeight = BestWeight / Total;
	return Best;
}

FDustTerrainParams FDustWorldSampler::BlendTerrainParams(
	const FVector2D& WorldXY, int32& OutDominantIndex, float& OutDominantWeight) const
{
	float Weights[DustSampler::MaxRegions] = {};
	const int32 Count = FMath::Min(Config.Regions.Num(), DustSampler::MaxRegions);

	float Total = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDustRegionSnapshot& Region = Config.Regions[Index];
		const double Distance = GetWarpedRegionDistance(Region, WorldXY);

		if (Distance >= Region.CoreRadius + Region.FalloffRadius)
		{
			continue;
		}

		double Weight = 1.0;
		if (Distance > Region.CoreRadius)
		{
			const double T = (Distance - Region.CoreRadius) / FMath::Max(Region.FalloffRadius, 1.0);
			Weight = 1.0 - FDustNoise::Quintic(static_cast<float>(FMath::Clamp(T, 0.0, 1.0)));
		}
		Weight *= FMath::Pow(2.0, static_cast<double>(Region.Priority));

		Weights[Index] = static_cast<float>(Weight);
		Total += Weights[Index];
	}

	OutDominantIndex = INDEX_NONE;
	OutDominantWeight = 0.0f;

	if (Total <= KINDA_SMALL_NUMBER)
	{
		return Config.FallbackTerrain;
	}

	FDustTerrainParams Blended;
	Blended.BaseElevation = 0.0;
	Blended.Amplitude     = 0.0;
	Blended.FeatureScale  = 0.0;
	Blended.WarpStrength  = 0.0f;
	Blended.LooseDepth    = 0.0f;
	Blended.DuneInfluence = 0.0f;
	Blended.SteepSurfaceAngle = 0.0f;

	double WaterAccum = 0.0;
	double WaterWeight = 0.0;
	float BestWeight = 0.0f;

	const float InvTotal = 1.0f / Total;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float W = Weights[Index] * InvTotal;
		if (W <= 0.0f)
		{
			continue;
		}

		const FDustTerrainParams& P = Config.Regions[Index].Terrain;

		Blended.BaseElevation     += P.BaseElevation * W;
		Blended.Amplitude         += P.Amplitude * W;
		Blended.FeatureScale      += P.FeatureScale * W;
		Blended.WarpStrength      += P.WarpStrength * W;
		Blended.LooseDepth        += P.LooseDepth * W;
		Blended.DuneInfluence     += P.DuneInfluence * W;
		Blended.SteepSurfaceAngle += P.SteepSurfaceAngle * W;

		// Water does not average sensibly across a region that has none, so we
		// only blend among regions that actually carry a water table.
		if (P.WaterLevel > -1.0e8)
		{
			WaterAccum += P.WaterLevel * W;
			WaterWeight += W;
		}

		if (W > BestWeight)
		{
			BestWeight = W;
			OutDominantIndex = Index;
		}
	}

	// Discrete fields take the dominant region's value rather than a blend:
	// there is no meaningful average of "salt crust" and "volcanic scoria".
	if (Config.Regions.IsValidIndex(OutDominantIndex))
	{
		const FDustTerrainParams& Dominant = Config.Regions[OutDominantIndex].Terrain;
		Blended.PrimarySurface = Dominant.PrimarySurface;
		Blended.SteepSurface   = Dominant.SteepSurface;
		Blended.DetailOctaves  = Dominant.DetailOctaves;
	}

	// A water table only exists if it dominates locally; a 20% oasis influence
	// bleeding a lake into the surrounding dunes would look absurd.
	Blended.WaterLevel = (WaterWeight > 0.5) ? (WaterAccum / WaterWeight) : -1.0e9;

	OutDominantWeight = BestWeight;
	return Blended;
}

// ---------------------------------------------------------------------------
// Terrain layers
// ---------------------------------------------------------------------------

double FDustWorldSampler::SampleContinental(const FVector2D& WorldXY) const
{
	// The landmass shape, shared by every region. Very low frequency: one
	// wavelength spans several kilometres, producing the broad tilt that makes
	// the world feel like it drains somewhere rather than being a flat plate.
	FDustNoise::FFractalParams Params;
	Params.Octaves    = 4;
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.13f;
	Params.Gain       = 0.5f;

	constexpr double ContinentalScale = 1800000.0; // 18 km
	const FVector2f P = DustSampler::ToNoiseSpace(WorldXY, ContinentalScale);

	const float Shape = FDustNoise::WarpedFBM2D(P, ContinentalSeed, Params, 0.45f);

	// Amplitude chosen so that crossing the world is a 900 m elevation journey:
	// enough for weather and temperature to change meaningfully, not so much
	// that the player is climbing constantly.
	return static_cast<double>(Shape) * 45000.0;
}

double FDustWorldSampler::SampleDetail(const FVector2D& WorldXY, const FDustTerrainParams& Params) const
{
	FDustNoise::FFractalParams Fractal;
	Fractal.Octaves    = FMath::Clamp(Params.DetailOctaves, 1, 9);
	Fractal.Frequency  = 1.0f;
	Fractal.Lacunarity = 2.03f;
	Fractal.Gain       = 0.48f;

	// Detail rides at a fixed absolute scale rather than a fraction of the
	// region's feature scale, so close-up roughness is consistent everywhere
	// the player's feet actually are.
	constexpr double DetailScale = 2400.0;
	const FVector2f P = DustSampler::ToNoiseSpace(WorldXY, DetailScale);

	const float Rough = FDustNoise::FBM2D(P, DetailSeed, Fractal);

	return static_cast<double>(Rough) * 140.0;
}

double FDustWorldSampler::SampleBoundaryRise(const FVector2D& WorldXY) const
{
	// Distance outside the playable square, using a Chebyshev metric so the
	// massif forms a rectangular rim rather than a circle inscribed in it.
	const double OutsideX = FMath::Abs(WorldXY.X) - Config.WorldHalfExtent;
	const double OutsideY = FMath::Abs(WorldXY.Y) - Config.WorldHalfExtent;
	const double Outside = FMath::Max(OutsideX, OutsideY);

	// The rise begins slightly *inside* the boundary so the player sees
	// mountains ahead of them well before they reach the edge, and reads them
	// as geography rather than as a wall.
	constexpr double PreRise = 250000.0;
	if (Outside < -PreRise)
	{
		return 0.0;
	}

	const double T = FMath::Clamp((Outside + PreRise) / (Config.BoundaryFalloff + PreRise), 0.0, 1.0);
	const double Ramp = FDustNoise::Quintic(static_cast<float>(T));

	// Break the rim up with ridged noise so it reads as a genuine mountain
	// chain. There is no invisible wall in DUST; a determined player can climb
	// this, and the design rewards them with the observation ridge and the
	// long view back across everything they have crossed.
	FDustNoise::FFractalParams Ridge;
	Ridge.Octaves    = 5;
	Ridge.Frequency  = 1.0f;
	Ridge.Lacunarity = 2.11f;
	Ridge.Gain       = 0.52f;

	const FVector2f P = DustSampler::ToNoiseSpace(WorldXY, 220000.0);
	const float RidgeValue = FDustNoise::Ridged2D(P, BoundarySeed, Ridge);

	constexpr double MaxRimHeight = 320000.0; // 3.2 km
	return Ramp * MaxRimHeight * (0.55 + 0.45 * static_cast<double>(RidgeValue));
}

// ---------------------------------------------------------------------------
// Dunes
// ---------------------------------------------------------------------------

double FDustWorldSampler::SampleDuneHeight(const FVector2D& WorldXY, double MigrationPhase) const
{
	// Work in wind-aligned coordinates: U runs downwind, V runs along the crest.
	const double U = FVector2D::DotProduct(WorldXY, WindAxis) + MigrationPhase;
	const double V = FVector2D::DotProduct(WorldXY, CrossWindAxis);

	// --- Primary dune train -------------------------------------------------
	// Wavelength varies slowly across the field, so the dune sea has regions of
	// tight ripples and regions of enormous slow draa, as real ergs do.
	FDustNoise::FFractalParams Envelope;
	Envelope.Octaves   = 3;
	Envelope.Frequency = 1.0f;

	const FVector2f EnvelopeP = DustSampler::ToNoiseSpace(WorldXY, 900000.0);
	const float WavelengthMod = FDustNoise::ToUnitRange(
		FDustNoise::FBM2D(EnvelopeP, DuneSeed ^ 0x11u, Envelope));

	const double Wavelength = FMath::Lerp(28000.0, 105000.0, static_cast<double>(WavelengthMod));

	// Crest sinuosity: displace the phase along the crest direction so dunes
	// snake rather than running as straight parallel bars.
	FDustNoise::FFractalParams Sinuosity;
	Sinuosity.Octaves   = 3;
	Sinuosity.Frequency = 1.0f;

	const FVector2f SinuosityP(
		static_cast<float>(V / 260000.0),
		static_cast<float>(U / 1400000.0));
	const float CrestOffset = FDustNoise::FBM2D(SinuosityP, DuneSeed ^ 0x22u, Sinuosity);

	const double PhaseU = (U + static_cast<double>(CrestOffset) * Wavelength * 0.85) / Wavelength;

	// Windward fraction varies a little so no two dunes are identical.
	const float WindwardFraction = FMath::Lerp(0.66f, 0.80f,
		FDustNoise::ToUnitRange(FDustNoise::Simplex2D(
			FVector2f(static_cast<float>(U / 400000.0), static_cast<float>(V / 400000.0)),
			DuneSeed ^ 0x33u)));

	const float Profile = DustSampler::DuneProfile(
		static_cast<float>(FMath::Frac(PhaseU)), WindwardFraction);

	// --- Field envelope -----------------------------------------------------
	// Not every square metre of an erg is dune. Interdune corridors - flat,
	// gravel-floored, and often where anything interesting has been left
	// exposed - are as important to the experience as the dunes themselves.
	FDustNoise::FFractalParams Coverage;
	Coverage.Octaves   = 4;
	Coverage.Frequency = 1.0f;

	const FVector2f CoverageP = DustSampler::ToNoiseSpace(WorldXY, 340000.0);
	const float CoverageRaw = FDustNoise::ToUnitRange(
		FDustNoise::FBM2D(CoverageP, DuneSeed ^ 0x44u, Coverage));

	// Remap so roughly a fifth of the field is corridor.
	const float Coverage01 = FMath::SmoothStep(0.28f, 0.62f, CoverageRaw);

	// --- Amplitude ----------------------------------------------------------
	FDustNoise::FFractalParams HeightVariation;
	HeightVariation.Octaves   = 3;
	HeightVariation.Frequency = 1.0f;

	const FVector2f HeightP = DustSampler::ToNoiseSpace(WorldXY, 520000.0);
	const float HeightMod = FDustNoise::ToUnitRange(
		FDustNoise::FBM2D(HeightP, DuneSeed ^ 0x55u, HeightVariation));

	// Dune height is physically tied to wavelength: a 1 km draa is tall, a 300 m
	// transverse dune is not. Keeping the ratio near 1:12 is what makes the
	// slip faces land near the angle of repose without having to clamp them.
	const double MaxHeight = Wavelength * FMath::Lerp(0.055, 0.105, static_cast<double>(HeightMod));

	// --- Superimposed ripples ----------------------------------------------
	// Metre-scale ripples on the windward face only. These are what sell the
	// surface at walking distance and they cost one noise sample.
	const double RippleWavelength = 220.0;
	const float RipplePhase = static_cast<float>(FMath::Frac(U / RippleWavelength));
	const float Ripple = DustSampler::DuneProfile(RipplePhase, 0.72f);

	// Ripples fade out on the slip face, where the sand is avalanching.
	const float WindwardMask = (FMath::Frac(PhaseU) < WindwardFraction) ? 1.0f : 0.15f;

	const double RippleHeight = static_cast<double>(Ripple * WindwardMask) * 9.0;

	return (static_cast<double>(Profile) * MaxHeight + RippleHeight) * static_cast<double>(Coverage01);
}

// ---------------------------------------------------------------------------
// Region profiles
// ---------------------------------------------------------------------------

double FDustWorldSampler::ProfileDuneSea(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// The substrate under a dune sea is a gently undulating sand sheet; the
	// dunes themselves are added later by the shared dune layer so that they
	// migrate coherently across region boundaries.
	FDustNoise::FFractalParams Params;
	Params.Octaves    = 4;
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.05f;
	Params.Gain       = 0.5f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 6.0);
	const float Sheet = FDustNoise::FBM2D(NoiseP, R.Seed, Params);

	return R.Terrain.BaseElevation + static_cast<double>(Sheet) * R.Terrain.Amplitude * 0.25;
}

double FDustWorldSampler::ProfileSaltFlat(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Near-perfectly level - an evaporite pan is the flattest natural surface
	// on Earth. The interest is entirely in the polygonal crust, which we build
	// from cellular edges at two scales and express as a few centimetres of
	// relief. Walking across it should feel unnervingly smooth.
	const FVector2f LargeP = DustSampler::ToNoiseSpace(P, 4200.0);
	const FVector2f SmallP = DustSampler::ToNoiseSpace(P, 900.0);

	const float LargePolygons = FDustNoise::CellularEdges2D(LargeP, R.Seed, 0.10f);
	const float SmallPolygons = FDustNoise::CellularEdges2D(SmallP, R.Seed ^ 0x9Eu, 0.16f);

	// Ridges push *up* at the polygon borders: as brine evaporates the crust
	// buckles upward along the cracks.
	const double CrustRelief =
		static_cast<double>(LargePolygons) * 26.0 +
		static_cast<double>(SmallPolygons) * 7.0;

	// A whisper of long-wavelength undulation so the horizon is not a razor.
	FDustNoise::FFractalParams Broad;
	Broad.Octaves   = 2;
	Broad.Frequency = 1.0f;

	const FVector2f BroadP = DustSampler::ToNoiseSpace(P, 700000.0);
	const float Undulation = FDustNoise::FBM2D(BroadP, R.Seed ^ 0x1Cu, Broad);

	return R.Terrain.BaseElevation + CrustRelief + static_cast<double>(Undulation) * 400.0;
}

double FDustWorldSampler::ProfileCanyon(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Canyons are cut, not built. We start from a plateau and subtract a
	// drainage network derived from cellular edge distance - the branching
	// structure of a Voronoi diagram's edges is a remarkably good stand-in for
	// a dendritic drainage pattern, and it is two orders of magnitude cheaper
	// than running an actual hydraulic erosion simulation at this scale.
	FDustNoise::FFractalParams Plateau;
	Plateau.Octaves   = 3;
	Plateau.Frequency = 1.0f;

	const FVector2f PlateauP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 4.0);
	const float PlateauNoise = FDustNoise::FBM2D(PlateauP, R.Seed, Plateau);

	const double PlateauHeight = R.Terrain.BaseElevation
		+ static_cast<double>(PlateauNoise) * R.Terrain.Amplitude * 0.3;

	// Warp the channel network so it meanders instead of running in straight
	// Voronoi segments.
	FDustNoise::FFractalParams Warp;
	Warp.Octaves   = 3;
	Warp.Frequency = 1.0f;

	const FVector2f WarpP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 2.0);
	const float WarpX = FDustNoise::FBM2D(WarpP, R.Seed ^ 0xA1u, Warp);
	const float WarpY = FDustNoise::FBM2D(WarpP + FVector2f(37.0f, 91.0f), R.Seed ^ 0xA1u, Warp);

	const FVector2D Warped = P + FVector2D(WarpX, WarpY) * (R.Terrain.FeatureScale * R.Terrain.WarpStrength);

	const FVector2f ChannelP = DustSampler::ToNoiseSpace(Warped, R.Terrain.FeatureScale);
	const FDustNoise::FCellularResult Cell = FDustNoise::Cellular2D(ChannelP, R.Seed ^ 0xB2u);

	// Distance from the channel centreline, normalised.
	const float ChannelProximity = 1.0f - FMath::SmoothStep(0.0f, 0.34f, Cell.EdgeDistance());

	// Stepped walls: sedimentary benches every ~40 m of depth. This single
	// detail is what makes a canyon read as rock rather than as a smooth gouge.
	const double RawDepth = static_cast<double>(ChannelProximity) * R.Terrain.Amplitude;
	const double BenchHeight = 4000.0;
	const double Stepped = FMath::Lerp(
		RawDepth,
		FMath::FloorToDouble(RawDepth / BenchHeight) * BenchHeight,
		0.55);

	return PlateauHeight - Stepped;
}

double FDustWorldSampler::ProfileMesa(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Isolated flat-topped remnants: take a cellular field, keep the cells
	// whose feature point passes a threshold, and give each a flat cap with a
	// steep cliff and a debris apron at its foot.
	const FVector2f CellP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);
	const FDustNoise::FCellularResult Cell = FDustNoise::Cellular2D(CellP, R.Seed);

	const uint32 CellHash = FDustNoise::Hash2(Cell.Cell.X, Cell.Cell.Y, R.Seed);
	const float CellRoll = FDustNoise::HashToUnit(CellHash);

	const double Base = R.Terrain.BaseElevation;

	// Only about 45% of cells host a mesa; the rest is open pediment.
	if (CellRoll > 0.45f)
	{
		return Base;
	}

	// Per-mesa height and radius, stable from the cell hash.
	const float HeightRoll = FDustNoise::HashToUnit(FDustNoise::Hash(CellHash ^ 0x5Au));
	const float RadiusRoll = FDustNoise::HashToUnit(FDustNoise::Hash(CellHash ^ 0xC3u));

	const double MesaHeight = R.Terrain.Amplitude * FMath::Lerp(0.45, 1.0, static_cast<double>(HeightRoll));
	const float CapRadius   = FMath::Lerp(0.18f, 0.34f, RadiusRoll);
	const float ApronRadius = CapRadius + 0.16f;

	// Distance from the mesa centre in cell units, warped so the plan outline
	// is irregular rather than round.
	const FVector2f ToCentre = CellP - Cell.FeaturePoint;
	const float Angle = FMath::Atan2(ToCentre.Y, ToCentre.X);
	const float OutlineWarp = FDustNoise::Simplex2D(
		FVector2f(FMath::Cos(Angle) * 2.0f, FMath::Sin(Angle) * 2.0f), CellHash) * 0.05f;

	const float Distance = ToCentre.Size() + OutlineWarp;

	if (Distance < CapRadius)
	{
		// Flat cap with a hard caprock lip.
		return Base + MesaHeight;
	}
	if (Distance < ApronRadius)
	{
		// Cliff and talus: steep at the top, easing into the apron.
		const float T = (Distance - CapRadius) / (ApronRadius - CapRadius);
		const float Falloff = 1.0f - FMath::Pow(T, 0.42f);
		return Base + MesaHeight * static_cast<double>(FMath::Max(Falloff, 0.0f));
	}

	return Base;
}

double FDustWorldSampler::ProfileMountains(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	FDustNoise::FFractalParams Params;
	Params.Octaves    = FMath::Clamp(R.Terrain.DetailOctaves + 2, 4, 9);
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.17f;
	Params.Gain       = 0.52f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);

	// Ridged multifractal gives sharp arêtes; warping it gives them the
	// characteristic curved sweep of a real range rather than radial spokes.
	FDustNoise::FFractalParams WarpParams;
	WarpParams.Octaves   = 3;
	WarpParams.Frequency = 1.0f;

	const float WarpX = FDustNoise::FBM2D(NoiseP * 0.35f, R.Seed ^ 0xD1u, WarpParams);
	const float WarpY = FDustNoise::FBM2D(NoiseP * 0.35f + FVector2f(19.0f, 73.0f), R.Seed ^ 0xD1u, WarpParams);

	const FVector2f Warped = NoiseP + FVector2f(WarpX, WarpY) * R.Terrain.WarpStrength;

	const float Ridges = FDustNoise::Ridged2D(Warped, R.Seed, Params);

	// Raising the ridge value to a power > 1 pushes the mass toward the peaks
	// and leaves broad valleys, which is both more plausible and more
	// traversable than a uniformly rough massif.
	const double Height = FMath::Pow(static_cast<double>(Ridges), 1.6) * R.Terrain.Amplitude;

	return R.Terrain.BaseElevation + Height;
}

double FDustWorldSampler::ProfileBasin(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// A broad bowl. Depth follows a radial falloff from the region anchor so
	// the basin genuinely drains toward one point - which is where the fog
	// pools at dawn and where anything that ever washed downhill has ended up.
	const double Distance = FVector2D::Distance(P, R.Anchor);
	const double Normalised = FMath::Clamp(Distance / FMath::Max(R.CoreRadius, 1.0), 0.0, 1.0);

	const double Bowl = -R.Terrain.Amplitude * (1.0 - FDustNoise::Quintic(static_cast<float>(Normalised)));

	FDustNoise::FFractalParams Params;
	Params.Octaves   = 4;
	Params.Frequency = 1.0f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);
	const float Texture = FDustNoise::FBM2D(NoiseP, R.Seed, Params);

	return R.Terrain.BaseElevation + Bowl + static_cast<double>(Texture) * R.Terrain.Amplitude * 0.12;
}

double FDustWorldSampler::ProfileVolcanic(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Two components: fractured plate topography, and cinder cones.
	const FVector2f PlateP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 0.5);
	const float Fractures = FDustNoise::CellularEdges2D(PlateP, R.Seed, 0.07f);

	FDustNoise::FFractalParams Params;
	Params.Octaves   = 4;
	Params.Frequency = 1.0f;

	const FVector2f FieldP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 2.0);
	const float Field = FDustNoise::FBM2D(FieldP, R.Seed ^ 0x77u, Params);

	// Pressure ridges push up along the fracture lines.
	double Height = R.Terrain.BaseElevation
		+ static_cast<double>(Field) * R.Terrain.Amplitude * 0.35
		+ static_cast<double>(Fractures) * 900.0;

	// Cinder cones: sparse, conical, with a crater at the summit.
	const FVector2f ConeP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale * 3.0);
	const FDustNoise::FCellularResult ConeCell = FDustNoise::Cellular2D(ConeP, R.Seed ^ 0x8Bu);

	const uint32 ConeHash = FDustNoise::Hash2(ConeCell.Cell.X, ConeCell.Cell.Y, R.Seed ^ 0x8Bu);
	if (FDustNoise::HashToUnit(ConeHash) < 0.22f)
	{
		const float ConeRadius = 0.30f;
		if (ConeCell.F1 < ConeRadius)
		{
			const float T = ConeCell.F1 / ConeRadius;
			const double ConeHeight = R.Terrain.Amplitude
				* FMath::Lerp(0.6, 1.4, static_cast<double>(FDustNoise::HashToUnit(ConeHash ^ 0x3Fu)));

			// Straight-sided cone at the angle of repose for scoria (~33 deg),
			// with a crater punched into the top 22%.
			double Cone = ConeHeight * static_cast<double>(1.0f - T);

			if (T < 0.22f)
			{
				const float CraterT = T / 0.22f;
				Cone -= ConeHeight * 0.30 * static_cast<double>(1.0f - CraterT * CraterT);
			}

			Height += Cone;
		}
	}

	return Height;
}

double FDustWorldSampler::ProfileDryForest(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Stabilised sand sheets: low, rounded, vegetated hummocks. Gentle enough
	// to be pleasant to walk after the dune sea, which is the point of putting
	// this region where it is.
	FDustNoise::FFractalParams Params;
	Params.Octaves    = FMath::Clamp(R.Terrain.DetailOctaves, 3, 7);
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 1.97f;
	Params.Gain       = 0.45f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);
	const float Hummocks = FDustNoise::Billow2D(NoiseP, R.Seed, Params);

	return R.Terrain.BaseElevation + static_cast<double>(Hummocks) * R.Terrain.Amplitude * 0.4;
}

double FDustWorldSampler::ProfileBuriedCity(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// The terrain follows the buried street grid: blocks stand slightly proud
	// where rubble accumulated over structures, streets sit lower where the
	// wind has scoured them out. The player reads a city plan from the ground
	// itself long before they find a standing wall - which is the intended
	// moment of realisation for this region.
	//
	// The grid is rotated by a per-region angle so it is not axis-aligned with
	// the world, and warped slightly so it has settled unevenly.
	const float GridAngle = FDustNoise::HashToUnit(R.Seed) * PI * 0.5f;
	const float CosA = FMath::Cos(GridAngle);
	const float SinA = FMath::Sin(GridAngle);

	const FVector2D Local = P - R.Anchor;
	const FVector2D Rotated(
		Local.X * CosA - Local.Y * SinA,
		Local.X * SinA + Local.Y * CosA);

	// Settling warp.
	FDustNoise::FFractalParams Settle;
	Settle.Octaves   = 3;
	Settle.Frequency = 1.0f;

	const FVector2f SettleP = DustSampler::ToNoiseSpace(P, 180000.0);
	const float SettleX = FDustNoise::FBM2D(SettleP, R.Seed ^ 0x2Du, Settle);
	const float SettleY = FDustNoise::FBM2D(SettleP + FVector2f(53.0f, 17.0f), R.Seed ^ 0x2Du, Settle);

	const double BlockSize = R.Terrain.FeatureScale;
	const FVector2D Warped = Rotated + FVector2D(SettleX, SettleY) * BlockSize * 0.35;

	// Distance to the nearest street centreline, in both axes.
	const double StreetWidth = BlockSize * 0.16;

	const double FracX = FMath::Abs(FMath::Frac(Warped.X / BlockSize) - 0.5) * BlockSize;
	const double FracY = FMath::Abs(FMath::Frac(Warped.Y / BlockSize) - 0.5) * BlockSize;
	const double StreetDistance = FMath::Min(FracX, FracY);

	// Scour: streets are channels the wind runs along, so they sit lower.
	const double StreetMask = 1.0 - FMath::Clamp(StreetDistance / StreetWidth, 0.0, 1.0);
	const double Scour = -StreetMask * R.Terrain.Amplitude * 0.55;

	// Blocks: rubble mounds, varying per block so the skyline is uneven.
	const int32 BlockX = FMath::FloorToInt32(Warped.X / BlockSize);
	const int32 BlockY = FMath::FloorToInt32(Warped.Y / BlockSize);
	const uint32 BlockHash = FDustNoise::Hash2(BlockX, BlockY, R.Seed);

	const double BlockHeight = R.Terrain.Amplitude
		* FMath::Lerp(0.15, 1.0, static_cast<double>(FDustNoise::HashToUnit(BlockHash)))
		* (1.0 - StreetMask);

	FDustNoise::FFractalParams Rubble;
	Rubble.Octaves   = 4;
	Rubble.Frequency = 1.0f;

	const FVector2f RubbleP = DustSampler::ToNoiseSpace(P, 6000.0);
	const float RubbleNoise = FDustNoise::FBM2D(RubbleP, BlockHash, Rubble);

	return R.Terrain.BaseElevation + Scour + BlockHeight
		+ static_cast<double>(RubbleNoise) * 700.0;
}

double FDustWorldSampler::ProfileOasis(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// A spring-fed depression. Shallower and much smaller than a basin, with a
	// hard rim of vegetation-stabilised sand that keeps the water in.
	const double Distance = FVector2D::Distance(P, R.Anchor);

	// Irregular outline.
	const FVector2D ToCentre = P - R.Anchor;
	const float Angle = static_cast<float>(FMath::Atan2(ToCentre.Y, ToCentre.X));
	const float OutlineNoise = FDustNoise::Simplex2D(
		FVector2f(FMath::Cos(Angle) * 3.0f, FMath::Sin(Angle) * 3.0f), R.Seed);

	const double EffectiveRadius = R.CoreRadius * (1.0 + static_cast<double>(OutlineNoise) * 0.28);
	const double T = FMath::Clamp(Distance / FMath::Max(EffectiveRadius, 1.0), 0.0, 1.0);

	// Rim at ~0.85 of the radius, floor in the middle.
	const double Rim = FMath::Exp(-FMath::Square((T - 0.85) / 0.10)) * R.Terrain.Amplitude * 0.55;
	const double Floor = -R.Terrain.Amplitude * (1.0 - FDustNoise::Quintic(static_cast<float>(T)));

	FDustNoise::FFractalParams Params;
	Params.Octaves   = 4;
	Params.Frequency = 1.0f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);
	const float Texture = FDustNoise::FBM2D(NoiseP, R.Seed ^ 0x4Eu, Params);

	return R.Terrain.BaseElevation + Floor + Rim + static_cast<double>(Texture) * 250.0;
}

double FDustWorldSampler::ProfileStormBelt(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Yardangs: streamlined ridges carved parallel to a wind that never stops.
	// Aligned to the prevailing wind, which means that if the designer changes
	// the world's wind direction, this entire region re-orients itself
	// correctly for free.
	const double U = FVector2D::DotProduct(P, WindAxis);
	const double V = FVector2D::DotProduct(P, CrossWindAxis);

	// Heavily anisotropic sampling: 12:1 stretched along the wind.
	const FVector2f YardangP(
		static_cast<float>(U / (R.Terrain.FeatureScale * 12.0)),
		static_cast<float>(V / R.Terrain.FeatureScale));

	FDustNoise::FFractalParams Params;
	Params.Octaves    = FMath::Clamp(R.Terrain.DetailOctaves, 3, 7);
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.09f;
	Params.Gain       = 0.5f;

	const float Ridges = FDustNoise::Ridged2D(YardangP, R.Seed, Params);

	// Undercut: the base of a yardang is eroded faster than its top, giving
	// the characteristic overhang. Approximated by sharpening the profile.
	const double Height = FMath::Pow(static_cast<double>(Ridges), 0.75) * R.Terrain.Amplitude;

	return R.Terrain.BaseElevation + Height;
}

double FDustWorldSampler::ProfileBadlands(const FDustRegionSnapshot& R, const FVector2D& P) const
{
	// Chaotic rilling in soft banded sediment. Built from stacked ridged noise
	// at aggressive lacunarity, then quantised into visible strata bands.
	FDustNoise::FFractalParams Params;
	Params.Octaves    = FMath::Clamp(R.Terrain.DetailOctaves + 1, 4, 8);
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.41f;
	Params.Gain       = 0.58f;

	const FVector2f NoiseP = DustSampler::ToNoiseSpace(P, R.Terrain.FeatureScale);
	const float Raw = FDustNoise::WarpedFBM2D(NoiseP, R.Seed, Params, R.Terrain.WarpStrength);

	double Height = R.Terrain.BaseElevation + static_cast<double>(Raw) * R.Terrain.Amplitude;

	// Strata: quantise to horizontal bands and blend partly back toward the
	// smooth value. Bands are what make badlands legible as *layers* and give
	// the player a readable sense of depth as they descend.
	constexpr double BandThickness = 1100.0;
	const double Banded = FMath::FloorToDouble(Height / BandThickness) * BandThickness;
	Height = FMath::Lerp(Height, Banded, 0.42);

	return Height;
}

// ---------------------------------------------------------------------------
// Composite queries
// ---------------------------------------------------------------------------

double FDustWorldSampler::SampleHeight(const FVector2D& WorldXY) const
{
	// 1. Continental shape, shared by everything.
	double Height = SampleContinental(WorldXY);

	// 2. Region profiles, weighted.
	float Weights[DustSampler::MaxRegions] = {};
	const int32 Count = FMath::Min(Config.Regions.Num(), DustSampler::MaxRegions);

	float Total = 0.0f;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FDustRegionSnapshot& Region = Config.Regions[Index];
		const double Distance = GetWarpedRegionDistance(Region, WorldXY);

		if (Distance >= Region.CoreRadius + Region.FalloffRadius)
		{
			continue;
		}

		double Weight = 1.0;
		if (Distance > Region.CoreRadius)
		{
			const double T = (Distance - Region.CoreRadius) / FMath::Max(Region.FalloffRadius, 1.0);
			Weight = 1.0 - FDustNoise::Quintic(static_cast<float>(FMath::Clamp(T, 0.0, 1.0)));
		}
		Weight *= FMath::Pow(2.0, static_cast<double>(Region.Priority));

		Weights[Index] = static_cast<float>(Weight);
		Total += Weights[Index];
	}

	double RegionHeight = 0.0;
	double DuneInfluence = 0.0;
	FDustTerrainParams DetailParams = Config.FallbackTerrain;

	if (Total > KINDA_SMALL_NUMBER)
	{
		const float InvTotal = 1.0f / Total;
		float BestWeight = 0.0f;
		int32 BestIndex = INDEX_NONE;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const float W = Weights[Index] * InvTotal;
			if (W <= 0.0f)
			{
				continue;
			}

			const FDustRegionSnapshot& Region = Config.Regions[Index];
			RegionHeight  += SampleRegionProfile(Region, WorldXY) * static_cast<double>(W);
			DuneInfluence += static_cast<double>(Region.Terrain.DuneInfluence * W);

			if (W > BestWeight)
			{
				BestWeight = W;
				BestIndex = Index;
			}
		}

		if (Config.Regions.IsValidIndex(BestIndex))
		{
			DetailParams = Config.Regions[BestIndex].Terrain;
		}
	}
	else
	{
		RegionHeight = Config.FallbackTerrain.BaseElevation;
		DuneInfluence = Config.FallbackTerrain.DuneInfluence;
	}

	Height += RegionHeight;

	// 3. Dunes, on top of the substrate, scaled by how sandy this place is.
	if (DuneInfluence > KINDA_SMALL_NUMBER)
	{
		Height += SampleDuneHeight(WorldXY, Config.DuneMigrationPhase) * DuneInfluence;
	}

	// 4. Close-range roughness.
	Height += SampleDetail(WorldXY, DetailParams);

	// 5. The encircling massif.
	Height += SampleBoundaryRise(WorldXY);

	return Height;
}

double FDustWorldSampler::SampleRegionProfile(const FDustRegionSnapshot& Region, const FVector2D& WorldXY) const
{
	switch (Region.Profile)
	{
	case EDustTerrainProfile::DuneSea:    return ProfileDuneSea(Region, WorldXY);
	case EDustTerrainProfile::SaltFlat:   return ProfileSaltFlat(Region, WorldXY);
	case EDustTerrainProfile::Canyon:     return ProfileCanyon(Region, WorldXY);
	case EDustTerrainProfile::Mesa:       return ProfileMesa(Region, WorldXY);
	case EDustTerrainProfile::Mountains:  return ProfileMountains(Region, WorldXY);
	case EDustTerrainProfile::Basin:      return ProfileBasin(Region, WorldXY);
	case EDustTerrainProfile::Volcanic:   return ProfileVolcanic(Region, WorldXY);
	case EDustTerrainProfile::DryForest:  return ProfileDryForest(Region, WorldXY);
	case EDustTerrainProfile::BuriedCity: return ProfileBuriedCity(Region, WorldXY);
	case EDustTerrainProfile::Oasis:      return ProfileOasis(Region, WorldXY);
	case EDustTerrainProfile::StormBelt:  return ProfileStormBelt(Region, WorldXY);
	case EDustTerrainProfile::Badlands:   return ProfileBadlands(Region, WorldXY);

	default:
		return Region.Terrain.BaseElevation;
	}
}

FVector FDustWorldSampler::SampleNormal(const FVector2D& WorldXY, double Epsilon) const
{
	// Central differences. Four extra height samples is more expensive than an
	// analytic derivative would be, but the terrain stack contains quantisation
	// (canyon benches, badlands strata) that has no analytic derivative, and a
	// normal that disagrees with the height field is worse than a slow one.
	const double HeightXPlus  = SampleHeight(WorldXY + FVector2D(Epsilon, 0.0));
	const double HeightXMinus = SampleHeight(WorldXY - FVector2D(Epsilon, 0.0));
	const double HeightYPlus  = SampleHeight(WorldXY + FVector2D(0.0, Epsilon));
	const double HeightYMinus = SampleHeight(WorldXY - FVector2D(0.0, Epsilon));

	const double DX = (HeightXPlus - HeightXMinus) / (2.0 * Epsilon);
	const double DY = (HeightYPlus - HeightYMinus) / (2.0 * Epsilon);

	return FVector(-DX, -DY, 1.0).GetSafeNormal();
}

float FDustWorldSampler::SampleLooseDepth(const FVector2D& WorldXY) const
{
	int32 DominantIndex = INDEX_NONE;
	float DominantWeight = 0.0f;
	const FDustTerrainParams Params = BlendTerrainParams(WorldXY, DominantIndex, DominantWeight);

	// The dune body is itself loose material, so anywhere a dune stands the
	// loose depth is the dune height plus the region's base sand sheet.
	const double DuneHeight = (Params.DuneInfluence > KINDA_SMALL_NUMBER)
		? SampleDuneHeight(WorldXY, Config.DuneMigrationPhase) * Params.DuneInfluence
		: 0.0;

	float Depth = Params.LooseDepth + static_cast<float>(DuneHeight);

	// Sand cannot rest on anything steeper than the angle of repose. This is
	// the same rule that gives the game its slip faces, applied to the loose
	// layer as a whole - which is why cliffs are bare rock without anyone
	// having to paint them that way.
	const FVector Normal = SampleNormal(WorldXY, 400.0);
	const float SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)));

	const float SlopeFalloff = 1.0f - FMath::SmoothStep(
		DustSampler::AngleOfReposeDegrees - 6.0f,
		DustSampler::AngleOfReposeDegrees + 8.0f,
		SlopeDegrees);

	return FMath::Max(0.0f, Depth * SlopeFalloff);
}

FDustGroundSample FDustWorldSampler::SampleGround(const FVector2D& WorldXY) const
{
	FDustGroundSample Sample;

	int32 DominantIndex = INDEX_NONE;
	float DominantWeight = 0.0f;
	const FDustTerrainParams Params = BlendTerrainParams(WorldXY, DominantIndex, DominantWeight);

	Sample.Height = SampleHeight(WorldXY);
	Sample.Normal = SampleNormal(WorldXY, 120.0);
	Sample.SlopeDegrees = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(Sample.Normal.Z, -1.0, 1.0)));

	Sample.DominantRegionIndex = DominantIndex;
	Sample.DominantRegionWeight = DominantWeight;

	// Loose depth, reusing the normal we already have rather than recomputing.
	{
		const double DuneHeight = (Params.DuneInfluence > KINDA_SMALL_NUMBER)
			? SampleDuneHeight(WorldXY, Config.DuneMigrationPhase) * Params.DuneInfluence
			: 0.0;

		const float SlopeFalloff = 1.0f - FMath::SmoothStep(
			DustSampler::AngleOfReposeDegrees - 6.0f,
			DustSampler::AngleOfReposeDegrees + 8.0f,
			Sample.SlopeDegrees);

		Sample.LooseDepth = FMath::Max(0.0f,
			(Params.LooseDepth + static_cast<float>(DuneHeight)) * SlopeFalloff);
	}

	// Surface classification: steep ground exposes the substrate regardless of
	// what the region nominally is.
	Sample.Surface = (Sample.SlopeDegrees > Params.SteepSurfaceAngle || Sample.LooseDepth < 4.0f)
		? Params.SteepSurface
		: Params.PrimarySurface;

	// Water.
	if (Params.WaterLevel > -1.0e8)
	{
		Sample.WaterHeight = Params.BaseElevation + Params.WaterLevel;

		if (Sample.WaterHeight > Sample.Height)
		{
			Sample.Surface = EDustSurface::Water;
			Sample.Wetness = 1.0f;
		}
		else
		{
			// A wet margin around standing water, fading over 8 m of elevation.
			const double Above = Sample.Height - Sample.WaterHeight;
			Sample.Wetness = 1.0f - FMath::SmoothStep(0.0f, 800.0f, static_cast<float>(Above));
		}
	}

	// Rime above the snow line, which is an elevation the mountains region owns.
	if (Sample.Height > 240000.0 && Sample.SlopeDegrees < 52.0f)
	{
		const float RimeBlend = FMath::SmoothStep(240000.0f, 285000.0f, static_cast<float>(Sample.Height));
		if (RimeBlend > 0.5f)
		{
			Sample.Surface = EDustSurface::Rime;
		}
	}

	return Sample;
}

float FDustWorldSampler::SampleCaveDensity(const FVector& WorldLocation) const
{
	// Positive = solid, negative = open space.
	//
	// Caves are carved as tubes: |noise| near zero forms a connected network of
	// passages, which is a far better structure than thresholded blobs because
	// the result is naturally navigable - passages connect, dead ends are rare,
	// and the player can always get back out.
	const FVector2D XY(WorldLocation.X, WorldLocation.Y);
	const double SurfaceHeight = SampleHeight(XY);
	const double Depth = SurfaceHeight - WorldLocation.Z;

	// No caves above ground, and none below the basement depth.
	constexpr double MinDepth = 1200.0;    // 12 m of roof, minimum
	constexpr double MaxDepth = 90000.0;   // 900 m

	if (Depth < MinDepth || Depth > MaxDepth)
	{
		return 1.0f;
	}

	FDustNoise::FFractalParams Params;
	Params.Octaves    = 4;
	Params.Frequency  = 1.0f;
	Params.Lacunarity = 2.11f;
	Params.Gain       = 0.5f;

	constexpr double TubeScale = 22000.0;
	const FVector NoiseP(
		WorldLocation.X / TubeScale,
		WorldLocation.Y / TubeScale,
		// Vertical scale is compressed so passages run more horizontally than
		// vertically. Vertical shafts are dramatic but unnavigable in bulk.
		WorldLocation.Z / (TubeScale * 0.45));

	const float TubeA = FDustNoise::FBM3D(NoiseP, CaveSeed, Params);
	const float TubeB = FDustNoise::FBM3D(NoiseP + FVector(41.7, 17.3, 88.1), CaveSeed ^ 0x5Cu, Params);

	// Intersecting two tube fields gives sparse, branching networks rather than
	// a uniform sponge.
	const float Tube = FMath::Max(FMath::Abs(TubeA), FMath::Abs(TubeB));

	constexpr float TubeRadius = 0.11f;
	float Density = Tube - TubeRadius;

	// Taper the network out near the depth limits so caves do not slice open
	// at the surface or terminate in a flat plane at the bottom.
	const float RoofFade  = FMath::SmoothStep(static_cast<float>(MinDepth), static_cast<float>(MinDepth * 3.0), static_cast<float>(Depth));
	const float FloorFade = 1.0f - FMath::SmoothStep(static_cast<float>(MaxDepth * 0.7), static_cast<float>(MaxDepth), static_cast<float>(Depth));

	Density = FMath::Lerp(1.0f, Density, RoofFade * FloorFade);

	return Density;
}
