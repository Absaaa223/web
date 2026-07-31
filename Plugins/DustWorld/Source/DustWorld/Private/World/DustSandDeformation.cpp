// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "World/DustSandDeformation.h"

#include "DustLog.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TextureResource.h"

namespace DustSandCVars
{
	static int32 SandDeformationEnabled = 1;
	static FAutoConsoleVariableRef CVarSandDeformation(
		TEXT("dust.sand.Deformation"),
		SandDeformationEnabled,
		TEXT("Enable persistent sand deformation (footprints, tracks, slides)."),
		ECVF_Scalability);

	static float HealRateScale = 1.0f;
	static FAutoConsoleVariableRef CVarHealRate(
		TEXT("dust.sand.HealRateScale"),
		HealRateScale,
		TEXT("Multiplier on how fast footprints are erased by the wind. 0 makes tracks permanent."),
		ECVF_Cheat);
}

UDustSandDeformationSubsystem* UDustSandDeformationSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustSandDeformationSubsystem>() : nullptr;
}

bool UDustSandDeformationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustSandDeformationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CreateResources();
}

void UDustSandDeformationSubsystem::Deinitialize()
{
	DepthBuffer.Empty();
	DeformationTexture = nullptr;
	Super::Deinitialize();
}

TStatId UDustSandDeformationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustSandDeformationSubsystem, STATGROUP_Tickables);
}

void UDustSandDeformationSubsystem::CreateResources()
{
	TexelSize = FieldExtent / static_cast<float>(Resolution);

	DepthBuffer.SetNumZeroed(Resolution * Resolution);
	DirtyRows.Init(false, Resolution);

	// A single-channel 16-bit float texture: 2 MB, enough precision for
	// centimetre depths up to a couple of metres, and it samples natively in
	// the material without an unpack.
	DeformationTexture = UTexture2D::CreateTransient(Resolution, Resolution, PF_R16F);

	if (!DeformationTexture)
	{
		UE_LOG(LogDustWorld, Error, TEXT("Failed to create sand deformation texture."));
		return;
	}

	DeformationTexture->SRGB = false;
	DeformationTexture->Filter = TF_Bilinear;
	DeformationTexture->AddressX = TA_Wrap;
	DeformationTexture->AddressY = TA_Wrap;
	DeformationTexture->CompressionSettings = TC_HDR;
	DeformationTexture->NeverStream = true;
	DeformationTexture->UpdateResource();

	UE_LOG(LogDustWorld, Log,
		TEXT("Sand deformation field: %dx%d over %.0f cm (%.1f cm/texel)."),
		Resolution, Resolution, FieldExtent, TexelSize);
}

void UDustSandDeformationSubsystem::ClearField()
{
	FMemory::Memzero(DepthBuffer.GetData(), DepthBuffer.Num() * sizeof(float));
	DirtyRows.Init(true, Resolution);
}

bool UDustSandDeformationSubsystem::WorldToTexel(const FVector2D& WorldXY, int32& OutX, int32& OutY) const
{
	// Absolute texel coordinates: the world is tiled by the texture, so a given
	// world position always maps to the same texel index modulo the resolution.
	const int32 AbsoluteX = FMath::FloorToInt32(WorldXY.X / TexelSize);
	const int32 AbsoluteY = FMath::FloorToInt32(WorldXY.Y / TexelSize);

	// Reject anything outside the currently covered window, otherwise a distant
	// impact would alias onto a texel near the player.
	const int32 RelativeX = AbsoluteX - FieldOriginTexel.X;
	const int32 RelativeY = AbsoluteY - FieldOriginTexel.Y;

	if (RelativeX < 0 || RelativeX >= Resolution || RelativeY < 0 || RelativeY >= Resolution)
	{
		return false;
	}

	OutX = AbsoluteX;
	OutY = AbsoluteY;
	return true;
}

