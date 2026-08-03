// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustBootstrapCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Fauna/DustSpeciesData.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Weather/DustWeatherSubsystem.h"
#include "World/DustRegionData.h"

namespace DustBootstrap
{
	static const TCHAR* WorldPath    = TEXT("/Game/Dust/World");
	static const TCHAR* RegionPath   = TEXT("/Game/Dust/World/Regions");
	static const TCHAR* WeatherPath  = TEXT("/Game/Dust/Weather/Presets");
	static const TCHAR* MaterialPath = TEXT("/Game/Dust/Materials/Collections");
	static const TCHAR* FaunaPath    = TEXT("/Game/Dust/Fauna/Species");

	/** One kilometre in world units. Every placement below is written in km. */
	static constexpr double Km = 100000.0;

	/** Creates or loads a package-backed asset. Returns null if it exists and !bForce. */
	template <typename TAsset>
	TAsset* CreateAsset(const FString& Directory, const FString& AssetName, bool bForce)
	{
		const FString PackageName = Directory / AssetName;

		if (!bForce && FPackageName::DoesPackageExist(PackageName))
		{
			UE_LOG(LogDust, Verbose, TEXT("Skipping existing asset '%s'."), *PackageName);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogDust, Error, TEXT("Failed to create package '%s'."), *PackageName);
			return nullptr;
		}

		Package->FullyLoad();

		TAsset* Asset = NewObject<TAsset>(
			Package, TAsset::StaticClass(), *AssetName, RF_Public | RF_Standalone);

		return Asset;
	}
}

UDustBootstrapCommandlet::UDustBootstrapCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

bool UDustBootstrapCommandlet::SavePackage(UObject* Asset)
{
	if (!Asset)
	{
		return false;
	}

	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Asset);

	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	const bool bSaved = UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);

	UE_CLOG(bSaved, LogDust, Display, TEXT("Created %s"), *Package->GetName());
	UE_CLOG(!bSaved, LogDust, Error, TEXT("Failed to save %s"), *Package->GetName());

	return bSaved;
}

