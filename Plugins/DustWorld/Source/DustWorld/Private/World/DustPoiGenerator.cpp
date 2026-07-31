// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "World/DustPoiGenerator.h"

#include "DustDeveloperSettings.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "Math/DustNoise.h"

namespace DustPoi
{
	/**
	 * Candidate slots evaluated per sector. Most are rejected; this number is
	 * the *ceiling* on density, and the region's PoiDensityPerKm2 controls how
	 * many of them are even considered.
	 */
	static constexpr int32 CandidateSlotsPerSector = 24;

	/** One sector in km2, used to convert the designer-facing density. */
	static constexpr double SectorAreaKm2 =
		(FDustPoiGenerator::SectorSize / 100000.0) * (FDustPoiGenerator::SectorSize / 100000.0);
}

FDustPoiGenerator::FDustPoiGenerator(const FDustWorldSampler* InSampler, uint32 InMasterSeed)
	: Sampler(InSampler)
	, MasterSeed(InMasterSeed)
{
	PoiSeed = FDustNoise::DeriveSeed(InMasterSeed, TEXT("PointsOfInterest"));
}

double FDustPoiGenerator::ComputeProminence(
	const FDustWorldSampler& InSampler, const FVector2D& WorldXY, double Radius, int32 SampleCount)
{
	const double CentreHeight = InSampler.SampleHeight(WorldXY);

	// Sample a ring rather than a disc: we care about how this point compares
	// to its surroundings, and the ring is where the surroundings are. A disc
	// would be dominated by points near the centre, which tells us nothing.
	double SumSurrounding = 0.0;
	double MaxSurrounding = -TNumericLimits<double>::Max();

	SampleCount = FMath::Max(SampleCount, 4);

	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const double Angle = (2.0 * PI * static_cast<double>(Index)) / static_cast<double>(SampleCount);
		const FVector2D Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);

		const double SampleHeight = InSampler.SampleHeight(WorldXY + Offset);
		SumSurrounding += SampleHeight;
		MaxSurrounding = FMath::Max(MaxSurrounding, SampleHeight);
	}

	const double MeanSurrounding = SumSurrounding / static_cast<double>(SampleCount);

	// Blend mean and max. Pure mean overrates a point on a slope (it is above
	// half its neighbours simply by being on a hillside); pure max only ever
	// rewards absolute summits. The blend rewards genuine local high points.
	const double Reference = FMath::Lerp(MeanSurrounding, MaxSurrounding, 0.55);

	return CentreHeight - Reference;
}