void UDustSandDeformationSubsystem::UpdateFieldCentre(const FVector& PlayerLocation)
{
	// Keep the player at the centre of the window.
	const int32 DesiredOriginX = FMath::FloorToInt32(PlayerLocation.X / TexelSize) - Resolution / 2;
	const int32 DesiredOriginY = FMath::FloorToInt32(PlayerLocation.Y / TexelSize) - Resolution / 2;

	const int32 DeltaX = DesiredOriginX - FieldOriginTexel.X;
	const int32 DeltaY = DesiredOriginY - FieldOriginTexel.Y;

	if (DeltaX == 0 && DeltaY == 0)
	{
		return;
	}

	// A jump larger than the field means a teleport; nothing is salvageable.
	if (FMath::Abs(DeltaX) >= Resolution || FMath::Abs(DeltaY) >= Resolution)
	{
		FieldOriginTexel = FIntPoint(DesiredOriginX, DesiredOriginY);
		ClearField();
		return;
	}

	// Clear the columns and rows that just scrolled into view. Because
	// addressing is toroidal we clear the texels that the *old* far edge
	// occupied - they are the same memory the new near edge will use.
	if (DeltaX != 0)
	{
		const int32 Start = (DeltaX > 0) ? FieldOriginTexel.X + Resolution : DesiredOriginX;
		const int32 Count = FMath::Abs(DeltaX);

		for (int32 Y = 0; Y < Resolution; ++Y)
		{
			const int32 RowBase = WrapTexel(FieldOriginTexel.Y + Y) * Resolution;
			for (int32 Offset = 0; Offset < Count; ++Offset)
			{
				DepthBuffer[RowBase + WrapTexel(Start + Offset)] = 0.0f;
			}
			DirtyRows[WrapTexel(FieldOriginTexel.Y + Y)] = true;
		}
	}

	if (DeltaY != 0)
	{
		const int32 Start = (DeltaY > 0) ? FieldOriginTexel.Y + Resolution : DesiredOriginY;
		const int32 Count = FMath::Abs(DeltaY);

		for (int32 Offset = 0; Offset < Count; ++Offset)
		{
			const int32 Row = WrapTexel(Start + Offset);
			FMemory::Memzero(&DepthBuffer[Row * Resolution], Resolution * sizeof(float));
			DirtyRows[Row] = true;
		}
	}

	FieldOriginTexel = FIntPoint(DesiredOriginX, DesiredOriginY);
}

void UDustSandDeformationSubsystem::AddImpact(const FDustSandImpact& Impact)
{
	if (DustSandCVars::SandDeformationEnabled == 0)
	{
		return;
	}

	FScopeLock Lock(&ImpactLock);
	PendingImpacts.Add(Impact);
}

void UDustSandDeformationSubsystem::AddFootstep(
	const FVector& Location, const FVector& Forward, float FootRadius, float Weight)
{
	FDustSandImpact Impact;
	Impact.Location = Location;
	Impact.Radius = FMath::Max(FootRadius, 1.0f);

	// Depth scales with weight but saturates: a heavier creature does not sink
	// proportionally deeper, it just compacts the sand under it.
	Impact.Depth = 7.5f * FMath::Sqrt(FMath::Clamp(Weight, 0.0f, 4.0f));

	Impact.Type = EDustImpactType::Footstep;
	Impact.Direction = FVector2D(Forward.X, Forward.Y).GetSafeNormal();

	AddImpact(Impact);
}

