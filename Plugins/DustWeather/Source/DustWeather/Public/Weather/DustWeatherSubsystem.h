// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "Save/DustSaveSubsystem.h"
#include "Engine/DataAsset.h"
#include "DustWeatherSubsystem.generated.h"

class UMaterialParameterCollection;

/**
 * A weather condition, authored as data.
 *
 * Each preset defines both what it looks like and what it *does*: a sandstorm
 * is not a particle effect, it is a wind speed that moves dunes, a visibility
 * ceiling that changes how the player navigates, a particulate density that
 * affects breathing audio, and a fauna behaviour trigger that sends animals to
 * shelter. Every one of those is a field on this asset.
 */
UCLASS(BlueprintType)
class DUSTWEATHER_API UDustWeatherPreset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Identifying tag (Dust.Weather.*). Other systems react to this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FGameplayTag WeatherTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	// --- Wind ---------------------------------------------------------------

	/** Sustained wind speed in cm/s. 100 = still, 2400 = full sandstorm. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0"))
	float WindSpeed = 300.0f;

	/** Gust speed as a multiple of WindSpeed at peak. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "1.0"))
	float GustMultiplier = 1.4f;

	/** How rapidly gusts arrive, in Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0"))
	float GustFrequency = 0.22f;

	/** Suspended particulate density, 0..1. Drives visibility and audio. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wind", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Particulate = 0.05f;

	// --- Atmosphere ---------------------------------------------------------

	/** Visibility distance in cm. Fog and scattering are fitted to this. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere", meta = (ClampMin = "1000.0", Units = "cm"))
	float VisibilityDistance = 1200000.0f;

	/** Cloud coverage, 0..1, driving the volumetric cloud material. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CloudCoverage = 0.08f;

	/** Rain intensity, 0..1. Wets surfaces and triggers the bloom event. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RainIntensity = 0.0f;

	/** Average seconds between lightning strikes. 0 disables lightning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere", meta = (ClampMin = "0.0", Units = "s"))
	float LightningInterval = 0.0f;

	/** Temperature offset in degrees C applied on top of the diurnal curve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Atmosphere")
	float TemperatureOffset = 0.0f;

	// --- Behaviour ----------------------------------------------------------

	/** In-game hours this weather typically lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.1"))
	float TypicalDurationHours = 4.0f;

	/** Real seconds to blend in and out. Long, so transitions are felt not seen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "1.0", Units = "s"))
	float TransitionSeconds = 90.0f;

	/**
	 * Which weathers this can transition into, and how likely each is.
	 * A sandstorm does not become a clear sky instantly; it decays through haze.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour")
	TMap<FGameplayTag, float> TransitionWeights;

	/** Base likelihood of this weather occurring at all, before regional bias. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Behaviour", meta = (ClampMin = "0.0"))
	float BaseLikelihood = 1.0f;

	/** Particle system spawned while this weather is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<class UFXSystemAsset> AmbientEffect;

	/** Audio state tag raised while this weather is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FGameplayTag AudioStateTag;
};

/** Fired when the weather begins transitioning to a new condition. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustWeatherChanging, FGameplayTag, NewWeather, FGameplayTag, OldWeather);

/** Fired on each lightning strike, with the strike location. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDustLightningStrike, FVector, StrikeLocation);

/**
 * Weather simulation and the global wind field.
 *
 * ------------------------------------------------------------------------
 * WEATHER AS A MARKOV CHAIN
 * ------------------------------------------------------------------------
 * Conditions transition according to per-preset weights, biased by the region
 * the player is standing in. This is what makes the Storm Belt *feel* like a
 * storm belt: it does not spawn a scripted sandstorm, it simply weights
 * sandstorm transitions so heavily that clear skies there are a rare event the
 * player will remember.
 *
 * Transitions are slow - a minute and a half of blending is typical - so the
 * player experiences weather arriving rather than switching.
 *
 * ------------------------------------------------------------------------
 * THE WIND FIELD
 * ------------------------------------------------------------------------
 * Wind is not a single global vector. It is a field: it accelerates over dune
 * crests, stalls in the lee, funnels along canyons and drops away in basins.
 * Querying it is cheap enough that flora, particles, the paraglider and the
 * audio system can all sample it per-instance, which is what stops the world
 * from looking like everything is being blown by the same fan.
 */
