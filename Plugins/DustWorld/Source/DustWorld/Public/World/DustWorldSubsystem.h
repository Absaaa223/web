// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "Save/DustSaveSubsystem.h"
#include "World/DustWorldSampler.h"
#include "DustWorldSubsystem.generated.h"

class UDustWorldComposition;

/** Fired when the player crosses from one region into another. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustRegionChanged, FName, NewRegionId, FName, PreviousRegionId);

/**
 * Owns the world sampler and everything derived from it.
 *
 * This is the front door for "what is the world like at X?" for the whole
 * game. Gameplay code should never construct its own sampler: doing so would
 * miss the dune migration phase and produce ground that disagrees with what is
 * rendered.
 *
 * CACHING
 * -------
 * The hot query pattern is enormously spatially coherent - the player, their
 * shadow, four foot IK traces, the vehicle's suspension and a dozen particle
 * emitters all ask about roughly the same square metre. A small direct-mapped
 * cache of recent samples absorbs most of that; the miss path is the sampler
 * itself, which is stateless and safe to call from anywhere.
 */
UCLASS()
class DUSTWORLD_API UDustWorldSubsystem
	: public UTickableWorldSubsystem
	, public IDustSaveParticipant
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	//~ Begin IDustSaveParticipant
	virtual void OnDustSave(UDustSaveGame& Save) override;
	virtual void OnDustLoad(const UDustSaveGame& Save) override;
	virtual int32 GetDustLoadPriority() const override { return 0; } // first: everything else needs terrain
	//~ End IDustSaveParticipant

	/** Convenience accessor. Null outside a game world. */
	static UDustWorldSubsystem* Get(const UObject* WorldContext);

	// -----------------------------------------------------------------------
	// Sampling
	// -----------------------------------------------------------------------

	/** Full ground description at a world XY. Cached. */
	UFUNCTION(BlueprintCallable, Category = "Dust|World", meta = (DisplayName = "Sample Ground"))
	FDustGroundSample SampleGround(const FVector& WorldLocation) const;

	/** Terrain height only. Cheapest query; use this for anything bulk. */
	UFUNCTION(BlueprintCallable, Category = "Dust|World")
	double SampleHeight(const FVector& WorldLocation) const;

	/**
	 * Projects a location onto the terrain surface, preserving XY.
	 * Does not trace against placed geometry - use for spawning and for
	 * long-range queries where the cell is not streamed in.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|World")
	FVector ProjectToGround(const FVector& WorldLocation) const;

	/** Surface normal at a world XY. */
	UFUNCTION(BlueprintCallable, Category = "Dust|World")
	FVector SampleNormal(const FVector& WorldLocation, double Epsilon = 100.0) const;

	/** Direct access to the sampler for worker-thread and bulk use. */
	const FDustWorldSampler& GetSampler() const { return Sampler; }

	// -----------------------------------------------------------------------
	// Regions
	// -----------------------------------------------------------------------

	/** Region id at a location, or NAME_None outside every region. */
	UFUNCTION(BlueprintCallable, Category = "Dust|World")
	FName GetRegionIdAt(const FVector& WorldLocation) const;

	/** The region the player is currently standing in. */
	UFUNCTION(BlueprintPure, Category = "Dust|World")
	FName GetCurrentRegionId() const { return CurrentRegionId; }

	/** Region data asset by id, or null. */
	UFUNCTION(BlueprintPure, Category = "Dust|World")
	const UDustRegionData* FindRegionData(FName RegionId) const;

	UPROPERTY(BlueprintAssignable, Category = "Dust|World")
	FDustRegionChanged OnRegionChanged;

	// -----------------------------------------------------------------------
	// Dune migration
	// -----------------------------------------------------------------------

	/**
	 * Advances dune migration. Called by the weather subsystem, whose wind
	 * speed determines how fast sand actually moves.
	 *
	 * Migration is applied on the game thread behind a fence, because the
	 * sampler is read by worker threads and a phase change mid-sample would
	 * produce a discontinuous height for one query.
	 *
	 * @param DownwindCentimetres  How far the dune train advances.
	 */
	void AdvanceDuneMigration(double DownwindCentimetres);

	/** Sets the prevailing wind used to orient dunes and yardangs. */
	void SetPrevailingWind(const FVector2D& WindDirection);

	UFUNCTION(BlueprintPure, Category = "Dust|World")
	float GetDuneMigrationPhase() const { return static_cast<float>(Sampler.GetDuneMigrationPhase()); }

	// -----------------------------------------------------------------------
	// Diagnostics
	// -----------------------------------------------------------------------

	/** Cache hit rate over the last second, for the developer overlay. */
	UFUNCTION(BlueprintPure, Category = "Dust|World")
	float GetCacheHitRate() const;

	/** Forces a full sampler rebuild. Editor-only path for iterating on regions. */
	void RebuildSampler();

private:
	/** Looks up the cache, falling back to the sampler on a miss. */
	const FDustGroundSample& GetCachedSample(const FVector2D& WorldXY) const;

	/** Tracks which region the player is in and fires the change event. */
	void UpdateCurrentRegion();

	/** Loads the world composition asset named in developer settings. */
	void LoadComposition();

private:
	/** The composition this world was built from. */
	UPROPERTY(Transient)
	TObjectPtr<const UDustWorldComposition> Composition;

	/** Region assets by id, for gameplay lookups that need the full asset. */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<const UDustRegionData>> RegionsById;

	FDustWorldSampler Sampler;

	/** Region the player was in on the previous update. */
	FName CurrentRegionId;

	// --- Sample cache -------------------------------------------------------

	/**
	 * Direct-mapped cache. Sized to a power of two so the index is a mask
	 * rather than a modulo, and small enough to live comfortably in L2.
	 */
	static constexpr int32 CacheSize = 2048;
	static constexpr int32 CacheMask = CacheSize - 1;

	/** Quantisation of cache keys, in cm. Finer than a footstep, coarser than a texel. */
	static constexpr double CacheQuantum = 25.0;

	struct FCacheEntry
	{
		int64 Key = TNumericLimits<int64>::Min();
		FDustGroundSample Sample;
	};

	/** Mutable because caching is not a semantic mutation of the subsystem. */
	mutable TArray<FCacheEntry> Cache;

	mutable int32 CacheHits = 0;
	mutable int32 CacheMisses = 0;
	mutable float CacheStatTimer = 0.0f;
	mutable float LastHitRate = 0.0f;
};