void UDustSandDeformationSubsystem::FlushImpacts()
{
	TArray<FDustSandImpact> Impacts;
	{
		FScopeLock Lock(&ImpactLock);
		if (PendingImpacts.Num() == 0)
		{
			return;
		}
		Impacts = MoveTemp(PendingImpacts);
		PendingImpacts.Reset();
	}

	DUST_SCOPED_TIMER(Dust_Sand_FlushImpacts);

	for (const FDustSandImpact& Impact : Impacts)
	{
		int32 CentreX = 0;
		int32 CentreY = 0;
		if (!WorldToTexel(FVector2D(Impact.Location.X, Impact.Location.Y), CentreX, CentreY))
		{
			continue;
		}

		const int32 TexelRadius = FMath::CeilToInt32(Impact.Radius / TexelSize);
		if (TexelRadius <= 0)
		{
			continue;
		}

		// The rim: sand displaced by a footfall has to go somewhere, and it
		// piles up around the print. Without this, footprints read as holes
		// punched in the ground rather than as displacement.
		const float RimHeight = Impact.Depth * 0.28f;
		const float RimStart = 0.72f;

		for (int32 OffsetY = -TexelRadius; OffsetY <= TexelRadius; ++OffsetY)
		{
			const int32 TexelY = CentreY + OffsetY;
			bool bRowTouched = false;

			for (int32 OffsetX = -TexelRadius; OffsetX <= TexelRadius; ++OffsetX)
			{
				const float DistanceTexels = FMath::Sqrt(
					static_cast<float>(OffsetX * OffsetX + OffsetY * OffsetY));

				float Normalised = DistanceTexels / static_cast<float>(TexelRadius);
				if (Normalised > 1.15f)
				{
					continue;
				}

				// Elongate along the direction of travel for tracks and slides.
				if (Impact.Type == EDustImpactType::Track || Impact.Type == EDustImpactType::Slide)
				{
					const FVector2D Offset(static_cast<float>(OffsetX), static_cast<float>(OffsetY));
					const float Along = FMath::Abs(FVector2D::DotProduct(Offset, Impact.Direction));
					const float Across = FMath::Abs(FVector2D::CrossProduct(Offset, Impact.Direction));

					// 2.5:1 aspect along the travel direction.
					Normalised = FMath::Sqrt(FMath::Square(Along / 2.5f) + FMath::Square(Across))
						/ static_cast<float>(TexelRadius);
				}

				float Delta = 0.0f;

				if (Normalised <= RimStart)
				{
					// Depression: a smooth bowl, deepest at the centre.
					const float T = Normalised / RimStart;
					Delta = Impact.Depth * (1.0f - T * T);
				}
				else if (Normalised <= 1.15f)
				{
					// Rim: displaced material piled around the edge.
					const float T = (Normalised - RimStart) / (1.15f - RimStart);
					Delta = -RimHeight * FMath::Sin(T * PI);
				}

				if (FMath::IsNearlyZero(Delta))
				{
					continue;
				}

				const int32 Index = BufferIndex(CentreX + OffsetX, TexelY);
				float& Existing = DepthBuffer[Index];

				// Accumulate, but saturate: walking over the same spot a
				// hundred times must not dig a pit to the centre of the earth.
				Existing = FMath::Clamp(Existing + Delta, -40.0f, 70.0f);

				bRowTouched = true;
			}

			if (bRowTouched)
			{
				DirtyRows[WrapTexel(TexelY)] = true;
			}
		}
	}
}

void UDustSandDeformationSubsystem::ApplyHealing(float DeltaSeconds)
{
	DUST_SCOPED_TIMER(Dust_Sand_Healing);

	// Healing rate scales with wind. The constants are tuned so that:
	//   still air (0 cm/s)      -> half-life ~180 s, tracks persist for minutes
	//   breeze (400 cm/s)       -> half-life ~35 s
	//   sandstorm (2200 cm/s)   -> half-life ~4 s, you watch them vanish
	const float WindMetresPerSecond = WindSpeed / 100.0f;
	const float HalfLife = FMath::Max(
		180.0f / (1.0f + WindMetresPerSecond * 2.1f),
		1.0f);

	const float Rate = DustSandCVars::HealRateScale;
	if (Rate <= 0.0f)
	{
		return;
	}

	// Exponential decay toward zero.
	const float Retain = FMath::Pow(0.5f, (DeltaSeconds * Rate) / HalfLife);

	// Only walk rows that hold something. Once the field has settled this is
	// most of the buffer skipped, which is why healing costs almost nothing
	// when the player is standing still on rock.
	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		float* RowData = &DepthBuffer[Row * Resolution];

		bool bRowHasContent = false;
		for (int32 Column = 0; Column < Resolution; ++Column)
		{
			float& Value = RowData[Column];
			if (FMath::Abs(Value) < 0.02f)
			{
				Value = 0.0f;
				continue;
			}

			Value *= Retain;
			bRowHasContent = true;
		}

		if (bRowHasContent)
		{
			DirtyRows[Row] = true;
		}
	}
}

