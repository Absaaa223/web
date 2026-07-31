// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Performance/DustPerformanceGovernor.h"

#include "DustLog.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

namespace DustPerf
{
	/** Helper that sets a console variable only if it exists on this platform. */
	static void SetCVar(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameOverride);
		}
	}

	static void SetCVar(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Var->Set(Value, ECVF_SetByGameOverride);
		}
	}
}

void UDustPerformanceGovernor::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SetTargetFrameRate(60.0f);
	ApplyStage(EDustSacrificeStage::None);

	UE_LOG(LogDustPerf, Log, TEXT("Performance governor online, target %.1f ms."), TargetFrameTimeMs);
}

void UDustPerformanceGovernor::Deinitialize()
{
	// Return everything to the user's chosen settings so the governor never
	// leaks its overrides into a menu or the next session.
	ApplyStage(EDustSacrificeStage::None);
	Super::Deinitialize();
}

TStatId UDustPerformanceGovernor::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustPerformanceGovernor, STATGROUP_Tickables);
}

void UDustPerformanceGovernor::SetTargetFrameRate(float FramesPerSecond)
{
	if (FramesPerSecond <= 0.0f)
	{
		// Uncapped: the governor stops intervening but keeps collecting stats
		// so the developer overlay still works.
		TargetFrameTimeMs = 0.0f;
		return;
	}

	TargetFrameTimeMs = 1000.0f / FramesPerSecond;
}

void UDustPerformanceGovernor::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}

	bEnabled = bInEnabled;

	if (!bEnabled)
	{
		ApplyStage(EDustSacrificeStage::None);
		CurrentStage = EDustSacrificeStage::None;
		SimulationScale = 1.0f;
	}
}

void UDustPerformanceGovernor::UpdateStats(float DeltaTime)
{
	// GAverageFrameTime and the thread time counters are engine globals updated
	// every frame; reading them is free compared to running our own timers.
	const float FrameMs = FMath::Max(DeltaTime * 1000.0f, KINDA_SMALL_NUMBER);

	const float GameMs   = FPlatformTime::ToMilliseconds(GGameThreadTime);
	const float RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
	const float GpuMs    = FPlatformTime::ToMilliseconds(GGPUFrameTime);

	// Exponential moving average. A fixed alpha rather than a frame-rate
	// independent one is intentional: when the frame rate collapses we want the
	// average to follow quickly, and at 20 FPS a fixed alpha does exactly that.
	Stats.FrameTimeMs    = FMath::Lerp(Stats.FrameTimeMs,    FrameMs,  StatSmoothing);
	Stats.GameThreadMs   = FMath::Lerp(Stats.GameThreadMs,   GameMs,   StatSmoothing);
	Stats.RenderThreadMs = FMath::Lerp(Stats.RenderThreadMs, RenderMs, StatSmoothing);
	Stats.GPUMs          = FMath::Lerp(Stats.GPUMs,          GpuMs,    StatSmoothing);
}

void UDustPerformanceGovernor::EvaluateStage(float DeltaTime)
{
	if (TargetFrameTimeMs <= 0.0f || bFrozen)
	{
		return;
	}

	const float Ratio = Stats.FrameTimeMs / TargetFrameTimeMs;

	if (Ratio > EscalateThreshold)
	{
		OverrunTimer += DeltaTime;
		HeadroomTimer = 0.0f;

		// A severe overrun (below half the target rate) escalates immediately
		// rather than waiting out the hold: at 30 FPS on a 60 FPS target the
		// player is already suffering.
		const bool bSevere = Ratio > 1.9f;

		if (bSevere || OverrunTimer >= EscalateHoldSeconds)
		{
			OverrunTimer = 0.0f;

			const uint8 Next = FMath::Min<uint8>(
				static_cast<uint8>(CurrentStage) + 1,
				static_cast<uint8>(EDustSacrificeStage::Count) - 1);

			if (Next != static_cast<uint8>(CurrentStage))
			{
				CurrentStage = static_cast<EDustSacrificeStage>(Next);
				ApplyStage(CurrentStage);

				UE_LOG(LogDustPerf, Log,
					TEXT("Escalated to stage %d (%.1f ms vs %.1f ms target, %s-bound)."),
					Next, Stats.FrameTimeMs, TargetFrameTimeMs,
					Stats.IsGPUBound() ? TEXT("GPU") : TEXT("CPU"));
			}
		}
	}
	else if (Ratio < RelaxThreshold)
	{
		HeadroomTimer += DeltaTime;
		OverrunTimer = 0.0f;

		if (HeadroomTimer >= RelaxHoldSeconds && CurrentStage != EDustSacrificeStage::None)
		{
			HeadroomTimer = 0.0f;

			CurrentStage = static_cast<EDustSacrificeStage>(static_cast<uint8>(CurrentStage) - 1);
			ApplyStage(CurrentStage);

			UE_LOG(LogDustPerf, Verbose, TEXT("Relaxed to stage %d."), static_cast<int32>(CurrentStage));
		}
	}
	else
	{
		// Inside the dead band; bleed both timers so a flickering workload does
		// not accumulate its way into an escalation.
		OverrunTimer  = FMath::Max(0.0f, OverrunTimer  - DeltaTime * 0.5f);
		HeadroomTimer = FMath::Max(0.0f, HeadroomTimer - DeltaTime * 0.5f);
	}
}