UCLASS()
class DUSTWEATHER_API UDustWeatherSubsystem
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
	virtual int32 GetDustLoadPriority() const override { return 20; }
	//~ End IDustSaveParticipant

	static UDustWeatherSubsystem* Get(const UObject* WorldContext);

	// -----------------------------------------------------------------------
	// Wind
	// -----------------------------------------------------------------------

	/**
	 * Samples the wind field at a world location.
	 *
	 * Accounts for terrain acceleration and shelter, so the lee side of a dune
	 * is genuinely calmer than its crest. This is the query that flora, dust
	 * emitters, the paraglider and the audio wind bed all use.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Weather")
	FDustWindSample SampleWind(const FVector& WorldLocation) const;

	/** The prevailing wind direction, ignoring local terrain effects. */
	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	FVector GetPrevailingWindDirection() const { return PrevailingWindDirection; }

	// -----------------------------------------------------------------------
	// Weather state
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	FGameplayTag GetCurrentWeatherTag() const;

	/** Blend factor toward the incoming weather, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	float GetTransitionAlpha() const { return TransitionAlpha; }

	/** Current interpolated visibility distance in cm. */
	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	float GetVisibilityDistance() const { return BlendedVisibility; }

	/** Current interpolated particulate density, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	float GetParticulate() const { return BlendedParticulate; }

	/** Current rain intensity, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dust|Weather")
	float GetRainIntensity() const { return BlendedRain; }

	/**
	 * Forces a transition to a specific weather. Used by debug, by narrative
	 * set-pieces, and by the region trigger that guarantees the player sees a
	 * sandstorm at least once before they leave the dune sea.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Weather")
	void ForceWeather(FGameplayTag WeatherTag, bool bInstant = false);

	UPROPERTY(BlueprintAssignable, Category = "Dust|Weather")
	FDustWeatherChanging OnWeatherChanging;

	UPROPERTY(BlueprintAssignable, Category = "Dust|Weather")
	FDustLightningStrike OnLightningStrike;

private:
	/** Loads every weather preset from the project's weather directory. */
	void LoadPresets();

	/** Chooses the next weather from the current one's transition weights. */
	FGameplayTag ChooseNextWeather() const;

	/** Advances the transition blend and recomputes derived values. */
	void UpdateTransition(float DeltaSeconds);

	/** Advances the prevailing wind direction, which drifts slowly. */
	void UpdateWindDirection(float DeltaSeconds);

	/** Moves the dunes downwind by however far the wind carried sand. */
	void UpdateDuneMigration(float DeltaSeconds);

	/** Rolls for lightning and picks a plausible strike point. */
	void UpdateLightning(float DeltaSeconds);

	/** Pushes weather values into the material parameter collection. */
	void PushToMaterials();

	const UDustWeatherPreset* FindPreset(FGameplayTag Tag) const;

private:
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UDustWeatherPreset>> Presets;

	UPROPERTY(Transient)
	TObjectPtr<const UDustWeatherPreset> CurrentPreset;

	UPROPERTY(Transient)
	TObjectPtr<const UDustWeatherPreset> IncomingPreset;

	/** 0 = fully current, 1 = fully incoming. */
	float TransitionAlpha = 0.0f;

	/** Seconds the whole transition takes. */
	float TransitionDuration = 90.0f;

	/** In-game hours remaining before the next transition is rolled. */
	float HoursUntilNextChange = 4.0f;

	// --- Blended values -----------------------------------------------------
	float BlendedWindSpeed = 300.0f;
	float BlendedGustMultiplier = 1.4f;
	float BlendedGustFrequency = 0.22f;
	float BlendedParticulate = 0.05f;
	float BlendedVisibility = 1200000.0f;
	float BlendedCloudCoverage = 0.08f;
	float BlendedRain = 0.0f;

	/** Prevailing wind, drifting slowly over hours. */
	FVector PrevailingWindDirection = FVector(1.0, 0.25, 0.0).GetSafeNormal();

	/** Target the prevailing direction is drifting toward. */
	FVector TargetWindDirection = FVector(1.0, 0.25, 0.0).GetSafeNormal();

	/** Phase accumulator for gusting. */
	float GustPhase = 0.0f;

	/** Countdown to the next lightning strike. */
	float LightningCountdown = 0.0f;

	/** Accumulated sand transport not yet applied to the sampler, in cm. */
	double PendingDuneMigration = 0.0;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> WeatherParameters;
};