UDustRegionData* UDustBootstrapCommandlet::MakeRegion(
	const FString& AssetName,
	FName RegionId,
	const FText& DisplayName,
	const FString& DesignIntent,
	const FVector2D& Anchor,
	int32 Priority,
	EDustTerrainProfile Profile,
	bool bForce)
{
	using namespace DustBootstrap;

	UDustRegionData* Region = CreateAsset<UDustRegionData>(RegionPath, AssetName, bForce);
	if (!Region)
	{
		return nullptr;
	}

	Region->RegionId       = RegionId;
	Region->DisplayName    = DisplayName;
	Region->DesignIntent   = DesignIntent;
	Region->AnchorLocation = Anchor;
	Region->Priority       = Priority;
	Region->Profile        = Profile;

	// Defaults per profile. These are the values documented in WORLD_BIBLE.md,
	// and they are chosen so that a bootstrapped world is genuinely playable
	// rather than merely non-crashing.
	FDustTerrainParams& Terrain = Region->TerrainParams;

	switch (Profile)
	{
	case EDustTerrainProfile::DuneSea:
		Region->CoreRadius = 2.2 * Km * 10.0;
		Region->FalloffRadius = 0.9 * Km * 10.0;
		Region->BoundaryWarp = 0.55 * Km * 10.0;
		Terrain.BaseElevation = 0.0;
		Terrain.Amplitude = 12000.0;
		Terrain.FeatureScale = 45000.0;
		Terrain.LooseDepth = 220.0f;
		Terrain.DuneInfluence = 1.0f;
		Terrain.PrimarySurface = EDustSurface::LooseSand;
		Terrain.SteepSurface = EDustSurface::PackedSand;
		Region->PoiDensityPerKm2 = 0.6f;
		Region->WindMultiplier = 1.15f;
		Region->ResidentSpecies = { TEXT("Sandswimmer"), TEXT("Duneglider"), TEXT("PaleFennec") };
		break;

	case EDustTerrainProfile::SaltFlat:
		Region->CoreRadius = 1.3 * Km * 10.0;
		Region->FalloffRadius = 0.5 * Km * 10.0;
		Region->BoundaryWarp = 0.30 * Km * 10.0;
		Terrain.BaseElevation = -8000.0;
		Terrain.Amplitude = 400.0;
		Terrain.FeatureScale = 4200.0;
		Terrain.LooseDepth = 6.0f;
		Terrain.DuneInfluence = 0.05f;
		Terrain.PrimarySurface = EDustSurface::Salt;
		Terrain.SteepSurface = EDustSurface::Clay;
		Region->PoiDensityPerKm2 = 0.4f;
		Region->TemperatureOffset = 3.0f;
		break;

	case EDustTerrainProfile::Canyon:
		Region->CoreRadius = 1.1 * Km * 10.0;
		Region->FalloffRadius = 0.45 * Km * 10.0;
		Region->BoundaryWarp = 0.28 * Km * 10.0;
		Terrain.BaseElevation = 26000.0;
		Terrain.Amplitude = 34000.0;
		Terrain.FeatureScale = 62000.0;
		Terrain.WarpStrength = 0.75f;
		Terrain.LooseDepth = 45.0f;
		Terrain.DuneInfluence = 0.2f;
		Terrain.PrimarySurface = EDustSurface::PackedSand;
		Terrain.SteepSurface = EDustSurface::Rock;
		Terrain.SteepSurfaceAngle = 30.0f;
		Region->PoiDensityPerKm2 = 5.0f;
		Region->WindMultiplier = 0.55f;
		Region->TemperatureOffset = -4.0f;
		Region->ResidentSpecies = { TEXT("Kite"), TEXT("CanyonGrazer"), TEXT("Nightstalker") };
		break;

	case EDustTerrainProfile::Mesa:
		Region->CoreRadius = 1.2 * Km * 10.0;
		Region->FalloffRadius = 0.5 * Km * 10.0;
		Region->BoundaryWarp = 0.32 * Km * 10.0;
		Terrain.BaseElevation = 14000.0;
		Terrain.Amplitude = 32000.0;
		Terrain.FeatureScale = 95000.0;
		Terrain.LooseDepth = 60.0f;
		Terrain.DuneInfluence = 0.35f;
		Terrain.PrimarySurface = EDustSurface::PackedSand;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 3.5f;
		Region->ResidentSpecies = { TEXT("Kite") };
		break;

	case EDustTerrainProfile::Mountains:
		Region->CoreRadius = 1.6 * Km * 10.0;
		Region->FalloffRadius = 0.8 * Km * 10.0;
		Region->BoundaryWarp = 0.45 * Km * 10.0;
		Terrain.BaseElevation = 45000.0;
		Terrain.Amplitude = 230000.0;
		Terrain.FeatureScale = 140000.0;
		Terrain.DetailOctaves = 7;
		Terrain.WarpStrength = 0.55f;
		Terrain.LooseDepth = 12.0f;
		Terrain.DuneInfluence = 0.05f;
		Terrain.PrimarySurface = EDustSurface::Rock;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 3.0f;
		Region->WindMultiplier = 1.4f;
		Region->TemperatureOffset = -12.0f;
		Region->ResidentSpecies = { TEXT("Ridgewalker"), TEXT("Kite") };
		break;

	case EDustTerrainProfile::Basin:
		Region->CoreRadius = 1.0 * Km * 10.0;
		Region->FalloffRadius = 0.6 * Km * 10.0;
		Region->BoundaryWarp = 0.30 * Km * 10.0;
		Terrain.BaseElevation = -4000.0;
		Terrain.Amplitude = 16000.0;
		Terrain.FeatureScale = 55000.0;
		Terrain.LooseDepth = 90.0f;
		Terrain.DuneInfluence = 0.4f;
		Terrain.PrimarySurface = EDustSurface::Clay;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 2.5f;
		Region->WindMultiplier = 0.6f;
		break;

	case EDustTerrainProfile::Volcanic:
		Region->CoreRadius = 1.0 * Km * 10.0;
		Region->FalloffRadius = 0.45 * Km * 10.0;
		Region->BoundaryWarp = 0.26 * Km * 10.0;
		Terrain.BaseElevation = 22000.0;
		Terrain.Amplitude = 42000.0;
		Terrain.FeatureScale = 38000.0;
		Terrain.LooseDepth = 25.0f;
		Terrain.DuneInfluence = 0.1f;
		Terrain.PrimarySurface = EDustSurface::Scoria;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 3.0f;
		Region->TemperatureOffset = 9.0f;
		Region->ResidentSpecies = { TEXT("Ashcrawler") };
		break;

	case EDustTerrainProfile::DryForest:
		Region->CoreRadius = 1.2 * Km * 10.0;
		Region->FalloffRadius = 0.6 * Km * 10.0;
		Region->BoundaryWarp = 0.34 * Km * 10.0;
		Terrain.BaseElevation = 6000.0;
		Terrain.Amplitude = 9000.0;
		Terrain.FeatureScale = 32000.0;
		Terrain.LooseDepth = 130.0f;
		Terrain.DuneInfluence = 0.45f;
		Terrain.PrimarySurface = EDustSurface::PackedSand;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 6.0f;
		Region->WindMultiplier = 0.7f;
		Region->ResidentSpecies = { TEXT("PaleFennec"), TEXT("CanyonGrazer"), TEXT("Kite") };
		break;

	case EDustTerrainProfile::BuriedCity:
		Region->CoreRadius = 0.9 * Km * 10.0;
		Region->FalloffRadius = 0.4 * Km * 10.0;
		Region->BoundaryWarp = 0.22 * Km * 10.0;
		Terrain.BaseElevation = 3000.0;
		Terrain.Amplitude = 11000.0;
		Terrain.FeatureScale = 9000.0; // block size
		Terrain.LooseDepth = 70.0f;
		Terrain.DuneInfluence = 0.3f;
		Terrain.PrimarySurface = EDustSurface::PackedSand;
		Terrain.SteepSurface = EDustSurface::Worked;
		Region->PoiDensityPerKm2 = 14.0f;
		Region->ResidentSpecies = { TEXT("PaleFennec"), TEXT("Nightstalker") };
		break;

	case EDustTerrainProfile::Oasis:
		Region->CoreRadius = 0.35 * Km * 10.0;
		Region->FalloffRadius = 0.25 * Km * 10.0;
		Region->BoundaryWarp = 0.09 * Km * 10.0;
		Terrain.BaseElevation = -2000.0;
		Terrain.Amplitude = 7000.0;
		Terrain.FeatureScale = 12000.0;
		Terrain.LooseDepth = 40.0f;
		Terrain.DuneInfluence = 0.15f;
		Terrain.PrimarySurface = EDustSurface::Verdant;
		Terrain.SteepSurface = EDustSurface::Rock;

		// Water sits below the rim by construction; the validator enforces it.
		Terrain.WaterLevel = -4200.0;

		Region->PoiDensityPerKm2 = 9.0f;
		Region->WindMultiplier = 0.45f;
		Region->TemperatureOffset = -6.0f;
		Region->ResidentSpecies = { TEXT("PaleFennec"), TEXT("CanyonGrazer"), TEXT("Kite") };
		Region->MigratorySpecies = { TEXT("Duneglider"), TEXT("Ridgewalker") };
		break;

	case EDustTerrainProfile::StormBelt:
		Region->CoreRadius = 1.4 * Km * 10.0;
		Region->FalloffRadius = 0.7 * Km * 10.0;
		Region->BoundaryWarp = 0.40 * Km * 10.0;
		Terrain.BaseElevation = 9000.0;
		Terrain.Amplitude = 18000.0;
		Terrain.FeatureScale = 26000.0;
		Terrain.LooseDepth = 30.0f;
		Terrain.DuneInfluence = 0.25f;
		Terrain.PrimarySurface = EDustSurface::PackedSand;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 0.5f;
		Region->WindMultiplier = 2.6f;

		// The Veil's character comes entirely from these weights. No scripting.
		Region->WeatherBias.Add(TAG_Dust_Weather_Sandstorm, 6.0f);
		Region->WeatherBias.Add(TAG_Dust_Weather_Haze, 2.0f);
		Region->WeatherBias.Add(TAG_Dust_Weather_Clear, 0.05f);
		break;

	case EDustTerrainProfile::Badlands:
		Region->CoreRadius = 1.1 * Km * 10.0;
		Region->FalloffRadius = 0.5 * Km * 10.0;
		Region->BoundaryWarp = 0.30 * Km * 10.0;
		Terrain.BaseElevation = 17000.0;
		Terrain.Amplitude = 28000.0;
		Terrain.FeatureScale = 48000.0;
		Terrain.DetailOctaves = 6;
		Terrain.WarpStrength = 0.85f;
		Terrain.LooseDepth = 20.0f;
		Terrain.DuneInfluence = 0.15f;
		Terrain.PrimarySurface = EDustSurface::Clay;
		Terrain.SteepSurface = EDustSurface::Rock;
		Region->PoiDensityPerKm2 = 4.0f;
		Region->ResidentSpecies = { TEXT("Nightstalker"), TEXT("CanyonGrazer") };
		break;

	default:
		break;
	}

	// Every region needs a landmark weight or it cannot be navigated to.
	Region->PoiCategoryWeights.Add(TAG_Dust_Discovery_Landmark, 1.5f);
	Region->PoiCategoryWeights.Add(TAG_Dust_Discovery_Ruin, 1.2f);
	Region->PoiCategoryWeights.Add(TAG_Dust_Discovery_Artefact, 2.0f);

	return Region;
}

