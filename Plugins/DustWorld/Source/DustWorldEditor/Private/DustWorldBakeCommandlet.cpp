// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "DustWorldBakeCommandlet.h"

#include "Async/ParallelFor.h"
#include "DustDeveloperSettings.h"
#include "DustGameplayTags.h"
#include "DustLog.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Math/DustNoise.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "World/DustPoiGenerator.h"
#include "World/DustRegionData.h"
#include "World/DustWorldSampler.h"

UDustWorldBakeCommandlet::UDustWorldBakeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

bool UDustWorldBakeCommandlet::BuildSampler(FDustWorldSamplerConfig& OutConfig) const
{
	const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();

	static const TCHAR* CompositionPath =
		TEXT("/Game/Dust/World/DA_WorldComposition.DA_WorldComposition");

	const UDustWorldComposition* Composition = LoadObject<UDustWorldComposition>(nullptr, CompositionPath);
	if (!Composition)
	{
		UE_LOG(LogDustWorld, Error, TEXT("World composition not found at '%s'."), CompositionPath);
		return false;
	}

	OutConfig = FDustWorldSamplerConfig::FromComposition(*Composition, static_cast<uint32>(Settings.MasterSeed));
	OutConfig.WorldHalfExtent = Settings.WorldHalfExtent;
	OutConfig.BoundaryFalloff = Settings.WorldBoundaryFalloff;

	return true;
}

