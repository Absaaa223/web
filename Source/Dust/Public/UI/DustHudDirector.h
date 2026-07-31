// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "DustHudDirector.generated.h"

/** One element of the interface. Each fades independently. */
UENUM(BlueprintType)
enum class EDustHudElement : uint8
{
	/** Stamina arc. Appears only while it is being spent or is low. */
	Stamina,

	/** Hydration mark. Appears only when parched or when drinking. */
	Hydration,

	/** Thermal indicator. Appears only at the extremes. */
	Thermal,

	/** Context prompt for the traversal or interact button. */
	Prompt,

	/** The name of a region, shown once on first entry and never again. */
	RegionTitle,

	/** Discovery notification. */
	Discovery,

	/** Compass rose. Off by default; a found artefact unlocks it. */
	Compass,

	Count UMETA(Hidden)
};

/**
 * The interface director.
 *
 * ========================================================================
 * THE HUD IS NOT THERE
 * ========================================================================
 * DUST ships with an empty screen. No bars, no minimap, no objective list, no
 * compass, no icons on the horizon. The interface appears only when there is
 * something the player cannot know any other way, and it leaves the moment
 * that stops being true.
 *
 * This is not minimalism for its own sake. It is because the game's entire
 * subject is *looking at the world*, and every pixel of interface is a pixel
 * the player is looking at instead.
 *
 * The rules, all implemented here:
 *
 *   1. Nothing appears above a threshold. Stamina shows below 45%, not at 99%.
 *   2. Everything fades out after its reason has gone, on a delay long enough
 *      that a flickering condition does not flicker the interface.
 *   3. Nothing ever tells the player where to go. The compass, if they find
 *      it, tells them which way is north - which is a tool, not a marker.
 *   4. A player who wants a persistent readout can have one via the HUD
 *      persistence setting, which raises every threshold to always-on.
 *
 * ========================================================================
 * WHY A DIRECTOR RATHER THAN PER-WIDGET LOGIC
 * ========================================================================
 * Putting the reveal logic in each widget produces an interface that fights
 * itself: three elements independently decide to appear and the screen fills
 * up at exactly the moment the player is most under pressure. The director
 * arbitrates - it knows the total on-screen budget and suppresses the least
 * urgent element when too much wants attention at once.
 */
UCLASS()
class DUST_API UDustHudDirector : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UWorldSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject

	static UDustHudDirector* Get(const UObject* WorldContext);

	/** Current opacity of an element, 0..1. Widgets bind to this. */
	UFUNCTION(BlueprintPure, Category = "Dust|UI")
	float GetElementOpacity(EDustHudElement Element) const;

	/**
	 * Requests that an element be shown for a while. Used for one-shot
	 * notifications such as entering a region or making a discovery.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dust|UI")
	void FlashElement(EDustHudElement Element, float DurationSeconds);

	/** Hides everything immediately. Photo mode uses this. */
	UFUNCTION(BlueprintCallable, Category = "Dust|UI")
	void SetHudSuppressed(bool bSuppressed);

	UFUNCTION(BlueprintPure, Category = "Dust|UI")
	bool IsHudSuppressed() const { return bHudSuppressed; }

	/** Text of the current context prompt, empty if there is none. */
	UFUNCTION(BlueprintPure, Category = "Dust|UI")
	FText GetPromptText() const { return PromptText; }

private:
	/** Evaluates whether each element wants to be visible. */
	void EvaluateElements(float DeltaTime);

	/** Arbitrates when too many elements want attention at once. */
	void ApplyAttentionBudget();

	/** Builds the context prompt from the traversal component's state. */
	void UpdatePrompt();

	/**
	 * Announces a region the first time the player enters it.
	 *
	 * Once only, ever. A title that reappears every time you cross a boundary
	 * turns a place into a level name, and DUST's regions are meant to be
	 * places you learn rather than areas you are told about.
	 */
	UFUNCTION()
	void HandleRegionChanged(FName NewRegionId, FName PreviousRegionId);

private:
	struct FElementState
	{
		/** What the element wants, 0 or 1. */
		float Target = 0.0f;

		/** What it is currently showing. */
		float Opacity = 0.0f;

		/** Seconds remaining on a flash request. */
		float FlashRemaining = 0.0f;

		/** Seconds remaining before it may fade out. */
		float HoldRemaining = 0.0f;
	};

	FElementState Elements[static_cast<int32>(EDustHudElement::Count)];

	FText PromptText;

	bool bHudSuppressed = false;

	/** Regions whose title has already been shown. */
	TSet<FName> AnnouncedRegions;

	/** Fade rates. Out is slower than in, so the interface leaves quietly. */
	static constexpr float FadeInRate = 4.0f;
	static constexpr float FadeOutRate = 1.6f;

	/** Seconds an element stays up after its reason has gone. */
	static constexpr float HoldSeconds = 1.8f;

	/**
	 * Maximum total opacity across all elements. Beyond this the director
	 * suppresses the least urgent. Deliberately low - two elements at full
	 * strength is already a busy screen by DUST's standards.
	 */
	static constexpr float AttentionBudget = 2.1f;
};
