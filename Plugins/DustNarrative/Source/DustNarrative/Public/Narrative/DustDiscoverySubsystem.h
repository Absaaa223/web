// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "Save/DustSaveSubsystem.h"
#include "World/DustPoiGenerator.h"
#include "DustDiscoverySubsystem.generated.h"

/** Fired when a point of interest changes knowledge state. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustDiscoveryStateChanged, const FGuid&, PoiId, EDustDiscoveryState, NewState);

/** Fired when the player resolves something meaningful, for the audio director. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustDiscoveryMade, FGameplayTag, Category, float, Significance);

/**
 * A candidate the curiosity director is considering surfacing to the player.
 */
USTRUCT(BlueprintType)
struct DUSTNARRATIVE_API FDustCuriosityCandidate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dust|Discovery")
	FDustPointOfInterest Poi;

	/** Distance from the player in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Discovery")
	double Distance = 0.0;

	/** Angle in degrees between the player's facing and this target. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Discovery")
	float BearingOffset = 0.0f;

	/** Whether the player currently has line of sight to it. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Discovery")
	bool bVisible = false;

	/** Composite score, higher is a better thing to draw the player toward. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Discovery")
	float Score = 0.0f;
};

/**
 * Discovery tracking and the curiosity director.
 *
 * ========================================================================
 * THE CURIOSITY DIRECTOR
 * ========================================================================
 * DUST makes one promise above all others: you never walk for the sake of
 * walking. There is always something out there worth going to look at.
 *
 * That promise is easy to state and very hard to keep across 100 km2, because
 * the failure is invisible during development - a designer testing a region
 * knows where everything is, so they never experience the twenty minutes of
 * nothing that a real player finds.
 *
 * The director makes the promise enforceable. Every second, it:
 *
 *   1. Gathers every point of interest within the curiosity horizon.
 *   2. Scores each by significance, novelty, distance and *whether the player
 *      can actually see it from where they are standing*.
 *   3. If the best score is below the floor, the player is in a dead zone.
 *
 * A dead zone is not fixed by teleporting content in. It is fixed the way a
 * good level designer would fix it: by finding the nearest unresolved thing
 * and raising its silhouette - lifting a landmark's readable range, allowing
 * its dust plume to catch the light, letting a bird circle above it. The world
 * does not change; the player's chance of noticing it does.
 *
 * The failure is also *reported*: dead-zone events are logged with a location,
 * and the offline world validator sweeps for them across the whole map. A
 * region that generates dead zones fails the build.
 *
 * ========================================================================
 * WHY VISIBILITY MATTERS MORE THAN DISTANCE
 * ========================================================================
 * A ruin 400 m away behind a dune is worth nothing to a player who cannot see
 * it. A mesa 4 km away on the skyline is worth a great deal. The director
 * scores against the terrain horizon rather than against raw distance, which
 * is why it makes decisions a designer would agree with.
 */
UCLASS()
class DUSTNARRATIVE_API UDustDiscoverySubsystem
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
	virtual int32 GetDustLoadPriority() const override { return 150; }
	//~ End IDustSaveParticipant

	static UDustDiscoverySubsystem* Get(const UObject* WorldContext);

	// -----------------------------------------------------------------------
	// Discovery state
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	EDustDiscoveryState GetDiscoveryState(const FGuid& PoiId) const;

	/** Advances a POI's state. Never regresses. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Discovery")
	void SetDiscoveryState(const FGuid& PoiId, EDustDiscoveryState NewState);

	/** Marks a sub-element of a POI as resolved (an inscription read, a cache opened). */
	UFUNCTION(BlueprintCallable, Category = "Dust|Discovery")
	void ResolveElement(const FGuid& PoiId, FName ElementId);

	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	bool IsElementResolved(const FGuid& PoiId, FName ElementId) const;

	/** Total POIs the player has visited or resolved. */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	int32 GetDiscoveryCount() const { return Records.Num(); }

	// -----------------------------------------------------------------------
	// Curiosity
	// -----------------------------------------------------------------------

	/**
	 * The best thing to walk toward right now, or an invalid POI if the
	 * director cannot find one. Used by the audio director to build anticipation
	 * and by the HUD to decide whether a directional hint is warranted.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	FDustCuriosityCandidate GetBestCuriosityTarget() const { return BestCandidate; }

	/**
	 * How well the player's current surroundings are meeting the promise, 0..1.
	 * Below CuriosityFloor the director intervenes. Surfaced to the developer
	 * overlay as a live readout, because it is the single most useful number
	 * for judging whether a region is working.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	float GetCuriosityLevel() const { return CuriosityLevel; }

	/** True if the player is currently in a dead zone. */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	bool IsInDeadZone() const { return DeadZoneSeconds > 0.0f; }

	/** Every candidate currently in the horizon, for the debug overlay. */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	const TArray<FDustCuriosityCandidate>& GetCandidates() const { return Candidates; }

	/**
	 * Silhouette boost applied to a POI by the director, 1.0 = none.
	 * The rendering side reads this to widen a landmark's readable range and
	 * to allow its ambient effects at longer distance.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Discovery")
	float GetSilhouetteBoost(const FGuid& PoiId) const;

	UPROPERTY(BlueprintAssignable, Category = "Dust|Discovery")
	FDustDiscoveryStateChanged OnDiscoveryStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Dust|Discovery")
	FDustDiscoveryMade OnDiscoveryMade;

private:
	/** Rebuilds the candidate list from the POI generator. */
	void RefreshCandidates(const FVector& PlayerLocation);

	/** Scores every candidate and picks the best. */
	void ScoreCandidates(const FVector& PlayerLocation, const FVector& PlayerForward);

	/** Raises silhouettes when the player is in a dead zone. */
	void ApplyDeadZoneIntervention();

	/** Marks POIs the player is close enough to as Glimpsed or Visited. */
	void UpdateProximityDiscovery(const FVector& PlayerLocation);

	/**
	 * Terrain line of sight from the player's eye to a target, sampled against
	 * the world sampler rather than traced against geometry. This works at
	 * kilometres, where nothing is streamed in and a trace would return nothing.
	 */
	bool HasTerrainLineOfSight(const FVector& From, const FVector& To) const;

private:
	/** Persistent knowledge, keyed by POI id. */
	TMap<FGuid, FDustDiscoveryRecord> Records;

	/** The generator, rebuilt when the world sampler changes. */
	FDustPoiGenerator Generator;

	TArray<FDustCuriosityCandidate> Candidates;
	FDustCuriosityCandidate BestCandidate;

	float CuriosityLevel = 1.0f;

	/** Seconds the player has been below the curiosity floor. */
	float DeadZoneSeconds = 0.0f;

	/** Silhouette boosts applied by the director. */
	TMap<FGuid, float> SilhouetteBoosts;

	/** Player position when candidates were last rebuilt. */
	FVector LastRefreshLocation = FVector(TNumericLimits<double>::Max());

	float RefreshAccumulator = 0.0f;

	/**
	 * Below this, the player is in a dead zone. Tuned so that a normal walk
	 * through the emptiest part of the dune sea sits just above it - the dune
	 * sea is meant to feel sparse, not broken.
	 */
	static constexpr float CuriosityFloor = 0.22f;

	/** Rebuild the candidate list when the player has moved this far, in cm. */
	static constexpr double RefreshDistance = 8000.0;

	/** Or at least this often, in seconds, so migrating targets stay current. */
	static constexpr float RefreshInterval = 3.0f;
};