int32 UDustBootstrapCommandlet::GenerateWorld(bool bForce)
{
	using namespace DustBootstrap;

	// Placement across the 10 x 10 km world. Laid out so that the likely first
	// crossing (Erg to Cut) is long enough to teach distance, and the Green Eye
	// sits roughly central so every region has a plausible route to water.
	struct FRegionSpec
	{
		const TCHAR* AssetName;
		const TCHAR* Id;
		const TCHAR* Display;
		const TCHAR* Intent;
		FVector2D AnchorKm;
		int32 Priority;
		EDustTerrainProfile Profile;
	};

	const FRegionSpec Specs[] =
	{
		{ TEXT("DA_Region_LongErg"), TEXT("LongErg"), TEXT("The Long Erg"),
		  TEXT("The place that teaches you what distance costs."),
		  FVector2D(-2.4, -1.8), 0, EDustTerrainProfile::DuneSea },

		{ TEXT("DA_Region_ScreamingFlat"), TEXT("ScreamingFlat"), TEXT("The Screaming Flat"),
		  TEXT("Somewhere so flat and so silent it becomes uncomfortable."),
		  FVector2D(3.1, -3.2), 1, EDustTerrainProfile::SaltFlat },

		{ TEXT("DA_Region_TheCut"), TEXT("TheCut"), TEXT("The Cut"),
		  TEXT("Shade, water, and the first place that feels like shelter."),
		  FVector2D(-0.6, 1.4), 1, EDustTerrainProfile::Canyon },

		{ TEXT("DA_Region_StandingStones"), TEXT("StandingStones"), TEXT("The Standing Stones"),
		  TEXT("Scale. The first time the player feels small."),
		  FVector2D(1.9, 0.7), 1, EDustTerrainProfile::Mesa },

		{ TEXT("DA_Region_TheSpine"), TEXT("TheSpine"), TEXT("The Spine"),
		  TEXT("The hard way, and the long view back over everything you crossed."),
		  FVector2D(-3.4, 3.2), 2, EDustTerrainProfile::Mountains },

		{ TEXT("DA_Region_TheSink"), TEXT("TheSink"), TEXT("The Sink"),
		  TEXT("Where everything that ever washed downhill has ended up."),
		  FVector2D(0.9, -2.6), 1, EDustTerrainProfile::Basin },

		{ TEXT("DA_Region_TheCinders"), TEXT("TheCinders"), TEXT("The Cinders"),
		  TEXT("A place that is actively hostile rather than passively indifferent."),
		  FVector2D(3.6, 2.4), 2, EDustTerrainProfile::Volcanic },

		{ TEXT("DA_Region_StandingDead"), TEXT("StandingDead"), TEXT("The Standing Dead"),
		  TEXT("Relief. And the realisation that this was not always desert."),
		  FVector2D(-1.6, -0.2), 1, EDustTerrainProfile::DryForest },

		{ TEXT("DA_Region_Ashkarrid"), TEXT("Ashkarrid"), TEXT("Ashkarrid"),
		  TEXT("The moment the world stops being a landscape and becomes a place someone lived."),
		  FVector2D(0.4, 3.0), 3, EDustTerrainProfile::BuriedCity },

		{ TEXT("DA_Region_GreenEye"), TEXT("GreenEye"), TEXT("The Green Eye"),
		  TEXT("Water. Safety. The place you plan routes around."),
		  FVector2D(0.2, 0.1), 4, EDustTerrainProfile::Oasis },

		{ TEXT("DA_Region_TheVeil"), TEXT("TheVeil"), TEXT("The Veil"),
		  TEXT("A region you can only cross, never inhabit."),
		  FVector2D(-3.8, -3.4), 2, EDustTerrainProfile::StormBelt },

		{ TEXT("DA_Region_FlayedGround"), TEXT("FlayedGround"), TEXT("The Flayed Ground"),
		  TEXT("Legible geological time. Depth you can read."),
		  FVector2D(2.6, -0.9), 1, EDustTerrainProfile::Badlands },
	};

	UDustWorldComposition* Composition =
		CreateAsset<UDustWorldComposition>(WorldPath, TEXT("DA_WorldComposition"), bForce);

	int32 Created = 0;

	TArray<TObjectPtr<UDustRegionData>> Regions;

	for (const FRegionSpec& Spec : Specs)
	{
		UDustRegionData* Region = MakeRegion(
			Spec.AssetName,
			Spec.Id,
			FText::FromString(Spec.Display),
			Spec.Intent,
			Spec.AnchorKm * Km * 10.0,
			Spec.Priority,
			Spec.Profile,
			bForce);

		if (Region && SavePackage(Region))
		{
			Regions.Add(Region);
			++Created;
		}
	}

	if (Composition)
	{
		Composition->Regions = Regions;

		// Fallback terrain for any point no region reaches. In a correctly laid
		// out world this is never sampled, but the sampler must be total.
		Composition->FallbackTerrain.BaseElevation = 0.0;
		Composition->FallbackTerrain.Amplitude = 8000.0;
		Composition->FallbackTerrain.FeatureScale = 60000.0;
		Composition->FallbackTerrain.LooseDepth = 150.0f;
		Composition->FallbackTerrain.DuneInfluence = 0.8f;
		Composition->FallbackTerrain.PrimarySurface = EDustSurface::LooseSand;
		Composition->FallbackTerrain.SteepSurface = EDustSurface::Rock;

		SavePackage(Composition);
		++Created;
	}

	return Created;
}

