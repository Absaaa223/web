// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Fauna/DustCreatureBrain.h"

#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Math/DustNoise.h"
#include "Weather/DustSkySubsystem.h"
#include "Weather/DustWeatherSubsystem.h"
#include "World/DustSandDeformation.h"
#include "World/DustWorldSubsystem.h"

constexpr float UDustCreatureBrain::DecisionIntervals[3];

UDustCreatureBrain::UDustCreatureBrain()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UDustCreatureBrain::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		HomeLocation = Owner->GetActorLocation();
		MoveTarget = HomeLocation;

		// Stagger decision-making across the population. Without this, every
		// creature spawned in the same frame thinks on the same frame forever,
		// producing a periodic spike and, worse, a herd that moves in unison
		// like a single organism.
		DecisionPhase = FDustNoise::HashToUnit(
			GetTypeHash(Owner->GetFName())) * DecisionIntervals[0];
	}

	// Start with plausible individual variation rather than identical needs,
	// so a herd is not synchronised into all drinking at once.
	const uint32 Seed = GetOwner() ? GetTypeHash(GetOwner()->GetFName()) : 0u;
	Thirst  = FDustNoise::HashToUnit(FDustNoise::Hash(Seed ^ 0x11u)) * 0.4f;
	Hunger  = FDustNoise::HashToUnit(FDustNoise::Hash(Seed ^ 0x22u)) * 0.4f;
	Fatigue = FDustNoise::HashToUnit(FDustNoise::Hash(Seed ^ 0x33u)) * 0.3f;
}

void UDustCreatureBrain::SetSpecies(UDustSpeciesData* InSpecies)
{
	Species = InSpecies;
}

void UDustCreatureBrain::SetSimulationLOD(int32 InLOD)
{
	SimulationLOD = FMath::Clamp(InLOD, 0, 2);
}

double UDustCreatureBrain::GetPlayerDistance() const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return TNumericLimits<double>::Max();
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		return TNumericLimits<double>::Max();
	}

	return FVector::Dist(Owner->GetActorLocation(), PlayerPawn->GetActorLocation());
}

void UDustCreatureBrain::UpdateNeeds(float DeltaHours)
{
	if (!Species)
	{
		return;
	}

	const FDustNeedProfile& Profile = Species->Needs;

	Thirst = FMath::Clamp(Thirst + Profile.ThirstRate * DeltaHours, 0.0f, 1.0f);
	Hunger = FMath::Clamp(Hunger + Profile.HungerRate * DeltaHours, 0.0f, 1.0f);

	// Resting reduces fatigue; anything else builds it.
	if (CurrentAction == EDustCreatureAction::Rest)
	{
		Fatigue = FMath::Clamp(Fatigue - Profile.FatigueRate * DeltaHours * 3.0f, 0.0f, 1.0f);
	}
	else
	{
		const float Exertion = (CurrentAction == EDustCreatureAction::Flee ||
		                        CurrentAction == EDustCreatureAction::Hunt) ? 3.0f : 1.0f;

		Fatigue = FMath::Clamp(Fatigue + Profile.FatigueRate * DeltaHours * Exertion, 0.0f, 1.0f);
	}

	// Alarm decays. The rate is slow on purpose: an animal that instantly
	// forgets being startled feels like a state machine, and an animal that
	// stays wary for a minute after you spooked it feels like an animal.
	Alarm = FMath::Clamp(Alarm - DeltaHours * 1.8f, 0.0f, 1.0f);
}

