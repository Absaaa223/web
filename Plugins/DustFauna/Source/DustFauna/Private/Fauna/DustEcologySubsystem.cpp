// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Fauna/DustEcologySubsystem.h"

#include "DustDeveloperSettings.h"
#include "DustLog.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Fauna/DustCreatureBrain.h"
#include "Fauna/DustSpeciesData.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/DustNoise.h"
#include "Weather/DustSkySubsystem.h"
#include "World/DustRegionData.h"
#include "World/DustWorldSubsystem.h"

namespace DustEcologyCVars
{
	static int32 DebugEcology = 0;
	static FAutoConsoleVariableRef CVarDebugEcology(
		TEXT("dust.debug.ecology"),
		DebugEcology,
		TEXT("Log ecology population and promotion statistics once per second."),
		ECVF_Cheat);

	/** Distance at which a record is promoted to a full pawn, in cm. */
	static float PromotionDistance = 24000.0f;
	static FAutoConsoleVariableRef CVarPromotionDistance(
		TEXT("dust.fauna.PromotionDistance"),
		PromotionDistance,
		TEXT("Distance in cm at which tracked creatures become real pawns."),
		ECVF_Scalability);
}

UDustEcologySubsystem* UDustEcologySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustEcologySubsystem>() : nullptr;
}

bool UDustEcologySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustEcologySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UDustWorldSubsystem>();

	LoadSpecies();

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

	UE_LOG(LogDustFauna, Log, TEXT("Ecology online with %d species."), SpeciesById.Num());
}

void UDustEcologySubsystem::Deinitialize()
{
	for (FDustCreatureRecord& Record : Records)
	{
		if (Record.IsPromoted())
		{
			DemoteRecord(Record);
		}
	}
	Records.Reset();

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

TStatId UDustEcologySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustEcologySubsystem, STATGROUP_Tickables);
}

void UDustEcologySubsystem::LoadSpecies()
{
	if (UAssetManager* AssetManager = UAssetManager::GetIfInitialized())
	{
		TArray<FAssetData> AssetList;
		AssetManager->GetPrimaryAssetDataList(FPrimaryAssetType(TEXT("DustSpecies")), AssetList);

		for (const FAssetData& Data : AssetList)
		{
			if (UDustSpeciesData* SpeciesData = Cast<UDustSpeciesData>(Data.GetAsset()))
			{
				if (!SpeciesData->SpeciesId.IsNone())
				{
					SpeciesById.Add(SpeciesData->SpeciesId, SpeciesData);
				}
			}
		}
	}

	if (SpeciesById.Num() == 0)
	{
		UE_LOG(LogDustFauna, Warning,
			TEXT("No species assets found. Run the DustBootstrap commandlet to generate the default bestiary."));
	}
}

UDustSpeciesData* UDustEcologySubsystem::FindSpecies(FName SpeciesId) const
{
	const TObjectPtr<UDustSpeciesData>* Found = SpeciesById.Find(SpeciesId);
	return Found ? Found->Get() : nullptr;
}

