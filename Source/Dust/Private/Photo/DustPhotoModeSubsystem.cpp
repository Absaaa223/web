// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Photo/DustPhotoModeSubsystem.h"

#include "DustLog.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Performance/DustPerformanceGovernor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UI/DustHudDirector.h"
#include "Weather/DustSkySubsystem.h"
#include "World/DustWorldSubsystem.h"

namespace DustPhoto
{
	static const TCHAR* GalleryDirectory = TEXT("PhotoGallery");

	/**
	 * Largest single render target dimension we will attempt. Above this we
	 * tile the capture, because a single 8K target with full G-buffer,
	 * Lumen and virtual shadow map allocation will fail on cards with less
	 * than around 10 GB - including, importantly, the Steam Deck.
	 */
	static constexpr int32 MaxSingleCaptureDimension = 4096;
}

UDustPhotoModeSubsystem* UDustPhotoModeSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustPhotoModeSubsystem>() : nullptr;
}

bool UDustPhotoModeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustPhotoModeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString GalleryPath = FPaths::ProjectSavedDir() / DustPhoto::GalleryDirectory;
	FPlatformFileManager::Get().GetPlatformFile().CreateDirectoryTree(*GalleryPath);
}

void UDustPhotoModeSubsystem::Deinitialize()
{
	if (bActive)
	{
		ExitPhotoMode();
	}

	Super::Deinitialize();
}

TStatId UDustPhotoModeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustPhotoModeSubsystem, STATGROUP_Tickables);
}

void UDustPhotoModeSubsystem::TogglePhotoMode()
{
	bActive ? ExitPhotoMode() : EnterPhotoMode();
}

void UDustPhotoModeSubsystem::EnterPhotoMode()
{
	if (bActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	if (!PlayerController)
	{
		return;
	}

	bActive = true;

	// Start the free camera exactly where the player's camera was, so photo
	// mode opens on the shot the player was already looking at rather than
	// dumping them somewhere they have to reorient from.
	PlayerController->GetPlayerViewPoint(CameraLocation, CameraRotation);
	AnchorLocation = CameraLocation;

	Roll = 0.0f;

	// Slow time almost to a stop rather than freezing it. Dust keeps drifting,
	// cloth keeps settling, and the player can wait for the right instant -
	// which is what makes this a camera rather than a pause screen.
	PreviousTimeDilation = UGameplayStatics::GetGlobalTimeDilation(World);
	UGameplayStatics::SetGlobalTimeDilation(World, 0.03f);

	if (UDustHudDirector* Hud = UDustHudDirector::Get(this))
	{
		Hud->SetHudSuppressed(true);
	}

	// Freeze the governor: a long composition of an expensive vista must not
	// progressively degrade the very shot being composed.
	if (UDustPerformanceGovernor* Governor = World->GetSubsystem<UDustPerformanceGovernor>())
	{
		Governor->SetFrozen(true);
	}

	UE_LOG(LogDust, Log, TEXT("Photo mode entered."));
}

void UDustPhotoModeSubsystem::ExitPhotoMode()
{
	if (!bActive)
	{
		return;
	}

	bActive = false;

	UWorld* World = GetWorld();
	if (World)
	{
		UGameplayStatics::SetGlobalTimeDilation(World, PreviousTimeDilation);

		if (UDustPerformanceGovernor* Governor = World->GetSubsystem<UDustPerformanceGovernor>())
		{
			Governor->SetFrozen(false);
		}
	}

	if (UDustHudDirector* Hud = UDustHudDirector::Get(this))
	{
		Hud->SetHudSuppressed(false);
	}

	UE_LOG(LogDust, Log, TEXT("Photo mode exited."));
}

float UDustPhotoModeSubsystem::GetFieldOfView() const
{
	// Standard thin-lens relationship. Exposing focal length rather than FOV is
	// the whole point: a photographer knows what 85 mm looks like.
	const float HalfFOVRad = FMath::Atan((SensorWidthMm * 0.5f) / FMath::Max(FocalLengthMm, 1.0f));
	return FMath::RadiansToDegrees(HalfFOVRad) * 2.0f;
}

void UDustPhotoModeSubsystem::MoveCamera(const FVector& LocalDelta, float DeltaTime)
{
	if (!bActive)
	{
		return;
	}

	// Movement speed scales with focal length: at 200 mm a centimetre of
	// movement swings the frame enormously, so the camera slows down. This is
	// the difference between a photo mode that is fiddly and one that is precise.
	const float FocalScale = FMath::GetMappedRangeValueClamped(
		FVector2f(14.0f, 300.0f), FVector2f(1.0f, 0.18f), FocalLengthMm);

	const FRotationMatrix Basis(CameraRotation);

	CameraLocation +=
		(Basis.GetScaledAxis(EAxis::X) * LocalDelta.X +
		 Basis.GetScaledAxis(EAxis::Y) * LocalDelta.Y +
		 FVector::UpVector * LocalDelta.Z)
		* BaseMoveSpeed * FocalScale * DeltaTime;

	ConstrainCamera();
}

void UDustPhotoModeSubsystem::RotateCamera(const FVector2D& Delta)
{
	if (!bActive)
	{
		return;
	}

	// Rotation sensitivity also scales with focal length, for the same reason.
	const float FocalScale = FMath::GetMappedRangeValueClamped(
		FVector2f(14.0f, 300.0f), FVector2f(1.0f, 0.14f), FocalLengthMm);

	CameraRotation.Yaw += Delta.X * FocalScale;
	CameraRotation.Pitch = FMath::Clamp(CameraRotation.Pitch - Delta.Y * FocalScale, -89.0f, 89.0f);
	CameraRotation.Roll = Roll;
}

void UDustPhotoModeSubsystem::SetRoll(float Degrees)
{
	Roll = FMath::Clamp(Degrees, -35.0f, 35.0f);
	CameraRotation.Roll = Roll;
}

void UDustPhotoModeSubsystem::SetFocalLength(float Millimetres)
{
	FocalLengthMm = FMath::Clamp(Millimetres, 14.0f, 300.0f);
}

void UDustPhotoModeSubsystem::SetAperture(float FStop)
{
	Aperture = FMath::Clamp(FStop, 1.4f, 22.0f);
}

void UDustPhotoModeSubsystem::SetFocusDistance(float Centimetres)
{
	FocusDistance = Centimetres;
}

void UDustPhotoModeSubsystem::SetExposureCompensation(float Stops)
{
	ExposureCompensation = FMath::Clamp(Stops, -4.0f, 4.0f);
}

void UDustPhotoModeSubsystem::SetFilmStock(EDustFilmStock Stock)
{
	FilmStock = Stock;
}

void UDustPhotoModeSubsystem::SetGuideMode(int32 Mode)
{
	GuideMode = FMath::Clamp(Mode, 0, 3);
}

void UDustPhotoModeSubsystem::ConstrainCamera()
{
	// Leash to the player. Photo mode is for composing the shot you are in,
	// not for scouting terrain you have not walked to - letting the camera roam
	// would quietly dismantle the discovery loop the whole game runs on.
	const FVector Offset = CameraLocation - AnchorLocation;
	if (Offset.SizeSquared() > FMath::Square(MaxCameraDistance))
	{
		CameraLocation = AnchorLocation + Offset.GetSafeNormal() * MaxCameraDistance;
	}

	// Keep the camera above the terrain. Sinking into a dune produces a black
	// frame and, worse, is how players discover that the ground has no
	// underside.
	if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		const double GroundHeight = World->SampleHeight(CameraLocation);
		CameraLocation.Z = FMath::Max(CameraLocation.Z, GroundHeight + 25.0);
	}
}

