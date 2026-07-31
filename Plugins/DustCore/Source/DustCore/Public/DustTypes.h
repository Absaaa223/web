// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DustTypes.generated.h"

/**
 * Shared value types used across module boundaries.
 *
 * Anything in here must be dependency-free: no subsystem pointers, no asset
 * references that would drag a plugin into another plugin's build graph.
 */

// ---------------------------------------------------------------------------
// Surface classification
// ---------------------------------------------------------------------------

/**
 * The physical surface the player or a creature is standing on. Drives
 * footstep audio, particle spawning, traction, footprint depth and whether
 * dune sliding is possible.
 */
UENUM(BlueprintType)
enum class EDustSurface : uint8
{
	/** Loose windblown sand. Deep footprints, sliding possible, poor traction. */
	LooseSand      UMETA(DisplayName = "Loose Sand"),

	/** Compacted sand or gravel pan. Shallow prints, good traction. */
	PackedSand     UMETA(DisplayName = "Packed Sand"),

	/** Exposed bedrock. No deformation, full traction, sharp footstep report. */
	Rock           UMETA(DisplayName = "Rock"),

	/** Cracked salt crust. Brittle, distinctive crunch, reflective when wet. */
	Salt           UMETA(DisplayName = "Salt Crust"),

	/** Dry riverbed clay. Hard, dusty, cracks underfoot. */
	Clay           UMETA(DisplayName = "Dry Clay"),

	/** Volcanic scoria and ash. Crunching, heat shimmer, unstable. */
	Scoria         UMETA(DisplayName = "Volcanic Scoria"),

	/** Living ground: oasis grass, moss, reed litter. */
	Verdant        UMETA(DisplayName = "Verdant"),

	/** Constructed floor: worked stone of the vanished civilisation. */
	Worked         UMETA(DisplayName = "Worked Stone"),

	/** Standing or flowing water. */
	Water          UMETA(DisplayName = "Water"),

	/** Snow or rime on the high peaks. */
	Rime           UMETA(DisplayName = "Rime"),

	Count          UMETA(Hidden)
};

// ---------------------------------------------------------------------------
// Sampler output
// ---------------------------------------------------------------------------

/**
 * The complete result of asking the world "what is here?".
 *
 * Deliberately a flat POD struct: it is produced by the world sampler on
 * worker threads, memcpy'd into caches, and read by gameplay without any
 * ownership questions.
 */
USTRUCT(BlueprintType)
struct DUSTCORE_API FDustGroundSample
{
	GENERATED_BODY()

	/** Terrain height at the queried XY, in world units (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	double Height = 0.0;

	/** Unit surface normal derived analytically from the height field. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	FVector Normal = FVector::UpVector;

	/** Slope in degrees from horizontal. Cached because everything wants it. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float SlopeDegrees = 0.0f;

	/** Dominant surface classification at this point. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	EDustSurface Surface = EDustSurface::LooseSand;

	/** Index of the dominant region at this point; INDEX_NONE outside the world. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	int32 DominantRegionIndex = INDEX_NONE;

	/** Blend weight of the dominant region, 0..1. Low values mean a transition zone. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float DominantRegionWeight = 0.0f;

	/**
	 * Depth of loose material above the hard substrate, in cm. Drives footprint
	 * depth, sinking, whether a sand board works, and dune slide eligibility.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float LooseDepth = 0.0f;

	/**
	 * Surface wetness, 0..1. Raised by rain and proximity to water, decays with
	 * sun exposure. Drives roughness and darkening in the ground material.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float Wetness = 0.0f;

	/** Height of the water surface at this XY, or -BIG if there is no water. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	double WaterHeight = -1.0e9;

	/** True if water covers the terrain at this point. */
	FORCEINLINE bool IsSubmerged() const { return WaterHeight > Height; }

	/** True if this surface can be slid down on foot or on a board. */
	FORCEINLINE bool IsSlideable() const
	{
		return SlopeDegrees > 22.0f
			&& LooseDepth > 12.0f
			&& (Surface == EDustSurface::LooseSand || Surface == EDustSurface::PackedSand);
	}
};

