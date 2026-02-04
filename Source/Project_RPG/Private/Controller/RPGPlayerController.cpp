// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/RPGPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/RPGInputComponent.h"
#include "RPGGameplayTags.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Inventory/Equipment/RPGEquipmentWidget.h"
#include "Component/RPGInventoryComponent.h"
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
	// 1. Check custom settings first
	URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings();
	if (UserSettings)
	{
		FKey CustomKey = UserSettings->GetKeyMapping(InTag);
		if (CustomKey.IsValid())
		{
			return CustomKey;
		}
	}

	// 2. Fallback to default from DataAsset/IMC
	if (InputConfigDataAsset)
	{
		UInputAction* TargetAction = InputConfigDataAsset->FindNativeInputActionByTag(InTag);
		if (!TargetAction)
		{
			TargetAction = InputConfigDataAsset->FindAbilityInputActionByTag(InTag);
		}

		if (TargetAction && InputConfigDataAsset->DefaultMappingContext)
		{
			for (const FEnhancedActionKeyMapping& Mapping : InputConfigDataAsset->DefaultMappingContext->GetMappings())
			{
				if (Mapping.Action == TargetAction)
				{
					return Mapping.Key;
				}
			}
		}
	}

	return FKey();
}

FGenericTeamId ARPGPlayerController::GetGenericTeamId() const
{
	return PlayerTeamID;
}

void ARPGPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	
}

//void ARPGPlayerController::PerformInteractionCheck_LineTrace()
//{
//	InteractionData.LastInteractionCheckTime = GetWorld()->GetWorld()->GetTimeSeconds();
//
//	FVector TraceStart{PlayerCharacter->GetPawnViewLocation()};
//	FVector TraceEnd{ TraceStart + (PlayerCharacter->GetViewRotation().Vector() * InteractionCheckDistance) };
//
//	float LookDirection =
//		FVector::DotProduct(
//			PlayerCharacter->GetActorForwardVector(),
//			PlayerCharacter->GetViewRotation().Vector());
//
//	if (LookDirection>0)
//	{
//		DrawDebugLine(GetWorld(), TraceStart, TraceEnd, FColor::Red, false, 1.0f, 0, 2.f);
//
//		FCollisionQueryParams QueryParams;
//		QueryParams.AddIgnoredActor(PlayerCharacter);
//		FHitResult TraceHit;
//
//		if (GetWorld()->LineTraceSingleByChannel(TraceHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams))
//		{
//			if (TraceHit.GetActor()->GetClass()->ImplementsInterface(UInteractionInterface::StaticClass()))
//			{
//				//const float Distance = (TraceStart - TraceHit.ImpactPoint).Size();
//
//				if (TraceHit.GetActor() != InteractionData.CurrentInteractable)
//				{
//					FoundInteractable(TraceHit.GetActor());
//					return;
//				}
//
//				else if (TraceHit.GetActor() == InteractionData.CurrentInteractable)
//				{
//					return;
//				}
//			}
//		}
//	}	
//
//	NoInteractableFound();
//}

void ARPGPlayerController::PerformInteractionCheck_Around()
{
	FVector Center = PlayerCharacter->GetActorLocation();

	TArray<FOverlapResult> OverlapResults;

	FCollisionObjectQueryParams ObjectQueryParams(ECollisionChannel::ECC_WorldDynamic);

	// 4. Ž���� ����(Sphere)�� �����մϴ�.
	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(PickupCheckRadius);

	// 5. ���� �Ķ���͸� �����մϴ�. (�÷��̾� �ڽ��� ����)
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PlayerCharacter);

	// 6. OverlapMultiByObjectType �Լ��� �����Ͽ� �ݰ� �� ��� ���͸� ã���ϴ�.
	bool bHasOverlapped = GetWorld()->OverlapMultiByObjectType(
		OverlapResults,
		Center,
		FQuat::Identity,
		ObjectQueryParams,
		CollisionShape,
		QueryParams
	);

	// ����� �뵵�� Ž�� ������ �׸��ϴ�.
	DrawDebugSphere(GetWorld(), Center, PickupCheckRadius, 12, FColor::Cyan, false, 0.5f);

	if (bHasOverlapped)
	{
		AActor* ClosestInteractable = nullptr;
		float MinDistanceSquared = FLT_MAX; // ���� ����� �Ÿ��� ã�� ���� ���� (���� �Ÿ� ���)

		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* OverlappedActor = Result.GetActor();
			if (!OverlappedActor) continue;
						// ��ȣ�ۿ� �������̽��� �����ߴ��� Ȯ���մϴ�.
			if (OverlappedActor->GetClass()->ImplementsInterface(UInteractionInterface::StaticClass()))
			{
				// ĳ���Ϳ��� �Ÿ��� ����մϴ� (���� �Ÿ��� ���� ����� �� �����մϴ�).
				float DistanceSquared =
					FVector::DistSquared(PlayerCharacter->GetActorLocation(), 
						OverlappedActor->GetActorLocation());

				// ������� ���� ������� ���ͺ��� �� �����ٸ�, �� ���͸� ���� ����� ���ͷ� �����մϴ�.
				if (DistanceSquared < MinDistanceSquared)
				{
					MinDistanceSquared = DistanceSquared;
					ClosestInteractable = OverlappedActor;
				}
			}
		}

		if (ClosestInteractable)
		{
			if (InteractionData.CurrentInteractable != ClosestInteractable)
			{
				FoundInteractable(ClosestInteractable);
			}
			return;
		}
	}

	NoInteractableFound();
}

