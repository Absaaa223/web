// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "World/DustWorldSubsystem.h"

#include "DustDeveloperSettings.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/DustNoise.h"
#include "World/DustRegionData.h"

namespace DustWorldCVars
{
	static float SampleEpsilon = 100.0f;
	static FAutoConsoleVariableRef CVarSampleEpsilon(
		TEXT("dust.world.NormalEpsilon"),
		SampleEpsilon,
		TEXT("Central-difference epsilon in cm for terrain normals. Larger values smooth foot placement over detail noise."),
		ECVF_Cheat);

	static int32 DebugWorld = 0;
	static FAutoConsoleVariableRef CVarDebugWorld(
		TEXT("dust.debug.world"),
		DebugWorld,
		TEXT("Overlay the live world sampler readout: region, surface, slope, loose depth, cache hit rate."),
		ECVF_Cheat);
}

UDustWorldSubsystem* UDustWorldSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustWorldSubsystem>() : nullptr;
}

bool UDustWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// The editor preview world needs the sampler too, so region iteration can
	// show real terrain without entering PIE.
	return WorldType == EWorldType::Game
		|| WorldType == EWorldType::PIE
		|| WorldType == EWorldType::Editor;
}

void UDustWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Cache.SetNum(CacheSize);

	LoadComposition();
	RebuildSampler();

	// Register with the save system so the dune migration phase and the
	// player's current region survive a save/load.
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
}

void UDustWorldSubsystem::Deinitialize()
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

	Cache.Reset();
	Super::Deinitialize();
}

TStatId UDustWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustWorldSubsystem, STATGROUP_Tickables);
}

void UDustWorldSubsystem::LoadComposition()
{
	// The composition is referenced by soft path in DefaultGame.ini so that the
	// runtime does not hard-reference content from a code module.
	static const TCHAR* CompositionPath =
		TEXT("/Game/Dust/World/DA_WorldComposition.DA_WorldComposition");

	Composition = LoadObject<UDustWorldComposition>(nullptr, CompositionPath);

	if (!Composition)
	{
		UE_LOG(LogDustWorld, Error,
			TEXT("World composition not found at '%s'. Run the DustBootstrap commandlet to generate the default world."),
			CompositionPath);
		return;
	}

	RegionsById.Reset();
	for (const TObjectPtr<UDustRegionData>& Region : Composition->Regions)
	{
		if (Region)
		{
			RegionsById.Add(Region->RegionId, Region);
		}
	}

	UE_LOG(LogDustWorld, Log, TEXT("Loaded world composition with %d regions."), RegionsById.Num());
}

void UDustWorldSubsystem::RebuildSampler()
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	FDustWorldSamplerConfig Config;

	if (Composition)
	{
		Config = FDustWorldSamplerConfig::FromComposition(*Composition, static_cast<uint32>(Settings.MasterSeed));
	}
	else
	{
		// Without a composition the sampler still has to be total: a flat
		// fallback plane is far better than a crash, and it makes the missing
		// asset obvious rather than mysterious.
		Config.MasterSeed = static_cast<uint32>(Settings.MasterSeed);
		Config.FallbackTerrain.BaseElevation = 0.0;
	}

	Config.WorldHalfExtent = Settings.WorldHalfExtent;
	Config.BoundaryFalloff = Settings.WorldBoundaryFalloff;

	Sampler.Rebuild(Config);

	// Any cached sample is now wrong.
	for (FCacheEntry& Entry : Cache)
	{
		Entry.Key = TNumericLimits<int64>::Min();
	}
}

const FDustGroundSample& UDustWorldSubsystem::GetCachedSample(const FVector2D& WorldXY) const
{
	// Quantise to the cache grid. Two queries within 25 cm of each other share
	// an entry, which is well inside the tolerance of everything that asks.
	const int64 QX = static_cast<int64>(FMath::RoundToDouble(WorldXY.X / CacheQuantum));
	const int64 QY = static_cast<int64>(FMath::RoundToDouble(WorldXY.Y / CacheQuantum));

	// Pack into a single key. 32 bits each covers +/- 21,000 km at this
	// quantisation - four orders of magnitude more than the world needs.
	const int64 Key = (QX << 32) ^ (QY & 0xFFFFFFFFLL);

	// Hash the key down to a bucket. Using the raw key would cluster badly for
	// axis-aligned scan patterns such as a landscape bake.
	const uint32 Bucket = FDustNoise::Hash(static_cast<uint32>(Key ^ (Key >> 32))) & CacheMask;

	FCacheEntry& Entry = Cache[Bucket];

	if (Entry.Key == Key)
	{
		++CacheHits;
		return Entry.Sample;
	}

	++CacheMisses;

	Entry.Key = Key;
	Entry.Sample = Sampler.SampleGround(FVector2D(
		static_cast<double>(QX) * CacheQuantum,
		static_cast<double>(QY) * CacheQuantum));

	return Entry.Sample;
}

