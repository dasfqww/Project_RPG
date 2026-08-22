// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/RPGPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/RPGInputComponent.h"
#include "RPGGameplayTags.h"
#include "RPGInputTags.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Inventory/Equipment/RPGEquipmentWidget.h"
#include "Component/RPGInventoryComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/WidgetComponent.h"
#include "UI/RPGItemNameWidget.h"
#include "Item/RPGItemBase.h"
#include "Components/TextBlock.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Settings/RPGGameUserSettings.h"

#include "RPGDebugHelper.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

ARPGPlayerController::ARPGPlayerController()
{
	PlayerTeamID = FGenericTeamId(0);
	CreateDefaultSubobject<URPGInventoryProjectionComponent>(
		TEXT("InventoryProjection"));
	ItemCommandComponent = CreateDefaultSubobject<URPGItemCommandComponent>(
		TEXT("ItemCommands"));
}

void ARPGPlayerController::UpdateInputMappings()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem) return;

	URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings();
	if (!UserSettings) return;

	for (const FRPGKeyMapping& Mapping : UserSettings->CustomKeyMappings)
	{
		ApplyKeyMapping(Mapping.InputTag, Mapping.Key);
	}
}

void ARPGPlayerController::ApplyKeyMapping(FGameplayTag InTag, FKey NewKey)
{
	if (!InputConfigDataAsset) return;

	UInputAction* TargetAction = InputConfigDataAsset->FindNativeInputActionByTag(InTag);
	if (!TargetAction)
	{
		TargetAction = InputConfigDataAsset->FindAbilityInputActionByTag(InTag);
	}

	if (!TargetAction) return;

	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem) return;

	// Clean re-binding
	InputConfigDataAsset->DefaultMappingContext->UnmapKey(TargetAction, NewKey);
	InputConfigDataAsset->DefaultMappingContext->MapKey(TargetAction, NewKey);
	
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;

	Subsystem->RequestRebuildControlMappings(Options);

	// Save to settings
	URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings();
	if (UserSettings)
	{
		UserSettings->SetKeyMapping(InTag, NewKey);
		UserSettings->ApplySettings(false);
		UserSettings->SaveSettings();
	}
}

FKey ARPGPlayerController::GetCurrentKeyForTag(FGameplayTag InTag) const
{
	URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings();
	if (UserSettings)
	{
		FKey CustomKey = UserSettings->GetKeyMapping(InTag);
		if (CustomKey.IsValid()) return CustomKey;
	}

	if (InputConfigDataAsset)
	{
		UInputAction* TargetAction = InputConfigDataAsset->FindNativeInputActionByTag(InTag);
		if (!TargetAction) TargetAction = InputConfigDataAsset->FindAbilityInputActionByTag(InTag);

		if (TargetAction && InputConfigDataAsset->DefaultMappingContext)
		{
			for (const FEnhancedActionKeyMapping& Mapping : InputConfigDataAsset->DefaultMappingContext->GetMappings())
			{
				if (Mapping.Action == TargetAction) return Mapping.Key;
			}
		}
	}

	return FKey();
}

FGenericTeamId ARPGPlayerController::GetGenericTeamId() const
{
	return PlayerTeamID;
}

void ARPGPlayerController::SetSkillLockedTarget(AActor* InTarget)
{
	SkillLockedTarget = IsValid(InTarget) ? InTarget : nullptr;
}

AActor* ARPGPlayerController::GetSkillLockedTarget() const
{
	return SkillLockedTarget.IsValid() ? SkillLockedTarget.Get() : nullptr;
}

void ARPGPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	SkillLockedTarget.Reset();
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::PerformInteractionCheck_Around()
{
	if (!PlayerCharacter) return;
	FVector Center = PlayerCharacter->GetActorLocation();
	TArray<FOverlapResult> OverlapResults;
	FCollisionObjectQueryParams ObjectQueryParams(ECollisionChannel::ECC_WorldDynamic);
	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(PickupCheckRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PlayerCharacter);

	bool bHasOverlapped = GetWorld()->OverlapMultiByObjectType(OverlapResults, Center, FQuat::Identity, ObjectQueryParams, CollisionShape, QueryParams);

	if (bHasOverlapped)
	{
		AActor* ClosestInteractable = nullptr;
		float MinDistanceSquared = FLT_MAX; 

		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* OverlappedActor = Result.GetActor();
			if (!OverlappedActor) continue;
						
			if (OverlappedActor->GetClass()->ImplementsInterface(UInteractionInterface::StaticClass()))
			{
				float DistanceSquared = FVector::DistSquared(PlayerCharacter->GetActorLocation(), OverlappedActor->GetActorLocation());
				if (DistanceSquared < MinDistanceSquared)
				{
					MinDistanceSquared = DistanceSquared;
					ClosestInteractable = OverlappedActor;
				}
			}
		}

		if (ClosestInteractable)
		{
			if (InteractionData.CurrentInteractable != ClosestInteractable) FoundInteractable(ClosestInteractable);
			return;
		}
	}
	NoInteractableFound();
}

void ARPGPlayerController::FoundInteractable(AActor* NewInteractable)
{
	if (IsInteracting()) EndInteract();
	
	if (InteractionData.CurrentInteractable)
	{
		TargetInteractable = InteractionData.CurrentInteractable;
		TargetInteractable->EndFocus();
	}

	InteractionData.CurrentInteractable = NewInteractable;
	TargetInteractable = NewInteractable;
	TargetInteractable->BeginFocus();
}

void ARPGPlayerController::NoInteractableFound()
{
	if (IsInteracting()) GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);

	if (InteractionData.CurrentInteractable)
	{
		if (IsValid(TargetInteractable.GetObject())) TargetInteractable->EndFocus();
		if (HUD) HUD->HideInteractionWidget();
		InteractionData.CurrentInteractable = nullptr;
		TargetInteractable = nullptr;
	}
}

void ARPGPlayerController::BeginInteract()
{
	PerformInteractionCheck_Around();
	if (InteractionData.CurrentInteractable && IsValid(TargetInteractable.GetObject()))
	{
		TargetInteractable->BeginInteract();
		if (FMath::IsNearlyZero(TargetInteractable->InteractableData.InteractionDuration, 0.1f)) Interact();
		else
		{
			GetWorldTimerManager().SetTimer(TimerHandle_Interaction, this, &ThisClass::Interact, TargetInteractable->InteractableData.InteractionDuration, false);
		}
	}
}

void ARPGPlayerController::EndInteract()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);
	if (IsValid(TargetInteractable.GetObject())) TargetInteractable->EndInteract();
}

void ARPGPlayerController::Interact()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);
	if (IsValid(TargetInteractable.GetObject())) TargetInteractable->Interact(this);
}

void ARPGPlayerController::PressSkillSlot(const int32 SlotIndex)
{
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->BeginUseSkillSlot(SlotIndex);
	}
}

void ARPGPlayerController::ReleaseSkillSlot(const int32 SlotIndex)
{
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->EndUseSkillSlot(SlotIndex);
	}
}

void ARPGPlayerController::UseItemSlot(int32 SlotIndex)
{
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->UseItemSlot(SlotIndex, this);
	}
}

void ARPGPlayerController::EnableCameraZoom()
{
	bCanZoom = true;
}

void ARPGPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	auto* SubSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (SubSystem) SubSystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext, 0);

	URPGInputComponent* RPGInputComponent = CastChecked<URPGInputComponent>(InputComponent);

	// =========================================================================
	// [완전 자동화] 이제부터는 데이터 에셋에 액션/태그만 추가하면 자동으로 연결됩니다.
	// =========================================================================
	
	// 1. 모든 Native 입력 자동 바인딩 (리플렉션 이용)
	// 규칙: 태그 InputTag.Move -> 함수 Input_Move
	RPGInputComponent->BindNativeInputActions(InputConfigDataAsset, this);

	// 2. 퀵슬롯 루프 바인딩
	RPGInputComponent->BindNativeInputAction(
		InputConfigDataAsset,
		RPGGameplayTags::InputTag_QuickItem_F1,
		ETriggerEvent::Started,
		this,
		&ThisClass::UseItemSlot,
		0);

	for (int32 i = 0; i < 8; ++i)
	{
		FGameplayTag SkillTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("InputTag.QuickSkill.%d"), i + 1)));
		FGameplayTag ItemTag = FGameplayTag::RequestGameplayTag(FName(*FString::Printf(TEXT("InputTag.QuickItem.%d"), i + 1)));

		RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, SkillTag, ETriggerEvent::Started, this, &ThisClass::PressSkillSlot, i);
		RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, SkillTag, ETriggerEvent::Completed, this, &ThisClass::ReleaseSkillSlot, i);
		RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, SkillTag, ETriggerEvent::Canceled, this, &ThisClass::ReleaseSkillSlot, i);
		RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, ItemTag, ETriggerEvent::Started, this, &ThisClass::UseItemSlot, i);
	}

	// 3. GAS 어빌리티 바인딩 (이미 자동화 구조)
	RPGInputComponent->BindAbilityInputAction(InputConfigDataAsset, this, &ThisClass::Input_AbilityInputPressed, &ThisClass::Input_AbilityInputReleased);

	UpdateInputMappings();
}