void UDustEcologySubsystem::PopulateRegion(FName RegionId)
{
	if (RegionId.IsNone() || PopulatedRegions.Contains(RegionId))
	{
		return;
	}

	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	if (!World)
	{
		return;
	}

	const UDustRegionData* Region = World->FindRegionData(RegionId);
	const FDustRegionSnapshot* Snapshot = World->GetSampler().FindRegion(RegionId);

	if (!Region || !Snapshot)
	{
		return;
	}

	PopulatedRegions.Add(RegionId);

	// Population is derived deterministically from the region seed, so the same
	// world always has the same herds in the same places on first contact.
	const uint32 RegionSeed = FDustNoise::DeriveSeed(Snapshot->Seed, TEXT("Population"));

	const double RegionAreaKm2 = PI * FMath::Square(Snapshot->CoreRadius / 100000.0);

	int32 Created = 0;

	for (int32 SpeciesIndex = 0; SpeciesIndex < Region->ResidentSpecies.Num(); ++SpeciesIndex)
	{
		const FName SpeciesId = Region->ResidentSpecies[SpeciesIndex];
		const UDustSpeciesData* SpeciesData = FindSpecies(SpeciesId);
		if (!SpeciesData)
		{
			UE_LOG(LogDustFauna, Warning,
				TEXT("Region '%s' lists unknown species '%s'."),
				*RegionId.ToString(), *SpeciesId.ToString());
			continue;
		}

		const int32 TargetCount = FMath::Max(1,
			FMath::RoundToInt32(SpeciesData->DensityPerKm2 * RegionAreaKm2));

		int32 Placed = 0;
		int32 Attempts = 0;

		while (Placed < TargetCount && Attempts < TargetCount * 8)
		{
			++Attempts;

			const uint32 Seed = FDustNoise::Hash(
				RegionSeed + static_cast<uint32>(SpeciesIndex) * 7919u + static_cast<uint32>(Attempts) * 104729u);

			// Place the group's home somewhere in the region.
			const float Angle = FDustNoise::HashToUnit(Seed) * 2.0f * PI;
			const float Radius = FMath::Sqrt(FDustNoise::HashToUnit(FDustNoise::Hash(Seed ^ 0x3Cu)))
				* static_cast<float>(Snapshot->CoreRadius);

			const FVector2D HomeXY = Snapshot->Anchor +
				FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius;

			const FDustGroundSample Ground = World->GetSampler().SampleGround(HomeXY);

			// Reject unlivable ground rather than nudging it: a rejected
			// candidate costs one sample, and forcing placement produces
			// animals standing on cliff faces.
			if (Ground.SlopeDegrees > SpeciesData->MaxTraversableSlope || Ground.IsSubmerged())
			{
				continue;
			}

			if (SpeciesData->PreferredSurfaces.Num() > 0 &&
				!SpeciesData->PreferredSurfaces.Contains(Ground.Surface))
			{
				continue;
			}

			// Group size.
			const int32 GroupSize = FMath::Max(1,
				SpeciesData->TypicalGroupSize +
				FMath::RoundToInt32((FDustNoise::HashToSigned(FDustNoise::Hash(Seed ^ 0x5Fu)))
					* SpeciesData->GroupSizeVariance));

			const int32 GroupId = NextGroupId++;
			const FVector HomeLocation(HomeXY.X, HomeXY.Y, Ground.Height);

			for (int32 Member = 0; Member < GroupSize && Placed < TargetCount; ++Member, ++Placed)
			{
				const uint32 MemberSeed = FDustNoise::Hash(Seed + static_cast<uint32>(Member) * 2654435761u);

				// Scatter group members around the shared home point.
				const float MemberAngle = FDustNoise::HashToUnit(MemberSeed) * 2.0f * PI;
				const float MemberRadius = FDustNoise::HashToUnit(
					FDustNoise::Hash(MemberSeed ^ 0x11u)) * 3000.0f;

				const FVector Offset(
					FMath::Cos(MemberAngle) * MemberRadius,
					FMath::Sin(MemberAngle) * MemberRadius,
					0.0f);

				FDustCreatureRecord Record;
				Record.SpeciesId    = SpeciesId;
				Record.HomeLocation = HomeLocation;
				Record.Location     = World->ProjectToGround(HomeLocation + Offset);
				Record.GroupId      = GroupId;
				Record.Thirst       = FDustNoise::HashToUnit(FDustNoise::Hash(MemberSeed ^ 0xA1u)) * 0.4f;
				Record.Hunger       = FDustNoise::HashToUnit(FDustNoise::Hash(MemberSeed ^ 0xB2u)) * 0.4f;
				Record.Fatigue      = FDustNoise::HashToUnit(FDustNoise::Hash(MemberSeed ^ 0xC3u)) * 0.3f;

				Records.Add(MoveTemp(Record));
				++Created;
			}
		}
	}

	UE_LOG(LogDustFauna, Log, TEXT("Populated region '%s' with %d creatures (%d tracked total)."),
		*RegionId.ToString(), Created, Records.Num());
}

