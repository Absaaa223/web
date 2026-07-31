// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#include "Player/DustCharacter.h"

#include "Camera/DustCameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "DustLog.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Photo/DustPhotoModeSubsystem.h"
#include "Player/DustTraversalComponent.h"
#include "Player/DustVitalsComponent.h"
#include "World/DustSandDeformation.h"
#include "World/DustWorldSubsystem.h"

ADustCharacter::ADustCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// Capsule sized to a person: 180 cm tall, 34 cm radius. Everything in the
	// world - ledge heights, doorway widths, the reach of a mantle - is
	// authored against these numbers, so changing them invalidates level art.
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 90.0f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bOrientRotationToMovement = true;
		Movement->RotationRate = FRotator(0.0f, 520.0f, 0.0f);

		Movement->MaxWalkSpeed = 360.0f;
		Movement->JumpZVelocity = 460.0f;
		Movement->AirControl = 0.32f;
		Movement->BrakingDecelerationWalking = 1800.0f;

		// Walkable floor at 50 degrees. Above that the player slides, which is
		// what the traversal component's sand physics builds on.
		Movement->SetWalkableFloorAngle(50.0f);

		// Step height generous enough that small rocks and rubble never stop
		// the player dead. Nothing is more corrosive to a walking game than
		// being caught on scenery.
		Movement->MaxStepHeight = 52.0f;

		Movement->NavAgentProps.bCanCrouch = true;
		Movement->NavAgentProps.bCanSwim = true;

		Movement->Buoyancy = 0.94f;
	}

	Camera = CreateDefaultSubobject<UDustCameraComponent>(TEXT("DustCamera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, 72.0f)); // eye height
	Camera->bUsePawnControlRotation = true;

	Vitals = CreateDefaultSubobject<UDustVitalsComponent>(TEXT("Vitals"));
	Traversal = CreateDefaultSubobject<UDustTraversalComponent>(TEXT("Traversal"));
}

void ADustCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				InputSubsystem->AddMappingContext(DefaultMappingContext, 0);
			}
			else
			{
				UE_LOG(LogDust, Warning,
					TEXT("No input mapping context assigned. Run the DustBootstrap commandlet to generate the default input assets."));
			}
		}
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
		{
			SaveSubsystem->RegisterParticipant(this);
		}
	}

	SettleOnGround();
}

void ADustCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
		{
			SaveSubsystem->UnregisterParticipant(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

void ADustCharacter::SettleOnGround()
{
	// The landscape may not have streamed in yet, so we place the player using
	// the sampler rather than a trace. This is the single most important use of
	// the "world is a pure function" property: the player can be positioned
	// correctly before any geometry exists.
	if (const UDustWorldSubsystem* World = UDustWorldSubsystem::Get(this))
	{
		const FVector Location = GetActorLocation();
		const double GroundHeight = World->SampleHeight(Location);
		const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		SetActorLocation(FVector(Location.X, Location.Y, GroundHeight + CapsuleHalfHeight + 5.0));
	}
}

void ADustCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogDust, Error, TEXT("Expected an EnhancedInputComponent."));
		return;
	}

	// Bind only what exists, so a partially configured character blueprint
	// still runs rather than crashing on a null action.
	auto BindIfValid = [Input, this](UInputAction* Action, ETriggerEvent Event, auto Handler)
	{
		if (Action)
		{
			Input->BindAction(Action, Event, this, Handler);
		}
	};

	BindIfValid(MoveAction,         ETriggerEvent::Triggered, &ADustCharacter::HandleMove);
	BindIfValid(LookAction,         ETriggerEvent::Triggered, &ADustCharacter::HandleLook);
	BindIfValid(TraverseAction,     ETriggerEvent::Started,   &ADustCharacter::HandleTraverse);
	BindIfValid(SprintAction,       ETriggerEvent::Started,   &ADustCharacter::HandleSprintStarted);
	BindIfValid(SprintAction,       ETriggerEvent::Completed, &ADustCharacter::HandleSprintCompleted);
	BindIfValid(CrouchAction,       ETriggerEvent::Started,   &ADustCharacter::HandleCrouchToggle);
	BindIfValid(CameraToggleAction, ETriggerEvent::Started,   &ADustCharacter::HandleCameraToggle);
	BindIfValid(InteractAction,     ETriggerEvent::Started,   &ADustCharacter::HandleInteract);
	BindIfValid(GliderAction,       ETriggerEvent::Started,   &ADustCharacter::HandleGlider);
	BindIfValid(BoardAction,        ETriggerEvent::Started,   &ADustCharacter::HandleBoard);
	BindIfValid(PhotoModeAction,    ETriggerEvent::Started,   &ADustCharacter::HandlePhotoMode);
	BindIfValid(JournalAction,      ETriggerEvent::Started,   &ADustCharacter::HandleJournal);
}

void ADustCharacter::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	if (!Controller || Axis.IsNearlyZero())
	{
		return;
	}

	// Movement is relative to where the camera is looking, not where the body
	// is facing. In a game where the player spends most of their time looking
	// at the horizon rather than their feet, anything else feels wrong.
	const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);

	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

	AddMovementInput(Forward, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void ADustCharacter::HandleLook(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();

	AddControllerYawInput(Axis.X * LookSensitivity * GetWorld()->GetDeltaSeconds());
	AddControllerPitchInput(-Axis.Y * LookSensitivity * GetWorld()->GetDeltaSeconds());
}

void ADustCharacter::HandleTraverse(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->TraverseAction();
	}
}