void UDustPerformanceGovernor::ApplyStage(EDustSacrificeStage Stage)
{
	using namespace DustPerf;

	const uint8 Level = static_cast<uint8>(Stage);

	auto AtLeast = [Level](EDustSacrificeStage Required)
	{
		return Level >= static_cast<uint8>(Required);
	};

	// --- Simulation scale ---------------------------------------------------
	// CPU-side systems throttle before anything visual changes, because a
	// slightly less busy ecology is far less noticeable than softer shadows.
	SimulationScale = AtLeast(EDustSacrificeStage::FoliageDensity)
		? FMath::GetMappedRangeValueClamped(
			FVector2f(2.0f, static_cast<float>(EDustSacrificeStage::Count) - 1.0f),
			FVector2f(0.85f, 0.35f),
			static_cast<float>(Level))
		: 1.0f;

	// --- Particles ----------------------------------------------------------
	SetCVar(TEXT("fx.Niagara.QualityLevel"), AtLeast(EDustSacrificeStage::Particles) ? 1 : 2);
	SetCVar(TEXT("r.ParticleLODBias"), AtLeast(EDustSacrificeStage::Particles) ? 1 : 0);

	// --- Foliage ------------------------------------------------------------
	SetCVar(TEXT("foliage.LODDistanceScale"),
		AtLeast(EDustSacrificeStage::FoliageDensity) ? 0.7f : 1.0f);
	SetCVar(TEXT("grass.DensityScale"),
		AtLeast(EDustSacrificeStage::FoliageDensity) ? 0.6f : 1.0f);

	// --- Volumetrics --------------------------------------------------------
	SetCVar(TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"),
		AtLeast(EDustSacrificeStage::VolumetricQuality) ? 384 : 768);
	SetCVar(TEXT("r.VolumetricFog.GridPixelSize"),
		AtLeast(EDustSacrificeStage::VolumetricQuality) ? 16 : 8);

	// --- Shadows ------------------------------------------------------------
	SetCVar(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"),
		AtLeast(EDustSacrificeStage::ShadowQuality) ? 0.5f : -0.5f);
	SetCVar(TEXT("r.Shadow.Virtual.ResolutionLodBiasLocal"),
		AtLeast(EDustSacrificeStage::ShadowQuality) ? 1.0f : 0.0f);

	// --- Lumen --------------------------------------------------------------
	SetCVar(TEXT("r.Lumen.ScreenProbeGather.RadianceCache.ProbeResolution"),
		AtLeast(EDustSacrificeStage::GlobalIllumination) ? 16 : 32);
	SetCVar(TEXT("r.Lumen.ScreenProbeGather.DownsampleFactor"),
		AtLeast(EDustSacrificeStage::GlobalIllumination) ? 32 : 16);

	// --- Resolution ---------------------------------------------------------
	// We only ever move the *lower* bound of dynamic resolution. The upscaler
	// keeps output resolution fixed, so this is invisible until it is severe.
	SetCVar(TEXT("r.DynamicRes.MinScreenPercentage"),
		AtLeast(EDustSacrificeStage::Resolution) ? 50.0f : 66.0f);

	// --- Reflections --------------------------------------------------------
	SetCVar(TEXT("r.Lumen.Reflections.DownsampleFactor"),
		AtLeast(EDustSacrificeStage::Reflections) ? 2 : 1);
	SetCVar(TEXT("r.RayTracing.Reflections.MaxRoughness"),
		AtLeast(EDustSacrificeStage::Reflections) ? 0.2f : 0.6f);
}

void UDustPerformanceGovernor::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_PerformanceGovernor_Tick);

	UpdateStats(DeltaTime);
	EvaluateStage(DeltaTime);
}