int32 UDustWorldBakeCommandlet::RunHeightmapExport(const FString& OutputDirectory, int32 TileResolution)
{
	FDustWorldSamplerConfig Config;
	if (!BuildSampler(Config))
	{
		return 1;
	}

	const FDustWorldSampler Sampler(Config);

	// Landscape wants (components * quads) + 1 samples per tile edge. 4033 is
	// 63 quads x 64 components + 1, the largest tile the importer handles well.
	TileResolution = FMath::Clamp(TileResolution, 505, 8129);

	const double WorldSize = Config.WorldHalfExtent * 2.0;

	// One tile per 2 km keeps each PNG under 32 MB and lets the import be
	// parallelised across machines.
	constexpr double TileWorldSize = 200000.0;
	const int32 TilesPerAxis = FMath::CeilToInt32(WorldSize / TileWorldSize);

	UE_LOG(LogDustWorld, Display,
		TEXT("Baking %d x %d tiles at %d x %d samples (%.1f km world)."),
		TilesPerAxis, TilesPerAxis, TileResolution, TileResolution, WorldSize / 100000.0);

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*OutputDirectory);

	// Elevation range across the whole world, needed to map heights into the
	// 16-bit landscape range. We compute it from a coarse pre-pass rather than
	// assuming, because a region tuning change moves it.
	double MinHeight = TNumericLimits<double>::Max();
	double MaxHeight = -TNumericLimits<double>::Max();

	{
		constexpr int32 ProbeResolution = 512;
		FCriticalSection RangeLock;

		ParallelFor(ProbeResolution, [&](int32 Y)
		{
			double LocalMin = TNumericLimits<double>::Max();
			double LocalMax = -TNumericLimits<double>::Max();

			for (int32 X = 0; X < ProbeResolution; ++X)
			{
				const FVector2D WorldXY(
					-Config.WorldHalfExtent + (static_cast<double>(X) / ProbeResolution) * WorldSize,
					-Config.WorldHalfExtent + (static_cast<double>(Y) / ProbeResolution) * WorldSize);

				const double Height = Sampler.SampleHeight(WorldXY);
				LocalMin = FMath::Min(LocalMin, Height);
				LocalMax = FMath::Max(LocalMax, Height);
			}

			FScopeLock Lock(&RangeLock);
			MinHeight = FMath::Min(MinHeight, LocalMin);
			MaxHeight = FMath::Max(MaxHeight, LocalMax);
		});

		// Pad the range so the coarse probe missing a peak does not clip it.
		const double Padding = (MaxHeight - MinHeight) * 0.08;
		MinHeight -= Padding;
		MaxHeight += Padding;
	}

	UE_LOG(LogDustWorld, Display,
		TEXT("Elevation range: %.1f m to %.1f m. Landscape Z scale should be %.4f."),
		MinHeight / 100.0, MaxHeight / 100.0,
		(MaxHeight - MinHeight) / 100.0 / 512.0);

	const double HeightRange = FMath::Max(MaxHeight - MinHeight, 1.0);

	int32 TilesWritten = 0;

	for (int32 TileY = 0; TileY < TilesPerAxis; ++TileY)
	{
		for (int32 TileX = 0; TileX < TilesPerAxis; ++TileX)
		{
			TArray<uint16> Heights;
			Heights.SetNumUninitialized(TileResolution * TileResolution);

			const double TileOriginX = -Config.WorldHalfExtent + TileX * TileWorldSize;
			const double TileOriginY = -Config.WorldHalfExtent + TileY * TileWorldSize;

			// Rows are independent, so the bake scales linearly with cores.
			// A 10 km world at this resolution is roughly four minutes on a
			// 16-core machine.
			ParallelFor(TileResolution, [&](int32 Y)
			{
				for (int32 X = 0; X < TileResolution; ++X)
				{
					const FVector2D WorldXY(
						TileOriginX + (static_cast<double>(X) / (TileResolution - 1)) * TileWorldSize,
						TileOriginY + (static_cast<double>(Y) / (TileResolution - 1)) * TileWorldSize);

					const double Height = Sampler.SampleHeight(WorldXY);
					const double Normalised = FMath::Clamp((Height - MinHeight) / HeightRange, 0.0, 1.0);

					Heights[Y * TileResolution + X] = static_cast<uint16>(Normalised * 65535.0);
				}
			});

			const TSharedPtr<IImageWrapper> ImageWrapper =
				ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

			if (!ImageWrapper.IsValid())
			{
				UE_LOG(LogDustWorld, Error, TEXT("Failed to create PNG wrapper."));
				return 1;
			}

			ImageWrapper->SetRaw(
				Heights.GetData(),
				Heights.Num() * sizeof(uint16),
				TileResolution, TileResolution,
				ERGBFormat::Gray, 16);

			const TArray64<uint8> Compressed = ImageWrapper->GetCompressed(100);

			const FString Filename = FString::Printf(
				TEXT("%s/DustHeight_x%02d_y%02d.png"), *OutputDirectory, TileX, TileY);

			if (!FFileHelper::SaveArrayToFile(Compressed, *Filename))
			{
				UE_LOG(LogDustWorld, Error, TEXT("Failed to write '%s'."), *Filename);
				return 1;
			}

			++TilesWritten;
			UE_LOG(LogDustWorld, Display, TEXT("Wrote %s"), *Filename);
		}
	}

	UE_LOG(LogDustWorld, Display, TEXT("Heightmap export complete: %d tiles."), TilesWritten);
	return 0;
}

