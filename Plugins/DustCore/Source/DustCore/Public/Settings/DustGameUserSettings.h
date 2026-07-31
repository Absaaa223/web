// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "DustGameUserSettings.generated.h"

/** Temporal upscaling backend. Availability is probed at startup per GPU. */
UENUM(BlueprintType)
enum class EDustUpscaler : uint8
{
	/** Engine default temporal AA. Cheapest, softest. */
	TAAU        UMETA(DisplayName = "TAAU"),

	/** Temporal Super Resolution. Our default: vendor-neutral, excellent at 66%. */
	TSR         UMETA(DisplayName = "Temporal Super Resolution"),

	/** NVIDIA DLSS. RTX only. */
	DLSS        UMETA(DisplayName = "NVIDIA DLSS"),

	/** AMD FidelityFX Super Resolution. */
	FSR         UMETA(DisplayName = "AMD FSR"),

	/** Intel Xe Super Sampling. */
	XeSS        UMETA(DisplayName = "Intel XeSS"),

	/** No upscaling; render at native resolution. */
	None        UMETA(DisplayName = "Native")
};

/** Upscaler quality preset, mapped per backend to its native quality enum. */
UENUM(BlueprintType)
enum class EDustUpscalerQuality : uint8
{
	UltraPerformance,
	Performance,
	Balanced,
	Quality,
	UltraQuality,

	/** Screen percentage is driven by the performance governor. */
	DynamicResolution
};

/** Camera shake and motion comfort profile. */
UENUM(BlueprintType)
enum class EDustMotionComfort : uint8
{
	/** Full cinematic motion: head bob, landing dip, gait sway. */
	Cinematic,

	/** Reduced amplitude across the board. */
	Reduced,

	/** No procedural camera motion at all. Some players need this. */
	Off
};

/**
 * User-facing settings for DUST.
 *
 * Extends UGameUserSettings rather than replacing it so that the engine's
 * resolution, window mode and scalability plumbing keeps working. Everything
 * DUST-specific is added on top and persisted to GameUserSettings.ini.
 *
 * ACCESSIBILITY IS NOT A SUBMENU. Motion comfort, HUD persistence, subtitle
 * sizing and colour-vision compensation are first-class options that sit
 * alongside graphics, not behind a separate wall.
 */
UCLASS(Config = GameUserSettings, ConfigDoNotCheckDefaults)
class DUSTCORE_API UDustGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UDustGameUserSettings();

	/** Typed accessor. Never null after engine init. */
	UFUNCTION(BlueprintPure, Category = "Dust|Settings", meta = (DisplayName = "Get DUST Settings"))
	static UDustGameUserSettings* Get();

	//~ Begin UGameUserSettings
	virtual void SetToDefaults() override;
	virtual void ApplySettings(bool bCheckForCommandLineOverrides) override;
	virtual void ApplyNonResolutionSettings() override;
	virtual void LoadSettings(bool bForceReload) override;
	//~ End UGameUserSettings

	// -----------------------------------------------------------------------
	// Display
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Display")
	void SetUpscaler(EDustUpscaler InUpscaler);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Display")
	EDustUpscaler GetUpscaler() const { return Upscaler; }

	/** Returns only the upscalers this machine can actually run. */
	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Display")
	TArray<EDustUpscaler> GetSupportedUpscalers() const;

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Display")
	void SetUpscalerQuality(EDustUpscalerQuality InQuality);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Display")
	EDustUpscalerQuality GetUpscalerQuality() const { return UpscalerQuality; }

	/** Target frame rate the performance governor aims for. 0 = uncapped. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Display")
	void SetTargetFrameRate(int32 InFrameRate);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Display")
	int32 GetTargetFrameRate() const { return TargetFrameRate; }

	/** Display nits for HDR output. Ignored when HDR is off. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Display")
	void SetHDRDisplayNits(int32 Nits);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Display")
	int32 GetHDRDisplayNits() const { return HDRDisplayNits; }

	// -----------------------------------------------------------------------
	// Camera
	// -----------------------------------------------------------------------

	/** Horizontal FOV in degrees for first person. Vertical is derived. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Camera")
	void SetFirstPersonFOV(float Degrees);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	float GetFirstPersonFOV() const { return FirstPersonFOV; }

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Camera")
	void SetThirdPersonFOV(float Degrees);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	float GetThirdPersonFOV() const { return ThirdPersonFOV; }

	/**
	 * Head bob amount, 0..1, scaling the authored curve. Independent of the
	 * motion comfort preset so players can keep bob but lose landing dip.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Camera")
	void SetHeadBobScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	float GetHeadBobScale() const { return HeadBobScale; }

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Camera")
	void SetMotionComfort(EDustMotionComfort InComfort);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	EDustMotionComfort GetMotionComfort() const { return MotionComfort; }

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Camera")
	void SetMotionBlurAmount(float Amount);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	float GetMotionBlurAmount() const { return MotionBlurAmount; }

	/**
	 * Ultrawide handling. DUST composes for 16:9 but does not crop wider
	 * displays: at 21:9 and 32:9 we widen the horizontal FOV (Hor+) so the
	 * extra pixels show more desert rather than less sky.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Camera")
	float GetAspectCorrectedFOV(float BaseHorizontalFOV, float AspectRatio) const;

	// -----------------------------------------------------------------------
	// Interface
	// -----------------------------------------------------------------------

	/**
	 * How eagerly the HUD reveals itself. DUST defaults to Minimal, where the
	 * HUD is invisible until a vital is low or an interaction is available.
	 * Persistent exists for players who genuinely want a readout on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Interface")
	void SetHUDPersistence(float InPersistence);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Interface")
	float GetHUDPersistence() const { return HUDPersistence; }

	UFUNCTION(BlueprintCallable, Category = "Dust|Settings|Interface")
	void SetSubtitleScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "Dust|Settings|Interface")
	float GetSubtitleScale() const { return SubtitleScale; }

	// -----------------------------------------------------------------------
	// Platform profiles
	// -----------------------------------------------------------------------

	/**
	 * Applies a known-good preset for the detected device. On Steam Deck this
	 * targets 60 FPS at 800p with FSR Balanced and the SteamDeck scalability
	 * group; on desktop it benchmarks and picks a scalability tier.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Settings")
	void ApplyAutoDetectedProfile();

	/** True if we are running on a Steam Deck (or equivalent handheld profile). */
	UFUNCTION(BlueprintPure, Category = "Dust|Settings")
	static bool IsHandheldProfile();

private:
	/** Pushes upscaler selection down to the relevant console variables. */
	void ApplyUpscalerSettings();

	/** Pushes camera and comfort settings to their console variables. */
	void ApplyCameraSettings();

private:
	UPROPERTY(Config)
	EDustUpscaler Upscaler = EDustUpscaler::TSR;

	UPROPERTY(Config)
	EDustUpscalerQuality UpscalerQuality = EDustUpscalerQuality::Quality;

	UPROPERTY(Config)
	int32 TargetFrameRate = 60;

	UPROPERTY(Config)
	int32 HDRDisplayNits = 1000;

	UPROPERTY(Config)
	float FirstPersonFOV = 95.0f;

	UPROPERTY(Config)
	float ThirdPersonFOV = 80.0f;

	UPROPERTY(Config)
	float HeadBobScale = 0.65f;

	UPROPERTY(Config)
	EDustMotionComfort MotionComfort = EDustMotionComfort::Cinematic;

	UPROPERTY(Config)
	float MotionBlurAmount = 0.35f;

	UPROPERTY(Config)
	float HUDPersistence = 0.0f;

	UPROPERTY(Config)
	float SubtitleScale = 1.0f;
};
