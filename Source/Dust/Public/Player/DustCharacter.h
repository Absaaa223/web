// Copyright (c) DUST // Beyond the Horizon. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Save/DustSaveSubsystem.h"
#include "DustTypes.h"
#include "DustCharacter.generated.h"

class UDustCameraComponent;
class UDustTraversalComponent;
class UDustVitalsComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * The player.
 *
 * Deliberately thin. The character owns components and routes input to them;
 * it does not implement movement, survival or camera logic itself. That
 * separation is what lets the hoverbike reuse the same vitals component, and
 * what lets photo mode detach the camera without the character noticing.
 *
 * The character is also the save participant for player state, which keeps the
 * save subsystem free of any knowledge about pawns.
 */
UCLASS()
class DUST_API ADustCharacter : public ACharacter, public IDustSaveParticipant
{
	GENERATED_BODY()

public:
	ADustCharacter();

	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

	//~ Begin ACharacter
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void Landed(const FHitResult& Hit) override;
	//~ End ACharacter

	//~ Begin IDustSaveParticipant
	virtual void OnDustSave(UDustSaveGame& Save) override;
	virtual void OnDustLoad(const UDustSaveGame& Save) override;
	/** After the world, before fauna: the player must land on real terrain. */
	virtual int32 GetDustLoadPriority() const override { return 100; }
	//~ End IDustSaveParticipant

	UFUNCTION(BlueprintPure, Category = "Dust|Player")
	UDustVitalsComponent* GetVitals() const { return Vitals; }

	UFUNCTION(BlueprintPure, Category = "Dust|Player")
	UDustTraversalComponent* GetTraversal() const { return Traversal; }

	UFUNCTION(BlueprintPure, Category = "Dust|Player")
	UDustCameraComponent* GetDustCamera() const { return Camera; }

protected:
	// --- Input handlers -----------------------------------------------------

	void HandleMove(const FInputActionValue& Value);
	void HandleLook(const FInputActionValue& Value);
	void HandleTraverse(const FInputActionValue& Value);
	void HandleSprintStarted(const FInputActionValue& Value);
	void HandleSprintCompleted(const FInputActionValue& Value);
	void HandleCrouchToggle(const FInputActionValue& Value);
	void HandleCameraToggle(const FInputActionValue& Value);
	void HandleInteract(const FInputActionValue& Value);
	void HandleGlider(const FInputActionValue& Value);
	void HandleBoard(const FInputActionValue& Value);
	void HandlePhotoMode(const FInputActionValue& Value);
	void HandleJournal(const FInputActionValue& Value);

private:
	/** Places the player on the terrain surface at spawn or after a load. */
	void SettleOnGround();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dust", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDustCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dust", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDustVitalsComponent> Vitals;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dust", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDustTraversalComponent> Traversal;

	// --- Input assets, assigned in the character blueprint ------------------

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> TraverseAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> CrouchAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> CameraToggleAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> GliderAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> BoardAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> PhotoModeAction;

	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	TObjectPtr<UInputAction> JournalAction;

	/** Look sensitivity, degrees per input unit. */
	UPROPERTY(EditDefaultsOnly, Category = "Dust|Input")
	float LookSensitivity = 45.0f;
};