FDustGroundSample UDustWorldSubsystem::SampleGround(const FVector& WorldLocation) const
{
	return GetCachedSample(FVector2D(WorldLocation.X, WorldLocation.Y));
}

double UDustWorldSubsystem::SampleHeight(const FVector& WorldLocation) const
{
	// Deliberately bypasses the cache: the height-only path is cheap enough
	// that a cache lookup plus a potential full-sample miss would cost more.
	return Sampler.SampleHeight(FVector2D(WorldLocation.X, WorldLocation.Y));
}

FVector UDustWorldSubsystem::ProjectToGround(const FVector& WorldLocation) const
{
	return FVector(WorldLocation.X, WorldLocation.Y, SampleHeight(WorldLocation));
}

FVector UDustWorldSubsystem::SampleNormal(const FVector& WorldLocation, double Epsilon) const
{
	return Sampler.SampleNormal(FVector2D(WorldLocation.X, WorldLocation.Y), Epsilon);
}

FName UDustWorldSubsystem::GetRegionIdAt(const FVector& WorldLocation) const
{
	float Weight = 0.0f;
	const int32 Index = Sampler.SampleDominantRegion(FVector2D(WorldLocation.X, WorldLocation.Y), Weight);

	const FDustRegionSnapshot* Region = Sampler.GetRegion(Index);
	return Region ? Region->RegionId : NAME_None;
}

const UDustRegionData* UDustWorldSubsystem::FindRegionData(FName RegionId) const
{
	const TObjectPtr<const UDustRegionData>* Found = RegionsById.Find(RegionId);
	return Found ? Found->Get() : nullptr;
}

void UDustWorldSubsystem::AdvanceDuneMigration(double DownwindCentimetres)
{
	check(IsInGameThread());

	if (FMath::IsNearlyZero(DownwindCentimetres))
	{
		return;
	}

	Sampler.SetDuneMigrationPhase(Sampler.GetDuneMigrationPhase() + DownwindCentimetres);

	// Every cached sample describes the previous dune positions.
	//
	// Invalidating the whole cache on every migration step would defeat the
	// point of having one, so migration is applied in discrete steps by the
	// weather subsystem - typically a few centimetres per second, batched into
	// a single update roughly once a second. At that rate the terrain visibly
	// changes over an afternoon of play without ever costing a frame.
	for (FCacheEntry& Entry : Cache)
	{
		Entry.Key = TNumericLimits<int64>::Min();
	}
}

void UDustWorldSubsystem::SetPrevailingWind(const FVector2D& WindDirection)
{
	check(IsInGameThread());

	Sampler.SetPrevailingWind(WindDirection);

	for (FCacheEntry& Entry : Cache)
	{
		Entry.Key = TNumericLimits<int64>::Min();
	}
}

void UDustWorldSubsystem::UpdateCurrentRegion()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	const FName NewRegion = GetRegionIdAt(Pawn->GetActorLocation());

	if (NewRegion != CurrentRegionId)
	{
		const FName Previous = CurrentRegionId;
		CurrentRegionId = NewRegion;

		UE_LOG(LogDustWorld, Log, TEXT("Region change: %s -> %s"),
			*Previous.ToString(), *NewRegion.ToString());

		OnRegionChanged.Broadcast(NewRegion, Previous);
	}
}

float UDustWorldSubsystem::GetCacheHitRate() const
{
	return LastHitRate;
}

void UDustWorldSubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_WorldSubsystem_Tick);

	UpdateCurrentRegion();

	// Roll up cache statistics once a second for the developer overlay.
	CacheStatTimer += DeltaTime;
	if (CacheStatTimer >= 1.0f)
	{
		const int32 Total = CacheHits + CacheMisses;
		LastHitRate = (Total > 0) ? static_cast<float>(CacheHits) / static_cast<float>(Total) : 0.0f;

		CacheHits = 0;
		CacheMisses = 0;
		CacheStatTimer = 0.0f;

		if (DustWorldCVars::DebugWorld != 0)
		{
			UE_LOG(LogDustWorld, Log,
				TEXT("Sampler cache hit rate %.1f%% | region '%s' | dune phase %.0f cm"),
				LastHitRate * 100.0f, *CurrentRegionId.ToString(), Sampler.GetDuneMigrationPhase());
		}
	}
}

void UDustWorldSubsystem::OnDustSave(UDustSaveGame& Save)
{
	// The dune phase is world state: reload without it and every dune snaps
	// back to where it was when the world was created.
	Save.WeatherPhase = static_cast<float>(Sampler.GetDuneMigrationPhase());
}

void UDustWorldSubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	Sampler.SetDuneMigrationPhase(static_cast<double>(Save.WeatherPhase));

	for (FCacheEntry& Entry : Cache)
	{
		Entry.Key = TNumericLimits<int64>::Min();
	}

	// Force a region re-evaluation on the next tick rather than firing the
	// change event now, when the player pawn may not yet be positioned.
	CurrentRegionId = NAME_None;
}