int32 UDustBootstrapCommandlet::GenerateWeather(bool bForce)
{
	using namespace DustBootstrap;

	struct FWeatherSpec
	{
		const TCHAR* AssetName;
		FGameplayTag Tag;
		float WindSpeed;
		float Particulate;
		float Visibility;
		float CloudCoverage;
		float Rain;
		float Lightning;
		float DurationHours;
		float Likelihood;
	};

	const FWeatherSpec Specs[] =
	{
		{ TEXT("DA_Weather_Clear"),     TAG_Dust_Weather_Clear,      280.0f, 0.04f, 1200000.0f, 0.08f, 0.0f,  0.0f,  6.0f, 3.0f },
		{ TEXT("DA_Weather_Haze"),      TAG_Dust_Weather_Haze,       520.0f, 0.24f,  420000.0f, 0.14f, 0.0f,  0.0f,  4.0f, 2.0f },
		{ TEXT("DA_Weather_Sandstorm"), TAG_Dust_Weather_Sandstorm, 2200.0f, 0.88f,   14000.0f, 0.30f, 0.0f,  0.0f,  2.5f, 0.7f },
		{ TEXT("DA_Weather_Fog"),       TAG_Dust_Weather_Fog,        140.0f, 0.10f,   38000.0f, 0.55f, 0.0f,  0.0f,  1.5f, 0.5f },
		{ TEXT("DA_Weather_Rain"),      TAG_Dust_Weather_Rain,       760.0f, 0.06f,  180000.0f, 0.90f, 0.75f, 0.0f,  1.2f, 0.12f },
		{ TEXT("DA_Weather_Storm"),     TAG_Dust_Weather_Storm,     1500.0f, 0.20f,   90000.0f, 0.95f, 0.45f, 22.0f, 1.0f, 0.10f },
	};

	int32 Created = 0;

	for (const FWeatherSpec& Spec : Specs)
	{
		UDustWeatherPreset* Preset = CreateAsset<UDustWeatherPreset>(WeatherPath, Spec.AssetName, bForce);
		if (!Preset)
		{
			continue;
		}

		Preset->WeatherTag         = Spec.Tag;
		Preset->WindSpeed          = Spec.WindSpeed;
		Preset->Particulate        = Spec.Particulate;
		Preset->VisibilityDistance = Spec.Visibility;
		Preset->CloudCoverage      = Spec.CloudCoverage;
		Preset->RainIntensity      = Spec.Rain;
		Preset->LightningInterval  = Spec.Lightning;
		Preset->TypicalDurationHours = Spec.DurationHours;
		Preset->BaseLikelihood     = Spec.Likelihood;
		Preset->TransitionSeconds  = 90.0f;

		// Transition graph. The important property: a sandstorm never becomes a
		// clear sky directly - it decays through haze, so the player experiences
		// weather lifting rather than switching off.
		if (Spec.Tag == TAG_Dust_Weather_Clear)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Haze, 3.0f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Fog, 0.6f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Sandstorm, 0.3f);
		}
		else if (Spec.Tag == TAG_Dust_Weather_Haze)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Clear, 2.5f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Sandstorm, 1.4f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Storm, 0.3f);
		}
		else if (Spec.Tag == TAG_Dust_Weather_Sandstorm)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Haze, 4.0f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Storm, 0.4f);
		}
		else if (Spec.Tag == TAG_Dust_Weather_Fog)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Clear, 3.0f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Haze, 1.0f);
		}
		else if (Spec.Tag == TAG_Dust_Weather_Rain)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Fog, 2.0f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Clear, 1.5f);
		}
		else if (Spec.Tag == TAG_Dust_Weather_Storm)
		{
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Rain, 2.0f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Sandstorm, 1.5f);
			Preset->TransitionWeights.Add(TAG_Dust_Weather_Haze, 1.0f);
		}

		if (SavePackage(Preset))
		{
			++Created;
		}
	}

	return Created;
}

