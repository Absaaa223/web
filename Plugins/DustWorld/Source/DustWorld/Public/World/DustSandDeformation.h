// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DustSandDeformation.generated.h"

class UTexture2D;
class UMaterialParameterCollection;

/** What made a mark in the sand. Determines shape, depth and how fast it heals. */
UENUM(BlueprintType)
enum class EDustImpactType : uint8
{
	/** A single footfall. Sharp, deep, short-lived. */
	Footstep,

	/** A body sliding: the player on a dune face, or a creature lying down. */
	Slide,

	/** A continuous vehicle or board track. */
	Track,

	/** A heavy impact: a landing, a fall, a rock striking the ground. */
	Impact,

	/** Displacement outward from a point, as when something bursts from below. */
	Eruption
};

/** One queued deformation, in world space. */
USTRUCT(BlueprintType)
struct DUSTWORLD_API FDustSandImpact
{
	GENERATED_BODY()

	/** World-space location of the impact. */
	UPROPERTY(BlueprintReadWrite, Category = "Dust|Sand")
	FVector Location = FVector::ZeroVector;

	/** Radius of the disturbance in cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Dust|Sand")
	float Radius = 18.0f;

	/** Depth in cm. Positive pushes sand down, negative piles it up. */
	UPROPERTY(BlueprintReadWrite, Category = "Dust|Sand")
	float Depth = 6.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Dust|Sand")
	EDustImpactType Type = EDustImpactType::Footstep;

	/**
	 * Direction of travel, for impacts that are not radially symmetric.
	 * A footstep leaves a rim pushed up behind it; a track is elongated.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Dust|Sand")
	FVector2D Direction = FVector2D::ZeroVector;
};

/**
 * Persistent sand deformation around the player.
 *
 * ------------------------------------------------------------------------
 * HOW IT WORKS
 * ------------------------------------------------------------------------
 * A single heightfield texture follows the player, covering a fixed square of
 * world around them. The texture is addressed *toroidally*: when the player
 * moves east, the column of texels that falls off the west edge is cleared and
 * reused as the new east edge. Nothing is ever copied, so the cost of moving is
 * proportional to the distance travelled rather than to the texture size, and
 * a footprint stays exactly where it was left in world space.
 *
 * The ground material samples this texture using world-space UVs with the same
 * wrap, offsets vertices downward by the stored depth, and perturbs the normal
 * from the gradient. Because the offset is applied in the material, deformation
 * costs nothing on the CPU once uploaded.
 *
 * ------------------------------------------------------------------------
 * HEALING
 * ------------------------------------------------------------------------
 * Footprints do not last forever, and how fast they vanish is one of the
 * clearest ways the game communicates wind strength. Healing is an exponential
 * decay whose rate scales with the local wind speed: in still air a trail lasts
 * many minutes, and during a sandstorm the player can watch their own tracks
 * disappear behind them within seconds.
 *
 * ------------------------------------------------------------------------
 * WHY CPU-SIDE
 * ------------------------------------------------------------------------
 * A render-target ping-pong would be cheaper per texel, but this field is small
 * (1024x1024 covering 80 m) and the CPU version buys us something the GPU one
 * cannot: gameplay can *read* the deformation. Creatures follow the player's
 * tracks, the hoverbike loses grip in churned sand, and the wind erases
 * evidence that an AI was using. That readback would cost a frame of latency
 * and a pipeline stall on the GPU.
 */
UCLASS()
class DUSTWORLD_API UDustSandDeformationSubsystem : public UTickableWorldSubsystem
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

	static UDustSandDeformationSubsystem* Get(const UObject* WorldContext);

	/**
	 * Queues a deformation. Safe to call from any thread; impacts are applied
	 * at the start of the next tick.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sand")
	void AddImpact(const FDustSandImpact& Impact);

	/** Convenience for the common case. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sand")
	void AddFootstep(const FVector& Location, const FVector& Forward, float FootRadius, float Weight);

	/**
	 * Reads the current deformation depth at a world location, in cm.
	 * Returns 0 outside the field. This is what lets creatures track the player.
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Sand")
	float GetDeformationAt(const FVector& WorldLocation) const;

	/**
	 * Total disturbance within a radius, normalised 0..1. Used by fauna to
	 * judge "something large came through here recently".
	 */
	UFUNCTION(BlueprintPure, Category = "Dust|Sand")
	float GetDisturbanceInRadius(const FVector& WorldLocation, float Radius) const;

	/** The heightfield texture, for binding into the ground material. */
	UFUNCTION(BlueprintPure, Category = "Dust|Sand")
	UTexture2D* GetDeformationTexture() const { return DeformationTexture; }

	/** World-space size of the field in cm. */
	UFUNCTION(BlueprintPure, Category = "Dust|Sand")
	float GetFieldExtent() const { return FieldExtent; }

	/** Sets the wind speed that drives healing, in cm/s. Called by DustWeather. */
	void SetWindSpeed(float InWindSpeedCmPerSecond) { WindSpeed = InWindSpeedCmPerSecond; }

	/** Clears all deformation. Used on teleport and on load. */
	UFUNCTION(BlueprintCallable, Category = "Dust|Sand")
	void ClearField();

private:
	/** Allocates the texture and CPU buffer. */
	void CreateResources();

	/** Recentres the field on the player, clearing texels that scrolled in. */
	void UpdateFieldCentre(const FVector& PlayerLocation);

	/** Applies every queued impact into the CPU buffer. */
	void FlushImpacts();

	/** Decays deformation toward zero at the wind-driven rate. */
	void ApplyHealing(float DeltaSeconds);

	/** Pushes dirty regions of the CPU buffer to the GPU texture. */
	void UploadDirtyRegions();

	/** Converts a world XY to a texel coordinate, or returns false if outside. */
	bool WorldToTexel(const FVector2D& WorldXY, int32& OutX, int32& OutY) const;

	/** Toroidal wrap of a texel index into the buffer. */
	FORCEINLINE int32 WrapTexel(int32 Coord) const
	{
		return ((Coord % Resolution) + Resolution) % Resolution;
	}

	FORCEINLINE int32 BufferIndex(int32 X, int32 Y) const
	{
		return WrapTexel(Y) * Resolution + WrapTexel(X);
	}

private:
	/** Texture resolution. 1024 over 80 m gives ~7.8 cm per texel. */
	static constexpr int32 Resolution = 1024;

	/** World-space extent of the field in cm. */
	float FieldExtent = 8000.0f;

	/** Centimetres per texel, derived. */
	float TexelSize = 8000.0f / 1024.0f;

	/**
	 * Field origin in texel space. Increments as the player moves, so a given
	 * world position always maps to the same texel until it scrolls out.
	 */
	FIntPoint FieldOriginTexel = FIntPoint::ZeroValue;

	/** CPU-side depth buffer in cm. Positive = pressed down. */
	TArray<float> DepthBuffer;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> DeformationTexture;

	/** Pending impacts, guarded by ImpactLock. */
	TArray<FDustSandImpact> PendingImpacts;
	mutable FCriticalSection ImpactLock;

	/** Rows touched since the last upload, so we upload only what changed. */
	TBitArray<> DirtyRows;

	/** Wind speed in cm/s, from the weather subsystem. */
	float WindSpeed = 0.0f;

	/** Accumulator so healing runs at a fixed rate rather than per frame. */
	float HealAccumulator = 0.0f;

	/** Healing tick rate. 10 Hz is imperceptible and a tenth of the cost. */
	static constexpr float HealInterval = 0.1f;
};
