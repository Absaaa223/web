// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DustTypes.h"
#include "DustRegionData.generated.h"

/**
 * The terrain archetype a region uses.
 *
 * Each profile is a different *shaping function* in the sampler, not just a
 * different set of noise constants. A dune sea and a canyon are computed by
 * genuinely different maths, which is why they read as different places rather
 * than as the same terrain with different textures.
 */
UENUM(BlueprintType)
enum class EDustTerrainProfile : uint8
{
	/** Transverse and barchan dunes that migrate with the wind. */
	DuneSea         UMETA(DisplayName = "Dune Sea"),

	/** Near-flat evaporite pan with polygonal salt cracking. */
	SaltFlat        UMETA(DisplayName = "Salt Flat"),

	/** Deeply incised drainage network cut into a plateau. */
	Canyon          UMETA(DisplayName = "Canyon"),

	/** Flat-topped erosional remnants above a debris apron. */
	Mesa            UMETA(DisplayName = "Mesa Field"),

	/** Ridged multifractal peaks with rime above the snow line. */
	Mountains       UMETA(DisplayName = "Mountains"),

	/** Broad shallow depression; collects water and fog. */
	Basin           UMETA(DisplayName = "Basin"),

	/** Fractured lava plates, cinder cones, fumarole vents. */
	Volcanic        UMETA(DisplayName = "Volcanic Field"),

	/** Fossil woodland on stabilised sand sheets. */
	DryForest       UMETA(DisplayName = "Dry Forest"),

	/** Buried urban grid; terrain follows the ruined street plan. */
	BuriedCity      UMETA(DisplayName = "Buried City"),

	/** Spring-fed depression with standing water and dense flora. */
	Oasis           UMETA(DisplayName = "Oasis"),

	/** Permanently storm-scoured terrain; low, wind-carved, hostile. */
	StormBelt       UMETA(DisplayName = "Storm Belt"),

	/** Chaotic eroded badlands of soft banded sediment. */
	Badlands        UMETA(DisplayName = "Badlands"),

	Count           UMETA(Hidden)
};

/**
 * Terrain shaping parameters. Every profile reads the fields it cares about
 * and ignores the rest; the editor hides irrelevant ones via EditCondition.
 */
USTRUCT(BlueprintType)
struct DUSTWORLD_API FDustTerrainParams
{
	GENERATED_BODY()

	/** Elevation this region sits at relative to sea datum, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (Units = "cm"))
	double BaseElevation = 0.0;

	/** Peak-to-trough amplitude of the primary shaping layer, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0.0", Units = "cm"))
	double Amplitude = 12000.0;

	/** Horizontal scale of the primary feature, in cm. Dune wavelength, canyon spacing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "100.0", Units = "cm"))
	double FeatureScale = 45000.0;

	/** Octave count for the detail stack. Higher = more expensive and rougher. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "9"))
	int32 DetailOctaves = 5;

	/** Strength of domain warping. 0 = clean noise, 1 = heavily eroded read. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarpStrength = 0.6f;

	/** Depth of loose windblown material over the substrate, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ClampMin = "0.0", Units = "cm"))
	float LooseDepth = 180.0f;

	/**
	 * How strongly dunes form here, 0..1. A canyon with 0.2 still collects sand
	 * in its floor, which is what makes the transition between regions read as
	 * geology rather than as a seam.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DuneInfluence = 1.0f;

	/** Primary surface classification for footsteps, particles and traction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	EDustSurface PrimarySurface = EDustSurface::LooseSand;

	/** Surface exposed on slopes steeper than SteepSurfaceAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface")
	EDustSurface SteepSurface = EDustSurface::Rock;

	/** Slope in degrees above which SteepSurface replaces PrimarySurface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surface", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float SteepSurfaceAngle = 38.0f;

	/**
	 * Height of standing water in this region relative to BaseElevation, in cm.
	 * Anything below -1e8 means the region has no water table.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water", meta = (Units = "cm"))
	double WaterLevel = -1.0e9;
};

/**
 * A region of the desert.
 *
 * Regions are authored, not generated: a designer places twelve of these,
 * gives each an anchor and a radius, and the sampler blends between them with
 * a noise-warped boundary so the borders read as natural transitions rather
 * than as circles. Everything else about the region - fauna, weather bias,
 * point-of-interest mix, audio - hangs off this asset.
 */
UCLASS(BlueprintType)
class DUSTWORLD_API UDustRegionData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Stable identifier. Used in saves and telemetry; never rename after ship. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName RegionId;