FDustPoiGenerator::FPlacementRules FDustPoiGenerator::GetRulesForCategory(const FGameplayTag& Category)
{
	FPlacementRules Rules;

	if (Category == TAG_Dust_Discovery_Landmark)
	{
		// Landmarks are the game's navigation system. They must stand well
		// above their surroundings and be visible from kilometres away.
		Rules.MinSlopeDegrees   = 0.0f;
		Rules.MaxSlopeDegrees   = 26.0f;
		Rules.MinProminence     = 9000.0;   // 90 m above the ring mean
		Rules.ProminenceRadius  = 120000.0; // evaluated over 1.2 km
		Rules.SilhouetteRadius  = 450000.0; // readable at 4.5 km
		Rules.Significance      = 1.0f;
	}
	else if (Category == TAG_Dust_Discovery_Ruin)
	{
		// Ruins sit on ground that was once worth building on: flat enough,
		// slightly raised, never on a slip face.
		Rules.MaxSlopeDegrees   = 14.0f;
		Rules.MinProminence     = 800.0;
		Rules.ProminenceRadius  = 30000.0;
		Rules.SilhouetteRadius  = 60000.0;
		Rules.Significance      = 0.75f;
		Rules.AllowedSurfaces   = { EDustSurface::PackedSand, EDustSurface::Rock,
									EDustSurface::Clay, EDustSurface::Worked };
	}
	else if (Category == TAG_Dust_Discovery_Inscription)
	{
		// Carvings need a rock face. Steep ground only.
		Rules.MinSlopeDegrees   = 42.0f;
		Rules.MaxSlopeDegrees   = 85.0f;
		Rules.SilhouetteRadius  = 3000.0;
		Rules.Significance      = 0.5f;
		Rules.AllowedSurfaces   = { EDustSurface::Rock, EDustSurface::Worked };
	}
	else if (Category == TAG_Dust_Discovery_Cache)
	{
		// Caches were hidden on purpose: low ground, out of sight.
		Rules.MaxSlopeDegrees   = 34.0f;
		Rules.MaxProminence     = -400.0; // must sit *below* its surroundings
		Rules.ProminenceRadius  = 15000.0;
		Rules.SilhouetteRadius  = 1200.0;
		Rules.Significance      = 0.45f;
	}
	else if (Category == TAG_Dust_Discovery_Artefact)
	{
		// Small objects, exposed by the wind in interdune corridors and on
		// deflation surfaces. Never on a dune face - it would be buried.
		Rules.MaxSlopeDegrees   = 18.0f;
		Rules.SilhouetteRadius  = 900.0;
		Rules.Significance      = 0.55f;
	}
	else if (Category == TAG_Dust_Discovery_Vista)
	{
		// An authored composition point: somewhere the view is the reward.
		Rules.MaxSlopeDegrees   = 22.0f;
		Rules.MinProminence     = 6000.0;
		Rules.ProminenceRadius  = 90000.0;
		Rules.SilhouetteRadius  = 12000.0;
		Rules.Significance      = 0.8f;
	}
	else if (Category == TAG_Dust_Discovery_Creature)
	{
		// A den or a watering point; fauna spawn hooks attach to these.
		Rules.MaxSlopeDegrees   = 30.0f;
		Rules.SilhouetteRadius  = 8000.0;
		Rules.Significance      = 0.6f;
	}
	else if (Category == TAG_Dust_Discovery_Anomaly)
	{
		// Deliberately unconstrained. Anomalies are rare and their strangeness
		// is partly that they turn up where nothing should be.
		Rules.SilhouetteRadius  = 25000.0;
		Rules.Significance      = 0.95f;
	}

	return Rules;
}

bool FDustPoiGenerator::PassesRules(const FVector2D& Candidate, const FPlacementRules& Rules) const
{
	if (!Sampler)
	{
		return false;
	}

	if (!Sampler->IsInsideWorld(Candidate))
	{
		return false;
	}

	const FDustGroundSample Ground = Sampler->SampleGround(Candidate);

	if (Ground.SlopeDegrees < Rules.MinSlopeDegrees || Ground.SlopeDegrees > Rules.MaxSlopeDegrees)
	{
		return false;
	}

	if (Rules.AllowedSurfaces.Num() > 0 && !Rules.AllowedSurfaces.Contains(Ground.Surface))
	{
		return false;
	}

	// Never place anything underwater unless it explicitly wants to be near it.
	if (Ground.IsSubmerged() && Rules.MaxWaterDistance <= 0.0)
	{
		return false;
	}

	// Prominence is the expensive test - a dozen extra height samples - so it
	// runs last, after the cheap rejections have thinned the candidates.
	const bool bNeedsProminence =
		Rules.MinProminence > -TNumericLimits<double>::Max() ||
		Rules.MaxProminence <  TNumericLimits<double>::Max();

	if (bNeedsProminence)
	{
		const double Prominence = ComputeProminence(*Sampler, Candidate, Rules.ProminenceRadius);

		if (Prominence < Rules.MinProminence || Prominence > Rules.MaxProminence)
		{
			return false;
		}
	}

	return true;
}

