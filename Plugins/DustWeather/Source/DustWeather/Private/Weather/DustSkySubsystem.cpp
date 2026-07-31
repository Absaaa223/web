// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Weather/DustSkySubsystem.h"

#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "World/DustWorldSubsystem.h"

namespace DustSkyCVars
{
	static float TimeScaleOverride = -1.0f;
	static FAutoConsoleVariableRef CVarTimeScale(
		TEXT("dust.sky.TimeScale"),
		TimeScaleOverride,
		TEXT("Override in-game hours per real minute. -1 uses the game's value."),
		ECVF_Cheat);

	static float ForceHour = -1.0f;
	static FAutoConsoleVariableRef CVarForceHour(
		TEXT("dust.sky.ForceHour"),
		ForceHour,
		TEXT("Pin the clock to a given hour (0-24) for lighting work. -1 to release."),
		ECVF_Cheat);
}

UDustSkySubsystem* UDustSkySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustSkySubsystem>() : nullptr;
}

bool UDustSkySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustSkySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The player wakes shortly after dawn. This is a narrative decision, not a
	// technical one: the first thing you see is the sun coming up over an
	// unfamiliar horizon, and the light is doing the work of the opening
	// cinematic we deliberately do not have.
	WorldTime.Day = 0;
	WorldTime.Hour = 6.25f;

	SkyParameters = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/Dust/Materials/Collections/MPC_DustSky.MPC_DustSky"));

	if (!SkyParameters)
	{
		UE_LOG(LogDustWeather, Warning,
			TEXT("MPC_DustSky not found. Sky parameters will not reach materials until the bootstrap commandlet has run."));
	}

	UpdateSkyState();

	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
			{
				SaveSubsystem->RegisterParticipant(this);
			}
		}
	}

	UE_LOG(LogDustWeather, Log, TEXT("Sky subsystem online at day %d, %.2fh."), WorldTime.Day, WorldTime.Hour);
}

void UDustSkySubsystem::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
			{
				SaveSubsystem->UnregisterParticipant(this);
			}
		}
	}

	Super::Deinitialize();
}

TStatId UDustSkySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustSkySubsystem, STATGROUP_Tickables);
}

void UDustSkySubsystem::SetWorldTime(const FDustWorldTime& NewTime)
{
	WorldTime = NewTime;
	UpdateSkyState();
	PushToMaterials();
}

void UDustSkySubsystem::SetTimeScale(float InHoursPerRealMinute)
{
	HoursPerRealMinute = FMath::Clamp(InHoursPerRealMinute, 0.0f, 120.0f);
}

void UDustSkySubsystem::FastForwardTo(float TargetHour, float RealSecondsToTake)
{
	TargetHour = FMath::Fmod(FMath::Max(TargetHour, 0.0f), 24.0f);

	// Always move forward through time; resting until dawn when it is already
	// afternoon means sleeping through the night, not rewinding.
	float HoursToAdvance = TargetHour - WorldTime.Hour;
	if (HoursToAdvance <= 0.0f)
	{
		HoursToAdvance += 24.0f;
	}

	FastForwardHoursRemaining = HoursToAdvance;
	FastForwardRemaining = FMath::Max(RealSecondsToTake, 0.1f);
}

void UDustSkySubsystem::ComputeSunPosition(float& OutElevation, float& OutAzimuth) const
{
	// Seasonal declination: the sun's north-south swing across the year.
	const float YearFraction = FMath::Fmod(static_cast<float>(WorldTime.Day), DaysPerYear) / DaysPerYear;
	const float DeclinationRad = FMath::DegreesToRadians(AxialTiltDegrees)
		* FMath::Sin(2.0f * PI * (YearFraction - 0.22f));

	// Hour angle: 15 degrees per hour, zero at solar noon.
	const float HourAngleRad = FMath::DegreesToRadians(15.0f * (WorldTime.Hour - 12.0f));

	const float LatitudeRad = FMath::DegreesToRadians(LatitudeDegrees);

	// Standard solar position formulae.
	const float SinElevation =
		FMath::Sin(LatitudeRad) * FMath::Sin(DeclinationRad) +
		FMath::Cos(LatitudeRad) * FMath::Cos(DeclinationRad) * FMath::Cos(HourAngleRad);

	const float ElevationRad = FMath::Asin(FMath::Clamp(SinElevation, -1.0f, 1.0f));

	const float CosAzimuth =
		(FMath::Sin(DeclinationRad) - FMath::Sin(ElevationRad) * FMath::Sin(LatitudeRad)) /
		FMath::Max(FMath::Cos(ElevationRad) * FMath::Cos(LatitudeRad), KINDA_SMALL_NUMBER);

	float AzimuthRad = FMath::Acos(FMath::Clamp(CosAzimuth, -1.0f, 1.0f));

	// Before solar noon the sun is in the east; the acos above loses that sign.
	if (HourAngleRad > 0.0f)
	{
		AzimuthRad = 2.0f * PI - AzimuthRad;
	}

	OutElevation = FMath::RadiansToDegrees(ElevationRad);
	OutAzimuth = FMath::RadiansToDegrees(AzimuthRad);
}

