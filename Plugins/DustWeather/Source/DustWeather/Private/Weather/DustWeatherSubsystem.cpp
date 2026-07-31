// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Weather/DustWeatherSubsystem.h"

#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Math/DustNoise.h"
#include "Weather/DustSkySubsystem.h"
#include "World/DustRegionData.h"
#include "World/DustSandDeformation.h"
#include "World/DustWorldSubsystem.h"

namespace DustWeatherCVars
{
	static int32 FreezeWeather = 0;
	static FAutoConsoleVariableRef CVarFreezeWeather(
		TEXT("dust.weather.Freeze"),
		FreezeWeather,
		TEXT("Stop weather transitions. Useful for lighting and VFX work."),
		ECVF_Cheat);

	static float DuneMigrationScale = 1.0f;
	static FAutoConsoleVariableRef CVarDuneMigration(
		TEXT("dust.weather.DuneMigrationScale"),
		DuneMigrationScale,
		TEXT("Multiplier on how fast dunes travel downwind. 0 freezes them, 200 makes the effect visible in seconds."),
		ECVF_Cheat);
}

UDustWeatherSubsystem* UDustWeatherSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustWeatherSubsystem>() : nullptr;
}

bool UDustWeatherSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustWeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The world subsystem must exist before us: we push wind direction into it.
	Collection.InitializeDependency<UDustWorldSubsystem>();

	LoadPresets();

	WeatherParameters = LoadObject<UMaterialParameterCollection>(
		nullptr, TEXT("/Game/Dust/Materials/Collections/MPC_DustWeather.MPC_DustWeather"));

	// Start clear. The player's first hour should be legible; the world earns
	// the right to obscure itself only after it has shown itself once.
	CurrentPreset = FindPreset(TAG_Dust_Weather_Clear);
	if (!CurrentPreset && Presets.Num() > 0)
	{
		CurrentPreset = Presets.CreateConstIterator()->Value;
	}

	if (CurrentPreset)
	{
		BlendedWindSpeed      = CurrentPreset->WindSpeed;
		BlendedGustMultiplier = CurrentPreset->GustMultiplier;
		BlendedGustFrequency  = CurrentPreset->GustFrequency;
		BlendedParticulate    = CurrentPreset->Particulate;
		BlendedVisibility     = CurrentPreset->VisibilityDistance;
		BlendedCloudCoverage  = CurrentPreset->CloudCoverage;
		BlendedRain           = CurrentPreset->RainIntensity;
		HoursUntilNextChange  = CurrentPreset->TypicalDurationHours;
	}

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

	UE_LOG(LogDustWeather, Log, TEXT("Weather subsystem online with %d presets."), Presets.Num());
}

void UDustWeatherSubsystem::Deinitialize()
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

TStatId UDustWeatherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustWeatherSubsystem, STATGROUP_Tickables);
}

void UDustWeatherSubsystem::LoadPresets()
{
	// Presets are discovered rather than hard-referenced so that adding a new
	// weather condition is a content operation with no code change.
	if (UAssetManager* AssetManager = UAssetManager::GetIfInitialized())
	{
		TArray<FAssetData> AssetList;
		AssetManager->GetPrimaryAssetDataList(FPrimaryAssetType(TEXT("DustWeatherPreset")), AssetList);

		for (const FAssetData& Data : AssetList)
		{
			if (UDustWeatherPreset* Preset = Cast<UDustWeatherPreset>(Data.GetAsset()))
			{
				if (Preset->WeatherTag.IsValid())
				{
					Presets.Add(Preset->WeatherTag, Preset);
				}
			}
		}
	}

	if (Presets.Num() == 0)
	{
		UE_LOG(LogDustWeather, Warning,
			TEXT("No weather presets found. Run the DustBootstrap commandlet to generate the defaults."));
	}
}

const UDustWeatherPreset* UDustWeatherSubsystem::FindPreset(FGameplayTag Tag) const
{
	const TObjectPtr<UDustWeatherPreset>* Found = Presets.Find(Tag);
	return Found ? Found->Get() : nullptr;
}

FGameplayTag UDustWeatherSubsystem::GetCurrentWeatherTag() const
{
	// During a transition, report whichever side dominates. Systems that react
	// to weather should flip once, at the halfway point, rather than at the
	// start of a ninety-second blend when nothing has visibly changed yet.
	if (TransitionAlpha > 0.5f && IncomingPreset)
	{
		return IncomingPreset->WeatherTag;
	}
	return CurrentPreset ? CurrentPreset->WeatherTag : FGameplayTag();
}