FGameplayTag FDustPoiGenerator::PickCategory(const FDustRegionSnapshot& Region, uint32 SlotSeed) const
{
	// The snapshot deliberately does not carry the category weight map - it is
	// a UObject-owned TMap and the snapshot must stay POD for thread safety.
	// Instead we derive a plausible mix from the region's terrain profile,
	// which is both cheap and, in practice, what the designer would author.
	//
	// Regions that need a bespoke mix override it through hand-authored POIs.
	struct FWeightedCategory
	{
		FGameplayTag Tag;
		float Weight;
	};

	TArray<FWeightedCategory, TInlineAllocator<8>> Mix;

	switch (Region.Profile)
	{
	case EDustTerrainProfile::BuriedCity:
		Mix = { { TAG_Dust_Discovery_Ruin, 5.0f }, { TAG_Dust_Discovery_Inscription, 3.0f },
				{ TAG_Dust_Discovery_Artefact, 2.5f }, { TAG_Dust_Discovery_Cache, 1.5f },
				{ TAG_Dust_Discovery_Landmark, 0.8f } };
		break;

	case EDustTerrainProfile::Canyon:
	case EDustTerrainProfile::Badlands:
		Mix = { { TAG_Dust_Discovery_Inscription, 3.0f }, { TAG_Dust_Discovery_Cache, 2.0f },
				{ TAG_Dust_Discovery_Ruin, 1.5f }, { TAG_Dust_Discovery_Vista, 1.5f },
				{ TAG_Dust_Discovery_Landmark, 1.0f } };
		break;

	case EDustTerrainProfile::Mountains:
		Mix = { { TAG_Dust_Discovery_Vista, 3.0f }, { TAG_Dust_Discovery_Landmark, 2.5f },
				{ TAG_Dust_Discovery_Cache, 1.5f }, { TAG_Dust_Discovery_Ruin, 1.0f } };
		break;

	case EDustTerrainProfile::Oasis:
		Mix = { { TAG_Dust_Discovery_Creature, 3.0f }, { TAG_Dust_Discovery_Ruin, 2.0f },
				{ TAG_Dust_Discovery_Artefact, 2.0f }, { TAG_Dust_Discovery_Landmark, 1.0f } };
		break;

	case EDustTerrainProfile::SaltFlat:
	case EDustTerrainProfile::StormBelt:
		// Deliberately sparse and strange. Crossing these is meant to feel like
		// crossing something, and the payoff is that what you do find is odd.
		Mix = { { TAG_Dust_Discovery_Anomaly, 2.0f }, { TAG_Dust_Discovery_Landmark, 2.0f },
				{ TAG_Dust_Discovery_Artefact, 1.0f } };
		break;

	case EDustTerrainProfile::Volcanic:
		Mix = { { TAG_Dust_Discovery_Anomaly, 2.5f }, { TAG_Dust_Discovery_Landmark, 2.0f },
				{ TAG_Dust_Discovery_Cache, 1.5f }, { TAG_Dust_Discovery_Vista, 1.0f } };
		break;

	case EDustTerrainProfile::DryForest:
		Mix = { { TAG_Dust_Discovery_Creature, 3.0f }, { TAG_Dust_Discovery_Artefact, 2.0f },
				{ TAG_Dust_Discovery_Ruin, 1.5f }, { TAG_Dust_Discovery_Cache, 1.0f } };
		break;

	case EDustTerrainProfile::DuneSea:
	default:
		Mix = { { TAG_Dust_Discovery_Artefact, 2.0f }, { TAG_Dust_Discovery_Landmark, 1.5f },
				{ TAG_Dust_Discovery_Ruin, 1.2f }, { TAG_Dust_Discovery_Creature, 1.0f },
				{ TAG_Dust_Discovery_Cache, 0.8f } };
		break;
	}

	float Total = 0.0f;
	for (const FWeightedCategory& Entry : Mix)
	{
		Total += Entry.Weight;
	}

	if (Total <= 0.0f)
	{
		return TAG_Dust_Discovery_Artefact;
	}

	float Roll = FDustNoise::HashToUnit(SlotSeed) * Total;

	for (const FWeightedCategory& Entry : Mix)
	{
		Roll -= Entry.Weight;
		if (Roll <= 0.0f)
		{
			return Entry.Tag;
		}
	}

	return Mix.Last().Tag;
}

FGuid FDustPoiGenerator::MakeStableId(const FVector2D& Location, const FGameplayTag& Category, uint32 Seed)
{
	// A GUID derived from position and category rather than randomly generated,
	// so a POI keeps its identity across sessions, across regenerations, and
	// across the save file - which is what makes "you have already been here"
	// work without persisting a world snapshot.
	const int32 QX = FMath::RoundToInt32(Location.X);
	const int32 QY = FMath::RoundToInt32(Location.Y);
	const uint32 CategoryHash = GetTypeHash(Category);

	return FGuid(
		FDustNoise::Hash2(QX, QY, Seed),
		FDustNoise::Hash(static_cast<uint32>(QX) ^ CategoryHash),
		FDustNoise::Hash(static_cast<uint32>(QY) ^ Seed),
		FDustNoise::Hash(CategoryHash ^ 0x9E3779B9u));
}

