// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "World/DustRegionData.h"

#include "DustGameplayTags.h"
#include "DustLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "DustRegionData"

FPrimaryAssetId UDustRegionData::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("DustRegion"), RegionId.IsNone() ? GetFName() : RegionId);
}

#if WITH_EDITOR

EDataValidationResult UDustRegionData::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (RegionId.IsNone())
	{
		Context.AddError(LOCTEXT("NoRegionId",
			"RegionId must be set. It is the save-file key and cannot be derived from the asset name."));
		Result = EDataValidationResult::Invalid;
	}

	if (DisplayName.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoDisplayName",
			"DisplayName is empty. The region will show as unnamed when the player first enters it."));
	}

	if (DesignIntent.IsEmpty())
	{
		Context.AddWarning(LOCTEXT("NoDesignIntent",
			"DesignIntent is empty. Every region must be able to answer 'why would anyone come here?'."));
	}

	if (FalloffRadius < CoreRadius * 0.2)
	{
		Context.AddWarning(LOCTEXT("HarshFalloff",
			"FalloffRadius is small relative to CoreRadius; the region boundary will read as a hard edge."));
	}

	// A region whose boundary warp exceeds its falloff can produce points where
	// no region has any influence, leaving holes filled by fallback terrain.
	if (BoundaryWarp > FalloffRadius)
	{
		Context.AddError(LOCTEXT("WarpExceedsFalloff",
			"BoundaryWarp exceeds FalloffRadius. This creates gaps between regions where fallback terrain shows through."));
		Result = EDataValidationResult::Invalid;
	}

	if (TerrainParams.Amplitude <= 0.0)
	{
		Context.AddError(LOCTEXT("ZeroAmplitude",
			"TerrainParams.Amplitude must be positive; a zero-amplitude region is a flat plate."));
		Result = EDataValidationResult::Invalid;
	}

	if (PoiDensityPerKm2 <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("NoPoi",
			"PoiDensityPerKm2 is zero. Nothing will ever be generated here, and the region will feel like filler."));
	}
	else if (PoiCategoryWeights.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoPoiCategories",
			"PoiDensityPerKm2 is non-zero but PoiCategoryWeights is empty; the generator has nothing to place."));
		Result = EDataValidationResult::Invalid;
	}

	// A region with no landmark cannot be navigated to, which breaks the
	// game's only wayfinding mechanism.
	if (!PoiCategoryWeights.Contains(TAG_Dust_Discovery_Landmark))
	{
		Context.AddWarning(LOCTEXT("NoLandmarks",
			"No Landmark weight. Regions without a silhouette readable at distance cannot be navigated to deliberately."));
	}

	if (ResidentSpecies.Num() == 0 && MigratorySpecies.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("NoFauna",
			"No resident or migratory species. Confirm this region is intended to feel lifeless."));
	}

	if (!AmbientAudioTag.IsValid())
	{
		Context.AddWarning(LOCTEXT("NoAmbientAudio",
			"AmbientAudioTag is unset; this region will fall back to the global wind bed."));
	}

	return Result;
}

EDataValidationResult UDustWorldComposition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Regions.Num() == 0)
	{
		Context.AddError(LOCTEXT("NoRegions", "World composition contains no regions."));
		return EDataValidationResult::Invalid;
	}

	TSet<FName> SeenIds;

	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		const UDustRegionData* Region = Regions[Index];

		if (!Region)
		{
			Context.AddError(FText::Format(
				LOCTEXT("NullRegion", "Region at index {0} is null."), Index));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		SeenIds.Add(Region->RegionId, &bAlreadySeen);

		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateRegionId", "Duplicate RegionId '{0}'. Save data keys on this and would collide."),
				FText::FromName(Region->RegionId)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// Overlap check: two regions of equal priority whose cores intersect will
	// average into terrain that is neither one thing nor the other.
	for (int32 A = 0; A < Regions.Num(); ++A)
	{
		for (int32 B = A + 1; B < Regions.Num(); ++B)
		{
			const UDustRegionData* RegionA = Regions[A];
			const UDustRegionData* RegionB = Regions[B];

			if (!RegionA || !RegionB || RegionA->Priority != RegionB->Priority)
			{
				continue;
			}

			const double Distance = FVector2D::Distance(RegionA->AnchorLocation, RegionB->AnchorLocation);
			if (Distance < RegionA->CoreRadius + RegionB->CoreRadius)
			{
				Context.AddWarning(FText::Format(
					LOCTEXT("CoreOverlap",
						"Region cores '{0}' and '{1}' overlap at equal priority; terrain will average into mush. Raise one region's Priority."),
					FText::FromName(RegionA->RegionId),
					FText::FromName(RegionB->RegionId)));
			}
		}
	}

	return Result;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