FGameplayTag UDustWeatherSubsystem::ChooseNextWeather() const
{
	if (!CurrentPreset)
	{
		return FGameplayTag();
	}

	// Regional bias: the region under the player weights the roll. This is the
	// whole mechanism behind "each region feels different weather-wise" and it
	// requires no per-region scripting at all.
	const UDustRegionData* Region = nullptr;
	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		Region = WorldSubsystem->FindRegionData(WorldSubsystem->GetCurrentRegionId());
	}

	struct FCandidate
	{
		FGameplayTag Tag;
		float Weight;
	};

	TArray<FCandidate, TInlineAllocator<8>> Candidates;
	float TotalWeight = 0.0f;

	for (const TPair<FGameplayTag, float>& Transition : CurrentPreset->TransitionWeights)
	{
		const UDustWeatherPreset* Candidate = FindPreset(Transition.Key);
		if (!Candidate)
		{
			continue;
		}

		float Weight = Transition.Value * Candidate->BaseLikelihood;

		if (Region)
		{
			if (const float* Bias = Region->WeatherBias.Find(Transition.Key))
			{
				Weight *= *Bias;
			}
		}

		if (Weight > 0.0f)
		{
			Candidates.Add({ Transition.Key, Weight });
			TotalWeight += Weight;
		}
	}

	if (Candidates.Num() == 0 || TotalWeight <= 0.0f)
	{
		// Nothing is reachable from here - stay put rather than snapping to a
		// condition the designer did not sanction.
		return CurrentPreset->WeatherTag;
	}

	// Seed the roll from the world clock so a reloaded save does not reroll to
	// a different outcome, which would make weather feel arbitrary across a
	// death-and-reload.
	uint32 RollSeed = 0;
	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		RollSeed = FDustNoise::Hash(static_cast<uint32>(Sky->GetWorldTime().GetAbsoluteHours() * 16.0));
	}
	else
	{
		RollSeed = FDustNoise::Hash(static_cast<uint32>(FPlatformTime::Cycles()));
	}

	float Roll = FDustNoise::HashToUnit(RollSeed) * TotalWeight;

	for (const FCandidate& Candidate : Candidates)
	{
		Roll -= Candidate.Weight;
		if (Roll <= 0.0f)
		{
			return Candidate.Tag;
		}
	}

	return Candidates.Last().Tag;
}

void UDustWeatherSubsystem::ForceWeather(FGameplayTag WeatherTag, bool bInstant)
{
	const UDustWeatherPreset* Target = FindPreset(WeatherTag);
	if (!Target)
	{
		UE_LOG(LogDustWeather, Warning, TEXT("No preset for weather tag '%s'."), *WeatherTag.ToString());
		return;
	}

	const FGameplayTag OldTag = GetCurrentWeatherTag();

	IncomingPreset = Target;
	TransitionAlpha = 0.0f;
	TransitionDuration = bInstant ? 0.05f : Target->TransitionSeconds;

	OnWeatherChanging.Broadcast(WeatherTag, OldTag);

	UE_LOG(LogDustWeather, Log, TEXT("Weather changing: %s -> %s over %.0fs."),
		*OldTag.ToString(), *WeatherTag.ToString(), TransitionDuration);
}