// ---------------------------------------------------------------------------
// Wind
// ---------------------------------------------------------------------------

/**
 * A sample of the global wind field. Wind is a first-class world system in
 * DUST: it moves dunes, bends flora, carries particles, shapes audio, pushes
 * the paraglider and determines which way a sandstorm arrives from.
 */
USTRUCT(BlueprintType)
struct DUSTCORE_API FDustWindSample
{
	GENERATED_BODY()

	/** Horizontal wind direction, normalised. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	FVector Direction = FVector::ForwardVector;

	/** Sustained wind speed in cm/s. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float Speed = 0.0f;

	/** Instantaneous gust speed in cm/s, including turbulence. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float GustSpeed = 0.0f;

	/** Suspended particulate density, 0..1. Drives visibility and lung audio. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float Particulate = 0.0f;

	/** Convenience: full velocity vector including gust. */
	FORCEINLINE FVector GetVelocity() const { return Direction * GustSpeed; }
};

// ---------------------------------------------------------------------------
// Time of day
// ---------------------------------------------------------------------------

/** A point in the world calendar. Serialised into saves. */
USTRUCT(BlueprintType)
struct DUSTCORE_API FDustWorldTime
{
	GENERATED_BODY()

	/** Days elapsed since the player woke. */
	UPROPERTY(BlueprintReadWrite, Category = "Dust")
	int32 Day = 0;

	/** Time of day in hours, 0..24. */
	UPROPERTY(BlueprintReadWrite, Category = "Dust")
	float Hour = 6.25f;

	/** Total elapsed in-game hours since the start. */
	FORCEINLINE double GetAbsoluteHours() const
	{
		return static_cast<double>(Day) * 24.0 + static_cast<double>(Hour);
	}

	/** Normalised day fraction, 0 at midnight, 0.5 at noon. */
	FORCEINLINE float GetDayFraction() const { return Hour / 24.0f; }

	/** Advances the clock, rolling days over. */
	void Advance(float DeltaHours)
	{
		Hour += DeltaHours;
		while (Hour >= 24.0f)
		{
			Hour -= 24.0f;
			++Day;
		}
		while (Hour < 0.0f)
		{
			Hour += 24.0f;
			--Day;
		}
	}
};

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

/** How the player came to know about a point of interest. */
UENUM(BlueprintType)
enum class EDustDiscoveryState : uint8
{
	/** Never seen, never hinted at. */
	Unknown,

	/** Visible on the horizon or hinted at by an inscription elsewhere. */
	Glimpsed,

	/** The player has been close enough to read what it is. */
	Visited,

	/** Fully explored: every inscription read, every artefact taken. */
	Resolved
};

/**
 * A record of something worth walking toward. Produced by both hand-authored
 * placement and procedural generation - the rest of the game cannot tell the
 * difference, which is the point.
 */
USTRUCT(BlueprintType)
struct DUSTCORE_API FDustPointOfInterest
{
	GENERATED_BODY()

	/** Stable identity, derived from position + kind so it survives regeneration. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	FGuid Id;

	/** World location of the interest anchor. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	FVector Location = FVector::ZeroVector;

	/** Discovery category (Dust.Discovery.*). */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	FGameplayTag Category;

	/** Current knowledge state. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	EDustDiscoveryState State = EDustDiscoveryState::Unknown;

	/**
	 * How far away this thing reads as interesting, in cm. A mountain temple
	 * has a 400,000 cm silhouette radius; a buried coin has 800.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	double SilhouetteRadius = 5000.0;

	/**
	 * Authored weight, 0..1. The curiosity director prefers high-weight
	 * targets when choosing what to make visible on the horizon.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	float Significance = 0.5f;

	/** True if this POI was placed by hand rather than generated. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust")
	bool bHandAuthored = false;

	FORCEINLINE bool IsValid() const { return Id.IsValid(); }
};

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

/** Player camera perspective. Switchable at any time without a load. */
UENUM(BlueprintType)
enum class EDustCameraMode : uint8
{
	FirstPerson,
	ThirdPerson,

	/** Detached free camera used by photo mode. */
	Photo
};