void UDustCreatureBrain::UpdatePerception()
{
	// Distant creatures skip perception entirely - they cannot see the player
	// and the player cannot see them, so the only thing that matters is that
	// their needs keep evolving.
	if (SimulationLOD >= 2 || !Species)
	{
		bAwareOfPlayer = false;
		return;
	}

	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!PlayerPawn)
	{
		bAwareOfPlayer = false;
		return;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const FVector PlayerLocation = PlayerPawn->GetActorLocation();
	const double Distance = FVector::Dist(OwnerLocation, PlayerLocation);

	float EffectiveRadius = Species->AwarenessRadius;

	// --- Player noise -------------------------------------------------------
	// Movement speed is a decent proxy for how much noise the player is making.
	const float PlayerSpeed = PlayerPawn->GetVelocity().Size();
	EffectiveRadius *= FMath::GetMappedRangeValueClamped(
		FVector2f(0.0f, 900.0f), FVector2f(0.55f, 1.9f), PlayerSpeed);

	// --- Scent on the wind --------------------------------------------------
	// Approaching downwind genuinely works. This is a real mechanic the player
	// can discover on their own by noticing which approaches spook animals -
	// and it is never explained anywhere, which is the point.
	if (const UDustWeatherSubsystem* Weather = UDustWeatherSubsystem::Get(this))
	{
		const FVector ToPlayer = (PlayerLocation - OwnerLocation).GetSafeNormal();
		const FVector Wind = Weather->GetPrevailingWindDirection();

		// Wind blowing from the player toward the creature carries scent.
		const float ScentAlignment = static_cast<float>(FVector::DotProduct(-ToPlayer, Wind));
		EffectiveRadius *= FMath::Lerp(0.7f, 1.6f, (ScentAlignment + 1.0f) * 0.5f);
	}

	// --- Darkness -----------------------------------------------------------
	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		const float Light = Sky->GetAmbientLightLevel();

		// Nocturnal species are not disadvantaged by darkness; diurnal ones are.
		if (Species->ActivityPattern != EDustActivityPattern::Nocturnal)
		{
			EffectiveRadius *= FMath::Lerp(0.4f, 1.0f, FMath::Sqrt(Light));
		}
	}

	const bool bWasAware = bAwareOfPlayer;
	bAwareOfPlayer = Distance < EffectiveRadius;

	if (bAwareOfPlayer)
	{
		LastKnownPlayerLocation = PlayerLocation;

		// Alarm scales with how far inside the reaction radius the player is.
		const float Proximity = 1.0f - FMath::Clamp(
			static_cast<float>(Distance) / FMath::Max(Species->ReactionRadius, 1.0f), 0.0f, 1.0f);

		float AlarmRate = Proximity * 0.9f;

		// Disposition scales how much proximity actually bothers them.
		switch (Species->Disposition)
		{
		case EDustDisposition::Skittish:     AlarmRate *= 2.4f; break;
		case EDustDisposition::Territorial:  AlarmRate *= 1.3f; break;
		case EDustDisposition::Curious:      AlarmRate *= 0.35f; break;
		case EDustDisposition::Indifferent:  AlarmRate *= 0.15f; break;
		case EDustDisposition::Predatory:    AlarmRate *= 0.5f; break;
		}

		Alarm = FMath::Clamp(Alarm + AlarmRate * 0.05f, 0.0f, 1.0f);

		if (!bWasAware)
		{
			DUST_VERBOSE(LogDustFauna, TEXT("%s became aware of the player at %.0f cm."),
				*GetOwner()->GetName(), Distance);
		}
	}

	// --- Tracks -------------------------------------------------------------
	// Creatures notice churned sand. A predator following the player's trail
	// long after they passed is a genuinely unsettling thing to look back and
	// realise is happening.
	if (Species->Disposition == EDustDisposition::Predatory && !bAwareOfPlayer)
	{
		if (const UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
		{
			const float Disturbance = Sand->GetDisturbanceInRadius(OwnerLocation, 800.0f);
			if (Disturbance > 0.35f)
			{
				Alarm = FMath::Max(Alarm, Disturbance * 0.5f);
				LastKnownPlayerLocation = OwnerLocation;
			}
		}
	}
}

