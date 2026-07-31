// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DustDeveloperSettings.generated.h"

/**
 * Project-wide DUST configuration, surfaced under Project Settings > DUST.
 *
 * These are *design constants*, not user preferences: world extent, the master
 * seed, simulation budgets. User-facing options live in UDustGameUserSettings.
 *
 * Config is stored in DefaultGame.ini so it is version controlled and cooked.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DUST"))
class DUSTCORE_API UDustDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDustDeveloperSettings();

	/** Convenience accessor. Never null after CDO construction. */
	static const UDustDeveloperSettings& Get();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override;
	//~ End UDeveloperSettings

	// -----------------------------------------------------------------------
	// World
	// -----------------------------------------------------------------------

	/**
	 * Master world seed. Everything procedural derives from this via
	 * FDustNoise::DeriveSeed. Changing it regenerates the entire world and
	 * invalidates every save.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World")
	int32 MasterSeed = 19770820;

	/**
	 * Half-extent of the playable world in centimetres, measured from origin.
	 * 500,000 cm = 5 km, giving a 10 x 10 km world = 100 km2.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "100000", Units = "cm"))
	double WorldHalfExtent = 500000.0 * 10.0;

	/**
	 * Distance beyond WorldHalfExtent over which terrain ramps into an
	 * impassable rise. There is no invisible wall; the world simply becomes
	 * mountain. Players who insist can climb it and find the observation ridge.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "10000", Units = "cm"))
	double WorldBoundaryFalloff = 400000.0;

	/**
	 * Size of one streaming cell in centimetres. Must divide evenly into
	 * WorldHalfExtent * 2. 25,600 cm = 256 m matches the landscape component
	 * grid so streaming and landscape LOD transitions line up.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (ClampMin = "6400", Units = "cm"))
	double StreamingCellSize = 25600.0;

	/**
	 * Whether world origin rebasing is enabled. Required at 10 km extents to
	 * keep float precision acceptable for physics and particles, unless the
	 * project is running with Large World Coordinates throughout.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "World")
	bool bUseOriginRebasing = true;

	/** Player distance from current origin that triggers a rebase. */
	UPROPERTY(Config, EditAnywhere, Category = "World", meta = (EditCondition = "bUseOriginRebasing", Units = "cm"))
	double OriginRebaseThreshold = 200000.0;

	// -----------------------------------------------------------------------
	// Simulation budgets
	// -----------------------------------------------------------------------

	/** Maximum creature agents simulated at full fidelity at once. */
	UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = "0"))
	int32 MaxFullFidelityCreatures = 24;

	/** Maximum creature agents tracked at low fidelity (position + needs only). */
	UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = "0"))
	int32 MaxLowFidelityCreatures = 240;

	/** Milliseconds per frame the world sampler cache may spend prefetching. */
	UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = "0.0", Units = "ms"))
	float SamplerPrefetchBudgetMs = 0.6f;

	/** Milliseconds per frame the curiosity director may spend evaluating. */
	UPROPERTY(Config, EditAnywhere, Category = "Budgets", meta = (ClampMin = "0.0", Units = "ms"))
	float CuriosityBudgetMs = 0.3f;

	// -----------------------------------------------------------------------
	// Survival tuning
	// -----------------------------------------------------------------------

	/**
	 * Real seconds of continuous sprinting from full stamina to empty.
	 * Deliberately generous: stamina shapes pacing, it does not gate content.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Survival", meta = (ClampMin = "1.0", Units = "s"))
	float SprintSecondsAtFullStamina = 22.0f;

	/**
	 * In-game hours from full hydration to the Parched threshold while walking
	 * in daytime heat. Never reaches lethal; the worst case is a reduced
	 * stamina ceiling and a desaturated screen.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Survival", meta = (ClampMin = "0.1"))
	float HoursToParched = 3.5f;

	/** If true, dehydration can never fully incapacitate. Ships enabled. */
	UPROPERTY(Config, EditAnywhere, Category = "Survival")
	bool bForgivingSurvival = true;

	// -----------------------------------------------------------------------
	// Discovery
	// -----------------------------------------------------------------------

	/**
	 * The curiosity director guarantees at least one unresolved point of
	 * interest within this radius of the player at all times.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Discovery", meta = (ClampMin = "1000", Units = "cm"))
	double GuaranteedInterestRadius = 90000.0;

	/**
	 * Minimum separation between generated points of interest, so the world
	 * never feels like a checklist grid.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Discovery", meta = (ClampMin = "1000", Units = "cm"))
	double MinimumInterestSeparation = 22000.0;

	// -----------------------------------------------------------------------
	// Save
	// -----------------------------------------------------------------------

	/** Seconds between autosaves. Autosave is deferred while airborne or in combat. */
	UPROPERTY(Config, EditAnywhere, Category = "Save", meta = (ClampMin = "30.0", Units = "s"))
	float AutosaveIntervalSeconds = 180.0f;

	/** Number of rolling autosave slots retained. */
	UPROPERTY(Config, EditAnywhere, Category = "Save", meta = (ClampMin = "1"))
	int32 AutosaveSlotCount = 3;

	/** Number of manual save slots offered. */
	UPROPERTY(Config, EditAnywhere, Category = "Save", meta = (ClampMin = "1"))
	int32 ManualSlotCount = 12;

	// -----------------------------------------------------------------------
	// Debug
	// -----------------------------------------------------------------------

	/** Master switch for developer overlays. Forced off in Shipping. */
	UPROPERTY(Config, EditAnywhere, Category = "Debug")
	bool bEnableDeveloperOverlays = true;
};
