// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Save/DustSaveSubsystem.h"
#include "DustTypes.h"
#include "DustPhotoModeSubsystem.generated.h"

class UTextureRenderTarget2D;
class USceneCaptureComponent2D;

/** A photographic look the player can apply. Not a filter stack - a film stock. */
UENUM(BlueprintType)
enum class EDustFilmStock : uint8
{
	/** Neutral digital. What the game normally looks like. */
	Native,

	/** Warm, low contrast, slightly lifted blacks. Kodachrome-ish. */
	Reversal,

	/** High contrast monochrome with a deep red filter, which is what you use
	    to make a desert sky go black. */
	Monochrome,

	/** Cool, desaturated, heavy grain. */
	Recovered,

	/** Blown highlights and a heavy halation bloom. */
	Overexposed
};

/**
 * Photo mode.
 *
 * ========================================================================
 * A CAMERA, NOT A SCREENSHOT BUTTON
 * ========================================================================
 * DUST is a game about looking at things, so its photo mode is a first-class
 * feature rather than a toggle that hides the HUD. It is built as a camera:
 * focal length, aperture, focus distance, exposure, tilt. Those controls do
 * what a photographer expects, which means the skill a player brings from
 * actual photography transfers.
 *
 * ========================================================================
 * TIME STOPS, THE WORLD DOES NOT
 * ========================================================================
 * Entering photo mode pauses gameplay but leaves the *simulation* readable:
 * dust still drifts, cloth still settles, the sun holds its position. Freezing
 * everything hard produces stiff, dead compositions; letting particles keep
 * moving lets the player wait for the right instant, which is exactly what
 * they would do with a real camera.
 *
 * The performance governor is frozen on entry so that a long composition of an
 * expensive vista does not progressively degrade the shot being composed.
 *
 * ========================================================================
 * 8K
 * ========================================================================
 * Captures render at up to 7680x4320 through a dedicated scene capture at full
 * quality regardless of the player's upscaler settings - a Steam Deck player
 * gets the same resolution print as an RTX 4090 player, just more slowly. The
 * capture is tiled if it exceeds what the GPU can allocate in one pass.
 */
UCLASS()
class DUST_API UDustPhotoModeSubsystem
	: public UTickableWorldSubsystem
	, public IDustSaveParticipant
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	//~ Begin IDustSaveParticipant
	virtual void OnDustSave(UDustSaveGame& Save) override;
	virtual void OnDustLoad(const UDustSaveGame& Save) override;
	virtual int32 GetDustLoadPriority() const override { return 250; }
	//~ End IDustSaveParticipant

	static UDustPhotoModeSubsystem* Get(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void TogglePhotoMode();

	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void EnterPhotoMode();

	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void ExitPhotoMode();

	UFUNCTION(BlueprintPure, Category = "Dust|Photo")
	bool IsInPhotoMode() const { return bActive; }

	// -----------------------------------------------------------------------
	// Camera controls
	// -----------------------------------------------------------------------

	/** Moves the free camera. Input is in camera space. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void MoveCamera(const FVector& LocalDelta, float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void RotateCamera(const FVector2D& Delta);

	/** Roll, for deliberate dutch angles. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetRoll(float Degrees);

	/** Focal length in mm. 14 to 300. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetFocalLength(float Millimetres);

	UFUNCTION(BlueprintPure, Category = "Dust|Photo")
	float GetFocalLength() const { return FocalLengthMm; }

	/** Aperture. f/1.4 to f/22. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetAperture(float FStop);

	UFUNCTION(BlueprintPure, Category = "Dust|Photo")
	float GetAperture() const { return Aperture; }

	/** Focus distance in cm. Set to a negative value for autofocus. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetFocusDistance(float Centimetres);

	/** Exposure compensation in stops. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetExposureCompensation(float Stops);

	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetFilmStock(EDustFilmStock Stock);

	UFUNCTION(BlueprintPure, Category = "Dust|Photo")
	EDustFilmStock GetFilmStock() const { return FilmStock; }

	/** Composition guides: thirds, golden ratio, centre. Purely an overlay. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void SetGuideMode(int32 Mode);

	// -----------------------------------------------------------------------
	// Capture
	// -----------------------------------------------------------------------

	/**
	 * Captures at the given resolution and writes a PNG to the gallery.
	 * Resolutions above the GPU's single-allocation limit are tiled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void Capture(int32 Width = 3840, int32 Height = 2160);

	/** Convenience: full 8K. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	void Capture8K() { Capture(7680, 4320); }

	/** The player's saved photographs, newest first. */
	UFUNCTION(BlueprintPure, Category = "Dust|Photo")
	const TArray<FDustPhotoRecord>& GetGallery() const { return Gallery; }

	/** Deletes a photograph from the gallery and from disk. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Photo")
	bool DeletePhoto(int32 Index);

private:
	/** Applies the current film stock to the capture's post-process settings. */
	void ApplyFilmStock(struct FPostProcessSettings& Settings) const;

	/** Converts focal length to horizontal FOV for the given sensor. */
	float GetFieldOfView() const;

	/** Clamps the free camera so it cannot leave the world or enter terrain. */
	void ConstrainCamera();

	/** Writes a captured image to the gallery directory. */
	bool WriteCapture(const TArray<FColor>& Pixels, int32 Width, int32 Height, FString& OutRelativePath);

private:
	bool bActive = false;

	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;

	/** Where the player was standing when photo mode was entered. */
	FVector AnchorLocation = FVector::ZeroVector;

	float FocalLengthMm = 35.0f;
	float Aperture = 5.6f;

	/** Negative means autofocus. */
	float FocusDistance = -1.0f;

	float ExposureCompensation = 0.0f;
	float Roll = 0.0f;

	EDustFilmStock FilmStock = EDustFilmStock::Native;
	int32 GuideMode = 0;

	UPROPERTY(Transient)
	TArray<FDustPhotoRecord> Gallery;

	/** Whether time dilation was already modified when we entered. */
	float PreviousTimeDilation = 1.0f;

	/** Sensor width in mm, matching the gameplay camera so looks carry over. */
	static constexpr float SensorWidthMm = 36.0f;

	/**
	 * How far the free camera may stray from the player, in cm.
	 * Generous enough for real composition, bounded so photo mode cannot be
	 * used to scout unexplored terrain - which would undermine the entire
	 * discovery loop the rest of the game is built on.
	 */
	static constexpr float MaxCameraDistance = 2500.0f;

	static constexpr float BaseMoveSpeed = 420.0f;
};
