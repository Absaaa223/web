// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "UI/DustHudDirector.h"

#include "DustLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Player/DustCharacter.h"
#include "Player/DustTraversalComponent.h"
#include "Player/DustVitalsComponent.h"
#include "Settings/DustGameUserSettings.h"
#include "World/DustRegionData.h"
#include "World/DustWorldSubsystem.h"

#define LOCTEXT_NAMESPACE "DustHud"

UDustHudDirector* UDustHudDirector::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UDustHudDirector>() : nullptr;
}

bool UDustHudDirector::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDustHudDirector::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Announce a region the first time the player enters it, then never again.
	if (UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		World->OnRegionChanged.AddDynamic(this, &UDustHudDirector::HandleRegionChanged);
	}
}

TStatId UDustHudDirector::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDustHudDirector, STATGROUP_Tickables);
}

float UDustHudDirector::GetElementOpacity(EDustHudElement Element) const
{
	const int32 Index = static_cast<int32>(Element);
	if (Index < 0 || Index >= static_cast<int32>(EDustHudElement::Count))
	{
		return 0.0f;
	}

	return bHudSuppressed ? 0.0f : Elements[Index].Opacity;
}

void UDustHudDirector::FlashElement(EDustHudElement Element, float DurationSeconds)
{
	const int32 Index = static_cast<int32>(Element);
	if (Index < 0 || Index >= static_cast<int32>(EDustHudElement::Count))
	{
		return;
	}

	Elements[Index].FlashRemaining = FMath::Max(Elements[Index].FlashRemaining, DurationSeconds);
}

void UDustHudDirector::SetHudSuppressed(bool bSuppressed)
{
	bHudSuppressed = bSuppressed;
}

void UDustHudDirector::HandleRegionChanged(FName NewRegionId, FName PreviousRegionId)
{
	if (NewRegionId.IsNone() || AnnouncedRegions.Contains(NewRegionId))
	{
		return;
	}

	const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this);
	const UDustRegionData* Region = World ? World->FindRegionData(NewRegionId) : nullptr;

	// A region with no authored name stays anonymous rather than displaying an
	// internal id. Some regions are meant to be unnamed.
	if (!Region || Region->DisplayName.IsEmpty())
	{
		AnnouncedRegions.Add(NewRegionId);
		return;
	}

	AnnouncedRegions.Add(NewRegionId);

	// Long, because it is the only title card the game has and it should be
	// allowed to sit there while the player looks at what they have walked into.
	FlashElement(EDustHudElement::RegionTitle, 6.5f);

	UE_LOG(LogDust, Log, TEXT("Region announced: %s"), *Region->DisplayName.ToString());
}

void UDustHudDirector::UpdatePrompt()
{
	PromptText = FText::GetEmpty();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	const ADustCharacter* Character = PlayerController ? Cast<ADustCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!Character)
	{
		return;
	}

	const UDustTraversalComponent* Traversal = Character->GetTraversal();
	if (!Traversal)
	{
		return;
	}

	// The prompt names the *move*, never the button. The player already knows
	// which button they are holding; what they need to know is what will happen.
	switch (Traversal->GetAvailableAction())
	{
	case EDustTraversalAction::Mantle:     PromptText = LOCTEXT("PromptMantle", "Climb up");   break;
	case EDustTraversalAction::Vault:      PromptText = LOCTEXT("PromptVault", "Vault");       break;
	case EDustTraversalAction::StartClimb: PromptText = LOCTEXT("PromptClimb", "Climb");       break;
	case EDustTraversalAction::StartSlide: PromptText = LOCTEXT("PromptSlide", "Slide");       break;
	case EDustTraversalAction::StartGlide: PromptText = LOCTEXT("PromptGlide", "Open glider"); break;
	case EDustTraversalAction::GrabRope:   PromptText = LOCTEXT("PromptRope", "Take hold");    break;

	// Deliberately no prompt for stopping a climb or closing the glider. The
	// player worked out how to start; they will work out how to stop, and a
	// permanent "release" prompt while climbing would be visual noise during
	// the exact moments the view matters most.
	default:
		break;
	}
}