void UDustWeatherSubsystem::UpdateTransition(float DeltaSeconds)
{
	if (!IncomingPreset || !CurrentPreset)
	{
		return;
	}

	TransitionAlpha = FMath::Clamp(
		TransitionAlpha + DeltaSeconds / FMath::Max(TransitionDuration, KINDA_SMALL_NUMBER),
		0.0f, 1.0f);

	// Smoothstep the blend so weather does not arrive at a constant rate; the
	// acceleration in the middle is what makes an incoming storm feel like it
	// is bearing down rather than fading in.
	const float T = FMath::SmoothStep(0.0f, 1.0f, TransitionAlpha);

	BlendedWindSpeed      = FMath::Lerp(CurrentPreset->WindSpeed,          IncomingPreset->WindSpeed,          T);
	BlendedGustMultiplier = FMath::Lerp(CurrentPreset->GustMultiplier,     IncomingPreset->GustMultiplier,     T);
	BlendedGustFrequency  = FMath::Lerp(CurrentPreset->GustFrequency,      IncomingPreset->GustFrequency,      T);
	BlendedParticulate    = FMath::Lerp(CurrentPreset->Particulate,        IncomingPreset->Particulate,        T);
	BlendedCloudCoverage  = FMath::Lerp(CurrentPreset->CloudCoverage,      IncomingPreset->CloudCoverage,      T);
	BlendedRain           = FMath::Lerp(CurrentPreset->RainIntensity,      IncomingPreset->RainIntensity,      T);

	// Visibility blends geometrically: perceptually, going from 12 km to 6 km
	// is the same size of change as going from 6 km to 3 km, and a linear lerp
	// would spend most of the transition in a range nobody can tell apart.
	BlendedVisibility = FMath::Exp(FMath::Lerp(
		FMath::Loge(FMath::Max(CurrentPreset->VisibilityDistance, 1.0f)),
		FMath::Loge(FMath::Max(IncomingPreset->VisibilityDistance, 1.0f)),
		T));

	if (TransitionAlpha >= 1.0f)
	{
		CurrentPreset = IncomingPreset;
		IncomingPreset = nullptr;
		TransitionAlpha = 0.0f;
		HoursUntilNextChange = CurrentPreset->TypicalDurationHours;

		// Vary the duration so weather is not metronomic.
		HoursUntilNextChange *= FMath::FRandRange(0.6f, 1.6f);

		LightningCountdown = CurrentPreset->LightningInterval;
	}
}

void UDustWeatherSubsystem::UpdateWindDirection(float DeltaSeconds)
{
	// The prevailing wind drifts. It must drift *slowly*: dune orientation is
	// derived from it, so a fast swing would visibly rotate the entire desert.
	// At this rate a full reversal takes several in-game days, which reads as
	// a season changing rather than as a bug.
	constexpr float DriftRatePerSecond = 0.0022f;

	if (FVector::DotProduct(PrevailingWindDirection, TargetWindDirection) > 0.995f)
	{
		// Reached the target; pick a new one within a limited arc so the wind
		// wanders rather than teleporting to the opposite quarter.
		const float ArcRadians = FMath::DegreesToRadians(FMath::FRandRange(25.0f, 70.0f));
		const float Sign = FMath::RandBool() ? 1.0f : -1.0f;

		TargetWindDirection = PrevailingWindDirection.RotateAngleAxisRad(
			ArcRadians * Sign, FVector::UpVector);
	}

	PrevailingWindDirection = FMath::VInterpNormalRotationTo(
		PrevailingWindDirection, TargetWindDirection, DeltaSeconds, DriftRatePerSecond * 60.0f);

	PrevailingWindDirection.Z = 0.0;
	PrevailingWindDirection.Normalize();

	if (UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		WorldSubsystem->SetPrevailingWind(
			FVector2D(PrevailingWindDirection.X, PrevailingWindDirection.Y));
	}
}

void UDustWeatherSubsystem::UpdateDuneMigration(float DeltaSeconds)
{
	UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this);
	if (!WorldSubsystem)
	{
		return;
	}

	// Sand transport rate scales roughly with the cube of wind speed above a
	// threshold - this is the Bagnold relation, and it is why a light breeze
	// moves nothing at all while a storm reshapes a dune field overnight.
	constexpr float ThresholdSpeed = 450.0f; // cm/s: below this, sand does not saltate

	const float Excess = FMath::Max(0.0f, BlendedWindSpeed - ThresholdSpeed);
	if (Excess <= 0.0f)
	{
		return;
	}

	// Tuned so a full sandstorm moves the dune train about 40 cm per real
	// second: visible if you sit and watch a crest, dramatic over an afternoon.
	constexpr double TransportCoefficient = 1.9e-8;

	const double MigrationThisFrame =
		TransportCoefficient * FMath::Pow(static_cast<double>(Excess), 3.0)
		* DeltaSeconds * DustWeatherCVars::DuneMigrationScale;

	PendingDuneMigration += MigrationThisFrame;

	// Applying migration invalidates the sampler cache, so we batch it up and
	// apply once per second rather than every frame. At these rates the
	// quantisation is well below a centimetre of visible motion.
	if (PendingDuneMigration >= 1.0)
	{
		WorldSubsystem->AdvanceDuneMigration(PendingDuneMigration);
		PendingDuneMigration = 0.0;
	}
}

