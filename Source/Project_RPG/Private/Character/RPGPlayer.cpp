// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/RPGPlayer.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/RPGInputComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataAsset/StartUpData/DataAsset_PlayerStartUpData.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Components/WidgetComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "GameMode/RPGGameModeBase.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

ARPGPlayer::ARPGPlayer():
	TargetArmLength(400.f)
{
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.f);

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetRootComponent());
	CameraBoom->TargetArmLength = TargetArmLength;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 55.f);
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	NS_SceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("TransformToPlayNS"));
	NS_SceneComponent->SetupAttachment(GetRootComponent());

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
	GetCharacterMovement()->MaxWalkSpeed = 400.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	PlayerCombatComponent = CreateDefaultSubobject<UPlayerCombatComponent>(TEXT("PlayerCombatComp"));
	PlayerUIComponent = CreateDefaultSubobject<UPlayerUIComponent>(TEXT("PlayerUIComp"));
	SecurityValidationComponent =
		CreateDefaultSubobject<URPGSecurityValidationComponent>(
			TEXT("SecurityValidationComponent"));
	
	/*PlayerInventory = CreateDefaultSubobject<URPGInventoryComponent>(TEXT("PlayerInventory"));
	PlayerInventory->SetSlotsCapacity(20);
	PlayerInventory->SetWeightCapacity(50.f);*/

	QuickSlotComponent = CreateDefaultSubobject<UQuickSlotComponent>(TEXT("QuickSlot"));

	DamageFontComponent = CreateDefaultSubobject<USceneComponent>(TEXT("DamageFontComp"));
	DamageFontComponent->SetupAttachment(GetRootComponent());

	BaseEyeHeight = 75.f;
}

void ARPGPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (!CharacterStartUpData.IsNull())
	{
		if (UDataAsset_StartUpDataBase* LoadedData = CharacterStartUpData.LoadSynchronous())
		{			
			int32 AbilityApplyLevel = 1;

			if (ARPGGameModeBase* BaseGameMode = GetWorld()->GetAuthGameMode<ARPGGameModeBase>())
			{
				switch (BaseGameMode->GetCurrentGameDifficulty())
				{
				case ERPGGameDifficulty::Easy:
					AbilityApplyLevel = 4;
					break;

				case ERPGGameDifficulty::Normal:
					AbilityApplyLevel = 3;
					break;

				case ERPGGameDifficulty::Hard:
					AbilityApplyLevel = 2;
					break;

				case ERPGGameDifficulty::Hell:
					AbilityApplyLevel = 1;
					break;

				default:
					break;
				}
			}

			LoadedData->GiveToAbilitySystemComponent(RPGAbilitySystemComponent, AbilityApplyLevel);
		}
	}
}

void ARPGPlayer::BeginPlay()
{
	Super::BeginPlay();

	
}

void ARPGPlayer::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	
		float NewLength = FMath::FInterpTo(CameraBoom->TargetArmLength, 
			TargetArmLength, DeltaTime, CameraZoomLerpSpeed);
		CameraBoom->TargetArmLength = NewLength;

		//Debug::Print("Current Arm Length: ", CameraBoom->TargetArmLength);
	

	
}

UPawnCombatComponent* ARPGPlayer::GetPawnCombatComponent() const
{
	return PlayerCombatComponent;
}

UPawnUIComponent* ARPGPlayer::GetPawnUIComponent() const
{
	return PlayerUIComponent;
}

UPlayerUIComponent* ARPGPlayer::GetPlayerUIComponent() const
{
	return PlayerUIComponent;
}

//void ARPGPlayer::SetupGASInputComponent()
//{
//
//}

void ARPGPlayer::Move(const FInputActionValue& InputActionValue)
{
	

	const FVector2D MovementVector = InputActionValue.Get<FVector2D>();

	const FRotator MovementRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);

	if (MovementVector.Y != 0.f)
	{
		const FVector ForwardDirection = MovementRotation.RotateVector(FVector::ForwardVector);

		AddMovementInput(ForwardDirection, MovementVector.Y);
	}

	if (MovementVector.X != 0.f)
	{
		const FVector RightDirection = MovementRotation.RotateVector(FVector::RightVector);

		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void ARPGPlayer::Look(const FInputActionValue& InputActionValue)
{


	const FVector2D LookAxisVector = InputActionValue.Get<FVector2D>();

	if (LookAxisVector.X != 0.f)
	{
		AddControllerYawInput(LookAxisVector.X);
	}

	if (LookAxisVector.Y != 0.f)
	{
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void ARPGPlayer::HandleCameraZoom(const FInputActionValue& InputActionValue)
{
	const float ScrollValue = InputActionValue.Get<float>();

	Debug::Print("Wheel scroll Value: ", ScrollValue);

	if (FMath::IsNearlyZero(ScrollValue))
		return;

	float ZoomAmount = ScrollValue > 0.f ? -CameraZoomStep : CameraZoomStep;

	TargetArmLength = FMath::Clamp(TargetArmLength + ZoomAmount, CameraZoomMin, CameraZoomMax);
}