void UDustSandDeformationSubsystem::UploadDirtyRegions()
{
	if (!DeformationTexture || !DeformationTexture->GetResource())
	{
		return;
	}

	// Find contiguous dirty spans so we issue a handful of updates rather than
	// one per row.
	int32 SpanStart = INDEX_NONE;

	auto FlushSpan = [this, &SpanStart](int32 SpanEnd)
	{
		if (SpanStart == INDEX_NONE)
		{
			return;
		}

		const int32 RowCount = SpanEnd - SpanStart;
		const int32 TexelCount = RowCount * Resolution;

		// The staging buffer is consumed asynchronously on the render thread,
		// so it must be a raw allocation that outlives this scope rather than a
		// TArray whose storage dies at the closing brace. UpdateTextureRegions
		// takes ownership and the cleanup lambda below frees it once the copy
		// has actually landed.
		FFloat16* Staging = new FFloat16[TexelCount];

		for (int32 Index = 0; Index < TexelCount; ++Index)
		{
			Staging[Index] = FFloat16(DepthBuffer[SpanStart * Resolution + Index]);
		}

		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(
			0, SpanStart, 0, SpanStart, Resolution, RowCount);

		DeformationTexture->UpdateTextureRegions(
			0, 1, Region,
			Resolution * sizeof(FFloat16),
			sizeof(FFloat16),
			reinterpret_cast<uint8*>(Staging),
			[](uint8* Data, const FUpdateTextureRegion2D* Regions)
			{
				delete[] reinterpret_cast<FFloat16*>(Data);
				delete Regions;
			});

		SpanStart = INDEX_NONE;
	};

	for (int32 Row = 0; Row < Resolution; ++Row)
	{
		if (DirtyRows[Row])
		{
			if (SpanStart == INDEX_NONE)
			{
				SpanStart = Row;
			}
		}
		else if (SpanStart != INDEX_NONE)
		{
			FlushSpan(Row);
		}
	}
	FlushSpan(Resolution);

	DirtyRows.Init(false, Resolution);
}

float UDustSandDeformationSubsystem::GetDeformationAt(const FVector& WorldLocation) const
{
	int32 TexelX = 0;
	int32 TexelY = 0;
	if (!WorldToTexel(FVector2D(WorldLocation.X, WorldLocation.Y), TexelX, TexelY))
	{
		return 0.0f;
	}

	return DepthBuffer[BufferIndex(TexelX, TexelY)];
}

float UDustSandDeformationSubsystem::GetDisturbanceInRadius(const FVector& WorldLocation, float Radius) const
{
	int32 CentreX = 0;
	int32 CentreY = 0;
	if (!WorldToTexel(FVector2D(WorldLocation.X, WorldLocation.Y), CentreX, CentreY))
	{
		return 0.0f;
	}

	const int32 TexelRadius = FMath::Clamp(FMath::CeilToInt32(Radius / TexelSize), 1, 64);

	float Sum = 0.0f;
	int32 Count = 0;

	for (int32 OffsetY = -TexelRadius; OffsetY <= TexelRadius; ++OffsetY)
	{
		for (int32 OffsetX = -TexelRadius; OffsetX <= TexelRadius; ++OffsetX)
		{
			if (OffsetX * OffsetX + OffsetY * OffsetY > TexelRadius * TexelRadius)
			{
				continue;
			}

			Sum += FMath::Abs(DepthBuffer[BufferIndex(CentreX + OffsetX, CentreY + OffsetY)]);
			++Count;
		}
	}

	if (Count == 0)
	{
		return 0.0f;
	}

	// Normalise against a footstep's depth so 1.0 means "thoroughly churned".
	return FMath::Clamp((Sum / static_cast<float>(Count)) / 7.5f, 0.0f, 1.0f);
}

void UDustSandDeformationSubsystem::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Sand_Tick);

	if (DustSandCVars::SandDeformationEnabled == 0)
	{
		return;
	}

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

	UpdateFieldCentre(Pawn->GetActorLocation());
	FlushImpacts();

	HealAccumulator += DeltaTime;
	if (HealAccumulator >= HealInterval)
	{
		ApplyHealing(HealAccumulator);
		HealAccumulator = 0.0f;
	}

	UploadDirtyRegions();
}