int32 UDustWorldBakeCommandlet::RunValidation(const FString& OutputDirectory)
{
	FDustWorldSamplerConfig Config;
	if (!BuildSampler(Config))
	{
		return 1;
	}

	const FDustWorldSampler Sampler(Config);
	const FDustPoiGenerator Generator(&Sampler, Config.MasterSeed);

	TArray<FString> Failures;
	TArray<FString> Warnings;

	const double WorldSize = Config.WorldHalfExtent * 2.0;

	// --- Sweep 1: impassable terrain ---------------------------------------
	// A basin ringed entirely by slopes above the climb limit is a trap. We
	// sample on a coarse grid and flag any low point whose entire neighbourhood
	// exceeds what the traversal set can handle.
	{
		constexpr int32 GridResolution = 256;
		constexpr float MaxClimbableSlope = 62.0f; // free-climb ceiling

		FCriticalSection ResultLock;

		ParallelFor(GridResolution, [&](int32 Y)
		{
			TArray<FString> LocalFailures;

			for (int32 X = 0; X < GridResolution; ++X)
			{
				const FVector2D WorldXY(
					-Config.WorldHalfExtent + (static_cast<double>(X) / GridResolution) * WorldSize,
					-Config.WorldHalfExtent + (static_cast<double>(Y) / GridResolution) * WorldSize);

				const FDustGroundSample Sample = Sampler.SampleGround(WorldXY);

				if (Sample.SlopeDegrees <= MaxClimbableSlope)
				{
					continue;
				}

				// Steep is fine; steep with no escape is not. Check whether any
				// direction within 200 m offers a climbable exit.
				bool bHasEscape = false;
				for (int32 Direction = 0; Direction < 8 && !bHasEscape; ++Direction)
				{
					const double Angle = (2.0 * PI * Direction) / 8.0;
					const FVector2D Probe = WorldXY + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 20000.0;

					if (Sampler.SampleGround(Probe).SlopeDegrees <= MaxClimbableSlope)
					{
						bHasEscape = true;
					}
				}

				if (!bHasEscape)
				{
					LocalFailures.Add(FString::Printf(
						TEXT("Impassable pocket at (%.0f, %.0f): %.1f degree slope with no climbable exit within 200 m."),
						WorldXY.X, WorldXY.Y, Sample.SlopeDegrees));
				}
			}

			if (LocalFailures.Num() > 0)
			{
				FScopeLock Lock(&ResultLock);
				Failures.Append(LocalFailures);
			}
		});
	}

	// --- Sweep 2: interest deserts -----------------------------------------
	// The design promise is that the player is never far from something worth
	// walking to. This sweep is the promise expressed as a test.
	{
		const UDustDeveloperSettings& Settings = UDustDeveloperSettings::Get();
		constexpr int32 GridResolution = 64;

		int32 DesertCells = 0;

		for (int32 Y = 0; Y < GridResolution; ++Y)
		{
			for (int32 X = 0; X < GridResolution; ++X)
			{
				const FVector2D WorldXY(
					-Config.WorldHalfExtent + (static_cast<double>(X) / GridResolution) * WorldSize,
					-Config.WorldHalfExtent + (static_cast<double>(Y) / GridResolution) * WorldSize);

				TArray<FDustPointOfInterest> Pois;
				Generator.QueryRadius(WorldXY, Settings.GuaranteedInterestRadius, Pois);

				if (Pois.Num() == 0)
				{
					++DesertCells;
					Warnings.Add(FString::Printf(
						TEXT("No point of interest within %.1f km of (%.0f, %.0f)."),
						Settings.GuaranteedInterestRadius / 100000.0, WorldXY.X, WorldXY.Y));
				}
			}
		}

		const float DesertFraction = static_cast<float>(DesertCells) /
			static_cast<float>(GridResolution * GridResolution);

		if (DesertFraction > 0.02f)
		{
			Failures.Add(FString::Printf(
				TEXT("%.1f%% of the world has nothing within the guaranteed interest radius (budget: 2%%). "
				     "The curiosity director cannot cover this much emptiness."),
				DesertFraction * 100.0f));
		}
	}

	// --- Sweep 3: water above its rim --------------------------------------
	{
		for (const FDustRegionSnapshot& Region : Config.Regions)
		{
			if (Region.Terrain.WaterLevel <= -1.0e8)
			{
				continue;
			}

			const double WaterHeight = Region.Terrain.BaseElevation + Region.Terrain.WaterLevel;

			// Probe the rim: if terrain at the region edge sits below the water
			// surface, the lake spills out into the surrounding desert.
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const double Angle = (2.0 * PI * Direction) / 16.0;
				const FVector2D RimPoint = Region.Anchor +
					FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Region.CoreRadius;

				const double RimHeight = Sampler.SampleHeight(RimPoint);

				if (RimHeight < WaterHeight)
				{
					Failures.Add(FString::Printf(
						TEXT("Region '%s' water surface (%.1f m) is above its rim at bearing %.0f degrees (%.1f m). Water will spill."),
						*Region.RegionId.ToString(), WaterHeight / 100.0,
						FMath::RadiansToDegrees(Angle), RimHeight / 100.0));
					break;
				}
			}
		}
	}

	// --- Report -------------------------------------------------------------
	FString Report;
	Report += FString::Printf(TEXT("DUST world validation\n"));
	Report += FString::Printf(TEXT("Seed: %u  Terrain algorithm: v%d\n"),
		Config.MasterSeed, FDustNoise::AlgorithmVersion);
	Report += FString::Printf(TEXT("Regions: %d\n\n"), Config.Regions.Num());

	Report += FString::Printf(TEXT("FAILURES (%d)\n"), Failures.Num());
	for (const FString& Failure : Failures)
	{
		Report += TEXT("  ") + Failure + TEXT("\n");
	}

	Report += FString::Printf(TEXT("\nWARNINGS (%d)\n"), Warnings.Num());
	for (int32 Index = 0; Index < FMath::Min(Warnings.Num(), 200); ++Index)
	{
		Report += TEXT("  ") + Warnings[Index] + TEXT("\n");
	}
	if (Warnings.Num() > 200)
	{
		Report += FString::Printf(TEXT("  ... and %d more\n"), Warnings.Num() - 200);
	}

	const FString ReportPath = OutputDirectory / TEXT("WorldValidation.txt");
	FFileHelper::SaveStringToFile(Report, *ReportPath);

	UE_LOG(LogDustWorld, Display, TEXT("%s"), *Report);
	UE_LOG(LogDustWorld, Display, TEXT("Validation report written to %s"), *ReportPath);

	return Failures.Num() > 0 ? 1 : 0;
}