int32 UDustBootstrapCommandlet::GenerateMaterialCollections(bool bForce)
{
	using namespace DustBootstrap;

	int32 Created = 0;

	// --- Sky ----------------------------------------------------------------
	if (UMaterialParameterCollection* Sky =
		CreateAsset<UMaterialParameterCollection>(MaterialPath, TEXT("MPC_DustSky"), bForce))
	{
		auto AddScalar = [Sky](const TCHAR* Name, float Default)
		{
			FCollectionScalarParameter Param;
			Param.ParameterName = Name;
			Param.DefaultValue = Default;
			Param.Id = FGuid::NewGuid();
			Sky->ScalarParameters.Add(Param);
		};

		auto AddVector = [Sky](const TCHAR* Name, const FLinearColor& Default)
		{
			FCollectionVectorParameter Param;
			Param.ParameterName = Name;
			Param.DefaultValue = Default;
			Param.Id = FGuid::NewGuid();
			Sky->VectorParameters.Add(Param);
		};

		AddVector(TEXT("SunDirection"), FLinearColor(0.0f, 0.0f, -1.0f, 0.0f));
		AddVector(TEXT("MoonDirection"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f));

		AddScalar(TEXT("SunElevation"), 45.0f);
		AddScalar(TEXT("MoonIllumination"), 0.0f);
		AddScalar(TEXT("MoonPhase"), 0.5f);
		AddScalar(TEXT("StarFieldRotation"), 0.0f);
		AddScalar(TEXT("StarVisibility"), 0.0f);
		AddScalar(TEXT("TimeOfDay"), 12.0f);
		AddScalar(TEXT("Temperature"), 24.0f);
		AddScalar(TEXT("HeatShimmer"), 0.0f);

		if (SavePackage(Sky))
		{
			++Created;
		}
	}

	// --- Weather ------------------------------------------------------------
	if (UMaterialParameterCollection* Weather =
		CreateAsset<UMaterialParameterCollection>(MaterialPath, TEXT("MPC_DustWeather"), bForce))
	{
		auto AddScalar = [Weather](const TCHAR* Name, float Default)
		{
			FCollectionScalarParameter Param;
			Param.ParameterName = Name;
			Param.DefaultValue = Default;
			Param.Id = FGuid::NewGuid();
			Weather->ScalarParameters.Add(Param);
		};

		FCollectionVectorParameter WindDirection;
		WindDirection.ParameterName = TEXT("WindDirection");
		WindDirection.DefaultValue = FLinearColor(1.0f, 0.25f, 0.0f, 0.0f);
		WindDirection.Id = FGuid::NewGuid();
		Weather->VectorParameters.Add(WindDirection);

		AddScalar(TEXT("WindSpeed"), 280.0f);
		AddScalar(TEXT("Particulate"), 0.04f);
		AddScalar(TEXT("Visibility"), 1200000.0f);
		AddScalar(TEXT("CloudCoverage"), 0.08f);
		AddScalar(TEXT("RainIntensity"), 0.0f);
		AddScalar(TEXT("SurfaceWetness"), 0.0f);

		if (SavePackage(Weather))
		{
			++Created;
		}
	}

	return Created;
}