float UDustCreatureBrain::ScoreAction(EDustCreatureAction Action) const
{
	if (!Species)
	{
		return 0.0f;
	}

	const double PlayerDistance = GetPlayerDistance();

	// How much the current time of day suits this species being active.
	float ActivityFit = 1.0f;
	float HeatPressure = 0.0f;

	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		const float Light = Sky->GetAmbientLightLevel();
		const float Hour = Sky->GetWorldTime().Hour;
		const float Temperature = Sky->GetSkyState().TemperatureCelsius;

		switch (Species->ActivityPattern)
		{
		case EDustActivityPattern::Diurnal:
			ActivityFit = Light;
			break;

		case EDustActivityPattern::Nocturnal:
			ActivityFit = 1.0f - Light;
			break;

		case EDustActivityPattern::Crepuscular:
			// Peaks at dawn and dusk. Two Gaussians centred on 6 and 19 hours.
			ActivityFit = FMath::Max(
				FMath::Exp(-FMath::Square((Hour - 6.0f) / 2.2f)),
				FMath::Exp(-FMath::Square((Hour - 19.0f) / 2.2f)));
			break;

		case EDustActivityPattern::Cathemeral:
			ActivityFit = 0.75f;
			break;
		}

		HeatPressure = FMath::GetMappedRangeValueClamped(
			FVector2f(32.0f, 46.0f), FVector2f(0.0f, 1.0f), Temperature)
			* Species->Needs.HeatSensitivity;
	}

	// Commitment bonus: whatever we are already doing gets a head start that
	// decays over roughly six seconds. Without it, near-equal scores cause the
	// creature to visibly dither.
	const float Commitment = (Action == CurrentAction)
		? 0.22f * FMath::Exp(-TimeInAction / 6.0f)
		: 0.0f;

	switch (Action)
	{
	case EDustCreatureAction::Flee:
	{
		if (Species->Disposition == EDustDisposition::Territorial ||
			Species->Disposition == EDustDisposition::Indifferent)
		{
			// These do not flee from the player at all; they have other answers.
			return Commitment;
		}

		// Fleeing is driven purely by alarm, and it dominates everything else
		// when high - which is correct: a frightened animal does not stop to
		// consider whether it is also thirsty.
		return FMath::Pow(Alarm, 0.6f) * 1.4f + Commitment;
	}

	case EDustCreatureAction::Threaten:
	{
		if (Species->Disposition != EDustDisposition::Territorial)
		{
			return 0.0f;
		}

		// Threaten only inside the territory, and only when the player is close
		// enough to be a real intrusion. The warning display is the whole point:
		// a territorial animal in DUST always gives the player a chance to back
		// off, and a player who reads the warning is never hurt.
		const double DistanceFromHome = GetOwner()
			? FVector::Dist(GetOwner()->GetActorLocation(), HomeLocation) : 0.0;

		if (DistanceFromHome > Species->TerritoryRadius || !bAwareOfPlayer)
		{
			return Commitment;
		}

		const float Intrusion = 1.0f - FMath::Clamp(
			static_cast<float>(PlayerDistance) / FMath::Max(Species->ReactionRadius, 1.0f), 0.0f, 1.0f);

		return Intrusion * 1.2f + Commitment;
	}

	case EDustCreatureAction::Investigate:
	{
		if (Species->Disposition != EDustDisposition::Curious || !bAwareOfPlayer)
		{
			return Commitment;
		}

		// Curiosity competes with caution: a curious animal approaches, but not
		// while alarmed, and not when it has urgent needs.
		const float Curiosity = FMath::Clamp(1.0f - Alarm * 1.6f, 0.0f, 1.0f);
		const float NeedsSatisfied = 1.0f - FMath::Max3(Thirst, Hunger, Fatigue);

		return Curiosity * NeedsSatisfied * 0.75f + Commitment;
	}

	case EDustCreatureAction::SeekWater:
	{
		// Thirst curves sharply: mild thirst is ignorable, severe thirst is not.
		// A linear need would have animals constantly trekking to water.
		return FMath::Pow(Thirst, 2.4f) * 1.3f + Commitment;
	}

	case EDustCreatureAction::Forage:
	{
		return FMath::Pow(Hunger, 2.2f) * ActivityFit + Commitment;
	}

	case EDustCreatureAction::SeekShelter:
	{
		// Midday heat drives everything to shade. This is why the desert at
		// 14:00 feels dead, and why dawn feels alive.
		return HeatPressure * 1.15f + Commitment;
	}

	case EDustCreatureAction::Rest:
	{
		// Rest when tired *and* when this is not your active time of day.
		return FMath::Pow(Fatigue, 1.8f) * (1.0f - ActivityFit * 0.7f) + Commitment;
	}

	case EDustCreatureAction::Hunt:
	{
		if (Species->Disposition != EDustDisposition::Predatory)
		{
			return 0.0f;
		}
		return FMath::Pow(Hunger, 1.6f) * ActivityFit * 1.1f + Commitment;
	}

	case EDustCreatureAction::Migrate:
	{
		if (!Species->bMigratory || Species->MigrationRoute.Num() < 2)
		{
			return 0.0f;
		}

		// Migration pressure builds as the stay at the current stop runs out.
		const float StayFraction = DaysAtCurrentStop / FMath::Max(Species->DaysPerMigrationStop, 0.1f);
		return FMath::Clamp(StayFraction - 0.85f, 0.0f, 1.0f) * 6.0f + Commitment;
	}

	case EDustCreatureAction::Wander:
	{
		// The baseline. Low but never zero, so a creature with no pressing
		// needs still moves around rather than standing like a statue.
		return 0.18f * ActivityFit + Commitment;
	}

	case EDustCreatureAction::Idle:
	default:
	{
		return 0.12f + Commitment;
	}
	}
}