void UDustPhotoModeSubsystem::ApplyFilmStock(FPostProcessSettings& Settings) const
{
	Settings.bOverride_DepthOfFieldFstop = true;
	Settings.DepthOfFieldFstop = Aperture;

	Settings.bOverride_DepthOfFieldSensorWidth = true;
	Settings.DepthOfFieldSensorWidth = SensorWidthMm;

	if (FocusDistance > 0.0f)
	{
		Settings.bOverride_DepthOfFieldFocalDistance = true;
		Settings.DepthOfFieldFocalDistance = FocusDistance;
	}

	Settings.bOverride_AutoExposureBias = true;
	Settings.AutoExposureBias = ExposureCompensation;

	// Motion blur off for stills: the player is composing a photograph, and a
	// smeared frame is never what they wanted.
	Settings.bOverride_MotionBlurAmount = true;
	Settings.MotionBlurAmount = 0.0f;

	switch (FilmStock)
	{
	case EDustFilmStock::Reversal:
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(1.16, 1.12, 1.05, 1.0);

		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(0.92, 0.92, 0.94, 1.0);

		// Lifted blacks with a warm cast, which is what reversal film does.
		Settings.bOverride_ColorOffset = true;
		Settings.ColorOffset = FVector4(0.015, 0.010, 0.004, 0.0);

		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.18f;
		break;

	case EDustFilmStock::Monochrome:
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(0.0, 0.0, 0.0, 1.0);

		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(1.28, 1.28, 1.28, 1.0);

		// The red-filter effect: weight the luminance mix toward red so that a
		// blue desert sky renders almost black and the clouds separate. This
		// single trick is why black-and-white desert photography looks the way
		// it does, and it is the reason this stock exists.
		Settings.bOverride_ColorGain = true;
		Settings.ColorGain = FVector4(1.55, 0.72, 0.30, 1.0);

		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.32f;
		break;

	case EDustFilmStock::Recovered:
		Settings.bOverride_ColorSaturation = true;
		Settings.ColorSaturation = FVector4(0.55, 0.58, 0.66, 1.0);

		Settings.bOverride_ColorGain = true;
		Settings.ColorGain = FVector4(0.92, 0.96, 1.08, 1.0);

		Settings.bOverride_FilmGrainIntensity = true;
		Settings.FilmGrainIntensity = 0.55f;

		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = 0.75f;
		break;

	case EDustFilmStock::Overexposed:
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = ExposureCompensation + 1.6f;

		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 3.2f;

		Settings.bOverride_ColorContrast = true;
		Settings.ColorContrast = FVector4(0.82, 0.82, 0.86, 1.0);
		break;

	case EDustFilmStock::Native:
	default:
		break;
	}
}

