// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Player/DustVitalsComponent.h"

#include "DustDeveloperSettings.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weather/DustSkySubsystem.h"
#include "Weather/DustWeatherSubsystem.h"
#include "World/DustWorldSubsystem.h"

UDustVitalsComponent::UDustVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UDustVitalsComponent::BeginPlay()
{
	Super::BeginPlay();

	Stamina = 1.0f;
	Hydration = 1.0f;
	ThermalLoad = 0.0f;
}

void UDustVitalsComponent::RestoreState(float InStamina, float InHydration)
{
	Stamina = FMath::Clamp(InStamina, 0.0f, 1.0f);
	Hydration = FMath::Clamp(InHydration, 0.0f, 1.0f);

	// Thermal load is deliberately not restored. It equilibrates within a
	// minute or two of play, and reloading into a heatstroke the player did not
	// walk into themselves would feel like the game cheating.
	ThermalLoad = 0.0f;
}

float UDustVitalsComponent::GetStaminaCeiling() const
{
	float Ceiling = 1.0f;

	// Thirst is the main limiter, and it is capped so the player always has
	// something to work with. At absolute worst they retain a third of their
	// stamina - enough to climb out of a canyon, just not comfortably.
	if (Hydration < ParchedThreshold)
	{
		const float Severity = 1.0f - (Hydration / ParchedThreshold);
		Ceiling -= Severity * 0.65f;
	}

	// Heat takes a smaller bite, but it stacks with thirst, which is why
	// crossing an open flat at midday with an empty canteen is the worst
	// combination in the game - and why it is a decision the player makes
	// rather than a thing that happens to them.
	if (ThermalLoad > OverheatingThreshold)
	{
		const float Severity = FMath::Clamp(
			(ThermalLoad - OverheatingThreshold) / (1.0f - OverheatingThreshold), 0.0f, 1.0f);
		Ceiling -= Severity * 0.25f;
	}

	// Cold is milder and mostly affects fine control rather than endurance.
	if (ThermalLoad < ColdThreshold)
	{
		Ceiling -= 0.12f;
	}

	return FMath::Clamp(Ceiling, 0.35f, 1.0f);
}

bool UDustVitalsComponent::TrySpendStamina(float Amount)
{
	if (bExhausted || Stamina < Amount)
	{
		return false;
	}

	Stamina = FMath::Max(0.0f, Stamina - Amount);
	StaminaIdleTime = 0.0f;
	return true;
}

void UDustVitalsComponent::DrainStamina(float AmountPerSecond, float DeltaTime)
{
	if (AmountPerSecond <= 0.0f)
	{
		return;
	}

	Stamina = FMath::Max(0.0f, Stamina - AmountPerSecond * DeltaTime);
	StaminaIdleTime = 0.0f;
}

void UDustVitalsComponent::Drink(float Amount)
{
	const float Previous = Hydration;
	Hydration = FMath::Clamp(Hydration + Amount, 0.0f, 1.0f);

	UE_LOG(LogDust, Verbose, TEXT("Drank: hydration %.2f -> %.2f."), Previous, Hydration);
}

void UDustVitalsComponent::SetStateTag(const FGameplayTag& Tag, bool bActive, bool& bFlag)
{
	if (bActive == bFlag)
	{
		return;
	}

	bFlag = bActive;
	OnVitalThreshold.Broadcast(Tag, bActive);
}

void UDustVitalsComponent::UpdateStamina(float DeltaTime)
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	StaminaIdleTime += DeltaTime;

	if (StaminaIdleTime > StaminaRecoveryDelay)
	{
		// Recovery is faster the longer you have been resting, so a short pause
		// gives a little back and sitting down for a while gives a lot. This
		// rewards the player for stopping to look at something, which is
		// exactly the behaviour the whole game is trying to encourage.
		const float RestBonus = FMath::Clamp((StaminaIdleTime - StaminaRecoveryDelay) / 6.0f, 0.0f, 1.0f);
		const float Rate = StaminaRecoveryRate * FMath::Lerp(1.0f, 2.1f, RestBonus);

		Stamina = FMath::Min(Stamina + Rate * DeltaTime, GetStaminaCeiling());
	}
	else
	{
		// Clamp down to the ceiling if thirst or heat lowered it while we were
		// above it. Gradual, so the bar does not visibly snap.
		Stamina = FMath::FInterpConstantTo(Stamina, FMath::Min(Stamina, GetStaminaCeiling()), DeltaTime, 0.2f);
	}

	// Exhaustion with hysteresis: enter at almost nothing, leave only after a
	// real recovery. Without the gap, the player would tap sprint every frame
	// at zero stamina and stutter.
	if (!bExhausted && Stamina <= ExhaustionEnter)
	{
		SetStateTag(TAG_Dust_Vitals_Exhausted, true, bExhausted);
	}
	else if (bExhausted && Stamina >= ExhaustionExit)
	{
		SetStateTag(TAG_Dust_Vitals_Exhausted, false, bExhausted);
	}

	Stamina = FMath::Clamp(Stamina, 0.0f, 1.0f);
}

