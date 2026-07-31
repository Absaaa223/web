// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustTypes.h"
#include "GameplayTagContainer.h"
#include "DustAudioDirector.generated.h"

class USoundBase;
class UAudioComponent;

/** One layer of the adaptive score. Layers fade independently. */
USTRUCT(BlueprintType)
struct DUSTAUDIO_API FDustMusicLayer
{
	GENERATED_BODY()

	/** Audio state this layer belongs to (Dust.Audio.*). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	FGameplayTag StateTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TSoftObjectPtr<USoundBase> Sound;

	/** Seconds to fade in. Long fades keep the score from announcing itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0"))
	float FadeInSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0"))
	float FadeOutSeconds = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TargetVolume = 1.0f;
};

/**
 * The audio director.
 *
 * ========================================================================
 * SILENCE IS THE MOST EXPENSIVE THING IN THE MIX
 * ========================================================================
 * The single most common failure in an exploration game's audio is that it is
 * always playing something. Wall-to-wall ambient music means that when a
 * moment genuinely deserves a cue, there is no headroom left to give it one -
 * the player has been acclimatised and hears nothing new.
 *
 * DUST therefore treats silence as a *budget*. The director tracks how much of
 * the recent past has had music in it, and refuses to start a new cue when that
 * ratio is too high. The consequence is that DUST is quiet a great deal of the
 * time, and that when a cue does arrive it lands.
 *
 * The rule the composer works to: no more than 35% of any twenty-minute window
 * carries music. Wind, sand and the player's own footsteps carry the rest.
 *
 * ========================================================================
 * THE WIND BED
 * ========================================================================
 * The wind layer is not an ambient loop. It is driven from the actual wind
 * field, so its volume and filtering follow the same gusts that bend the flora
 * and move the sand. The player hears a gust arrive a moment before they see it
 * cross the dune in front of them, which is the sort of coherence that makes a
 * world feel physically real without anyone consciously noticing it.
 *
 * Exposure also shapes it: on an open crest the wind is loud and unfiltered;
 * in the lee of a dune it drops and dulls. Shelter you can hear is shelter you
 * will look for.
 */
UCLASS()
class DUSTAUDIO_API UDustAudioDirector : public UTickableWorldSubsystem
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

	static UDustAudioDirector* Get(const UObject* WorldContext);

	/** The audio state the director currently believes we are in. */
	UFUNCTION(BlueprintPure, Category = "Dust|Audio")
	FGameplayTag GetAudioState() const { return CurrentState; }

	/** Fraction of the recent window that carried music, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dust|Audio")
	float GetMusicDensity() const { return MusicDensity; }

	/** True if the silence budget is currently blocking new cues. */
	UFUNCTION(BlueprintPure, Category = "Dust|Audio")
	bool IsSilenceEnforced() const { return MusicDensity > MaxMusicDensity; }

	/**
	 * Requests a musical cue. May be declined by the silence budget - callers
	 * must not assume it played. Returns true if the cue was accepted.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Audio")
	bool RequestCue(FGameplayTag StateTag, float Intensity);

	/**
	 * Overrides the silence budget for a cue that must play - the one-time
	 * reveal of a major landmark, for instance. Use sparingly; every override
	 * spends credibility that the next unforced cue relies on.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Audio")
	void ForceCue(FGameplayTag StateTag, float Intensity);

	/** Current wind bed loudness, 0..1, for the mix and for debug. */
	UFUNCTION(BlueprintPure, Category = "Dust|Audio")
	float GetWindBedLevel() const { return WindBedLevel; }

	/**
	 * How enclosed the player is, 0 = open desert, 1 = deep cave.
	 * Drives reverb send and the wind bed's low-pass. Derived from terrain,
	 * so a canyon sounds like a canyon without anyone placing an audio volume.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Audio")
	float GetEnclosure() const { return Enclosure; }

private:
	/** Decides which audio state the current situation calls for. */
	FGameplayTag EvaluateDesiredState() const;

	/** Updates the rolling music density figure. */
	void UpdateMusicDensity(float DeltaTime);

	/** Drives the wind bed from the live wind field. */
	void UpdateWindBed(float DeltaTime);

	/** Estimates acoustic enclosure by sampling the terrain horizon. */
	void UpdateEnclosure();

	/** Reacts to a discovery by requesting the appropriate cue. */
	UFUNCTION()
	void HandleDiscoveryMade(FGameplayTag Category, float Significance);

private:
	FGameplayTag CurrentState;

	/** Seconds the current state has been active. */
	float TimeInState = 0.0f;

	/**
	 * Rolling fraction of recent time carrying music. Updated with a long time
	 * constant so a single cue does not lock out the next twenty minutes.
	 */
	float MusicDensity = 0.0f;

	/** True while any music layer is audible. */
	bool bMusicPlaying = false;

	/** The budget. See the class comment: this is a design constant. */
	static constexpr float MaxMusicDensity = 0.35f;

	/**
	 * Window over which density is measured, in seconds. Twenty minutes is
	 * roughly the length of a walk between regions.
	 */
	static constexpr float DensityWindowSeconds = 1200.0f;

	/**
	 * Minimum silence after a cue ends before another may start, in seconds.
	 * Independent of the density budget: even inside budget, two cues back to
	 * back would read as a soundtrack rather than as punctuation.
	 */
	static constexpr float MinimumSilenceSeconds = 75.0f;

	float SilenceTimer = 0.0f;

	float WindBedLevel = 0.0f;
	float Enclosure = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> WindBedComponent;

	float EnclosureAccumulator = 0.0f;
};
