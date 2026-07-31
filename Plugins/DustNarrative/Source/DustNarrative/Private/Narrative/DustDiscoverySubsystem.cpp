// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Narrative/DustDiscoverySubsystem.h"

#include "DustDeveloperSettings.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "World/DustWorldSubsystem.h"

namespace DustCuriosityCVars
{
	static int32 DebugCuriosity = 0;
	static FAutoConsoleVariableRef CVarDebugCuriosity(
		TEXT("dust.debug.curiosity"),
		DebugCuriosity,
		TEXT("Log the curiosity director's candidate list and score once per second."),
		ECVF_Cheat);

	static int32 DisableIntervention = 0;
	static FAutoConsoleVariableRef CVarDisableIntervention(
		TEXT("dust.curiosity.DisableIntervention"),
		DisableIntervention,
		TEXT("Stop the director raising silhouettes in dead zones. Used when auditing raw region density."),
		ECVF_Cheat);
}

UDustDiscoverySubsystem* UDustDiscoverySubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustDiscoverySubsystem>() : nullptr;
}

bool UDustDiscoverySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustDiscoverySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency<UDustWorldSubsystem>();

	if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		Generator = FDustPoiGenerator(&World->GetSampler(), World->GetSampler().GetConfig().MasterSeed);
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

	UE_LOG(LogDustNarrative, Log, TEXT("Discovery subsystem online."));
}

void UDustDiscoverySubsystem::Deinitialize()
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

TStatId UDustDiscoverySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustDiscoverySubsystem, STATGROUP_Tickables);
}

EDustDiscoveryState UDustDiscoverySubsystem::GetDiscoveryState(const FGuid& PoiId) const
{
	const FDustDiscoveryRecord* Record = Records.Find(PoiId);
	return Record ? Record->State : EDustDiscoveryState::Unknown;
}

void UDustDiscoverySubsystem::SetDiscoveryState(const FGuid& PoiId, EDustDiscoveryState NewState)
{
	if (!PoiId.IsValid())
	{
		return;
	}

	FDustDiscoveryRecord& Record = Records.FindOrAdd(PoiId);

	// Knowledge never regresses. A player who has read an inscription does not
	// un-read it by walking away, and the state machine must not allow a
	// proximity check to demote something the player has fully resolved.
	if (static_cast<uint8>(NewState) <= static_cast<uint8>(Record.State))
	{
		return;
	}

	if (!Record.PoiId.IsValid())
	{
		Record.PoiId = PoiId;
	}

	Record.State = NewState;

	OnDiscoveryStateChanged.Broadcast(PoiId, NewState);

	// Only Visited and above count as "a discovery happened"; merely glimpsing
	// something on the horizon should not trigger the reveal sting.
	if (NewState >= EDustDiscoveryState::Visited)
	{
		const FDustCuriosityCandidate* Candidate = Candidates.FindByPredicate(
			[&PoiId](const FDustCuriosityCandidate& C) { return C.Poi.Id == PoiId; });

		const FGameplayTag Category = Candidate ? Candidate->Poi.Category : FGameplayTag();
		const float Significance = Candidate ? Candidate->Poi.Significance : 0.5f;

		OnDiscoveryMade.Broadcast(Category, Significance);

		// A resolved target is no longer a curiosity target, so drop any
		// silhouette boost the director had applied to it.
		SilhouetteBoosts.Remove(PoiId);
	}
}

void UDustDiscoverySubsystem::ResolveElement(const FGuid& PoiId, FName ElementId)
{
	FDustDiscoveryRecord& Record = Records.FindOrAdd(PoiId);
	if (!Record.PoiId.IsValid())
	{
		Record.PoiId = PoiId;
	}

	Record.ResolvedElements.Add(ElementId);
}

bool UDustDiscoverySubsystem::IsElementResolved(const FGuid& PoiId, FName ElementId) const
{
	const FDustDiscoveryRecord* Record = Records.Find(PoiId);
	return Record && Record->ResolvedElements.Contains(ElementId);
}

float UDustDiscoverySubsystem::GetSilhouetteBoost(const FGuid& PoiId) const
{
	const float* Boost = SilhouetteBoosts.Find(PoiId);
	return Boost ? *Boost : 1.0f;
}