void ARPGPlayerController::FoundInteractable(AActor* NewInteractable)
{
	if (IsInteracting())
	{
		EndInteract();
	}
	
	if (InteractionData.CurrentInteractable)
	{
		TargetInteractable = InteractionData.CurrentInteractable;
		TargetInteractable->EndFocus();
	}

	InteractionData.CurrentInteractable = NewInteractable;
	TargetInteractable = NewInteractable;

	//HUD->UpdateInteractionWidget(&TargetInteractable->InteractableData);

	TargetInteractable->BeginFocus();
}

void ARPGPlayerController::NoInteractableFound()
{
	if (IsInteracting())
	{
		GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);
	}

	if (InteractionData.CurrentInteractable)
	{
		if (IsValid(TargetInteractable.GetObject()))
		{
			TargetInteractable->EndFocus();
		}

		HUD->HideInteractionWidget();

		InteractionData.CurrentInteractable = nullptr;
		TargetInteractable = nullptr;
	}
}

void ARPGPlayerController::BeginInteract()
{
	PerformInteractionCheck_Around();

	if (InteractionData.CurrentInteractable)
	{
		if (IsValid(TargetInteractable.GetObject()))
		{
			TargetInteractable->BeginInteract();

			if (FMath::IsNearlyZero(TargetInteractable->InteractableData.InteractionDuration, 0.1f))
			{
				Interact();
			}

			else
			{
				GetWorldTimerManager().SetTimer(TimerHandle_Interaction, 
					this,
					&ThisClass::Interact,
					TargetInteractable->InteractableData.InteractionDuration,
					false
					);
			}
		}
	}
}

void ARPGPlayerController::EndInteract()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);

	if (IsValid(TargetInteractable.GetObject()))
	{
		TargetInteractable->EndInteract();
	}
}

void ARPGPlayerController::Interact()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);

	if (IsValid(TargetInteractable.GetObject()))
	{
		TargetInteractable->Interact(this);
	}
}

void ARPGPlayerController::UseQuickSlot(int32 SlotIndex)
{
	// �÷��̾� Pawn�� QuickSlotComponent�� ��������
	if (APawn* PlayerPawn = GetPawn())
	{
		QuickSlotComponent = PlayerPawn->FindComponentByClass<UQuickSlotComponent>();

		if (QuickSlotComponent)
		{			
			QuickSlotComponent->UseItemInQuickSlot(SlotIndex, this);
		}
	}
}

void ARPGPlayerController::EnableCameraZoom()
{
	bCanZoom = true;
}

void ARPGPlayerController::DropItem(URPGItemBase* ItemToDrop, const int32 QuantityToDrop)
{
	/*if (PlayerCharacter->GetRPGInventory()->FindMatchingItem(ItemToDrop))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.bNoFail = true;
		SpawnParams.SpawnCollisionHandlingOverride 
			= ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		const FVector SpawnPos{ PlayerCharacter->GetActorLocation() +
			(PlayerCharacter->GetActorForwardVector() * 50.f) };
		const FTransform SpawnTransform(PlayerCharacter->GetActorRotation(), SpawnPos);

		const int32 RemoveQuantity
			= PlayerCharacter->GetRPGInventory()->RemoveAmountOfItem(ItemToDrop, QuantityToDrop);

		ARPGPickUpBase* Pickup= GetWorld()->SpawnActor<ARPGPickUpBase>(PickupClass, SpawnTransform, SpawnParams);

		Pickup->InitializeDrop(ItemToDrop, RemoveQuantity);
	}

	else
	{
		Debug::Print("����� ������ ��������,..");
	}*/
}

void ARPGPlayerController::UpdateInteractionWidget() const
{
	if (IsValid(TargetInteractable.GetObject()))
	{
		//HUD->UpdateInteractionWidget(&TargetInteractable->InteractableData);
	}
}

void ARPGPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	Debug::Print(TEXT("comp init.."));

	// EnhancedInput
	auto* SubSystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (SubSystem)
	{
		
		SubSystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext, 0);
	}

	URPGInputComponent* RPGInputComponent = CastChecked<URPGInputComponent>(InputComponent);

	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_Move,
		ETriggerEvent::Triggered, this, &ThisClass::Input_Move);
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_Look,
		ETriggerEvent::Triggered, this, &ThisClass::Input_Look);

	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_CameraZoom,
		ETriggerEvent::Triggered, this, &ThisClass::Input_CameraZoom);

	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_SwitchTarget,
		ETriggerEvent::Triggered, this, &ThisClass::Input_SwitchTargetTriggered);
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_SwitchTarget,
		ETriggerEvent::Completed, this, &ThisClass::Input_SwitchTargetCompleted);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_PickUp_Items,
		ETriggerEvent::Started, this, &ThisClass::Input_PickUpItemsStarted);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_PickUp_Items,
		ETriggerEvent::Completed, this, &ThisClass::EndInteract);

	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_ToggleMenu,
		ETriggerEvent::Started, this, &ThisClass::ToggleInventory);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_ShowEquipmentWidget,
		ETriggerEvent::Started, this, &ThisClass::ToggleInventory);

	/*RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_UseQuickSlotF1,
		ETriggerEvent::Started, this, &ThisClass::UseQuickSlot, 0);*/
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_UseQuickSlot1,
		ETriggerEvent::Started, this, &ThisClass::UseQuickSlot, 0);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_UseQuickSlot2,
		ETriggerEvent::Started, this, &ThisClass::UseQuickSlot, 1);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_UseQuickSlot3,
		ETriggerEvent::Started, this, &ThisClass::UseQuickSlot, 2);
	
	RPGInputComponent->BindNativeInputAction(InputConfigDataAsset, RPGGameplayTags::InputTag_UseQuickSlot4,
		ETriggerEvent::Started, this, &ThisClass::UseQuickSlot, 3);

	

	RPGInputComponent->BindAbilityInputAction(InputConfigDataAsset,
			this, &ThisClass::Input_AbilityInputPressed, &ThisClass::Input_AbilityInputReleased);

	UpdateInputMappings();
}

void ARPGPlayerController::BeginPlay()
{
	Super::BeginPlay();

	PlayerCharacter = Cast<ARPGPlayer>(GetPawn());
	HUD = Cast<ARPGHUD>(GetWorld()->GetFirstPlayerController()->GetHUD());

	InventoryComponent = FindComponentByClass<URPGInventoryComponent>();

	// ������ ������Ʈ ��������
	QuickSlotComponent = GetPawn()->FindComponentByClass<UQuickSlotComponent>();

	// �������� ������ �ʱ�ȭ
	if (QuickSlotComponent)
	{
		//QuickSlotComponent->InitializeQuickSlots();
	}
}

void ARPGPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	/*if (GetWorld()->TimeSince(InteractionData.LastInteractionCheckTime)>InteractionCheckFequency)
	{
		PerformInteractionCheck_LineTrace();
	}*/
}

void ARPGPlayerController::Input_Move(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter)
	{
		PlayerCharacter->Move(InputActionValue);
		
	}
}

void ARPGPlayerController::Input_Look(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter)
	{
		PlayerCharacter->Look(InputActionValue);
		
	}
}

void ARPGPlayerController::Input_CameraZoom(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter&&bCanZoom)
	{		
		PlayerCharacter->HandleCameraZoom(InputActionValue);
		bCanZoom = false;
		GetWorld()->GetTimerManager().SetTimer(ZoomDelayHandle, this, &ThisClass::EnableCameraZoom, 0.5f, false);
	}
}



void ARPGPlayerController::Input_SwitchTargetTriggered(const FInputActionValue& InputActionValue)
{
	SwitchDirection = InputActionValue.Get<FVector2D>();
}

void ARPGPlayerController::Input_SwitchTargetCompleted(const FInputActionValue& InputActionValue)
{
	FGameplayEventData Data;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		PlayerCharacter,
		SwitchDirection.X > 0.f ? RPGGameplayTags::Player_Event_SwitchTarget_Right : 
		RPGGameplayTags::Player_Event_SwitchTarget_Left,
		Data
	);

	//Debug::Print(TEXT("SwitchDir: ") + SwitchDirection.ToString());
}

void ARPGPlayerController::Input_PickUpItemsStarted(const FInputActionValue& InputActionValue)
{
	/*PlayerCharacter->GetRPGAbilitySystemComponent()->
		TryActivateAbilityByTag(RPGGameplayTags::Player_Ability_PickUp_Instant);*/
	BeginInteract();
}

void ARPGPlayerController::Input_AbilityInputPressed(FGameplayTag InInputTag)
{
	if (PlayerCharacter)
	{
		PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputPressed(InInputTag);
	}
}

void ARPGPlayerController::Input_AbilityInputReleased(FGameplayTag InInputTag)
{
	if (PlayerCharacter)
	{
		PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputReleased(InInputTag);
	}
}

void ARPGPlayerController::ToggleInventory()
{
	if (!InventoryComponent.IsValid()) return;

	InventoryComponent->ToggleInventoryMenu();
	//HUD->ToggleMenu();
}

void ARPGPlayerController::ShowEquipmentWidget()
{
	
}