void UDustCreatureBrain::SelectAction()
{
	DUST_SCOPED_TIMER(Dust_Fauna_SelectAction);

	EDustCreatureAction Best = EDustCreatureAction::Idle;
	float BestScore = -1.0f;

	for (uint8 Index = 0; Index < static_cast<uint8>(EDustCreatureAction::Count); ++Index)
	{
		const EDustCreatureAction Action = static_cast<EDustCreatureAction>(Index);
		const float Score = ScoreAction(Action);

		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Action;
		}
	}

	if (Best != CurrentAction)
	{
		const EDustCreatureAction Previous = CurrentAction;
		CurrentAction = Best;
		TimeInAction = 0.0f;

		UpdateMoveTarget();

		OnActionChanged.Broadcast(CurrentAction, Previous);
	}
}

bool UDustCreatureBrain::FindWater(FVector& OutLocation) const
{
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	const AActor* Owner = GetOwner();
	if (!World || !Owner || !Species)
	{
		return false;
	}

	// Spiral outward looking for a submerged sample. Creatures know their
	// range: this is not pathfinding, it is the animal remembering where the
	// water is, which is exactly what a real one would be doing.
	const FVector Origin = Owner->GetActorLocation();
	const float Range = Species->Needs.ForagingRange;

	constexpr int32 RingCount = 5;
	constexpr int32 SamplesPerRing = 10;

	for (int32 Ring = 1; Ring <= RingCount; ++Ring)
	{
		const double Radius = (static_cast<double>(Ring) / RingCount) * Range;

		for (int32 Index = 0; Index < SamplesPerRing; ++Index)
		{
			const double Angle = (2.0 * PI * Index) / SamplesPerRing +
				static_cast<double>(Ring) * 0.4; // offset each ring so samples do not align

			const FVector Probe = Origin + FVector(
				FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0);

			const FDustGroundSample Sample = World->SampleGround(Probe);
			if (Sample.IsSubmerged())
			{
				OutLocation = FVector(Probe.X, Probe.Y, Sample.WaterHeight);
				return true;
			}
		}
	}

	return false;
}

bool UDustCreatureBrain::FindShelter(FVector& OutLocation) const
{
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this);
	const AActor* Owner = GetOwner();

	if (!World || !Owner)
	{
		return false;
	}

	const FVector Origin = Owner->GetActorLocation();

	// Shade is wherever the sun is blocked. We evaluate the solar load at a
	// ring of candidates and take the darkest - which naturally finds the north
	// side of a mesa in the morning and the other side in the afternoon,
	// without anyone placing a single shade volume.
	FVector Best = Origin;
	float BestLoad = TNumericLimits<float>::Max();

	constexpr int32 SampleCount = 12;
	constexpr double SearchRadius = 25000.0;

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const double Angle = (2.0 * PI * Index) / SampleCount;
		const FVector Probe = Origin + FVector(
			FMath::Cos(Angle) * SearchRadius, FMath::Sin(Angle) * SearchRadius, 0.0);

		const FVector Grounded = World->ProjectToGround(Probe);

		const float Load = Sky ? Sky->GetSolarLoadAt(Grounded + FVector(0, 0, 60.0)) : 1.0f;
		if (Load < BestLoad)
		{
			BestLoad = Load;
			Best = Grounded;
		}
	}

	OutLocation = Best;
	return BestLoad < 0.5f;
}