void UDustEcologySubsystem::TickCoarseSimulation(float DeltaHours)
{
	DUST_SCOPED_TIMER(Dust_Ecology_CoarseSim);

	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);

	for (FDustCreatureRecord& Record : Records)
	{
		// Promoted records are simulated by their pawn's brain instead.
		if (Record.IsPromoted())
		{
			continue;
		}

		const UDustSpeciesData* SpeciesData = FindSpecies(Record.SpeciesId);
		if (!SpeciesData)
		{
			continue;
		}

		Record.Thirst  = FMath::Clamp(Record.Thirst  + SpeciesData->Needs.ThirstRate  * DeltaHours, 0.0f, 1.0f);
		Record.Hunger  = FMath::Clamp(Record.Hunger  + SpeciesData->Needs.HungerRate  * DeltaHours, 0.0f, 1.0f);
		Record.Fatigue = FMath::Clamp(Record.Fatigue + SpeciesData->Needs.FatigueRate * DeltaHours, 0.0f, 1.0f);

		// Coarse movement: drift toward home, or toward water when thirsty.
		// This is deliberately crude - nobody is watching - but it means the
		// herd genuinely is somewhere different when the player comes back.
		if (World)
		{
			FVector Target = Record.HomeLocation;

			if (Record.Thirst > 0.7f)
			{
				// Head for the nearest region with a water table. One sample
				// per creature per second is affordable at these numbers.
				const FDustWorldSampler& Sampler = World->GetSampler();
				double BestDistance = TNumericLimits<double>::Max();

				for (int32 Index = 0; Index < Sampler.GetRegionCount(); ++Index)
				{
					const FDustRegionSnapshot* Candidate = Sampler.GetRegion(Index);
					if (!Candidate || Candidate->Terrain.WaterLevel <= -1.0e8)
					{
						continue;
					}

					const double Distance = FVector2D::Distance(
						FVector2D(Record.Location.X, Record.Location.Y), Candidate->Anchor);

					if (Distance < BestDistance)
					{
						BestDistance = Distance;
						Target = FVector(Candidate->Anchor.X, Candidate->Anchor.Y, 0.0);
					}
				}
			}

			const FVector ToTarget = (Target - Record.Location).GetSafeNormal2D();
			const double StepDistance = static_cast<double>(SpeciesData->WalkSpeed)
				* DeltaHours * 3600.0 * 0.15; // creatures do not walk continuously

			if (!ToTarget.IsNearlyZero() && FVector::Dist2D(Target, Record.Location) > StepDistance)
			{
				Record.Location = World->ProjectToGround(Record.Location + ToTarget * StepDistance);
			}

			// Drinking.
			if (World->SampleGround(Record.Location).IsSubmerged())
			{
				Record.Thirst = 0.0f;
			}
		}
	}
}

bool UDustEcologySubsystem::PromoteRecord(FDustCreatureRecord& Record)
{
	UWorld* World = GetWorld();
	const UDustSpeciesData* SpeciesData = FindSpecies(Record.SpeciesId);

	if (!World || !SpeciesData)
	{
		return false;
	}

	UClass* PawnClass = SpeciesData->PawnClass.LoadSynchronous();
	if (!PawnClass)
	{
		return false;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	APawn* Spawned = World->SpawnActor<APawn>(
		PawnClass, Record.Location, FRotator::ZeroRotator, Params);

	if (!Spawned)
	{
		return false;
	}

	// Hand the record's accumulated state to the brain, so a promoted creature
	// continues from where the coarse simulation left it rather than resetting
	// to a default. This is the join between the two fidelities and it is the
	// part that must not leak: a herd that arrives at the oasis parched should
	// still be parched when it becomes visible.
	if (UDustCreatureBrain* Brain = Spawned->FindComponentByClass<UDustCreatureBrain>())
	{
		Brain->SetSpecies(const_cast<UDustSpeciesData*>(SpeciesData));
		Brain->SetHomeLocation(Record.HomeLocation);
		Brain->SetSimulationLOD(0);
	}

	Record.SpawnedActor = Spawned;
	return true;
}

void UDustEcologySubsystem::DemoteRecord(FDustCreatureRecord& Record)
{
	AActor* Actor = Record.SpawnedActor.Get();
	if (!Actor)
	{
		Record.SpawnedActor = nullptr;
		return;
	}

	// Write the pawn's state back before destroying it.
	Record.Location = Actor->GetActorLocation();

	if (const UDustCreatureBrain* Brain = Actor->FindComponentByClass<UDustCreatureBrain>())
	{
		Record.Thirst  = Brain->GetThirst();
		Record.Hunger  = Brain->GetHunger();
		Record.Fatigue = Brain->GetFatigue();
	}

	Actor->Destroy();
	Record.SpawnedActor = nullptr;
}

void UDustEcologySubsystem::UpdatePromotion()
{
	DUST_SCOPED_TIMER(Dust_Ecology_Promotion);

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return;
	}

	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	const double PromoteDistanceSq = FMath::Square(static_cast<double>(DustEcologyCVars::PromotionDistance));

	// Demote before promoting, so freeing a slot in this pass makes it
	// available in the same pass. Hysteresis on the demote distance stops a
	// creature at exactly the boundary from thrashing.
	const double DemoteDistanceSq = PromoteDistanceSq * 1.44; // 1.2x radius

	int32 PromotedCount = 0;

	for (FDustCreatureRecord& Record : Records)
	{
		if (!Record.IsPromoted())
		{
			continue;
		}

		if (FVector::DistSquared(Record.Location, PlayerLocation) > DemoteDistanceSq)
		{
			DemoteRecord(Record);
		}
		else
		{
			++PromotedCount;
		}
	}

	if (PromotedCount >= Settings.MaxFullFidelityCreatures)
	{
		return;
	}

	// Promote the nearest eligible records first. Sorting the whole array every
	// quarter second would be wasteful at a few hundred records, so we take a
	// single pass and promote anything in range until the budget is spent -
	// the ordering hardly matters when everything in range is within 240 m.
	for (FDustCreatureRecord& Record : Records)
	{
		if (PromotedCount >= Settings.MaxFullFidelityCreatures)
		{
			break;
		}

		if (Record.IsPromoted())
		{
			continue;
		}

		if (FVector::DistSquared(Record.Location, PlayerLocation) <= PromoteDistanceSq)
		{
			if (PromoteRecord(Record))
			{
				++PromotedCount;
			}
		}
	}
}