void UDustWeatherSubsystem::UpdateLightning(float DeltaSeconds)
{
	const float Interval = CurrentPreset ? CurrentPreset->LightningInterval : 0.0f;
	if (Interval <= 0.0f)
	{
		return;
	}

	LightningCountdown -= DeltaSeconds;
	if (LightningCountdown > 0.0f)
	{
		return;
	}

	LightningCountdown = Interval * FMath::FRandRange(0.4f, 1.8f);

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	// Lightning strikes high ground. We sample a ring of candidate points
	// around the player and pick the highest, which puts strikes on the ridges
	// and mesas the player can see rather than at random in the flat.
	const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this);
	if (!WorldSubsystem)
	{
		return;
	}

	const FVector PlayerLocation = Pawn->GetActorLocation();

	FVector BestStrike = PlayerLocation;
	double BestHeight = -TNumericLimits<double>::Max();

	constexpr int32 CandidateCount = 12;
	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
		const float Distance = FMath::FRandRange(20000.0f, 220000.0f);

		const FVector Candidate = PlayerLocation +
			FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Distance;

		const double Height = WorldSubsystem->SampleHeight(Candidate);
		if (Height > BestHeight)
		{
			BestHeight = Height;
			BestStrike = FVector(Candidate.X, Candidate.Y, Height);
		}
	}

	OnLightningStrike.Broadcast(BestStrike);
}

FDustWindSample UDustWeatherSubsystem::SampleWind(const FVector& WorldLocation) const
{
	FDustWindSample Sample;
	Sample.Direction = PrevailingWindDirection;
	Sample.Particulate = BlendedParticulate;

	float Speed = BlendedWindSpeed;

	// --- Regional modifier --------------------------------------------------
	if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
	{
		const FDustWorldSampler& Sampler = WorldSubsystem->GetSampler();

		float RegionWeight = 0.0f;
		const int32 RegionIndex = Sampler.SampleDominantRegion(
			FVector2D(WorldLocation.X, WorldLocation.Y), RegionWeight);

		if (const FDustRegionSnapshot* Region = Sampler.GetRegion(RegionIndex))
		{
			Speed *= Region->WindMultiplier;
		}

		// --- Terrain acceleration and shelter -------------------------------
		// Wind speeds up over a crest and stalls in the lee. We approximate
		// this from the terrain gradient along the wind direction: ground
		// rising into the wind accelerates the flow, ground falling away from
		// it creates a wind shadow.
		//
		// This is the single detail that makes standing behind a dune feel like
		// shelter, which in turn makes shelter a real tactical consideration
		// during a storm.
		constexpr double ProbeDistance = 3000.0;

		const FVector2D Here(WorldLocation.X, WorldLocation.Y);
		const FVector2D Upwind = Here - FVector2D(PrevailingWindDirection.X, PrevailingWindDirection.Y) * ProbeDistance;

		const double HereHeight = Sampler.SampleHeight(Here);
		const double UpwindHeight = Sampler.SampleHeight(Upwind);

		const double Rise = HereHeight - UpwindHeight;
		const double Gradient = Rise / ProbeDistance;

		// +40% at a steep windward face, -70% deep in a lee.
		const float TerrainFactor = FMath::Clamp(
			1.0f + static_cast<float>(Gradient) * 1.4f, 0.3f, 1.4f);

		Speed *= TerrainFactor;

		// Height above ground: the boundary layer means wind is slower near the
		// surface. A paraglider fifty metres up catches noticeably more.
		const double HeightAboveGround = WorldLocation.Z - HereHeight;
		const float AltitudeFactor = 1.0f + FMath::Clamp(
			static_cast<float>(HeightAboveGround) / 20000.0f, 0.0f, 0.55f);

		Speed *= AltitudeFactor;
	}

	Sample.Speed = Speed;

	// --- Gusting ------------------------------------------------------------
	// Two out-of-phase sine components plus a spatial noise term, so gusts move
	// across the landscape as coherent fronts rather than every blade of grass
	// twitching independently.
	{
		const FVector2f NoiseP(
			static_cast<float>(WorldLocation.X / 60000.0),
			static_cast<float>(WorldLocation.Y / 60000.0));

		const float SpatialGust = FDustNoise::ToUnitRange(
			FDustNoise::Simplex2D(NoiseP + FVector2f(GustPhase * 0.4f, 0.0f), 0x6057u));

		const float TemporalGust =
			0.6f * FMath::Sin(GustPhase * 2.0f * PI) +
			0.4f * FMath::Sin(GustPhase * 2.0f * PI * 2.7f + 1.3f);

		const float GustAmount = FMath::Clamp(
			SpatialGust * 0.6f + (TemporalGust * 0.5f + 0.5f) * 0.4f, 0.0f, 1.0f);

		Sample.GustSpeed = Speed * FMath::Lerp(1.0f, BlendedGustMultiplier, GustAmount);
	}

	return Sample;
}

