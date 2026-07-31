// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Deterministic, allocation-free noise primitives.
 *
 * DESIGN CONTRACT
 * ---------------
 * Every function here is a pure function of its arguments. No engine state, no
 * globals, no RNG streams, no floating point that varies by platform beyond
 * IEEE-754 basics. The same (seed, position) must produce bit-identical results
 * on a build machine, a Steam Deck and an RTX 4090, because:
 *
 *   - the editor bakes landscape heightmaps offline from these functions;
 *   - the runtime samples them again for foot placement and creature nav;
 *   - the save file stores only a seed, so a re-loaded world must rebuild
 *     identically.
 *
 * If any of those three disagree by even a centimetre, the player falls
 * through the floor. Treat changes to this file as content-invalidating: bump
 * FDustNoise::AlgorithmVersion so the sampler cache and baked landscape are
 * regenerated.
 *
 * We deliberately avoid FMath::PerlinNoise / FRandomStream here: both have
 * changed behaviour across engine versions, which would silently reshape a
 * shipped world on an engine upgrade.
 */
struct DUSTCORE_API FDustNoise
{
	/**
	 * Bump when any function below changes its output for the same inputs.
	 * Cached terrain, baked landscapes and POI placement are keyed on this.
	 */
	static constexpr int32 AlgorithmVersion = 3;

	// -----------------------------------------------------------------------
	// Integer hashing
	// -----------------------------------------------------------------------

	/** 32-bit integer avalanche (variant of Murmur3's finaliser). */
	static FORCEINLINE uint32 Hash(uint32 X)
	{
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return X;
	}

	static FORCEINLINE uint32 Hash2(int32 X, int32 Y, uint32 Seed)
	{
		// Large odd multipliers keep the lattice from aliasing along axes.
		uint32 H = Seed;
		H ^= static_cast<uint32>(X) * 0x9E3779B1u;
		H ^= static_cast<uint32>(Y) * 0x85EBCA77u;
		return Hash(H);
	}

	static FORCEINLINE uint32 Hash3(int32 X, int32 Y, int32 Z, uint32 Seed)
	{
		uint32 H = Seed;
		H ^= static_cast<uint32>(X) * 0x9E3779B1u;
		H ^= static_cast<uint32>(Y) * 0x85EBCA77u;
		H ^= static_cast<uint32>(Z) * 0xC2B2AE3Du;
		return Hash(H);
	}

	/** Hash to the unit interval [0,1). */
	static FORCEINLINE float HashToUnit(uint32 H)
	{
		// 24 bits of mantissa is plenty and avoids denormal edge cases.
		return static_cast<float>(H >> 8) * (1.0f / 16777216.0f);
	}

	/** Hash to [-1,1]. */
	static FORCEINLINE float HashToSigned(uint32 H)
	{
		return HashToUnit(H) * 2.0f - 1.0f;
	}

	/** Uniformly distributed unit vector in 2D from a hash. */
	static FORCEINLINE FVector2f HashToUnitVector2D(uint32 H)
	{
		const float Angle = HashToUnit(H) * 2.0f * PI;
		return FVector2f(FMath::Cos(Angle), FMath::Sin(Angle));
	}

	// -----------------------------------------------------------------------
	// Value / gradient noise
	// -----------------------------------------------------------------------

	/**
	 * 2D gradient (Perlin-style) noise. Output range is approximately
	 * [-1,1]; the theoretical bound is +/- sqrt(2)/2 before normalisation, and
	 * we scale accordingly so callers can treat it as unit range.
	 */
	static float Gradient2D(FVector2f P, uint32 Seed);

	/** 3D gradient noise. Used for cloud shaping and cave carving. */
	static float Gradient3D(FVector P, uint32 Seed);

	/**
	 * Simplex-style noise with a cheaper kernel than Gradient2D and no
	 * axis-aligned artefacts. Preferred for large-scale continental shaping
	 * where lattice artefacts would be visible across kilometres.
	 */
	static float Simplex2D(FVector2f P, uint32 Seed);

	// -----------------------------------------------------------------------
	// Fractal composition
	// -----------------------------------------------------------------------

	/** Parameters for a fractal stack. Kept POD so it can live in a data asset. */
	struct FFractalParams
	{
		/** Number of octaves. Cost is linear; 5-7 is the useful range. */
		int32 Octaves = 5;

		/** Frequency multiplier per octave. ~2.0 is standard; 1.93 breaks up repetition. */
		float Lacunarity = 1.93f;

		/** Amplitude multiplier per octave. Lower = smoother. */
		float Gain = 0.5f;

		/** World units per noise unit at octave 0. */
		float Frequency = 1.0f / 8192.0f;

