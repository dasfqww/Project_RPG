// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/UI/QuickSlotComponent.h"
#include "Net/UnrealNetwork.h"
#include "UI/RPGQuickSlotWidget.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "UI/RPGWidgetBase.h"
#include "RPGFunctionLibrary.h"
#include "Component/RPGInventoryComponent.h"

#include "RPGDebugHelper.h"

UQuickSlotComponent::UQuickSlotComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	QuickSlotItems.SetNum(MaxSlots);
}

void UQuickSlotComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UQuickSlotComponent, QuickSlotItems);
}

void UQuickSlotComponent::OnRep_QuickSlotItems()
{
	for (int32 i = 0; i < QuickSlotWidgets.Num(); i++)
	{
		if (QuickSlotWidgets[i])
		{
			if (QuickSlotItems.IsValidIndex(i))
			{
				OnQuickSlotChanged.Broadcast(i, QuickSlotItems[i]);
			}
		}
	}
}

void UQuickSlotComponent::SetQuickSlotItem(int32 Index, URPGItemBase* NewItem)
{
	if (GetOwner()->HasAuthority())
	{
		if (!QuickSlotItems.IsValidIndex(Index)) return;

		// 소모성 아이템만 등록 가능하도록 제한
		if (NewItem && !NewItem->IsConsumable())
		{
			UE_LOG(LogTemp, Warning, TEXT("QuickSlot: Only consumable items can be registered."));
			return;
		}

		QuickSlotItems[Index] = NewItem;
		OnQuickSlotChanged.Broadcast(Index, NewItem);
	}
	else
	{
		Server_SetQuickSlotItem(Index, NewItem);
	}
}

void UQuickSlotComponent::Server_SetQuickSlotItem_Implementation(int32 Index, URPGItemBase* NewItem)
{
	SetQuickSlotItem(Index, NewItem);
}

void UQuickSlotComponent::BeginPlay()
{
	Super::BeginPlay();

	// 인벤토리 컴포넌트 구독
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());

		// OwnerPawn->GetController()를 통해 인벤토리를 가져옴
		if (URPGInventoryComponent* Inventory = URPGFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC))
		{
			Inventory->OnQuantityChanged.AddDynamic(this, &UQuickSlotComponent::HandleOnItemQuantityChanged);
			Inventory->OnItemRemoved.AddDynamic(this, &UQuickSlotComponent::HandleOnItemRemoved);
		}
	}
	
	InitializeItemQuickSlots();
}

void UQuickSlotComponent::InitializeItemQuickSlots()
{
	if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (!OwnerPawn->IsLocallyControlled()) return;
	}

	URPGWidgetBase* PlayerHUD = CreateWidget<URPGWidgetBase>(GetWorld(), PlayerHUDClass);

	if (PlayerHUD)
	{
		for (int i = 0; i < MaxSlots; i++)
		{
			FString SlotName = FString::Printf(TEXT("WBP_QuickSlot%d"), i+1);
			URPGQuickSlotWidget* QuickSlot = Cast<URPGQuickSlotWidget>(PlayerHUD->GetWidgetFromName(FName(*SlotName)));
			
			if (QuickSlot)
			{
				QuickSlotWidgets.Add(QuickSlot);
			}
		}
	}
}

void UQuickSlotComponent::HandleOnItemRemoved(URPGItemBase* RemovedItem)
{
	if (!RemovedItem) return;

	for (int32 i = 0; i < QuickSlotItems.Num(); i++)
	{
		if (QuickSlotItems[i] == RemovedItem)
		{
			ClearQuickSlot(i);
		}
	}
}

void UQuickSlotComponent::HandleOnItemQuantityChanged(const FSlotAvailabilityResult& Result)
{
	URPGItemBase* ChangedItem = Result.Item.Get();
	if (!ChangedItem) return;

	// 해당 아이템이 퀵슬롯에 있는지 전수 조사
	for (int32 i = 0; i < QuickSlotItems.Num(); i++)
	{
		if (QuickSlotItems[i] == ChangedItem)
		{
			// 수량 델리게이트 방송
			OnQuickSlotQuantityChanged.Broadcast(ChangedItem, ChangedItem->GetTotalQuantity());
		}
	}
}

void UQuickSlotComponent::UseItemInQuickSlot(int32 Index, const APlayerController* PC)
{
	if (URPGItemBase* ItemToUse = GetItemInSlot(Index))
	{
		if (URPGInventoryComponent* InventoryComponent = URPGFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(PC))
		{
			InventoryComponent->Server_ConsumeItem(ItemToUse);
		}
	}
}

void UQuickSlotComponent::SwapQuickSlotItems(int32 SlotIndexA, int32 SlotIndexB)
{
	if (GetOwner()->HasAuthority())
	{
		if (!QuickSlotItems.IsValidIndex(SlotIndexA) || !QuickSlotItems.IsValidIndex(SlotIndexB)) return;

		QuickSlotItems.Swap(SlotIndexA, SlotIndexB);

		OnQuickSlotChanged.Broadcast(SlotIndexA, QuickSlotItems[SlotIndexA]);
		OnQuickSlotChanged.Broadcast(SlotIndexB, QuickSlotItems[SlotIndexB]);
	}
	else
	{
		Server_SwapQuickSlotItems(SlotIndexA, SlotIndexB);
	}
}

void UQuickSlotComponent::Server_SwapQuickSlotItems_Implementation(int32 SlotIndexA, int32 SlotIndexB)
{
	SwapQuickSlotItems(SlotIndexA, SlotIndexB);
}

void UQuickSlotComponent::ClearQuickSlot(int32 Index)
{
	if (GetOwner()->HasAuthority())
	{
		if (!QuickSlotItems.IsValidIndex(Index)) return;
		QuickSlotItems[Index] = nullptr;
		OnQuickSlotChanged.Broadcast(Index, nullptr);
	}
	else
	{
		Server_ClearQuickSlot(Index);
	}
}

void UQuickSlotComponent::Server_ClearQuickSlot_Implementation(int32 Index)
{
	ClearQuickSlot(Index);
}

URPGItemBase* UQuickSlotComponent::GetItemInSlot(int32 Index) const
{
	if (QuickSlotItems.IsValidIndex(Index))
	{
		return QuickSlotItems[Index];
	}
	return nullptr;
}