bool FDustPoiGenerator::IsSuppressed(const FVector2D& Candidate) const
{
	for (const FHandAuthoredEntry& Entry : HandAuthored)
	{
		const FVector2D AuthoredXY(Entry.Poi.Location.X, Entry.Poi.Location.Y);
		if (FVector2D::DistSquared(Candidate, AuthoredXY) < FMath::Square(Entry.ExclusionRadius))
		{
			return true;
		}
	}
	return false;
}

void FDustPoiGenerator::RegisterHandAuthored(const FDustPointOfInterest& Poi, double ExclusionRadius)
{
	FHandAuthoredEntry Entry;
	Entry.Poi = Poi;
	Entry.Poi.bHandAuthored = true;
	Entry.ExclusionRadius = ExclusionRadius;

	HandAuthored.Add(MoveTemp(Entry));
}

void FDustPoiGenerator::ClearHandAuthored()
{
	HandAuthored.Reset();
}

void FDustPoiGenerator::GenerateSector(const FIntPoint& SectorCoord, TArray<FDustPointOfInterest>& OutPois) const
{
	DUST_SCOPED_TIMER(Dust_Poi_GenerateSector);

	if (!Sampler)
	{
		return;
	}

	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	const uint32 SectorSeed = FDustNoise::Hash2(SectorCoord.X, SectorCoord.Y, PoiSeed);

	const FVector2D SectorOrigin(
		static_cast<double>(SectorCoord.X) * SectorSize,
		static_cast<double>(SectorCoord.Y) * SectorSize);

	// The region at the sector centre determines the density and the mix.
	// Sampling once per sector rather than per candidate is a deliberate
	// approximation: at 512 m a sector is well inside a region's blend width,
	// and it saves a dozen region evaluations per sector.
	const FVector2D SectorCentre = SectorOrigin + FVector2D(SectorSize * 0.5, SectorSize * 0.5);

	float RegionWeight = 0.0f;
	const int32 RegionIndex = Sampler->SampleDominantRegion(SectorCentre, RegionWeight);
	const FDustRegionSnapshot* Region = Sampler->GetRegion(RegionIndex);

	if (!Region)
	{
		return;
	}

	// Convert the designer-facing density into a number of candidate slots to
	// actually evaluate. Candidates are then further thinned by the placement
	// rules, so the realised density is always at or below the authored figure -
	// which is the correct direction to err in a game about sparseness.
	//
	// Density is carried on the region asset, not the snapshot, so we recover
	// it from the profile's characteristic density. Hand-authored regions
	// override this through the composition's POI overlay.
	float DensityPerKm2 = 2.0f;
	switch (Region->Profile)
	{
	case EDustTerrainProfile::BuriedCity: DensityPerKm2 = 14.0f; break;
	case EDustTerrainProfile::Canyon:     DensityPerKm2 = 5.0f;  break;
	case EDustTerrainProfile::Badlands:   DensityPerKm2 = 4.0f;  break;
	case EDustTerrainProfile::Oasis:      DensityPerKm2 = 9.0f;  break;
	case EDustTerrainProfile::DryForest:  DensityPerKm2 = 6.0f;  break;
	case EDustTerrainProfile::Mountains:  DensityPerKm2 = 3.0f;  break;
	case EDustTerrainProfile::Volcanic:   DensityPerKm2 = 3.0f;  break;
	case EDustTerrainProfile::Mesa:       DensityPerKm2 = 3.5f;  break;
	case EDustTerrainProfile::Basin:      DensityPerKm2 = 2.5f;  break;
	case EDustTerrainProfile::DuneSea:    DensityPerKm2 = 0.6f;  break;
	case EDustTerrainProfile::SaltFlat:   DensityPerKm2 = 0.4f;  break;
	case EDustTerrainProfile::StormBelt:  DensityPerKm2 = 0.5f;  break;
	default: break;
	}

	const double TargetCount = static_cast<double>(DensityPerKm2) * DustPoi::SectorAreaKm2;

	// Fractional targets are resolved stochastically per sector rather than
	// rounded, so a density of 0.6/km2 does not silently become 0.
	const int32 SlotCount = FMath::Min(
		FMath::FloorToInt32(TargetCount) +
			((FDustNoise::HashToUnit(SectorSeed ^ 0xABCDu) < FMath::Frac(TargetCount)) ? 1 : 0),
		DustPoi::CandidateSlotsPerSector);

	const double MinSeparationSq = FMath::Square(Settings.MinimumInterestSeparation);

	// Track what we have accepted in this sector so we can enforce separation.
	TArray<FVector2D, TInlineAllocator<DustPoi::CandidateSlotsPerSector>> Accepted;

	for (int32 Slot = 0; Slot < SlotCount; ++Slot)
	{
		const uint32 SlotSeed = FDustNoise::Hash(SectorSeed + static_cast<uint32>(Slot) * 2654435761u);

		// Jittered position within the sector. Full jitter rather than a
		// stratified grid: the grid shows up as visible rows from the air.
		const FVector2D Candidate = SectorOrigin + FVector2D(
			static_cast<double>(FDustNoise::HashToUnit(SlotSeed)) * SectorSize,
			static_cast<double>(FDustNoise::HashToUnit(FDustNoise::Hash(SlotSeed ^ 0x77u))) * SectorSize);

		if (IsSuppressed(Candidate))
		{
			continue;
		}

		bool bTooClose = false;
		for (const FVector2D& Existing : Accepted)
		{
			if (FVector2D::DistSquared(Candidate, Existing) < MinSeparationSq)
			{
				bTooClose = true;
				break;
			}
		}
		if (bTooClose)
		{
			continue;
		}

		const FGameplayTag Category = PickCategory(*Region, SlotSeed ^ 0x1357u);
		const FPlacementRules Rules = GetRulesForCategory(Category);

		if (!PassesRules(Candidate, Rules))
		{
			continue;
		}

		FDustPointOfInterest Poi;
		Poi.Id = MakeStableId(Candidate, Category, PoiSeed);
		Poi.Location = FVector(Candidate.X, Candidate.Y, Sampler->SampleHeight(Candidate));
		Poi.Category = Category;
		Poi.State = EDustDiscoveryState::Unknown;
		Poi.SilhouetteRadius = Rules.SilhouetteRadius;
		Poi.bHandAuthored = false;

		// Significance varies per instance so that not every ruin is equally
		// worth the walk. The variation is what makes the exceptional ones
		// register as exceptional.
		Poi.Significance = FMath::Clamp(
			Rules.Significance * FMath::Lerp(0.7f, 1.25f, FDustNoise::HashToUnit(SlotSeed ^ 0x2468u)),
			0.0f, 1.0f);

		Accepted.Add(Candidate);
		OutPois.Add(MoveTemp(Poi));
	}
}