void UDustCreatureBrain::UpdateMoveTarget()
{
	const AActor* Owner = GetOwner();
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	if (!Owner || !Species)
	{
		return;
	}

	const FVector Origin = Owner->GetActorLocation();

	switch (CurrentAction)
	{
	case EDustCreatureAction::Flee:
	{
		// Run away from the player, but not blindly: prefer downhill and away
		// from where they came. Fleeing uphill into a dead end reads as stupid.
		FVector Away = (Origin - LastKnownPlayerLocation).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector::ForwardVector;
		}

		FVector Candidate = Origin + Away * 40000.0;

		if (World)
		{
			// Nudge toward whichever of three directions loses the most height.
			double BestHeight = TNumericLimits<double>::Max();
			for (float AngleOffset : { -0.5f, 0.0f, 0.5f })
			{
				const FVector Direction = Away.RotateAngleAxis(
					FMath::RadiansToDegrees(AngleOffset), FVector::UpVector);
				const FVector Probe = Origin + Direction * 40000.0;

				const double Height = World->SampleHeight(Probe);
				if (Height < BestHeight)
				{
					BestHeight = Height;
					Candidate = Probe;
				}
			}
			Candidate = World->ProjectToGround(Candidate);
		}

		MoveTarget = Candidate;
		break;
	}

	case EDustCreatureAction::Investigate:
	{
		// Approach, but stop at a respectful distance. Curious animals in DUST
		// never touch the player; they come and look, which is far more
		// affecting than an animal that walks into you.
		const FVector ToPlayer = (LastKnownPlayerLocation - Origin).GetSafeNormal2D();
		const double StandoffDistance = Species->ReactionRadius * 0.8;

		MoveTarget = LastKnownPlayerLocation - ToPlayer * StandoffDistance;
		if (World)
		{
			MoveTarget = World->ProjectToGround(MoveTarget);
		}
		break;
	}

	case EDustCreatureAction::Threaten:
	{
		// Hold position between the player and the centre of the territory.
		const FVector ToPlayer = (LastKnownPlayerLocation - HomeLocation).GetSafeNormal2D();
		MoveTarget = HomeLocation + ToPlayer * (Species->TerritoryRadius * 0.55);
		if (World)
		{
			MoveTarget = World->ProjectToGround(MoveTarget);
		}
		break;
	}

	case EDustCreatureAction::SeekWater:
	{
		FVector Water;
		if (FindWater(Water))
		{
			MoveTarget = Water;
		}
		else
		{
			// No water in range. Head home and endure - which is what a real
			// animal does, and which is why finding a dying herd far from an
			// oasis is a thing the player can stumble across.
			MoveTarget = HomeLocation;
		}
		break;
	}

	case EDustCreatureAction::SeekShelter:
	{
		FVector Shelter;
		if (FindShelter(Shelter))
		{
			MoveTarget = Shelter;
		}
		break;
	}

	case EDustCreatureAction::Migrate:
	{
		if (Species->MigrationRoute.IsValidIndex(MigrationStopIndex) && World)
		{
			// Head toward the next region on the route. We aim at the region
			// anchor, which the sampler knows.
			const FName TargetRegion = Species->MigrationRoute[
				(MigrationStopIndex + 1) % Species->MigrationRoute.Num()];

			if (const FDustRegionSnapshot* Region = World->GetSampler().FindRegion(TargetRegion))
			{
				MoveTarget = World->ProjectToGround(
					FVector(Region->Anchor.X, Region->Anchor.Y, 0.0));
			}
		}
		break;
	}

	case EDustCreatureAction::Forage:
	case EDustCreatureAction::Hunt:
	case EDustCreatureAction::Wander:
	{
		// Wander within the home range, biased to stay inside it.
		const uint32 Seed = FDustNoise::Hash(
			GetTypeHash(Owner->GetFName()) ^ static_cast<uint32>(TimeInAction * 100.0f));

		const float Angle = FDustNoise::HashToUnit(Seed) * 2.0f * PI;
		const float Distance = FMath::Lerp(4000.0f, Species->Needs.ForagingRange * 0.4f,
			FDustNoise::HashToUnit(FDustNoise::Hash(Seed ^ 0x5Au)));

		FVector Candidate = Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Distance;

		// Pull back toward home if we have strayed.
		const double FromHome = FVector::Dist2D(Candidate, HomeLocation);
		if (FromHome > Species->Needs.ForagingRange)
		{
			Candidate = FMath::Lerp(Candidate, HomeLocation, 0.6);
		}

		if (World)
		{
			Candidate = World->ProjectToGround(Candidate);

			// Do not wander onto ground this species cannot walk on.
			const FDustGroundSample Sample = World->SampleGround(Candidate);
			if (Sample.SlopeDegrees > Species->MaxTraversableSlope || Sample.IsSubmerged())
			{
				Candidate = Origin;
			}
		}

		MoveTarget = Candidate;
		break;
	}

	case EDustCreatureAction::Rest:
	case EDustCreatureAction::Idle:
	default:
		MoveTarget = Origin;
		break;
	}
}