void UDustWeatherSubsystem::PushToMaterials()
{
	if (!WeatherParameters)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UKismetMaterialLibrary::SetVectorParameterValue(World, WeatherParameters,
		TEXT("WindDirection"), FLinearColor(PrevailingWindDirection));

	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("WindSpeed"), BlendedWindSpeed);
	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("Particulate"), BlendedParticulate);
	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("Visibility"), BlendedVisibility);
	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("CloudCoverage"), BlendedCloudCoverage);
	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("RainIntensity"), BlendedRain);

	// Surface wetness lags rain: puddles form over a minute and dry over many.
	// Materials read this rather than RainIntensity so that the world stays wet
	// for a while after a shower passes, which is most of what sells rain in a
	// place that almost never gets any.
	static float Wetness = 0.0f;
	const float TargetWetness = FMath::Clamp(BlendedRain * 1.4f, 0.0f, 1.0f);
	const float WetnessRate = (TargetWetness > Wetness) ? 0.012f : 0.0016f;
	Wetness = FMath::FInterpConstantTo(Wetness, TargetWetness, World->GetDeltaSeconds(), WetnessRate * 60.0f);

	UKismetMaterialLibrary::SetScalarParameterValue(World, WeatherParameters,
		TEXT("SurfaceWetness"), Wetness);
}

void UDustWeatherSubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Weather_Tick);

	GustPhase += DeltaTime * BlendedGustFrequency;

	if (DustWeatherCVars::FreezeWeather == 0)
	{
		if (IncomingPreset)
		{
			UpdateTransition(DeltaTime);
		}
		else if (CurrentPreset)
		{
			// Count down in game time, not real time, so weather duration
			// tracks the day/night cycle regardless of the time scale setting.
			float HoursPerSecond = 1.0f / 60.0f;
			if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
			{
				HoursPerSecond = Sky->GetTimeScale() / 60.0f;
			}

			HoursUntilNextChange -= DeltaTime * HoursPerSecond;

			if (HoursUntilNextChange <= 0.0f)
			{
				const FGameplayTag Next = ChooseNextWeather();
				if (Next.IsValid() && Next != CurrentPreset->WeatherTag)
				{
					ForceWeather(Next, /*bInstant=*/false);
				}
				else
				{
					HoursUntilNextChange = CurrentPreset->TypicalDurationHours * 0.5f;
				}
			}
		}
	}

	UpdateWindDirection(DeltaTime);
	UpdateDuneMigration(DeltaTime);
	UpdateLightning(DeltaTime);
	PushToMaterials();

	// Feed the sand deformation system so footprints heal at a rate that
	// matches what the player can see and hear the wind doing.
	if (UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
	{
		Sand->SetWindSpeed(BlendedWindSpeed);
	}
}

void UDustWeatherSubsystem::OnDustSave(UDustSaveGame& Save)
{
	Save.ActiveWeatherTag = GetCurrentWeatherTag();

	// Note: Save.WeatherPhase carries the dune migration phase, written by the
	// world subsystem. Weather's own transition state is deliberately not
	// persisted - reloading into the middle of a ninety-second blend would be
	// indistinguishable from reloading into the settled condition.
}

void UDustWeatherSubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	if (Save.ActiveWeatherTag.IsValid())
	{
		if (const UDustWeatherPreset* Preset = FindPreset(Save.ActiveWeatherTag))
		{
			CurrentPreset = Preset;
			IncomingPreset = nullptr;
			TransitionAlpha = 0.0f;

			BlendedWindSpeed      = Preset->WindSpeed;
			BlendedGustMultiplier = Preset->GustMultiplier;
			BlendedGustFrequency  = Preset->GustFrequency;
			BlendedParticulate    = Preset->Particulate;
			BlendedVisibility     = Preset->VisibilityDistance;
			BlendedCloudCoverage  = Preset->CloudCoverage;
			BlendedRain           = Preset->RainIntensity;
			HoursUntilNextChange  = Preset->TypicalDurationHours;
		}
	}
}