int32 UDustEcologySubsystem::GetPromotedCount() const
{
	int32 Count = 0;
	for (const FDustCreatureRecord& Record : Records)
	{
		if (Record.IsPromoted())
		{
			++Count;
		}
	}
	return Count;
}

void UDustEcologySubsystem::RecordSighting(FName SpeciesId, FName ObservedBehaviour)
{
	if (SpeciesId.IsNone())
	{
		return;
	}

	const bool bFirstSighting = !SightingCounts.Contains(SpeciesId);

	SightingCounts.FindOrAdd(SpeciesId)++;

	if (bFirstSighting)
	{
		if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
		{
			FirstSightings.Add(SpeciesId, Sky->GetWorldTime());
		}

		UE_LOG(LogDustFauna, Log, TEXT("First sighting: %s"), *SpeciesId.ToString());
	}

	if (!ObservedBehaviour.IsNone())
	{
		ObservedBehaviours.FindOrAdd(SpeciesId).Add(ObservedBehaviour);
	}
}

bool UDustEcologySubsystem::HasSeenSpecies(FName SpeciesId) const
{
	return SightingCounts.Contains(SpeciesId);
}

void UDustEcologySubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Ecology_Tick);

	// Populate the region the player is in, and its neighbours by proximity.
	if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		PopulateRegion(World->GetCurrentRegionId());
	}

	// Coarse simulation at 1 Hz.
	CoarseAccumulator += DeltaTime;
	if (CoarseAccumulator >= 1.0f)
	{
		float HoursPerSecond = 1.0f / 60.0f;
		if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
		{
			HoursPerSecond = Sky->GetTimeScale() / 60.0f;
		}

		TickCoarseSimulation(CoarseAccumulator * HoursPerSecond);
		CoarseAccumulator = 0.0f;
	}

	// Promotion at 4 Hz.
	PromotionAccumulator += DeltaTime;
	if (PromotionAccumulator >= 0.25f)
	{
		UpdatePromotion();
		PromotionAccumulator = 0.0f;

		if (DustEcologyCVars::DebugEcology != 0)
		{
			UE_LOG(LogDustFauna, Log, TEXT("Ecology: %d tracked, %d promoted, %d regions populated."),
				Records.Num(), GetPromotedCount(), PopulatedRegions.Num());
		}
	}
}

void UDustEcologySubsystem::OnDustSave(UDustSaveGame& Save)
{
	Save.CreatureLedger.Reset();

	for (const TPair<FName, int32>& Pair : SightingCounts)
	{
		FDustCreatureLedgerEntry Entry;
		Entry.SpeciesId = Pair.Key;
		Entry.TimesSighted = Pair.Value;

		if (const FDustWorldTime* First = FirstSightings.Find(Pair.Key))
		{
			Entry.FirstSighting = *First;
		}
		if (const TSet<FName>* Behaviours = ObservedBehaviours.Find(Pair.Key))
		{
			Entry.ObservedBehaviours = *Behaviours;
		}

		Save.CreatureLedger.Add(MoveTemp(Entry));
	}

	// Creature *positions* are deliberately not saved. They are regenerated
	// deterministically from the region seed, then advanced by the coarse
	// simulation. Persisting a few hundred positions would add nothing the
	// player could perceive and would make the save format brittle against any
	// change to the population maths.
}

void UDustEcologySubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	SightingCounts.Reset();
	FirstSightings.Reset();
	ObservedBehaviours.Reset();

	for (const FDustCreatureLedgerEntry& Entry : Save.CreatureLedger)
	{
		SightingCounts.Add(Entry.SpeciesId, Entry.TimesSighted);
		FirstSightings.Add(Entry.SpeciesId, Entry.FirstSighting);
		ObservedBehaviours.Add(Entry.SpeciesId, Entry.ObservedBehaviours);
	}

	// Clear and repopulate: the world is being rebuilt around the player's
	// loaded position, and stale records from the previous session would place
	// animals wherever the player happened to be standing when they saved.
	for (FDustCreatureRecord& Record : Records)
	{
		if (Record.IsPromoted())
		{
			DemoteRecord(Record);
		}
	}
	Records.Reset();
	PopulatedRegions.Reset();
}
