// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "DustVitalsComponent.generated.h"

/** Fired when a vital crosses a threshold that the HUD should reveal itself for. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDustVitalThreshold, FGameplayTag, StateTag, bool, bEntered);

/**
 * Stamina, hydration and heat.
 *
 * ========================================================================
 * SURVIVAL THAT SHAPES ROUTES, NOT SURVIVAL THAT PUNISHES
 * ========================================================================
 * DUST has survival mechanics and no survival failure. You cannot die of
 * thirst. What you can do is become slow, weak and uncomfortable, until going
 * to find water is obviously the most interesting thing available - which is
 * the same outcome a death timer produces, arrived at by making the player want
 * something rather than fear something.
 *
 * The design rules, all enforced here:
 *
 *   1. Hydration never reaches zero-with-consequences. At its worst it caps
 *      stamina at 35% and desaturates the image. That is deeply inconvenient
 *      and completely survivable.
 *   2. Stamina exhaustion locks out sprinting, never walking. The player can
 *      always continue, just not quickly.
 *   3. Every vital recovers on its own given time and shade. Nothing requires
 *      an item the player might not have found.
 *   4. Nothing about any of this appears on screen until it matters.
 *
 * ========================================================================
 * HEAT IS THE INTERESTING ONE
 * ========================================================================
 * Thirst is a clock. Heat is a *place*: it depends on the sun's angle, on
 * whether you are in shade, on the wind, on what you are standing on. It is
 * what makes the player look at a canyon and think "I could cross in there",
 * and what makes midday genuinely different from dawn rather than just brighter.
 */
UCLASS(ClassGroup = (Dust), meta = (BlueprintSpawnableComponent))
class DUST_API UDustVitalsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDustVitalsComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	// -----------------------------------------------------------------------
	// Stamina
	// -----------------------------------------------------------------------

	/** Current stamina, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetStamina() const { return Stamina; }

	/**
	 * The ceiling stamina can currently recover to, 0..1. Reduced by thirst and
	 * by heat. This is the mechanism by which survival state limits the player
	 * without ever stopping them.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetStaminaCeiling() const;

	/** True if stamina is too low to start sprinting or climbing. */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	bool IsExhausted() const { return bExhausted; }

	/**
	 * Spends stamina. Returns false if there was not enough, in which case
	 * nothing is spent - callers must treat a false return as "the move does
	 * not happen" rather than performing it anyway.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Vitals")
	bool TrySpendStamina(float Amount);

	/** Continuous drain, e.g. while sprinting or hanging on a wall. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Vitals")
	void DrainStamina(float AmountPerSecond, float DeltaTime);

	// -----------------------------------------------------------------------
	// Hydration
	// -----------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetHydration() const { return Hydration; }

	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	bool IsParched() const { return Hydration < ParchedThreshold; }

	/** Drinks, restoring hydration. Called when the player uses water. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Vitals")
	void Drink(float Amount);

	// -----------------------------------------------------------------------
	// Heat
	// -----------------------------------------------------------------------

	/**
	 * Core temperature offset from comfortable, in arbitrary units.
	 * Positive is too hot, negative too cold. Zero is fine.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetThermalLoad() const { return ThermalLoad; }

	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	bool IsOverheating() const { return ThermalLoad > OverheatingThreshold; }

	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	bool IsCold() const { return ThermalLoad < ColdThreshold; }

	/** Current exposure to the sun, 0..1. Surfaced for the HUD's shade hint. */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetSolarExposure() const { return SolarExposure; }

	// -----------------------------------------------------------------------
	// Aggregate
	// -----------------------------------------------------------------------

	/**
	 * Overall distress, 0..1. Drives post-process desaturation, breathing
	 * audio and whether the HUD reveals itself. A single number so that
	 * presentation code does not have to know about three separate vitals.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	float GetDistress() const;

	/** True if the player is in a state it is reasonable to autosave from. */
	UFUNCTION(BlueprintPure, Category = "Dust|Vitals")
	bool IsInSafeState() const;

	UPROPERTY(BlueprintAssignable, Category = "Dust|Vitals")
	FDustVitalThreshold OnVitalThreshold;

	/** Restores from a save. */
	void RestoreState(float InStamina, float InHydration);

private:
	void UpdateStamina(float DeltaTime);
	void UpdateHydration(float DeltaHours);
	void UpdateThermal(float DeltaTime, float DeltaHours);

	/** Raises or clears a state tag and fires the threshold event once. */
	void SetStateTag(const FGameplayTag& Tag, bool bActive, bool& bFlag);

private:
	UPROPERTY(Transient)
	float Stamina = 1.0f;

	UPROPERTY(Transient)
	float Hydration = 1.0f;

	UPROPERTY(Transient)
	float ThermalLoad = 0.0f;

	UPROPERTY(Transient)
	float SolarExposure = 0.0f;

	/** Seconds since stamina was last spent; recovery has a grace delay. */
	float StaminaIdleTime = 0.0f;

	bool bExhausted = false;
	bool bParchedFlag = false;
	bool bOverheatingFlag = false;
	bool bColdFlag = false;

	// --- Tuning -------------------------------------------------------------

	/** Below this, sprint is locked out until stamina recovers past the exit. */
	static constexpr float ExhaustionEnter = 0.02f;

	/** Hysteresis: sprint becomes available again only above this. */
	static constexpr float ExhaustionExit = 0.25f;

	static constexpr float ParchedThreshold = 0.3f;
	static constexpr float OverheatingThreshold = 0.65f;
	static constexpr float ColdThreshold = -0.55f;

	/** Seconds after exertion before stamina begins recovering. */
	static constexpr float StaminaRecoveryDelay = 1.1f;

	/** Fraction of maximum stamina recovered per second when resting. */
	static constexpr float StaminaRecoveryRate = 0.28f;
};