void ADustCharacter::HandleSprintStarted(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->SetSprinting(true);
	}
}

void ADustCharacter::HandleSprintCompleted(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->SetSprinting(false);
	}
}

void ADustCharacter::HandleCrouchToggle(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->SetCrouching(!bIsCrouched);
	}
}

void ADustCharacter::HandleCameraToggle(const FInputActionValue& Value)
{
	if (Camera)
	{
		Camera->ToggleCameraMode();
	}
}

void ADustCharacter::HandleInteract(const FInputActionValue& Value)
{
	// Interaction is resolved by the interaction component on whatever the
	// player is looking at, which keeps the character free of any knowledge
	// about artefacts, inscriptions or water sources.
}

void ADustCharacter::HandleGlider(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->ToggleGlider();
	}
}

void ADustCharacter::HandleBoard(const FInputActionValue& Value)
{
	if (Traversal)
	{
		Traversal->ToggleBoard();
	}
}

void ADustCharacter::HandlePhotoMode(const FInputActionValue& Value)
{
	if (UDustPhotoModeSubsystem* PhotoMode = UDustPhotoModeSubsystem::Get(this))
	{
		PhotoMode->TogglePhotoMode();
	}
}

void ADustCharacter::HandleJournal(const FInputActionValue& Value)
{
	// The journal widget listens for this through the HUD director.
}

void ADustCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	const float ImpactSpeed = FMath::Abs(GetVelocity().Z);

	// Camera dip proportional to landing force. There is no fall damage in
	// DUST - the world is meant to be climbed on and jumped off - so the dip
	// and the sound are the entire consequence, and they have to carry it.
	if (Camera && ImpactSpeed > 200.0f)
	{
		const float Severity = FMath::Clamp(ImpactSpeed / 1400.0f, 0.0f, 1.0f);
		Camera->AddCameraImpulse(FVector(0.0f, 0.0f, -Severity * 260.0f));
	}

	// A heavy landing displaces a lot of sand.
	if (UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
	{
		if (ImpactSpeed > 300.0f)
		{
			FDustSandImpact Impact;
			Impact.Location = Hit.ImpactPoint;
			Impact.Radius = FMath::Lerp(30.0f, 85.0f, FMath::Clamp(ImpactSpeed / 1400.0f, 0.0f, 1.0f));
			Impact.Depth = FMath::Lerp(8.0f, 26.0f, FMath::Clamp(ImpactSpeed / 1400.0f, 0.0f, 1.0f));
			Impact.Type = EDustImpactType::Impact;

			Sand->AddImpact(Impact);
		}
	}
}

void ADustCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Drive the autosave timer, which only actually saves when the player is
	// somewhere it would be reasonable to reload from.
	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UDustSaveSubsystem* SaveSubsystem = GameInstance->GetSubsystem<UDustSaveSubsystem>())
		{
			const bool bSafe = Vitals && Vitals->IsInSafeState();
			SaveSubsystem->TickAutosave(DeltaSeconds, bSafe);
			SaveSubsystem->AccumulatePlayTime(DeltaSeconds);
		}
	}
}

void ADustCharacter::OnDustSave(UDustSaveGame& Save)
{
	Save.PlayerLocation = GetActorLocation();
	Save.PlayerRotation = Controller ? Controller->GetControlRotation() : GetActorRotation();

	if (Vitals)
	{
		Save.Stamina = Vitals->GetStamina();
		Save.Hydration = Vitals->GetHydration();
	}

	if (Traversal)
	{
		Save.UnlockedEquipment.Reset();

		for (const FName& Equipment : { UDustTraversalComponent::EquipmentBoard,
		                                UDustTraversalComponent::EquipmentGlider,
		                                UDustTraversalComponent::EquipmentRope,
		                                UDustTraversalComponent::EquipmentHoverbike })
		{
			if (Traversal->HasEquipment(Equipment))
			{
				Save.UnlockedEquipment.Add(Equipment);
			}
		}
	}
}

void ADustCharacter::OnDustLoad(const UDustSaveGame& Save)
{
	SetActorLocation(Save.PlayerLocation, /*bSweep=*/false);

	if (Controller)
	{
		Controller->SetControlRotation(Save.PlayerRotation);
	}

	if (Vitals)
	{
		Vitals->RestoreState(Save.Stamina, Save.Hydration);
	}

	if (Traversal)
	{
		for (const FName& Equipment : Save.UnlockedEquipment)
		{
			Traversal->UnlockEquipment(Equipment);
		}
	}

	// Re-settle: the saved Z came from a session whose dune phase may differ
	// from this one, and after several in-game days of migration the sand under
	// a saved position can genuinely have moved. Without this the player can
	// load a few centimetres inside a dune.
	SettleOnGround();

	// The sand around the player is from wherever they were standing before.
	if (UDustSandDeformationSubsystem* Sand = UDustSandDeformationSubsystem::Get(this))
	{
		Sand->ClearField();
	}
}