int32 UDustWorldBakeCommandlet::RunStatistics(const FString& OutputDirectory)
{
	FDustWorldSamplerConfig Config;
	if (!BuildSampler(Config))
	{
		return 1;
	}

	const FDustWorldSampler Sampler(Config);
	const FDustPoiGenerator Generator(&Sampler, Config.MasterSeed);

	const double WorldSize = Config.WorldHalfExtent * 2.0;
	constexpr int32 GridResolution = 384;

	TArray<int32> RegionCellCounts;
	RegionCellCounts.SetNumZeroed(Config.Regions.Num());

	TMap<EDustSurface, int32> SurfaceCounts;
	double TotalHeight = 0.0;
	double PeakHeight = -TNumericLimits<double>::Max();
	double LowestHeight = TNumericLimits<double>::Max();

	for (int32 Y = 0; Y < GridResolution; ++Y)
	{
		for (int32 X = 0; X < GridResolution; ++X)
		{
			const FVector2D WorldXY(
				-Config.WorldHalfExtent + (static_cast<double>(X) / GridResolution) * WorldSize,
				-Config.WorldHalfExtent + (static_cast<double>(Y) / GridResolution) * WorldSize);

			const FDustGroundSample Sample = Sampler.SampleGround(WorldXY);

			if (RegionCellCounts.IsValidIndex(Sample.DominantRegionIndex))
			{
				++RegionCellCounts[Sample.DominantRegionIndex];
			}

			SurfaceCounts.FindOrAdd(Sample.Surface)++;

			TotalHeight += Sample.Height;
			PeakHeight = FMath::Max(PeakHeight, Sample.Height);
			LowestHeight = FMath::Min(LowestHeight, Sample.Height);
		}
	}

	const double TotalCells = static_cast<double>(GridResolution * GridResolution);
	const double WorldAreaKm2 = (WorldSize / 100000.0) * (WorldSize / 100000.0);

	FString Report;
	Report += TEXT("DUST world statistics\n");
	Report += TEXT("=====================\n\n");
	Report += FString::Printf(TEXT("World area: %.1f km2\n"), WorldAreaKm2);
	Report += FString::Printf(TEXT("Elevation: %.0f m to %.0f m (mean %.0f m)\n\n"),
		LowestHeight / 100.0, PeakHeight / 100.0, (TotalHeight / TotalCells) / 100.0);

	Report += TEXT("REGION COVERAGE\n");
	for (int32 Index = 0; Index < Config.Regions.Num(); ++Index)
	{
		const double Fraction = RegionCellCounts[Index] / TotalCells;
		Report += FString::Printf(TEXT("  %-24s %6.2f%%  (%.1f km2)\n"),
			*Config.Regions[Index].RegionId.ToString(),
			Fraction * 100.0,
			Fraction * WorldAreaKm2);
	}

	Report += TEXT("\nSURFACE DISTRIBUTION\n");
	for (const TPair<EDustSurface, int32>& Pair : SurfaceCounts)
	{
		Report += FString::Printf(TEXT("  Surface %2d  %6.2f%%\n"),
			static_cast<int32>(Pair.Key),
			(Pair.Value / TotalCells) * 100.0);
	}

	// POI census over a representative transect rather than the whole world,
	// which would take hours; the sector generator is uniform enough that a
	// 10% sample is within a couple of percent of the true count.
	{
		TMap<FGameplayTag, int32> CategoryCounts;
		int32 SampledSectors = 0;

		const int32 SectorsPerAxis = FMath::CeilToInt32(WorldSize / FDustPoiGenerator::SectorSize);
		const int32 Stride = FMath::Max(1, SectorsPerAxis / 32);

		for (int32 SY = 0; SY < SectorsPerAxis; SY += Stride)
		{
			for (int32 SX = 0; SX < SectorsPerAxis; SX += Stride)
			{
				TArray<FDustPointOfInterest> Pois;
				Generator.GenerateSector(
					FIntPoint(SX - SectorsPerAxis / 2, SY - SectorsPerAxis / 2), Pois);

				for (const FDustPointOfInterest& Poi : Pois)
				{
					CategoryCounts.FindOrAdd(Poi.Category)++;
				}
				++SampledSectors;
			}
		}

		const double SampleFraction = static_cast<double>(SampledSectors) /
			FMath::Max(1.0, static_cast<double>(SectorsPerAxis) * SectorsPerAxis);

		Report += FString::Printf(TEXT("\nPOINTS OF INTEREST (extrapolated from a %.1f%% sector sample)\n"),
			SampleFraction * 100.0);

		int32 EstimatedTotal = 0;
		for (const TPair<FGameplayTag, int32>& Pair : CategoryCounts)
		{
			const int32 Estimate = FMath::RoundToInt32(Pair.Value / FMath::Max(SampleFraction, KINDA_SMALL_NUMBER));
			EstimatedTotal += Estimate;
			Report += FString::Printf(TEXT("  %-40s %6d\n"), *Pair.Key.ToString(), Estimate);
		}
		Report += FString::Printf(TEXT("  %-40s %6d\n"), TEXT("TOTAL"), EstimatedTotal);
		Report += FString::Printf(TEXT("  %-40s %6.2f\n"), TEXT("per km2"), EstimatedTotal / WorldAreaKm2);
	}

	const FString ReportPath = OutputDirectory / TEXT("WorldStatistics.txt");
	FFileHelper::SaveStringToFile(Report, *ReportPath);

	UE_LOG(LogDustWorld, Display, TEXT("%s"), *Report);
	return 0;
}

int32 UDustWorldBakeCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString Mode = ParamMap.FindRef(TEXT("mode")).IsEmpty()
		? TEXT("all")
		: ParamMap.FindRef(TEXT("mode"));

	FString OutputDirectory = ParamMap.FindRef(TEXT("out"));
	if (OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::ProjectSavedDir() / TEXT("WorldBake");
	}

	int32 Resolution = 4033;
	if (const FString* ResolutionParam = ParamMap.Find(TEXT("resolution")))
	{
		Resolution = FCString::Atoi(**ResolutionParam);
	}

	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*OutputDirectory);

	int32 Result = 0;

	if (Mode == TEXT("heightmap") || Mode == TEXT("all"))
	{
		Result |= RunHeightmapExport(OutputDirectory, Resolution);
	}
	if (Mode == TEXT("validate") || Mode == TEXT("all"))
	{
		Result |= RunValidation(OutputDirectory);
	}
	if (Mode == TEXT("stats") || Mode == TEXT("all"))
	{
		Result |= RunStatistics(OutputDirectory);
	}

	return Result;
}