void ARPGPlayerController::BeginPlay()
{
	Super::BeginPlay();
	HUD = Cast<ARPGHUD>(GetHUD());
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::RefreshControlledPawnReferences()
{
	PlayerCharacter = Cast<ARPGPlayer>(GetPawn());
	InventoryComponent = FindComponentByClass<URPGInventoryComponent>();
	QuickSlotComponent = GetPawn() ? GetPawn()->FindComponentByClass<UQuickSlotComponent>() : nullptr;

	if (QuickSlotComponent)
	{
		QuickSlotComponent->BindInventory(InventoryComponent.Get());
	}
	if (ItemCommandComponent)
	{
		ItemCommandComponent->HandlePawnChanged();
	}
}

void ARPGPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

// --- 핸들러 구현부 (이름 규칙: Input_태그명) ---

void ARPGPlayerController::Input_Move(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter) PlayerCharacter->Move(InputActionValue);
}

void ARPGPlayerController::Input_Look(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter) PlayerCharacter->Look(InputActionValue);
}

void ARPGPlayerController::Input_CameraZoom(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter && bCanZoom)
	{		
		PlayerCharacter->HandleCameraZoom(InputActionValue);
		bCanZoom = false;
		GetWorld()->GetTimerManager().SetTimer(ZoomDelayHandle, this, &ThisClass::EnableCameraZoom, 0.5f, false);
	}
}

void ARPGPlayerController::Input_SwitchTarget(const FInputActionValue& InputActionValue)
{
	// Triggered와 Completed를 모두 처리할 수 있음
	if (InputActionValue.Get<FVector2D>() != FVector2D::ZeroVector)
	{
		SwitchDirection = InputActionValue.Get<FVector2D>();
	}
	else // Completed 시점 (Value가 0이 됨)
	{
		FGameplayEventData Data;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(PlayerCharacter,
			SwitchDirection.X > 0.f ? RPGGameplayTags::Player_Event_SwitchTarget_Right : RPGGameplayTags::Player_Event_SwitchTarget_Left, Data);
	}
}

void ARPGPlayerController::Input_PickUp_Items(const FInputActionValue& InputActionValue)
{
	if (InputActionValue.Get<bool>()) BeginInteract();
	else EndInteract();
}

void ARPGPlayerController::Input_ToggleMenu(const FInputActionValue& Value)
{
	if (HUD) HUD->ToggleMenu();
}

void ARPGPlayerController::Input_ShowEquipmentWidget(const FInputActionValue& Value)
{
	ToggleInventory();
}

void ARPGPlayerController::Input_AbilityInputPressed(FGameplayTag InInputTag)
{
	if (PlayerCharacter) PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputPressed(InInputTag);
}

void ARPGPlayerController::Input_AbilityInputReleased(FGameplayTag InInputTag)
{
	if (PlayerCharacter) PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputReleased(InInputTag);
}

void ARPGPlayerController::ToggleInventory()
{
	if (InventoryComponent.IsValid()) InventoryComponent->ToggleInventoryMenu();
}

void ARPGPlayerController::ToggleOptionMenu()
{
	if (HUD) HUD->ToggleOptionMenu();
}

void ARPGPlayerController::ShowEquipmentWidget()
{
}