bool UDustDiscoverySubsystem::HasTerrainLineOfSight(const FVector& From, const FVector& To) const
{
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	if (!World)
	{
		return true;
	}

	const FDustWorldSampler& Sampler = World->GetSampler();

	const double Distance = FVector::Dist2D(From, To);
	if (Distance < 100.0)
	{
		return true;
	}

	// March the ray against the height field. Sample count scales with distance
	// but is capped: at 5 km, 24 samples is one every 200 m, which is fine for
	// catching a dune or a mesa and misses only features narrower than that -
	// and a feature narrower than 200 m at 5 km is below a pixel anyway.
	const int32 SampleCount = FMath::Clamp(
		FMath::CeilToInt32(Distance / 20000.0), 6, 24);

	for (int32 Index = 1; Index < SampleCount; ++Index)
	{
		const double T = static_cast<double>(Index) / SampleCount;

		const FVector Point = FMath::Lerp(From, To, T);
		const double TerrainHeight = Sampler.SampleHeight(FVector2D(Point.X, Point.Y));

		// Small tolerance so that grazing a crest does not count as occlusion:
		// in practice you can see over a ridge you are level with.
		if (TerrainHeight > Point.Z + 200.0)
		{
			return false;
		}
	}

	return true;
}

void UDustDiscoverySubsystem::RefreshCandidates(const FVector& PlayerLocation)
{
	DUST_SCOPED_TIMER(Dust_Curiosity_Refresh);

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	// The curiosity horizon is wider than the guaranteed interest radius: we
	// want to know about the mountain 6 km away, because on a clear day the
	// player can see it and it is a perfectly good thing to walk toward.
	const double Horizon = Settings.GuaranteedInterestRadius * 4.0;

	TArray<FDustPointOfInterest> Pois;
	Generator.QueryRadius(FVector2D(PlayerLocation.X, PlayerLocation.Y), Horizon, Pois);

	Candidates.Reset(Pois.Num());

	for (FDustPointOfInterest& Poi : Pois)
	{
		// Apply what we know about it.
		Poi.State = GetDiscoveryState(Poi.Id);

		// Fully resolved things are no longer curiosities. Visited-but-not-
		// resolved ones still are, at reduced weight - there is more here.
		if (Poi.State == EDustDiscoveryState::Resolved)
		{
			continue;
		}

		FDustCuriosityCandidate Candidate;
		Candidate.Poi = MoveTemp(Poi);
		Candidates.Add(MoveTemp(Candidate));
	}

	LastRefreshLocation = PlayerLocation;
}

