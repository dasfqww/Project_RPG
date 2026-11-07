// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/RPGPlayer.h"
#include "Riding/RPGRidingComponent.h"
#include "Riding/RPGRidingPresentationComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputSubsystems.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/RPGInputComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "DataAsset/StartUpData/DataAsset_PlayerStartUpData.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Equipment/RPGPlayerWeaponLoadoutComponent.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/Development/RPGPlayerClassDevelopmentComponent.h"
#include "Progression/RPGCharacterProgressionComponent.h"
#include "Components/WidgetComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Component/RPGPlayerMovementComponent.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "GameMode/RPGGameModeBase.h"
#include "Attribute/RPGAttributeSet.h"
#include "Abilities/GameplayAbility.h"
#include "Net/UnrealNetwork.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

ARPGPlayer::ARPGPlayer(const FObjectInitializer& ObjectInitializer):
	Super(ObjectInitializer.SetDefaultSubobjectClass<URPGPlayerMovementComponent>(ACharacter::CharacterMovementComponentName)),
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
    CreateDefaultSubobject<URPGRidingComponent>(TEXT("Riding"));
    CreateDefaultSubobject<URPGRidingPresentationComponent>(TEXT("RidingPresentation"));
	WeaponLoadoutComponent = CreateDefaultSubobject<URPGPlayerWeaponLoadoutComponent>(TEXT("WeaponLoadoutComponent"));
	PlayerUIComponent = CreateDefaultSubobject<UPlayerUIComponent>(TEXT("PlayerUIComp"));
	CreateDefaultSubobject<URPGShieldComponent>(TEXT("ShieldComponent"));
	PlayerSkillComponent = CreateDefaultSubobject<URPGPlayerSkillComponent>(TEXT("PlayerSkillComponent"));
	PlayerClassComponent = CreateDefaultSubobject<URPGPlayerClassComponent>(TEXT("PlayerClassComponent"));
	CharacterProgressionComponent = CreateDefaultSubobject<URPGCharacterProgressionComponent>(TEXT("CharacterProgressionComponent"));
	ClassDevelopmentComponent = CreateDefaultSubobject<URPGPlayerClassDevelopmentComponent>(TEXT("ClassDevelopmentComponent"));
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

void ARPGPlayer::ProcessEvent(UFunction* Function, void* Parms)
{
	// UE 5.8's packed move _Implementation is non-virtual. Gate the actual RPC
	// dispatch, not a shadow method that the inherited exec thunk would bypass.
	static const FName PackedMoveName = GET_FUNCTION_NAME_CHECKED(ACharacter, ServerMovePacked);
	if (Function && Function->GetFName() == PackedMoveName
		&& Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer)
		&& HasAuthority() && IsDungeonGameplayRevoked(this)) return;
	Super::ProcessEvent(Function, Parms);
}

void ARPGPlayer::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (IsDungeonGameplayRevoked(NewController) || IsDungeonGameplayRevoked(this))
	{
		if (const auto* State = GetPlayerState<ARPGPlayerState>())
			RPGAbilitySystemComponent->RevokeDungeonGameplayAuthority(*State);
		return;
	}

	if (bApplyingStartup) return;
	if (InitializedStartupASC.IsValid())
	{
		if (InitializedStartupASC.Get() == RPGAbilitySystemComponent)
			PlayerClassComponent->NotifyAbilitySystemReady(*RPGAbilitySystemComponent, StartupAbilityGrantLevel);
		CharacterProgressionComponent->NotifyOwnerContextChanged();
		WeaponLoadoutComponent->RefreshLoadout();
		return;
	}
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

			// Publish before startup callbacks can synchronously reenter PossessedBy.
			TGuardValue<bool> StartupGuard(bApplyingStartup, true);
			InitializedStartupASC = RPGAbilitySystemComponent;
			StartupAbilityGrantLevel = AbilityApplyLevel;
			LoadedData->GiveToAbilitySystemComponent(RPGAbilitySystemComponent, AbilityApplyLevel);
			if (!IsActorBeingDestroyed() && !IsDungeonGameplayRevoked(this)
				&& RPGAbilitySystemComponent->GetAvatarActor() == this)
				PlayerClassComponent->NotifyAbilitySystemReady(*RPGAbilitySystemComponent, AbilityApplyLevel);
		}
	}
	CharacterProgressionComponent->NotifyOwnerContextChanged();
	WeaponLoadoutComponent->RefreshLoadout();
}

void ARPGPlayer::PawnClientRestart()
{
	Super::PawnClientRestart();
	// Initial spec/RPC replication can precede local Controller assignment.
	// Refresh cached GAS player context only after the engine has restarted the Pawn.
	if (RPGAbilitySystemComponent)
	{
		RPGAbilitySystemComponent->RefreshLocalAbilityActorInfo();
	}
	WeaponLoadoutComponent->RefreshPresentation();
}

void ARPGPlayer::BeginPlay()
{
	InitialCapsuleCollision = GetCapsuleComponent()->GetCollisionEnabled();
	bInitialPauseAnims = GetMesh()->bPauseAnims;
	bPresentationCaptured = true;
	Super::BeginPlay();
	if (EncounterRecoveryRevision != 0) ApplyEncounterRecovery();
	// Server weapon collision uses animated sockets even when no mesh is rendered.
	// ACharacter's AlwaysTickPose default does not refresh those bone transforms.
	if (HasAuthority())
	{
		GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}

	
}

void ARPGPlayer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, EncounterRecoveryRevision);
}

bool ARPGPlayer::RestoreEncounterPresentation()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const URPGAttributeSet* Attributes = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	if (!HasAuthority() || !ASC || ASC->GetAvatarActor() != this || !bPresentationCaptured ||
		!Attributes || !FMath::IsFinite(Attributes->GetCurrentHealth()) || Attributes->GetCurrentHealth() <= 0.f ||
		ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) || IsDungeonGameplayRevoked(this)) return false;
	const FGameplayTagContainer DeathAbilities(RPGGameplayTags::Shared_Ability_Death);
	ASC->CancelAbilities(&DeathAbilities);
	if (!IsValid(this) || IsActorBeingDestroyed() || ASC->GetAvatarActor() != this) return false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetAssetTags().HasTag(RPGGameplayTags::Shared_Ability_Death))
			return false;
	}
	++EncounterRecoveryRevision;
	GetCombatControlComponent()->ResetForAvatarReuse();
	ApplyEncounterRecovery();
	ForceNetUpdate();
	return true;
}

void ARPGPlayer::OnRep_EncounterRecoveryRevision()
{
	if (EncounterRecoveryRevision != 0) ApplyEncounterRecovery();
}

void ARPGPlayer::ApplyEncounterRecovery()
{
	if (!bPresentationCaptured || IsActorBeingDestroyed() || IsDungeonGameplayRevoked(this)) return;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		const FGameplayTagContainer DeathAbilities(RPGGameplayTags::Shared_Ability_Death);
		ASC->CancelAbilities(&DeathAbilities);
	}
	if (!IsValid(this) || IsActorBeingDestroyed()) return;
	GetCapsuleComponent()->SetCollisionEnabled(InitialCapsuleCollision);
	GetMesh()->bPauseAnims = bInitialPauseAnims;
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
