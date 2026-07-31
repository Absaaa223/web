// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "Save/DustSaveSubsystem.h"
#include "DustSkySubsystem.generated.h"

class UMaterialParameterCollection;
class UDirectionalLightComponent;

/** A complete description of the sky at an instant. Read by lighting and audio. */
USTRUCT(BlueprintType)
struct DUSTWEATHER_API FDustSkyState
{
	GENERATED_BODY()

	/** Sun direction, pointing *from* the sun toward the world. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	FVector SunDirection = FVector(0.0, 0.0, -1.0);

	/** Sun elevation above the horizon, in degrees. Negative below. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float SunElevation = 0.0f;

	/** Sun azimuth in degrees, 0 = world north. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float SunAzimuth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	FVector MoonDirection = FVector(0.0, 0.0, 1.0);

	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float MoonElevation = 0.0f;

	/**
	 * Moon phase, 0 = new, 0.5 = full, 1 = new again. Drives both the moon's
	 * appearance and how much light a night actually has - which is the
	 * difference between a navigable night and a genuinely dark one.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float MoonPhase = 0.5f;

	/** Moon illumination reaching the ground, 0..1, accounting for phase and elevation. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float MoonIllumination = 0.0f;

	/** Rotation of the star field, in degrees, for the Milky Way band. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float StarFieldRotation = 0.0f;

	/**
	 * How visible the stars are, 0..1. Falls to zero in daylight and is
	 * suppressed by moonlight and by suspended dust.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float StarVisibility = 0.0f;

	/** Which quarter of the day we are in, as a tag for other systems. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	FGameplayTag TimeOfDayTag;

	/** Air temperature at ground level in degrees C. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	float TemperatureCelsius = 25.0f;

	/** True during the ~40 minutes either side of sunrise and sunset. */
	UPROPERTY(BlueprintReadOnly, Category = "Dust|Sky")
	bool bGoldenHour = false;
};

/**
 * Time of day and celestial mechanics.
 *
 * ------------------------------------------------------------------------
 * WHY PHYSICAL ASTRONOMY
 * ------------------------------------------------------------------------
 * We could fake the sun with a rotating light and nobody would file a bug. We
 * do not, for two reasons that both turn out to be gameplay reasons:
 *
 *   1. NAVIGATION. With no map and no compass, the sun and the stars are the
 *      player's only absolute reference. That only works if they behave
 *      consistently: the sun must rise in the same place every morning, the
 *      pole star must actually hold still, and the Milky Way must sweep across
 *      the sky at a rate you can learn. A faked sky teaches nothing.
 *
 *   2. THE MOON. Moon phase driving night-time light level, on a cycle long
 *      enough that the player notices it across a play session, turns "how dark
 *      is it tonight" into a thing the world does to you rather than a constant.
 *      A full-moon night in the salt flat is one of the game's best images and
 *      it only exists because the phase is modelled.
 *
 * The model is a simplified circular-orbit solar position algorithm: accurate
 * to within a degree or so, which is far beyond what anyone can perceive, and
 * a few hundred times cheaper than the full VSOP87 series.
 */
UCLASS()
class DUSTWEATHER_API UDustSkySubsystem
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
	virtual int32 GetDustLoadPriority() const override { return 10; }
	//~ End IDustSaveParticipant

	static UDustSkySubsystem* Get(const UObject* WorldContext);

	// -----------------------------------------------------------------------
	// Time
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	const FDustWorldTime& GetWorldTime() const { return WorldTime; }

	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	const FDustSkyState& GetSkyState() const { return SkyState; }

	/** Sets the clock directly. Used by sleep, by debug and by load. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sky")
	void SetWorldTime(const FDustWorldTime& NewTime);

	/**
	 * In-game hours that pass per real-world minute.
	 *
	 * The default of 1.0 gives a 24-minute day, which is the sweet spot found
	 * in playtesting: long enough that noon is a state you live in rather than
	 * a moment you pass through, short enough that a player who wants to see a
	 * particular region at dawn does not have to schedule their evening around it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sky")
	void SetTimeScale(float HoursPerRealMinute);

	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	float GetTimeScale() const { return HoursPerRealMinute; }

	/**
	 * Advances time smoothly to a target hour over a real-time duration.
	 * Used when the player rests: time visibly races rather than cutting.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sky")
	void FastForwardTo(float TargetHour, float RealSecondsToTake);

	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	bool IsFastForwarding() const { return FastForwardRemaining > 0.0f; }

	// -----------------------------------------------------------------------
	// Queries other systems ask
	// -----------------------------------------------------------------------

	/** Ambient light level at ground, 0..1. Drives fauna activity and audio. */
	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	float GetAmbientLightLevel() const;

	/** True if the sun is below the horizon. */
	UFUNCTION(BlueprintPure, Category = "Dust|Sky")
	bool IsNight() const { return SkyState.SunElevation < 0.0f; }

	/**
	 * Solar heat load at a location, 0..1, accounting for sun elevation,
	 * shadowing by terrain and cloud cover. The survival system reads this.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sky")
	float GetSolarLoadAt(const FVector& WorldLocation) const;

private:
	/** Recomputes the full sky state from the current clock. */
	void UpdateSkyState();

	/** Pushes sky parameters into the material parameter collection. */
	void PushToMaterials();

	/** Solar position for a given day and hour, at the world's latitude. */
	void ComputeSunPosition(float& OutElevation, float& OutAzimuth) const;

	/** Lunar position and phase. */
	void ComputeMoonPosition(float& OutElevation, float& OutAzimuth, float& OutPhase) const;

private:
	UPROPERTY(Transient)
	FDustWorldTime WorldTime;

	UPROPERTY(Transient)
	FDustSkyState SkyState;

	/**
	 * The world's latitude in degrees. 23.4 puts it on the Tropic of Cancer:
	 * the sun passes directly overhead at midsummer noon, shadows collapse to
	 * nothing, and the heat model has a defensible reason to be brutal.
	 */
	float LatitudeDegrees = 23.4f;

	/** Axial tilt, controlling how much the sun's path shifts across the year. */
	float AxialTiltDegrees = 23.4f;

	/** Days in a full lunar cycle. */
	float LunarCycleDays = 29.5f;

	/** Days in a year, for the seasonal solar declination. */
	float DaysPerYear = 96.0f;

	float HoursPerRealMinute = 1.0f;

	/** Fast-forward state. */
	float FastForwardRemaining = 0.0f;
	float FastForwardHoursRemaining = 0.0f;

	/** Material parameter collection driving sky, ground and flora materials. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialParameterCollection> SkyParameters;

	/** Throttle: the sky does not need recomputing every single frame. */
	float UpdateAccumulator = 0.0f;
	static constexpr float UpdateInterval = 1.0f / 15.0f;
};
