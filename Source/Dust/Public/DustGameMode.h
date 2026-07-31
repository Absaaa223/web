// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DustGameMode.generated.h"

/**
 * The game mode.
 *
 * DUST is single-player with no rules in the traditional sense - nothing is
 * won, nothing is lost, there are no states to arbitrate. The game mode's only
 * real jobs are choosing where the player wakes up and deciding whether this is
 * a new world or a continuation.
 *
 * ========================================================================
 * WHERE YOU WAKE UP
 * ========================================================================
 * The opening position is not a fixed point in the level. It is chosen by the
 * same sampler everything else uses, against a set of criteria that make the
 * first thirty seconds work:
 *
 *   - Flat enough to stand up on.
 *   - Facing east, so the first thing on screen is the sun coming up.
 *   - Within sight of exactly one landmark, and no more. One is an invitation;
 *     three is a menu.
 *   - Not inside any region's core, so the player's first walk crosses a
 *     boundary and learns, immediately, that this place changes.
 *
 * Those criteria are evaluated at runtime rather than baked, which means the
 * opening still works if a designer retunes the regions.
 */
UCLASS()
class DUST_API ADustGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADustGameMode();

	//~ Begin AGameModeBase
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	//~ End AGameModeBase

	/** True if this session began from a save rather than as a new world. */
	UFUNCTION(BlueprintPure, Category = "Dust")
	bool IsContinuedSession() const { return bContinuedSession; }

private:
	/** Finds a world position that satisfies the opening criteria. */
	FTransform FindWakingPlace() const;

	/** Scores a candidate waking position. Higher is better; 0 disqualifies. */
	float ScoreWakingPlace(const FVector2D& Candidate, FRotator& OutFacing) const;

private:
	bool bContinuedSession = false;

	/** Cached so ChoosePlayerStart and the save path agree. */
	mutable FTransform CachedWakingTransform;
	mutable bool bWakingPlaceResolved = false;
};
