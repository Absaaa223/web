// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustPerformanceGovernor.generated.h"

/**
 * What the governor is allowed to sacrifice, in the order it sacrifices it.
 *
 * The ordering here is a *design* decision, not an engineering one. We would
 * rather lose foliage density than shadow quality, and rather lose shadow
 * quality than resolution, because at 10 km sightlines the silhouette read is
 * the game's primary navigation channel and softening it costs more than a
 * slightly noisier ground plane.
 */
UENUM(BlueprintType)
enum class EDustSacrificeStage : uint8
{
	/** Nothing given up. */
	None                 = 0,

	/** Reduce distant particle counts and dust density. */
	Particles            = 1,

	/** Pull in grass and small-detail foliage draw distance. */
	FoliageDensity       = 2,

	/** Drop volumetric cloud and fog sample counts. */
	VolumetricQuality    = 3,

	/** Lower shadow map resolution and virtual shadow page pool pressure. */
	ShadowQuality        = 4,

	/** Reduce Lumen final gather quality. */
	GlobalIllumination   = 5,

	/** Drop internal render resolution within the upscaler's comfort range. */
	Resolution           = 6,

	/** Reduce reflection quality, disabling ray traced reflections if enabled. */
	Reflections          = 7,

	Count                UMETA(Hidden)
};

/** A single frame-time reading. */
USTRUCT(BlueprintType)
struct DUSTCORE_API FDustFrameStats
{
	GENERATED_BODY()

	/** Smoothed total frame time in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Performance")
	float FrameTimeMs = 0.0f;

	/** Smoothed game thread time in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Performance")
	float GameThreadMs = 0.0f;

	/** Smoothed render thread time in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Performance")
	float RenderThreadMs = 0.0f;

	/** Smoothed GPU time in ms. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Performance")
	float GPUMs = 0.0f;

	/** True if the GPU is the limiting factor this frame. */
	FORCEINLINE bool IsGPUBound() const
	{
		return GPUMs > FMath::Max(GameThreadMs, RenderThreadMs);
	}
};

/**
 * Keeps DUST at its target frame rate by trading quality in a designer-defined
 * order, on a slow hysteretic curve so the player never sees quality pumping.
 *
 * WHY THIS EXISTS
 * ---------------
 * Engine dynamic resolution alone solves the GPU case but does nothing when the
 * game thread stalls - which, in an open world with a live ecology and a dune
 * simulation, is the more common failure. The governor watches all four timings
 * and picks the right lever: CPU-bound frames reduce simulation and foliage
 * density; GPU-bound frames reduce resolution and lighting quality.
 *
 * HYSTERESIS
 * ----------
 * Degradation is fast (we must recover the frame rate), recovery is slow
 * (quality oscillation is more objectionable than a slightly conservative
 * setting). The asymmetry is deliberate and tuned in DefaultGame.ini.
 */
UCLASS()
class DUSTCORE_API UDustPerformanceGovernor : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bEnabled; }
	//~ End FTickableGameObject

	/** Target frame time in ms, derived from the user's frame rate target. */
	UFUNCTION(BlueprintPure, Category = "Dust|Performance")
	float GetTargetFrameTimeMs() const { return TargetFrameTimeMs; }

	/** Sets the target frame rate. 0 means uncapped (governor idles). */
	UFUNCTION(BlueprintCallable, Category = "Dust|Performance")
	void SetTargetFrameRate(float FramesPerSecond);

	/** Current smoothed timings. */
	UFUNCTION(BlueprintPure, Category = "Dust|Performance")
	const FDustFrameStats& GetFrameStats() const { return Stats; }

	/** How much quality is currently being sacrificed. */
	UFUNCTION(BlueprintPure, Category = "Dust|Performance")
	EDustSacrificeStage GetCurrentStage() const { return CurrentStage; }

	/**
	 * Current simulation scale, 0.25..1. Systems with variable cost - fauna
	 * ticking, curiosity evaluation, sand simulation - multiply their per-frame
	 * budget by this instead of being switched off outright, so degradation is
	 * continuous rather than a visible pop.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Performance")
	float GetSimulationScale() const { return SimulationScale; }

	/** Disables the governor entirely; used by photo mode and benchmarks. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Performance")
	void SetEnabled(bool bInEnabled);

	/**
	 * Freezes the governor at its current stage. Photo mode uses this so that
	 * a long exposure of an expensive vista does not progressively degrade the
	 * very shot the player is composing.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Performance")
	void SetFrozen(bool bInFrozen) { bFrozen = bInFrozen; }

private:
	/** Samples engine timing and updates the exponential moving averages. */
	void UpdateStats(float DeltaTime);

	/** Decides whether to escalate, hold, or relax. */
	void EvaluateStage(float DeltaTime);

	/** Applies the console variables associated with a stage. */
	void ApplyStage(EDustSacrificeStage Stage);

private:
	UPROPERTY(Transient)
	FDustFrameStats Stats;

	/** Derived from the user's target frame rate. Defaults to 60 FPS. */
	float TargetFrameTimeMs = 16.67f;

	/**
	 * Frames must exceed target by this fraction before we escalate. Prevents
	 * reacting to a single expensive frame such as a streaming spike.
	 */
	float EscalateThreshold = 1.08f;

	/** Frames must be under target by this fraction before we relax. */
	float RelaxThreshold = 0.82f;

	/** Seconds of sustained overrun before escalating one stage. */
	float EscalateHoldSeconds = 1.5f;

	/** Seconds of sustained headroom before relaxing one stage. */
	float RelaxHoldSeconds = 8.0f;

	/** Running timers for the above. */
	float OverrunTimer = 0.0f;
	float HeadroomTimer = 0.0f;

	EDustSacrificeStage CurrentStage = EDustSacrificeStage::None;

	float SimulationScale = 1.0f;

	bool bEnabled = true;
	bool bFrozen = false;

	/** Smoothing factor for the moving averages; ~0.5s window at 60 FPS. */
	static constexpr float StatSmoothing = 0.03f;
};