void UDustHudDirector::EvaluateElements(float DeltaTime)
{
	const UDustGameUserSettings* Settings = UDustGameUserSettings::Get();

	// Persistence raises every threshold toward always-on, for players who
	// genuinely want a readout. At 1.0 the HUD behaves conventionally.
	const float Persistence = Settings ? Settings->GetHUDPersistence() : 0.0f;

	for (FElementState& Element : Elements)
	{
		Element.Target = 0.0f;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? UGameplayStatics::GetPlayerController(World, 0) : nullptr;
	const ADustCharacter* Character = PlayerController ? Cast<ADustCharacter>(PlayerController->GetPawn()) : nullptr;

	if (Character)
	{
		if (const UDustVitalsComponent* Vitals = Character->GetVitals())
		{
			// --- Stamina -----------------------------------------------------
			// Shows when actually being spent or when low enough to matter.
			// Never at full, never while standing still.
			const float Stamina = Vitals->GetStamina();
			const float StaminaThreshold = FMath::Lerp(0.45f, 1.01f, Persistence);

			if (Stamina < StaminaThreshold || Vitals->IsExhausted())
			{
				Elements[static_cast<int32>(EDustHudElement::Stamina)].Target = 1.0f;
			}

			// --- Hydration ---------------------------------------------------
			const float HydrationThreshold = FMath::Lerp(0.32f, 1.01f, Persistence);
			if (Vitals->GetHydration() < HydrationThreshold)
			{
				Elements[static_cast<int32>(EDustHudElement::Hydration)].Target = 1.0f;
			}

			// --- Thermal -----------------------------------------------------
			if (Vitals->IsOverheating() || Vitals->IsCold() || Persistence > 0.5f)
			{
				Elements[static_cast<int32>(EDustHudElement::Thermal)].Target = 1.0f;
			}
		}

		// --- Prompt ---------------------------------------------------------
		if (!PromptText.IsEmpty())
		{
			Elements[static_cast<int32>(EDustHudElement::Prompt)].Target = 1.0f;
		}
	}

	// --- Flashes ------------------------------------------------------------
	for (FElementState& Element : Elements)
	{
		if (Element.FlashRemaining > 0.0f)
		{
			Element.FlashRemaining -= DeltaTime;
			Element.Target = 1.0f;
		}
	}

	// --- Fade ---------------------------------------------------------------
	for (FElementState& Element : Elements)
	{
		if (Element.Target > 0.0f)
		{
			Element.HoldRemaining = HoldSeconds;
			Element.Opacity = FMath::FInterpConstantTo(Element.Opacity, 1.0f, DeltaTime, FadeInRate);
		}
		else if (Element.HoldRemaining > 0.0f)
		{
			// Hold before fading. Stops the stamina arc strobing every time the
			// player crosses the threshold while jogging.
			Element.HoldRemaining -= DeltaTime;
		}
		else
		{
			Element.Opacity = FMath::FInterpConstantTo(Element.Opacity, 0.0f, DeltaTime, FadeOutRate);
		}
	}
}

void UDustHudDirector::ApplyAttentionBudget()
{
	float Total = 0.0f;
	for (const FElementState& Element : Elements)
	{
		Total += Element.Opacity;
	}

	if (Total <= AttentionBudget)
	{
		return;
	}

	// Over budget. Scale everything down except the prompt, which is the only
	// element that is time-critical: a fading mantle prompt at the moment the
	// player needs it is a worse failure than a dim stamina arc.
	const int32 PromptIndex = static_cast<int32>(EDustHudElement::Prompt);
	const float PromptOpacity = Elements[PromptIndex].Opacity;

	const float Remaining = FMath::Max(AttentionBudget - PromptOpacity, 0.0f);
	const float Others = Total - PromptOpacity;

	if (Others <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const float Scale = FMath::Clamp(Remaining / Others, 0.0f, 1.0f);

	for (int32 Index = 0; Index < static_cast<int32>(EDustHudElement::Count); ++Index)
	{
		if (Index != PromptIndex)
		{
			Elements[Index].Opacity *= Scale;
		}
	}
}

void UDustHudDirector::Tick(float DeltaTime)
{
	DUST_SCOPED_TIMER(Dust_Hud_Tick);

	UpdatePrompt();
	EvaluateElements(DeltaTime);
	ApplyAttentionBudget();
}

#undef LOCTEXT_NAMESPACE