void UDustVitalsComponent::UpdateHydration(float DeltaHours)
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	// Baseline loss reaches the Parched threshold in the configured number of
	// hours of ordinary daytime walking.
	float LossPerHour = (1.0f - ParchedThreshold) / FMath::Max(Settings.HoursToParched, 0.1f);

	// Heat and exertion multiply it. Sprinting through open sun at 44 degrees
	// costs roughly three times what walking in shade does, which makes the
	// choice to push on rather than wait out the afternoon a real one.
	LossPerHour *= FMath::Lerp(1.0f, 2.4f, FMath::Clamp(ThermalLoad, 0.0f, 1.0f));

	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			const float SpeedRatio = FMath::Clamp(
				Movement->Velocity.Size() / FMath::Max(Movement->MaxWalkSpeed, 1.0f), 0.0f, 2.0f);

			LossPerHour *= FMath::Lerp(0.55f, 1.5f, SpeedRatio * 0.5f);

			// Swimming or standing in water rehydrates - the player does not
			// need to find a "drink" prompt to survive next to a lake.
			if (Movement->IsSwimming())
			{
				Drink(DeltaHours * 3.0f);
			}
		}
	}

	Hydration = FMath::Clamp(Hydration - LossPerHour * DeltaHours, 0.0f, 1.0f);

	// Forgiving survival: hydration bottoms out above zero, so the player is
	// permanently inconvenienced but never dying. Shipping default.
	if (Settings.bForgivingSurvival)
	{
		Hydration = FMath::Max(Hydration, 0.04f);
	}

	SetStateTag(TAG_Dust_Vitals_Parched, IsParched(), bParchedFlag);
}

void UDustVitalsComponent::UpdateThermal(float DeltaTime, float DeltaHours)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector Location = Owner->GetActorLocation();

	// --- Solar load ---------------------------------------------------------
	float TargetLoad = 0.0f;
	float AmbientTemperature = 24.0f;

	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		SolarExposure = Sky->GetSolarLoadAt(Location);
		AmbientTemperature = Sky->GetSkyState().TemperatureCelsius;

		// Ambient temperature sets where the body wants to sit; direct sun
		// pushes it further. Shade is worth roughly eight degrees, which is
		// about right and, more importantly, is enough to be worth walking to.
		const float EffectiveTemperature = AmbientTemperature + SolarExposure * 9.0f;

		// Map to load: 22 C is neutral, 46 C is fully overheating, 2 C is fully cold.
		TargetLoad = (EffectiveTemperature > 22.0f)
			? FMath::GetMappedRangeValueClamped(FVector2f(22.0f, 46.0f), FVector2f(0.0f, 1.0f), EffectiveTemperature)
			: FMath::GetMappedRangeValueClamped(FVector2f(22.0f, 2.0f), FVector2f(0.0f, -1.0f), EffectiveTemperature);
	}

	// --- Wind chill ---------------------------------------------------------
	// Wind cools when hot and cools further when cold. This is why a sandstorm
	// at night is the most thermally hostile thing in the game, and why the
	// player learns to want shelter for two different reasons.
	if (const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this))
	{
		const FDustWindSample Wind = Weather->SampleWind(Location);
		const float WindFactor = FMath::Clamp(Wind.Speed / 2000.0f, 0.0f, 1.0f);

		TargetLoad -= WindFactor * 0.35f;
	}

	// --- Water --------------------------------------------------------------
	if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		if (World->SampleGround(Location).IsSubmerged())
		{
			TargetLoad -= 0.6f;
		}
	}

	// The body has thermal inertia: it takes minutes to heat up and minutes to
	// cool down. Without this, stepping in and out of shade would flicker every
	// dependent system, and the player would learn to ignore it.
	constexpr float ThermalTimeConstant = 0.09f;
	ThermalLoad = FMath::FInterpTo(ThermalLoad, TargetLoad, DeltaTime, ThermalTimeConstant);

	SetStateTag(TAG_Dust_Vitals_Overheating, IsOverheating(), bOverheatingFlag);
	SetStateTag(TAG_Dust_Vitals_Cold, IsCold(), bColdFlag);
}

float UDustVitalsComponent::GetDistress() const
{
	// Each vital contributes only once it is genuinely a problem; below the
	// thresholds the player should feel completely unbothered.
	const float ThirstDistress = FMath::Clamp(
		(ParchedThreshold - Hydration) / ParchedThreshold, 0.0f, 1.0f);

	const float HeatDistress = FMath::Clamp(
		(ThermalLoad - OverheatingThreshold) / (1.0f - OverheatingThreshold), 0.0f, 1.0f);

	const float ColdDistress = FMath::Clamp(
		(ColdThreshold - ThermalLoad) / (1.0f + ColdThreshold), 0.0f, 1.0f);

	const float StaminaDistress = bExhausted ? 0.35f : 0.0f;

	return FMath::Clamp(
		FMath::Max3(ThirstDistress, FMath::Max(HeatDistress, ColdDistress), StaminaDistress),
		0.0f, 1.0f);
}

bool UDustVitalsComponent::IsInSafeState() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement)
	{
		return false;
	}

	// Autosave only when reloading here would put the player somewhere they
	// can stand. Mid-air, mid-climb or underwater are all states where an
	// autosave would reload into an immediate scramble.
	return Movement->IsMovingOnGround()
		&& !Movement->IsFalling()
		&& !Movement->IsSwimming()
		&& Movement->Velocity.Size() < 800.0f;
}

void UDustVitalsComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	DUST_SCOPED_TIMER(Dust_Vitals_Tick);

	float HoursPerSecond = 1.0f / 60.0f;
	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		HoursPerSecond = Sky->GetTimeScale() / 60.0f;
	}

	const float DeltaHours = DeltaTime * HoursPerSecond;

	UpdateThermal(DeltaTime, DeltaHours);
	UpdateHydration(DeltaHours);
	UpdateStamina(DeltaTime);
}