void UDustSkySubsystem::ComputeMoonPosition(float& OutElevation, float& OutAzimuth, float& OutPhase) const
{
	// Phase advances once per lunar cycle.
	const double AbsoluteHours = WorldTime.GetAbsoluteHours();
	const double CycleDays = AbsoluteHours / 24.0;

	OutPhase = static_cast<float>(FMath::Frac(CycleDays / static_cast<double>(LunarCycleDays)));

	// The moon lags the sun by the phase: at full moon it is opposite the sun,
	// so it rises as the sun sets. This relationship is the whole reason to
	// model phase at all - a full moon that rose at noon would read as broken
	// even to a player who could not say why.
	const float MoonHourOffset = OutPhase * 24.0f;
	const float MoonHour = FMath::Fmod(WorldTime.Hour - MoonHourOffset + 24.0f, 24.0f);

	const float HourAngleRad = FMath::DegreesToRadians(15.0f * (MoonHour - 12.0f));
	const float LatitudeRad = FMath::DegreesToRadians(LatitudeDegrees);

	// The lunar orbit is inclined ~5 degrees to the ecliptic; folding that in
	// makes the moon's nightly path drift over a cycle rather than repeating.
	const float DeclinationRad = FMath::DegreesToRadians(
		AxialTiltDegrees * FMath::Sin(2.0f * PI * OutPhase) +
		5.14f * FMath::Sin(2.0f * PI * static_cast<float>(FMath::Frac(CycleDays / 27.2f))));

	const float SinElevation =
		FMath::Sin(LatitudeRad) * FMath::Sin(DeclinationRad) +
		FMath::Cos(LatitudeRad) * FMath::Cos(DeclinationRad) * FMath::Cos(HourAngleRad);

	const float ElevationRad = FMath::Asin(FMath::Clamp(SinElevation, -1.0f, 1.0f));

	OutElevation = FMath::RadiansToDegrees(ElevationRad);
	OutAzimuth = FMath::RadiansToDegrees(FMath::Fmod(HourAngleRad + PI, 2.0f * PI));
}