float UDustCreatureBrain::GetDesiredSpeed() const
{
	if (!Species)
	{
		return 0.0f;
	}

	switch (CurrentAction)
	{
	case EDustCreatureAction::Flee:
		return Species->RunSpeed;

	case EDustCreatureAction::Hunt:
		return Species->RunSpeed * 0.8f;

	case EDustCreatureAction::Migrate:
		return Species->WalkSpeed * 1.35f;

	case EDustCreatureAction::Investigate:
		// Approach slowly. A curious animal that sprints at you is a threat.
		return Species->WalkSpeed * 0.6f;

	case EDustCreatureAction::Threaten:
		return Species->WalkSpeed * 0.35f;

	case EDustCreatureAction::SeekWater:
	case EDustCreatureAction::SeekShelter:
		// Urgency scales with how badly it is needed.
		return FMath::Lerp(Species->WalkSpeed, Species->RunSpeed * 0.6f,
			FMath::Max(Thirst, 0.0f));

	case EDustCreatureAction::Rest:
	case EDustCreatureAction::Idle:
		return 0.0f;

	case EDustCreatureAction::Forage:
	case EDustCreatureAction::Wander:
	default:
		return Species->WalkSpeed;
	}
}

void UDustCreatureBrain::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Species)
	{
		return;
	}

	TimeInAction += DeltaTime;

	// In-game hours elapsed, for needs.
	float HoursPerSecond = 1.0f / 60.0f;
	if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
	{
		HoursPerSecond = Sky->GetTimeScale() / 60.0f;
	}

	UpdateNeeds(DeltaTime * HoursPerSecond);

	if (Species->bMigratory)
	{
		DaysAtCurrentStop += DeltaTime * HoursPerSecond / 24.0f;
	}

	// Decision-making runs on a timer, phase-offset per creature.
	DecisionAccumulator += DeltaTime;
	const float Interval = DecisionIntervals[FMath::Clamp(SimulationLOD, 0, 2)];

	if (DecisionAccumulator + DecisionPhase >= Interval)
	{
		DecisionAccumulator = 0.0f;
		DecisionPhase = 0.0f;

		UpdatePerception();
		SelectAction();

		// Refresh the target periodically even without an action change, so a
		// wandering animal keeps picking new places to go.
		if (TimeInAction > 4.0f)
		{
			UpdateMoveTarget();
			TimeInAction = 0.0f;
		}
	}
}