int32 UDustBootstrapCommandlet::GenerateFauna(bool bForce)
{
	using namespace DustBootstrap;

	struct FSpeciesSpec
	{
		const TCHAR* AssetName;
		const TCHAR* Id;
		const TCHAR* Display;
		const TCHAR* Rationale;
		EDustDisposition Disposition;
		EDustActivityPattern Activity;
		float Density;
		int32 GroupSize;
		float WalkSpeed;
		float RunSpeed;
		float Awareness;
		float Reaction;
	};

	// Every species answers a specific environmental pressure. The validator
	// rejects any asset whose rationale is shorter than 40 characters, which is
	// how this rule survives a deadline.
	const FSpeciesSpec Specs[] =
	{
		{ TEXT("DA_Species_Sandswimmer"), TEXT("Sandswimmer"), TEXT("Sandswimmer"),
		  TEXT("Elongated, limb-reduced burrower. Moves through loose sand rather than over it, avoiding both the "
		       "surface heat and every surface predator. Fringed scales shed grains; nictitating membranes seal the eyes."),
		  EDustDisposition::Skittish, EDustActivityPattern::Crepuscular, 0.9f, 1, 90.0f, 420.0f, 3500.0f, 900.0f },

		{ TEXT("DA_Species_Duneglider"), TEXT("Duneglider"), TEXT("Duneglider"),
		  TEXT("Broad membranous flanks let it descend slip faces at speed without expending energy climbing back. "
		       "Migrates by riding the wind across the erg rather than walking it, which is the only affordable strategy at this scale."),
		  EDustDisposition::Indifferent, EDustActivityPattern::Diurnal, 0.35f, 4, 160.0f, 780.0f, 7000.0f, 2400.0f },

		{ TEXT("DA_Species_PaleFennec"), TEXT("PaleFennec"), TEXT("Pale Fennec"),
		  TEXT("Enormous ears for thermal dissipation and for locating prey moving under sand. Dens in ruins and "
		       "aqueduct voids, drinking from seeps the vanished civilisation's infrastructure still produces."),
		  EDustDisposition::Curious, EDustActivityPattern::Nocturnal, 1.2f, 2, 200.0f, 900.0f, 6000.0f, 1800.0f },

		{ TEXT("DA_Species_CanyonGrazer"), TEXT("CanyonGrazer"), TEXT("Canyon Grazer"),
		  TEXT("Long-necked browser adapted to the sparse high foliage of canyon walls. Splayed hooves for loose "
		       "scree; herds because visibility in a canyon is poor and many eyes are the only defence."),
		  EDustDisposition::Skittish, EDustActivityPattern::Crepuscular, 0.6f, 7, 210.0f, 860.0f, 9000.0f, 3200.0f },

		{ TEXT("DA_Species_Kite"), TEXT("Kite"), TEXT("Kite"),
		  TEXT("Soaring scavenger that exploits the thermal columns rising off heated rock. Nests on caprock ledges "
		       "no ground predator can reach. Its circling is visible for kilometres and reliably marks something dead."),
		  EDustDisposition::Indifferent, EDustActivityPattern::Diurnal, 0.4f, 3, 180.0f, 1400.0f, 14000.0f, 4000.0f },

		{ TEXT("DA_Species_Ridgewalker"), TEXT("Ridgewalker"), TEXT("Ridgewalker"),
		  TEXT("Large high-altitude browser with a dense insulating pelt and hooves adapted to rime. Territorial "
		       "because forage above the snow line is scarce enough that sharing it is not survivable."),
		  EDustDisposition::Territorial, EDustActivityPattern::Diurnal, 0.15f, 1, 190.0f, 700.0f, 11000.0f, 3600.0f },

		{ TEXT("DA_Species_Nightstalker"), TEXT("Nightstalker"), TEXT("Nightstalker"),
		  TEXT("Ambush predator with a reflective tapetum and pressure-sensitive foot pads that read vibration through "
		       "sand. Follows disturbed ground rather than scent, which works long after a trail has gone cold."),
		  EDustDisposition::Predatory, EDustActivityPattern::Nocturnal, 0.12f, 1, 170.0f, 1050.0f, 9000.0f, 2600.0f },

		{ TEXT("DA_Species_Ashcrawler"), TEXT("Ashcrawler"), TEXT("Ashcrawler"),
		  TEXT("Heat-tolerant detritivore with a mineralised carapace that survives contact with hot scoria. Feeds on "
		       "the sulphur-fixing crusts around fumarole vents, a food source nothing else can reach."),
		  EDustDisposition::Indifferent, EDustActivityPattern::Cathemeral, 0.8f, 5, 70.0f, 240.0f, 2200.0f, 700.0f },
	};

	int32 Created = 0;

	for (const FSpeciesSpec& Spec : Specs)
	{
		UDustSpeciesData* Species = CreateAsset<UDustSpeciesData>(FaunaPath, Spec.AssetName, bForce);
		if (!Species)
		{
			continue;
		}

		Species->SpeciesId = Spec.Id;
		Species->DisplayName = FText::FromString(Spec.Display);
		Species->EvolutionaryRationale = Spec.Rationale;
		Species->Disposition = Spec.Disposition;
		Species->ActivityPattern = Spec.Activity;
		Species->DensityPerKm2 = Spec.Density;
		Species->TypicalGroupSize = Spec.GroupSize;
		Species->GroupSizeVariance = FMath::Max(0, Spec.GroupSize / 3);
		Species->WalkSpeed = Spec.WalkSpeed;
		Species->RunSpeed = Spec.RunSpeed;
		Species->AwarenessRadius = Spec.Awareness;
		Species->ReactionRadius = Spec.Reaction;

		Species->ObservableBehaviours = { TEXT("Drinking"), TEXT("Resting"), TEXT("Foraging") };

		if (Spec.Disposition == EDustDisposition::Territorial)
		{
			Species->TerritoryRadius = 45000.0f;
			Species->ObservableBehaviours.Add(TEXT("Warning"));
		}
		if (Spec.Disposition == EDustDisposition::Predatory)
		{
			Species->ObservableBehaviours.Add(TEXT("Stalking"));
		}

		if (SavePackage(Species))
		{
			++Created;
		}
	}

	return Created;
}