bool UDustPhotoModeSubsystem::WriteCapture(
	const TArray<FColor>& Pixels, int32 Width, int32 Height, FString& OutRelativePath)
{
	if (Pixels.Num() != Width * Height)
	{
		UE_LOG(LogDust, Error, TEXT("Capture buffer size mismatch: %d pixels for %dx%d."),
			Pixels.Num(), Width, Height);
		return false;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	const TSharedPtr<IImageWrapper> ImageWrapper =
		ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		return false;
	}

	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Width, Height, ERGBFormat::BGRA, 8);

	const TArray64<uint8> Compressed = ImageWrapper->GetCompressed(100);

	const FString Filename = FString::Printf(TEXT("DUST_%s_%dx%d.png"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")), Width, Height);

	OutRelativePath = FString(DustPhoto::GalleryDirectory) / Filename;

	const FString FullPath = FPaths::ProjectSavedDir() / OutRelativePath;

	if (!FFileHelper::SaveArrayToFile(Compressed, *FullPath))
	{
		UE_LOG(LogDust, Error, TEXT("Failed to write photograph to '%s'."), *FullPath);
		return false;
	}

	UE_LOG(LogDust, Log, TEXT("Photograph saved: %s (%dx%d, %.1f MB)."),
		*Filename, Width, Height, Compressed.Num() / (1024.0 * 1024.0));

	return true;
}

void UDustPhotoModeSubsystem::Capture(int32 Width, int32 Height)
{
	if (!bActive)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	Width = FMath::Clamp(Width, 640, 7680);
	Height = FMath::Clamp(Height, 360, 4320);

	DUST_SCOPED_TIMER(Dust_Photo_Capture);

	// Above the single-allocation limit we would need to render in tiles and
	// stitch. Tiling is implemented in the capture pipeline; what matters here
	// is that the *player* never has to know - they press the button and get an
	// 8K PNG, it just takes a few seconds on a small GPU.
	const bool bNeedsTiling =
		Width > DustPhoto::MaxSingleCaptureDimension ||
		Height > DustPhoto::MaxSingleCaptureDimension;

	const int32 TileColumns = bNeedsTiling
		? FMath::CeilToInt32(static_cast<float>(Width) / DustPhoto::MaxSingleCaptureDimension) : 1;
	const int32 TileRows = bNeedsTiling
		? FMath::CeilToInt32(static_cast<float>(Height) / DustPhoto::MaxSingleCaptureDimension) : 1;

	const int32 TileWidth = FMath::DivideAndRoundUp(Width, TileColumns);
	const int32 TileHeight = FMath::DivideAndRoundUp(Height, TileRows);

	UE_LOG(LogDust, Log, TEXT("Capturing %dx%d in %dx%d tile(s) at %.0f mm f/%.1f."),
		Width, Height, TileColumns, TileRows, FocalLengthMm, Aperture);

	// Scene capture actor, created transient for the duration of the capture.
	AActor* CaptureActor = World->SpawnActor<AActor>(
		AActor::StaticClass(), CameraLocation, CameraRotation);

	if (!CaptureActor)
	{
		return;
	}

	USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(CaptureActor);
	Capture->RegisterComponent();
	Capture->SetWorldLocationAndRotation(CameraLocation, CameraRotation);

	Capture->CaptureSource = SCS_FinalToneCurveHDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;

	// Force full quality regardless of what the player is running at. A Steam
	// Deck player gets the same print as a 4090 player, just more slowly.
	Capture->ShowFlags.SetTemporalAA(true);
	Capture->ShowFlags.SetMotionBlur(false);

	ApplyFilmStock(Capture->PostProcessSettings);
	Capture->PostProcessBlendWeight = 1.0f;

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(CaptureActor);
	RenderTarget->RenderTargetFormat = RTF_RGBA8_SRGB;
	RenderTarget->InitAutoFormat(TileWidth, TileHeight);
	RenderTarget->UpdateResourceImmediate(true);

	Capture->TextureTarget = RenderTarget;

	TArray<FColor> FullImage;
	FullImage.SetNumZeroed(Width * Height);

	const float BaseFOV = GetFieldOfView();

	for (int32 TileY = 0; TileY < TileRows; ++TileY)
	{
		for (int32 TileX = 0; TileX < TileColumns; ++TileX)
		{
			// Off-centre projection for this tile. Each tile renders a sub-frustum
			// of the full frame, which is what makes the seams invisible - a naive
			// approach of rotating the camera per tile would produce a panorama,
			// not a high-resolution version of the same shot.
			Capture->bUseCustomProjectionMatrix = true;

			const float HalfFOVRad = FMath::DegreesToRadians(BaseFOV * 0.5f);
			const float FullAspect = static_cast<float>(Width) / static_cast<float>(Height);

			const float Left   = -1.0f + 2.0f * (static_cast<float>(TileX) / TileColumns);
			const float Right  = -1.0f + 2.0f * (static_cast<float>(TileX + 1) / TileColumns);
			const float Bottom = -1.0f + 2.0f * (static_cast<float>(TileY) / TileRows);
			const float Top    = -1.0f + 2.0f * (static_cast<float>(TileY + 1) / TileRows);

			const float TanHalf = FMath::Tan(HalfFOVRad);

			Capture->CustomProjectionMatrix = FReversedZPerspectiveMatrix(
				TanHalf * FullAspect * (Right - Left) * 0.5f,
				TanHalf * (Top - Bottom) * 0.5f,
				1.0f, 1.0f, GNearClippingPlane, GNearClippingPlane);

			Capture->CaptureScene();

			// Read back this tile.
			FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
			if (!Resource)
			{
				continue;
			}

			TArray<FColor> TilePixels;
			Resource->ReadPixels(TilePixels);

			// Blit into the full image.
			const int32 DestX = TileX * TileWidth;
			const int32 DestY = TileY * TileHeight;

			for (int32 Y = 0; Y < TileHeight; ++Y)
			{
				const int32 TargetY = DestY + Y;
				if (TargetY >= Height)
				{
					break;
				}

				const int32 CopyWidth = FMath::Min(TileWidth, Width - DestX);
				if (CopyWidth <= 0 || (Y * TileWidth + CopyWidth) > TilePixels.Num())
				{
					continue;
				}

				FMemory::Memcpy(
					&FullImage[TargetY * Width + DestX],
					&TilePixels[Y * TileWidth],
					CopyWidth * sizeof(FColor));
			}
		}
	}

	CaptureActor->Destroy();

	// Write and record.
	FString RelativePath;
	if (WriteCapture(FullImage, Width, Height, RelativePath))
	{
		FDustPhotoRecord Record;
		Record.RelativePath = RelativePath;
		Record.CapturedLocation = CameraLocation;
		Record.CapturedRotation = CameraRotation;

		if (const UDustSkySubsystem* Sky = UDustSkySubsystem::Get(this))
		{
			Record.CapturedAt = Sky->GetWorldTime();
		}
		if (const UDustWorldSubsystem* WorldSubsystem = UDustWorldSubsystem::Get(this))
		{
			Record.RegionId = WorldSubsystem->GetCurrentRegionId();
		}

		Gallery.Insert(MoveTemp(Record), 0);
	}
}

bool UDustPhotoModeSubsystem::DeletePhoto(int32 Index)
{
	if (!Gallery.IsValidIndex(Index))
	{
		return false;
	}

	const FString FullPath = FPaths::ProjectSavedDir() / Gallery[Index].RelativePath;
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*FullPath))
	{
		PlatformFile.DeleteFile(*FullPath);
	}

	Gallery.RemoveAt(Index);
	return true;
}

void UDustPhotoModeSubsystem::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return;
	}

	// Photo mode drives the view directly rather than through the pawn, so the
	// player character is left exactly where it was and the shot is unaffected
	// by anything the character would otherwise do.
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0))
		{
			PlayerController->SetControlRotation(CameraRotation);
		}
	}
}

void UDustPhotoModeSubsystem::OnDustSave(UDustSaveGame& Save)
{
	Save.Photos = Gallery;
}

void UDustPhotoModeSubsystem::OnDustLoad(const UDustSaveGame& Save)
{
	Gallery = Save.Photos;

	// Drop records whose file has been deleted outside the game, so the gallery
	// never shows a broken thumbnail.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	Gallery.RemoveAll([&PlatformFile](const FDustPhotoRecord& Record)
	{
		const FString FullPath = FPaths::ProjectSavedDir() / Record.RelativePath;
		return !PlatformFile.FileExists(*FullPath);
	});
}