		/** Output multiplier applied after normalisation. */
		float Amplitude = 1.0f;
	};

	/** Standard fractional Brownian motion. Smooth, rolling. Range ~[-1,1]. */
	static float FBM2D(FVector2f P, uint32 Seed, const FFractalParams& Params);

	/** 3D fBm. */
	static float FBM3D(FVector P, uint32 Seed, const FFractalParams& Params);

	/**
	 * Ridged multifractal. Produces sharp crest lines - the basis of both
	 * mountain ridges and dune crests. Range [0,1], with 1 at the ridge.
	 */
	static float Ridged2D(FVector2f P, uint32 Seed, const FFractalParams& Params);

	/**
	 * Billowy noise (absolute-value fBm). Rounded, cloud-like lobes. Used for
	 * the softer barchan bodies and for cumulus shaping. Range [0,1].
	 */
	static float Billow2D(FVector2f P, uint32 Seed, const FFractalParams& Params);

	/**
	 * Domain-warped fBm. Warping the input by another noise field is what
	 * turns "procedural bumps" into something that reads as geology. This is
	 * the single highest-value trick in the terrain stack.
	 *
	 * @param WarpStrength  Warp displacement in the same units as P.
	 */
	static float WarpedFBM2D(FVector2f P, uint32 Seed, const FFractalParams& Params, float WarpStrength);

	// -----------------------------------------------------------------------
	// Cellular / Worley
	// -----------------------------------------------------------------------

	/** Result of a cellular query. */
	struct FCellularResult
	{
		/** Distance to the nearest feature point, in the same units as the input. */
		float F1 = 0.0f;

		/** Distance to the second nearest feature point. */
		float F2 = 0.0f;

		/** Integer cell coordinate of the nearest feature point. */
		FIntPoint Cell = FIntPoint::ZeroValue;

		/** World position of the nearest feature point. */
		FVector2f FeaturePoint = FVector2f::ZeroVector;

		/** F2 - F1. Near zero on cell borders - use for crack and vein masks. */
		FORCEINLINE float EdgeDistance() const { return F2 - F1; }
	};

	/**
	 * Worley / cellular noise over a unit-cell lattice.
	 *
	 * @param P         Query point, already scaled so that 1.0 == one cell.
	 * @param Seed      Field seed.
	 * @param Jitter    0 = perfect grid, 1 = fully jittered feature points.
	 */
	static FCellularResult Cellular2D(FVector2f P, uint32 Seed, float Jitter = 1.0f);

	/**
	 * Convenience: returns only the normalised edge mask, 1 on a cell border
	 * falling to 0 at cell centres. Used for salt-flat polygon cracks and for
	 * the fractured plates of the volcanic region.
	 */
	static float CellularEdges2D(FVector2f P, uint32 Seed, float EdgeWidth = 0.05f);

	// -----------------------------------------------------------------------
	// Utility
	// -----------------------------------------------------------------------

	/** Quintic smoothstep, C2 continuous. Required for artefact-free gradients. */
	static FORCEINLINE float Quintic(float T)
	{
		return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
	}

	/** Derivative of Quintic; used for analytic normals. */
	static FORCEINLINE float QuinticDeriv(float T)
	{
		return 30.0f * T * T * (T * (T - 2.0f) + 1.0f);
	}

	/**
	 * Remaps [-1,1] to [0,1] without a branch.
	 */
	static FORCEINLINE float ToUnitRange(float SignedValue)
	{
		return SignedValue * 0.5f + 0.5f;
	}

	/**
	 * Smooth minimum. Blends two heightfields without a crease, which is how
	 * handcrafted region cards merge into procedural terrain invisibly.
	 *
	 * @param K  Blend radius in the same units as A and B.
	 */
	static FORCEINLINE float SmoothMin(float A, float B, float K)
	{
		if (K <= KINDA_SMALL_NUMBER)
		{
			return FMath::Min(A, B);
		}
		const float H = FMath::Clamp(0.5f + 0.5f * (B - A) / K, 0.0f, 1.0f);
		return FMath::Lerp(B, A, H) - K * H * (1.0f - H);
	}

	/** Smooth maximum; see SmoothMin. */
	static FORCEINLINE float SmoothMax(float A, float B, float K)
	{
		return -SmoothMin(-A, -B, K);
	}

	/**
	 * Derives a stable sub-seed from a master seed and a string identifier.
	 * Lets each system own an independent noise field ("dunes", "caves",
	 * "artefacts") without any of them having to agree on magic numbers.
	 */
	static uint32 DeriveSeed(uint32 MasterSeed, const TCHAR* Channel);
};
