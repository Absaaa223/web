// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustGameMode.h"

#include "DustDeveloperSettings.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Math/DustNoise.h"
#include "Player/DustCharacter.h"
#include "Save/DustSaveSubsystem.h"
#include "World/DustPoiGenerator.h"
#include "World/DustWorldSubsystem.h"

ADustGameMode::ADustGameMode()
{
	DefaultPawnClass = ADustCharacter::StaticClass();

	// No HUD class: the interface is driven entirely by the HUD director and
	// UMG widgets, so there is nothing for an AHUD to do.
	HUDClass = nullptr;

	bStartPlayersAsSpectators = false;
}

void ADustGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
		{
			bContinuedSession = UGameplayStatics::HasOption(Options, TEXT("Continue"))
				&& SaveSubsystem->HasAnySave();
		}
	}

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	UE_LOG(LogDust, Log,
		TEXT("DUST starting. Seed %d, terrain v%d, world %.0f km2, %s session."),
		Settings.MasterSeed,
		FDustNoise::AlgorithmVersion,
		FMath::Square(Settings.WorldHalfExtent * 2.0 / 100000.0),
		bContinuedSession ? TEXT("continued") : TEXT("new"));
}

void ADustGameMode::StartPlay()
{
	Super::StartPlay();

	if (bContinuedSession)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
			{
				SaveSubsystem->LoadMostRecent();
			}
		}
	}
}

float ADustGameMode::ScoreWakingPlace(const FVector2D& Candidate, FRotator& OutFacing) const
{
	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	if (!World)
	{
		return 0.0f;
	}

	const FDustWorldSampler& Sampler = World->GetSampler();
	const FDustGroundSample Ground = Sampler.SampleGround(Candidate);

	// --- Must be somewhere you can stand up ---------------------------------
	if (Ground.SlopeDegrees > 9.0f || Ground.IsSubmerged())
	{
		return 0.0f;
	}

	// Loose sand specifically: waking on rock is a different, colder opening
	// than waking half-buried, and the latter is the one the game wants.
	if (Ground.Surface != EDustSurface::LooseSand && Ground.Surface != EDustSurface::PackedSand)
	{
		return 0.0f;
	}

	// --- Not deep inside a region -------------------------------------------
	// A weight below 0.8 means we are in a transition zone, so the player's
	// first walk in any direction crosses into somewhere different. That is the
	// game teaching its central promise in the first ten minutes without saying
	// anything.
	float RegionWeight = 0.0f;
	const int32 RegionIndex = Sampler.SampleDominantRegion(Candidate, RegionWeight);
	if (RegionIndex == INDEX_NONE || RegionWeight > 0.8f)
	{
		return 0.0f;
	}

	// --- Exactly one visible landmark ---------------------------------------
	const FDustPoiGenerator Generator(&Sampler, Sampler.GetConfig().MasterSeed);

	TArray<FDustPointOfInterest> Pois;
	Generator.QueryRadius(Candidate, 500000.0, Pois);

	const FVector EyeLocation(Candidate.X, Candidate.Y, Ground.Height + 170.0);

	int32 VisibleLandmarks = 0;
	FVector BestLandmark = FVector::ZeroVector;
	float BestSignificance = 0.0f;

	for (const FDustPointOfInterest& Poi : Pois)
	{
		if (Poi.Category != TAG_Dust_Discovery_Landmark)
		{
			continue;
		}

		const double Distance = FVector::Dist2D(EyeLocation, Poi.Location);
		if (Distance > Poi.SilhouetteRadius)
		{
			continue;
		}

		// Terrain line of sight, marched against the height field.
		bool bVisible = true;
		constexpr int32 SampleCount = 16;

		for (int32 Index = 1; Index < SampleCount; ++Index)
		{
			const double T = static_cast<double>(Index) / SampleCount;
			const FVector Point = FMath::Lerp(EyeLocation, Poi.Location + FVector(0, 0, 400.0), T);

			if (Sampler.SampleHeight(FVector2D(Point.X, Point.Y)) > Point.Z + 200.0)
			{
				bVisible = false;
				break;
			}
		}

		if (bVisible)
		{
			++VisibleLandmarks;

			if (Poi.Significance > BestSignificance)
			{
				BestSignificance = Poi.Significance;
				BestLandmark = Poi.Location;
			}
		}
	}

	// One is an invitation. Zero is abandonment. Three is a menu.
	if (VisibleLandmarks != 1)
	{
		return 0.0f;
	}

	// --- Facing --------------------------------------------------------------
	// Face east so the sunrise is the first thing on screen, unless doing so
	// would put the one visible landmark behind the player - in which case the
	// landmark wins, because it is the thing that makes them stand up.
	const FVector ToLandmark = (BestLandmark - EyeLocation).GetSafeNormal2D();
	const FVector East = FVector(0.0, 1.0, 0.0);

	const float LandmarkEastAlignment = static_cast<float>(FVector::DotProduct(ToLandmark, East));

	OutFacing = (LandmarkEastAlignment > -0.25f)
		? FRotationMatrix::MakeFromX(
			(ToLandmark * 0.6 + East * 0.4).GetSafeNormal()).Rotator()
		: ToLandmark.Rotation();

	OutFacing.Pitch = 0.0f;
	OutFacing.Roll = 0.0f;

	// Score by how well framed that single landmark is: we prefer it at a
	// middle distance, where it is unmistakably a made thing but far enough to
	// be a walk rather than a stroll.
	const double LandmarkDistance = FVector::Dist2D(EyeLocation, BestLandmark);
	const float DistanceScore = static_cast<float>(
		FMath::Exp(-FMath::Square((LandmarkDistance - 220000.0) / 130000.0)));

	return DistanceScore * BestSignificance * (1.0f - RegionWeight);
}