int32 UDustBootstrapCommandlet::GenerateInput(bool bForce)
{
	// Enhanced Input assets are created here in the shipping tool. They are
	// omitted from this pass because the mapping context needs key bindings
	// that the rebinding UI owns, and generating them twice would produce two
	// sources of truth for the default layout.
	//
	// The defaults are documented in Config/DefaultInput.ini and the assets are
	// authored once, by hand, against that list.
	UE_LOG(LogDust, Display,
		TEXT("Input assets: author IMC_DustDefault and IA_* against the layout in Config/DefaultInput.ini."));

	return 0;
}

int32 UDustBootstrapCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const bool bForce = Switches.Contains(TEXT("force"));
	const FString Only = ParamMap.FindRef(TEXT("only"));

	auto ShouldRun = [&Only](const TCHAR* Stage)
	{
		return Only.IsEmpty() || Only.Equals(Stage, ESearchCase::IgnoreCase);
	};

	UE_LOG(LogDust, Display, TEXT("DUST bootstrap starting%s."), bForce ? TEXT(" (force)") : TEXT(""));

	int32 Created = 0;

	if (ShouldRun(TEXT("materials")))
	{
		Created += GenerateMaterialCollections(bForce);
	}
	if (ShouldRun(TEXT("regions")) || ShouldRun(TEXT("world")))
	{
		Created += GenerateWorld(bForce);
	}
	if (ShouldRun(TEXT("weather")))
	{
		Created += GenerateWeather(bForce);
	}
	if (ShouldRun(TEXT("fauna")))
	{
		Created += GenerateFauna(bForce);
	}
	if (ShouldRun(TEXT("input")))
	{
		Created += GenerateInput(bForce);
	}

	UE_LOG(LogDust, Display,
		TEXT("Bootstrap complete: %d assets created. "
		     "Next: open L_Dust_Persistent, then run -run=DustWorldBake -mode=validate."),
		Created);

	return 0;
}