void FDustPoiGenerator::QueryRadius(const FVector2D& Centre, double Radius, TArray<FDustPointOfInterest>& OutPois) const
{
	DUST_SCOPED_TIMER(Dust_Poi_QueryRadius);

	if (!Sampler)
	{
		return;
	}

	const FIntPoint MinSector = WorldToSector(Centre - FVector2D(Radius, Radius));
	const FIntPoint MaxSector = WorldToSector(Centre + FVector2D(Radius, Radius));

	const double RadiusSq = Radius * Radius;

	TArray<FDustPointOfInterest> SectorPois;

	for (int32 Y = MinSector.Y; Y <= MaxSector.Y; ++Y)
	{
		for (int32 X = MinSector.X; X <= MaxSector.X; ++X)
		{
			SectorPois.Reset();
			GenerateSector(FIntPoint(X, Y), SectorPois);

			for (FDustPointOfInterest& Poi : SectorPois)
			{
				const FVector2D PoiXY(Poi.Location.X, Poi.Location.Y);
				if (FVector2D::DistSquared(PoiXY, Centre) <= RadiusSq)
				{
					OutPois.Add(MoveTemp(Poi));
				}
			}
		}
	}

	// Hand-authored content is always included, and always wins.
	for (const FHandAuthoredEntry& Entry : HandAuthored)
	{
		const FVector2D AuthoredXY(Entry.Poi.Location.X, Entry.Poi.Location.Y);
		if (FVector2D::DistSquared(AuthoredXY, Centre) <= RadiusSq)
		{
			OutPois.Add(Entry.Poi);
		}
	}
}