	/** Player-visible name, revealed only once the region has been entered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	/**
	 * A single sentence of intent, for the team rather than the player.
	 * "The place you cross when you have given up on finding anything."
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FString DesignIntent;

	// -----------------------------------------------------------------------
	// Placement
	// -----------------------------------------------------------------------

	/** World-space XY anchor of the region centre, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	FVector2D AnchorLocation = FVector2D::ZeroVector;

	/** Radius within which this region is at full strength, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1000.0", Units = "cm"))
	double CoreRadius = 120000.0;

	/** Distance beyond CoreRadius over which influence falls to zero, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "1000.0", Units = "cm"))
	double FalloffRadius = 90000.0;

	/**
	 * How far the region boundary is displaced by noise, in cm. Without this
	 * every region is visibly a circle from the air. 30-60 km of displacement
	 * at continental scale is what makes the map read as a real landmass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement", meta = (ClampMin = "0.0", Units = "cm"))
	double BoundaryWarp = 55000.0;

	/**
	 * Priority when two regions overlap at equal weight. Higher wins. Used so
	 * that the Buried City always cuts through the dune sea rather than being
	 * averaged into a mush.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Placement")
	int32 Priority = 0;

	// -----------------------------------------------------------------------
	// Terrain
	// -----------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	EDustTerrainProfile Profile = EDustTerrainProfile::DuneSea;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Terrain")
	FDustTerrainParams TerrainParams;

	// -----------------------------------------------------------------------
	// Atmosphere and weather bias
	// -----------------------------------------------------------------------

	/**
	 * Per-weather likelihood multipliers. A value of 0 forbids that weather
	 * here; the Storm Belt sets Sandstorm to 6.0 and Clear to 0.05.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	TMap<FGameplayTag, float> WeatherBias;

	/** Multiplier on the global wind speed within this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather", meta = (ClampMin = "0.0"))
	float WindMultiplier = 1.0f;

	/** Ambient ground temperature offset in degrees C, applied to the heat model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weather")
	float TemperatureOffset = 0.0f;

	// -----------------------------------------------------------------------
	// Content
	// -----------------------------------------------------------------------

	/**
	 * Points of interest per square kilometre generated in this region.
	 * The dune sea runs at 0.6 - deliberately sparse, so that arriving
	 * somewhere means something. The Buried City runs at 14.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content", meta = (ClampMin = "0.0"))
	float PoiDensityPerKm2 = 2.0f;

	/**
	 * Weighted mix of discovery categories generated here. Weights are
	 * relative, not normalised - the generator normalises at build time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TMap<FGameplayTag, float> PoiCategoryWeights;

	/** Species that may spawn here, by species id. Resolved by DustFauna. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TArray<FName> ResidentSpecies;

	/** Species that pass through on migration but do not live here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TArray<FName> MigratorySpecies;

	/** Tag identifying the ambient audio bed for this region. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	FGameplayTag AmbientAudioTag;

	// -----------------------------------------------------------------------
	// Validation
	// -----------------------------------------------------------------------

#if WITH_EDITOR
	/**
	 * Checks the region against the design rules that the world composition
	 * relies on: a valid id, a sane falloff, at least one landmark-class POI
	 * category, and a resident species list that is not empty unless the
	 * region is explicitly marked lifeless.
	 */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	//~ Begin UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset
};

/**
 * The ordered list of regions that make up the world.
 *
 * One of these exists per world. It is the only asset the sampler needs to
 * build its thread-safe snapshot, which is why the sampler can be constructed
 * on a worker thread during a cook without touching the asset registry.
 */
UCLASS(BlueprintType)
class DUSTWORLD_API UDustWorldComposition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** All regions, in no particular order; Priority resolves conflicts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	TArray<TObjectPtr<UDustRegionData>> Regions;

	/**
	 * Fallback terrain used outside every region's influence. In practice the
	 * regions tile the world, but this guarantees the sampler is total: it has
	 * a defined answer for every point on the plane, including outside the
	 * playable bounds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World")
	FDustTerrainParams FallbackTerrain;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