void UDustDiscoverySubsystem::ScoreCandidates(const FVector& PlayerLocation, const FVector& PlayerForward)
{
	DUST_SCOPED_TIMER(Dust_Curiosity_Score);

	BestCandidate = FDustCuriosityCandidate();
	float BestScore = 0.0f;

	// Eye height above the player's feet, for the line-of-sight march.
	const FVector EyeLocation = PlayerLocation + FVector(0, 0, 170.0);

	for (FDustCuriosityCandidate& Candidate : Candidates)
	{
		Candidate.Distance = FVector::Dist(PlayerLocation, Candidate.Poi.Location);

		const FVector ToTarget = (Candidate.Poi.Location - PlayerLocation).GetSafeNormal2D();
		Candidate.BearingOffset = FMath::RadiansToDegrees(
			FMath::Acos(FMath::Clamp(
				static_cast<float>(FVector::DotProduct(ToTarget, PlayerForward)), -1.0f, 1.0f)));

		// --- Readability -----------------------------------------------------
		// A thing is only a curiosity target if the player could plausibly
		// notice it: within its silhouette radius *and* not behind a dune.
		const double EffectiveSilhouette = Candidate.Poi.SilhouetteRadius
			* static_cast<double>(GetSilhouetteBoost(Candidate.Poi.Id));

		if (Candidate.Distance > EffectiveSilhouette)
		{
			Candidate.bVisible = false;
			Candidate.Score = 0.0f;
			continue;
		}

		Candidate.bVisible = HasTerrainLineOfSight(
			EyeLocation, Candidate.Poi.Location + FVector(0, 0, 400.0));

		if (!Candidate.bVisible)
		{
			// Not visible, but not worthless: something just over the ridge
			// still counts for a little, because the player will crest it.
			Candidate.Score = Candidate.Poi.Significance * 0.12f;
			continue;
		}

		// --- Distance --------------------------------------------------------
		// Peaks at a middle distance. Something at your feet is not a reason to
		// walk anywhere; something at the very limit of visibility is too vague
		// to act on. The sweet spot is "twenty minutes away and unmistakable".
		const double DistanceRatio = Candidate.Distance / FMath::Max(EffectiveSilhouette, 1.0);
		const float DistanceScore = static_cast<float>(
			FMath::Exp(-FMath::Square((DistanceRatio - 0.42) / 0.34)));

		// --- Bearing ---------------------------------------------------------
		// Things ahead of the player matter more than things behind, but not
		// overwhelmingly: turning around because you glimpsed something is one
		// of the game's best moments and the scoring should not suppress it.
		const float BearingScore = FMath::Lerp(0.45f, 1.0f,
			1.0f - FMath::Clamp(Candidate.BearingOffset / 180.0f, 0.0f, 1.0f));

		// --- Novelty ---------------------------------------------------------
		const float NoveltyScore =
			(Candidate.Poi.State == EDustDiscoveryState::Unknown)  ? 1.0f :
			(Candidate.Poi.State == EDustDiscoveryState::Glimpsed) ? 0.85f :
			                                                         0.35f;

		Candidate.Score = Candidate.Poi.Significance * DistanceScore * BearingScore * NoveltyScore;

		if (Candidate.Score > BestScore)
		{
			BestScore = Candidate.Score;
			BestCandidate = Candidate;
		}
	}

	// Smooth the curiosity level so a momentary occlusion - walking through a
	// dip - does not register as entering a dead zone.
	CuriosityLevel = FMath::FInterpTo(CuriosityLevel, BestScore, 1.0f, 0.6f);
}

void UDustDiscoverySubsystem::ApplyDeadZoneIntervention()
{
	if (DustCuriosityCVars::DisableIntervention != 0)
	{
		return;
	}

	// Find the nearest unresolved candidate, visible or not, and raise its
	// silhouette so the player has a chance of noticing it.
	//
	// This is deliberately conservative. We do not spawn content, we do not
	// move anything, and we do not exceed a 2.5x boost - beyond that a landmark
	// becomes readable from implausibly far away and the world starts to feel
	// like it is helping, which is precisely the feeling DUST is built to avoid.
	FDustCuriosityCandidate* Nearest = nullptr;
	double NearestDistance = TNumericLimits<double>::Max();

	for (FDustCuriosityCandidate& Candidate : Candidates)
	{
		if (Candidate.Poi.State == EDustDiscoveryState::Resolved)
		{
			continue;
		}

		if (Candidate.Distance < NearestDistance)
		{
			NearestDistance = Candidate.Distance;
			Nearest = &Candidate;
		}
	}

	if (!Nearest)
	{
		// Nothing at all within four times the guaranteed radius. This is a
		// content failure, not something the director can paper over, and it
		// is exactly what the offline validator sweeps for.
		UE_LOG(LogDustNarrative, Warning,
			TEXT("Curiosity dead zone with no candidate at all within the horizon. This region needs content."));
		return;
	}

	float& Boost = SilhouetteBoosts.FindOrAdd(Nearest->Poi.Id, 1.0f);

	// Ramp in over several seconds rather than snapping, so a landmark does not
	// visibly pop into legibility.
	Boost = FMath::Min(Boost + 0.02f, 2.5f);

	DUST_VERBOSE(LogDustNarrative,
		TEXT("Dead zone %.1fs: raising silhouette of POI at %.0f cm to %.2fx."),
		DeadZoneSeconds, NearestDistance, Boost);
}

void UDustDiscoverySubsystem::UpdateProximityDiscovery(const FVector& PlayerLocation)
{
	for (const FDustCuriosityCandidate& Candidate : Candidates)
	{
		// Glimpsed: the player has seen it on the horizon.
		if (Candidate.bVisible && Candidate.Poi.State == EDustDiscoveryState::Unknown)
		{
			SetDiscoveryState(Candidate.Poi.Id, EDustDiscoveryState::Glimpsed);
			continue;
		}

		// Visited: the player has been close enough to make out what it is.
		// The threshold scales with the thing's size, so arriving at a temple
		// registers from further out than arriving at a buried coin.
		const double VisitThreshold = FMath::Clamp(
			Candidate.Poi.SilhouetteRadius * 0.035, 600.0, 9000.0);

		if (Candidate.Distance < VisitThreshold &&
			Candidate.Poi.State < EDustDiscoveryState::Visited)
		{
			SetDiscoveryState(Candidate.Poi.Id, EDustDiscoveryState::Visited);
		}
	}
}

void UDustDiscoverySubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Curiosity_Tick);

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

	const FVector PlayerLocation = Pawn->GetActorLocation();

	// Use the camera's facing rather than the pawn's, because in third person
	// the player's *attention* is where the camera looks, not where the body
	// happens to be pointing.
	FVector ViewLocation = PlayerLocation;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector PlayerForward = ViewRotation.Vector().GetSafeNormal2D();

	// --- Refresh -----------------------------------------------------------
	// Regenerating sectors is the expensive part, so we only do it when the
	// player has actually moved somewhere new.
	RefreshAccumulator += DeltaTime;

	const bool bMovedFar = FVector::DistSquared(PlayerLocation, LastRefreshLocation)
		> FMath::Square(RefreshDistance);

	if (bMovedFar || RefreshAccumulator >= RefreshInterval)
	{
		RefreshAccumulator = 0.0f;
		RefreshCandidates(PlayerLocation);
	}

	// --- Score and act -----------------------------------------------------
	ScoreCandidates(PlayerLocation, PlayerForward);
	UpdateProximityDiscovery(PlayerLocation);

	if (CuriosityLevel < CuriosityFloor)
	{
		DeadZoneSeconds += DeltaTime;

		// Give the player a few seconds before intervening. Cresting a dune
		// briefly hides everything, and that momentary emptiness is a good
		// feeling that we should not immediately fix.
		if (DeadZoneSeconds > 6.0f)
		{
			ApplyDeadZoneIntervention();
		}
	}
	else
	{
		if (DeadZoneSeconds > 0.0f)
		{
			DUST_VERBOSE(LogDustNarrative, TEXT("Dead zone resolved after %.1fs."), DeadZoneSeconds);
		}
		DeadZoneSeconds = 0.0f;

		// Decay boosts back toward neutral once the player has something to
		// look at again, so the intervention leaves no permanent trace.
		for (TPair<FGuid, float>& Pair : SilhouetteBoosts)
		{
			Pair.Value = FMath::Max(1.0f, Pair.Value - DeltaTime * 0.08f);
		}
	}

	if (DustCuriosityCVars::DebugCuriosity != 0)
	{
		static float DebugTimer = 0.0f;
		DebugTimer += DeltaTime;
		if (DebugTimer >= 1.0f)
		{
			DebugTimer = 0.0f;
			UE_LOG(LogDustNarrative, Log,
				TEXT("Curiosity %.3f (floor %.2f) | %d candidates | best %.3f at %.0f m, %s | dead zone %.1fs"),
				CuriosityLevel, CuriosityFloor, Candidates.Num(),
				BestCandidate.Score, BestCandidate.Distance / 100.0,
				BestCandidate.bVisible ? TEXT("visible") : TEXT("occluded"),
				DeadZoneSeconds);
		}
	}
}

void UDustDiscoverySubsystem::OnDustSave(UDustSaveGame& Save)
{
	Save.Discoveries.Reset(Records.Num());

	for (const TPair<FGuid, FDustDiscoveryRecord>& Pair : Records)
	{
		Save.Discoveries.Add(Pair.Value);
	}
}

void UDustDiscoverySubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	Records.Reset();

	for (const FDustDiscoveryRecord& Record : Save.Discoveries)
	{
		Records.Add(Record.PoiId, Record);
	}

	// Force a candidate rebuild: the player has been repositioned and the old
	// list describes wherever they were when the game was last running.
	LastRefreshLocation = FVector(TNumericLimits<double>::Max());
	Candidates.Reset();
	SilhouetteBoosts.Reset();
	DeadZoneSeconds = 0.0f;
	CuriosityLevel = 1.0f;

	UE_LOG(LogDustNarrative, Log, TEXT("Restored %d discovery records."), Records.Num());
}