void UDustSkySubsystem::UpdateSkyState()
{
	DUST_SCOPED_TIMER(Dust_Sky_Update);

	// --- Sun ---------------------------------------------------------------
	ComputeSunPosition(SkyState.SunElevation, SkyState.SunAzimuth);

	{
		const float ElevationRad = FMath::DegreesToRadians(SkyState.SunElevation);
		const float AzimuthRad = FMath::DegreesToRadians(SkyState.SunAzimuth);

		// Direction the light travels, i.e. from the sun toward the ground.
		SkyState.SunDirection = -FVector(
			FMath::Cos(ElevationRad) * FMath::Cos(AzimuthRad),
			FMath::Cos(ElevationRad) * FMath::Sin(AzimuthRad),
			FMath::Sin(ElevationRad));
	}

	// --- Moon --------------------------------------------------------------
	float MoonAzimuth = 0.0f;
	ComputeMoonPosition(SkyState.MoonElevation, MoonAzimuth, SkyState.MoonPhase);

	{
		const float ElevationRad = FMath::DegreesToRadians(SkyState.MoonElevation);
		const float AzimuthRad = FMath::DegreesToRadians(MoonAzimuth);

		SkyState.MoonDirection = -FVector(
			FMath::Cos(ElevationRad) * FMath::Cos(AzimuthRad),
			FMath::Cos(ElevationRad) * FMath::Sin(AzimuthRad),
			FMath::Sin(ElevationRad));
	}

	// Illuminated fraction of the lunar disc: a cosine of the phase angle.
	const float IlluminatedFraction = (1.0f - FMath::Cos(2.0f * PI * SkyState.MoonPhase)) * 0.5f;

	// Below the horizon the moon contributes nothing; near it, atmospheric
	// extinction cuts it down sharply.
	const float MoonAltitudeFactor = FMath::Clamp(SkyState.MoonElevation / 25.0f, 0.0f, 1.0f);

	SkyState.MoonIllumination = IlluminatedFraction * MoonAltitudeFactor;

	// --- Stars -------------------------------------------------------------
	// One full rotation per sidereal day. The band sweeps overhead over the
	// course of a night, which is what makes it usable as a clock.
	SkyState.StarFieldRotation = FMath::Fmod(
		static_cast<float>(WorldTime.GetAbsoluteHours()) * (360.0f / 23.934f), 360.0f);

	{
		// Stars fade in through astronomical twilight (sun below -18 degrees is
		// full darkness) and are washed out by a bright moon.
		const float TwilightFade = 1.0f - FMath::GetMappedRangeValueClamped(
			FVector2f(-18.0f, 0.0f), FVector2f(0.0f, 1.0f), SkyState.SunElevation);

		const float MoonWash = 1.0f - SkyState.MoonIllumination * 0.65f;

		SkyState.StarVisibility = FMath::Clamp(TwilightFade * MoonWash, 0.0f, 1.0f);
	}

	// --- Time of day tag ---------------------------------------------------
	// Other systems switch on this rather than reading raw elevation, so the
	// thresholds live in exactly one place.
	if (SkyState.SunElevation < -0.5f)
	{
		SkyState.TimeOfDayTag = TAG_Dust_Time_Night;
	}
	else if (SkyState.SunElevation < 10.0f)
	{
		SkyState.TimeOfDayTag = (WorldTime.Hour < 12.0f) ? TAG_Dust_Time_Dawn : TAG_Dust_Time_Dusk;
	}
	else
	{
		SkyState.TimeOfDayTag = TAG_Dust_Time_Day;
	}

	SkyState.bGoldenHour = SkyState.SunElevation > -4.0f && SkyState.SunElevation < 9.0f;

	// --- Temperature -------------------------------------------------------
	// A desert's daily swing is enormous - 45 C at mid-afternoon, near freezing
	// before dawn. The peak lags solar noon by roughly two hours because the
	// ground keeps absorbing after the sun starts descending; that lag is why
	// the hottest, most punishing part of a DUST day is mid-afternoon rather
	// than noon, and why late afternoon shade actually matters.
	{
		const float ThermalLagHours = 2.2f;
		const float LaggedHour = FMath::Fmod(WorldTime.Hour - ThermalLagHours + 24.0f, 24.0f);
		const float DailyCurve = -FMath::Cos(2.0f * PI * (LaggedHour / 24.0f));

		constexpr float MeanTemperature = 24.0f;
		constexpr float DailySwing = 21.0f;

		SkyState.TemperatureCelsius = MeanTemperature + DailyCurve * DailySwing;
	}
}

void UDustSkySubsystem::PushToMaterials()
{
	if (!SkyParameters)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Materials read these rather than querying the subsystem, so that sky,
	// ground, flora, water and particle materials all agree without any of them
	// needing a blueprint tick.
	UKismetMaterialLibrary::SetVectorParameterValue(World, SkyParameters,
		TEXT("SunDirection"), FLinearColor(SkyState.SunDirection));
	UKismetMaterialLibrary::SetVectorParameterValue(World, SkyParameters,
		TEXT("MoonDirection"), FLinearColor(SkyState.MoonDirection));

	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("SunElevation"), SkyState.SunElevation);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("MoonIllumination"), SkyState.MoonIllumination);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("MoonPhase"), SkyState.MoonPhase);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("StarFieldRotation"), SkyState.StarFieldRotation);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("StarVisibility"), SkyState.StarVisibility);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("TimeOfDay"), WorldTime.Hour);
	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("Temperature"), SkyState.TemperatureCelsius);

	// Heat shimmer is driven from here so that every material that wants it -
	// ground, distant rock, the horizon card - distorts by the same amount.
	// It peaks with ground temperature, not with sun elevation, which is why
	// the air over the salt flat still boils an hour before sunset.
	const float ShimmerStrength = FMath::GetMappedRangeValueClamped(
		FVector2f(28.0f, 46.0f), FVector2f(0.0f, 1.0f), SkyState.TemperatureCelsius);

	UKismetMaterialLibrary::SetScalarParameterValue(World, SkyParameters,
		TEXT("HeatShimmer"), ShimmerStrength);
}