FTransform ADustGameMode::FindWakingPlace() const
{
	if (bWakingPlaceResolved)
	{
		return CachedWakingTransform;
	}

	DUST_SCOPED_TIMER(Dust_GameMode_FindWakingPlace);

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();
	const uint32 Seed = FDustNoise::DeriveSeed(static_cast<uint32>(Settings.MasterSeed), TEXT("WakingPlace"));

	FVector2D BestCandidate = FVector2D::ZeroVector;
	FRotator BestFacing = FRotator::ZeroRotator;
	float BestScore = 0.0f;

	// Sample candidates across the world. Two hundred is enough to reliably
	// find a good one, and the whole search costs a few milliseconds because
	// the cheap disqualifications reject most candidates before the expensive
	// line-of-sight work.
	constexpr int32 CandidateCount = 200;

	for (int32 Index = 0; Index < CandidateCount; ++Index)
	{
		const uint32 CandidateSeed = FDustNoise::Hash(Seed + static_cast<uint32>(Index) * 2654435761u);

		// Bias toward the middle half of the world: waking against the boundary
		// massif would give the player a wall on one side from the first frame.
		const double X = (FDustNoise::HashToSigned(CandidateSeed) * 0.55) * Settings.WorldHalfExtent;
		const double Y = (FDustNoise::HashToSigned(FDustNoise::Hash(CandidateSeed ^ 0x5Au)) * 0.55)
			* Settings.WorldHalfExtent;

		const FVector2D Candidate(X, Y);

		FRotator Facing;
		const float Score = ScoreWakingPlace(Candidate, Facing);

		if (Score > BestScore)
		{
			BestScore = Score;
			BestCandidate = Candidate;
			BestFacing = Facing;
		}
	}

	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);

	if (BestScore <= 0.0f || !World)
	{
		// Nothing satisfied the criteria. Rather than refusing to start, fall
		// back to the world origin projected onto the ground - the opening will
		// be less good, and the log line tells us the region layout needs work.
		UE_LOG(LogDust, Warning,
			TEXT("No waking place satisfied the opening criteria across %d candidates. "
			     "Falling back to origin; check region layout and landmark density."),
			CandidateCount);

		const double Height = World ? World->SampleHeight(FVector::ZeroVector) : 0.0;
		CachedWakingTransform = FTransform(FRotator(0, 90, 0), FVector(0, 0, Height + 95.0));
	}
	else
	{
		const double Height = World->SampleHeight(FVector(BestCandidate.X, BestCandidate.Y, 0.0));

		CachedWakingTransform = FTransform(
			BestFacing,
			FVector(BestCandidate.X, BestCandidate.Y, Height + 95.0));

		UE_LOG(LogDust, Log,
			TEXT("Waking place: (%.0f, %.0f) at %.0f m, facing %.0f degrees, score %.3f."),
			BestCandidate.X, BestCandidate.Y, Height / 100.0, BestFacing.Yaw, BestScore);
	}

	bWakingPlaceResolved = true;
	return CachedWakingTransform;
}

AActor* ADustGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// A hand-placed PlayerStart always wins: it means a designer is working on
	// something specific and does not want to be dropped somewhere else.
	if (AActor* AuthoredStart = Super::ChoosePlayerStart_Implementation(Player))
	{
		if (!AuthoredStart->ActorHasTag(TEXT("DustDefaultStart")))
		{
			return AuthoredStart;
		}
	}

	// Otherwise place a transient start at the computed waking place.
	const FTransform WakingTransform = FindWakingPlace();

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	APlayerStart* Start = World->SpawnActor<APlayerStart>(
		APlayerStart::StaticClass(),
		WakingTransform.GetLocation(),
		WakingTransform.GetRotation().Rotator(),
		Params);

	return Start;
}