float UDustSkySubsystem::GetAmbientLightLevel() const
{
	// Daylight dominates when the sun is up; twilight is a smooth ramp; night
	// is lit only by the moon and by a small floor of starlight and airglow.
	const float SunContribution = FMath::GetMappedRangeValueClamped(
		FVector2f(-8.0f, 12.0f), FVector2f(0.0f, 1.0f), SkyState.SunElevation);

	// The floor is deliberately very low. DUST's nights are meant to be
	// genuinely dark - dark enough that a new-moon night changes how you play
	// rather than just tinting the screen blue.
	constexpr float StarlightFloor = 0.012f;

	return FMath::Clamp(
		FMath::Max(SunContribution, SkyState.MoonIllumination * 0.14f) + StarlightFloor,
		0.0f, 1.0f);
}

float UDustSkySubsystem::GetSolarLoadAt(const FVector& WorldLocation) const
{
	if (SkyState.SunElevation <= 0.0f)
	{
		return 0.0f;
	}

	// Air mass: sunlight passing obliquely through the atmosphere is
	// attenuated, which is why the sun at 10 degrees does not cook you the way
	// the sun at 80 degrees does.
	const float ElevationRad = FMath::DegreesToRadians(FMath::Max(SkyState.SunElevation, 3.0f));
	const float AirMass = 1.0f / FMath::Max(FMath::Sin(ElevationRad), 0.05f);
	const float Attenuation = FMath::Exp(-0.14f * (AirMass - 1.0f));

	float Load = FMath::Sin(ElevationRad) * Attenuation;

	// Terrain shadowing: march toward the sun and see whether anything blocks
	// it. Six samples over 400 m is enough to catch canyon walls and mesas,
	// which are the shade that actually matters to a player managing heat.
	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		const FVector ToSun = -SkyState.SunDirection;
		constexpr int32 ShadowSamples = 6;
		constexpr double MarchDistance = 40000.0;

		for (int32 Index = 1; Index <= ShadowSamples; ++Index)
		{
			const double T = (static_cast<double>(Index) / ShadowSamples) * MarchDistance;
			const FVector Probe = WorldLocation + ToSun * T;

			if (WorldSubsystem->SampleHeight(Probe) > Probe.Z)
			{
				// Occluded. Shade is not total - the sky still contributes -
				// but it is the difference between survivable and not.
				Load *= 0.18f;
				break;
			}
		}
	}

	return FMath::Clamp(Load, 0.0f, 1.0f);
}

void UDustSkySubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Sky_Tick);

	// --- Advance the clock --------------------------------------------------
	if (DustSkyCVars::ForceHour >= 0.0f)
	{
		WorldTime.Hour = FMath::Fmod(DustSkyCVars::ForceHour, 24.0f);
	}
	else if (FastForwardRemaining > 0.0f)
	{
		// Rest: consume the remaining hours proportionally to the remaining
		// real time, so the sky sweeps at a constant visible rate.
		const float Fraction = FMath::Min(DeltaTime / FastForwardRemaining, 1.0f);
		const float HoursThisFrame = FastForwardHoursRemaining * Fraction;

		WorldTime.Advance(HoursThisFrame);

		FastForwardHoursRemaining -= HoursThisFrame;
		FastForwardRemaining -= DeltaTime;

		if (FastForwardRemaining <= 0.0f)
		{
			WorldTime.Advance(FastForwardHoursRemaining);
			FastForwardHoursRemaining = 0.0f;
			FastForwardRemaining = 0.0f;
		}
	}
	else
	{
		const float Scale = (DustSkyCVars::TimeScaleOverride >= 0.0f)
			? DustSkyCVars::TimeScaleOverride
			: HoursPerRealMinute;

		WorldTime.Advance((DeltaTime / 60.0f) * Scale);
	}

	// --- Recompute at a fixed rate -----------------------------------------
	// The sun moves 0.25 degrees per in-game minute. At 15 Hz the largest
	// possible step is far below anything visible, and it saves the trig on
	// three quarters of frames.
	UpdateAccumulator += DeltaTime;
	if (UpdateAccumulator >= UpdateInterval || IsFastForwarding())
	{
		UpdateAccumulator = 0.0f;
		UpdateSkyState();
		PushToMaterials();
	}
}

void UDustSkySubsystem::OnDustSave(UDustSaveGame& Save)
{
	Save.WorldTime = WorldTime;
}

void UDustSkySubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	SetWorldTime(Save.WorldTime);
}
