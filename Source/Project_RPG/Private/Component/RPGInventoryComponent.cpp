#include "Component/RPGInventoryComponent.h"
#include "UI/Inventory/RPGInventoryBase.h"
#include "Controller/RPGPlayerController.h"
#include "Character/RPGPlayer.h"
#include "Item/RPGItemBase.h"
#include "Manager/DataManager.h"
#include "Manager/HttpWebManager.h" // HTTP Manager Include
#include "Interface/PawnUIInterface.h"
#include "Component/UI/PlayerUIComponent.h"
#include "UI/Inventory/Spatial/RPGInventoryGrid.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Net/UnrealNetwork.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Kismet/GameplayStatics.h"
#include "RPGDebugHelper.h"

// Sets default values for this component's properties
URPGInventoryComponent::URPGInventoryComponent() : InventoryList(this)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	SetIsReplicatedByDefault(true);
	bReplicateUsingRegisteredSubObjectList = true;
	bShowInventory = false;
	// ...
}

void URPGInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ThisClass, InventoryList);
}

// Called when the game starts
void URPGInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	
	ARPGPlayerController* PC = Cast<ARPGPlayerController>(GetOwner());
	if (IsValid(PC))
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
		if (IsValid(Player))
		{
			CachedPawnUIInterface = Cast<IPawnUIInterface>(Player);
		}
	}

	ConstructInventory();

	// [NEW] 웹 서버에서 인벤토리 데이터 로드 요청 (로컬 플레이어만)
	if (PC && PC->IsLocalController())
	{
		UGameInstance* GI = GetWorld()->GetGameInstance();
		if (GI)
		{
			UHttpWebManager* WebManager = GI->GetSubsystem<UHttpWebManager>();
			if (WebManager)
			{
				// 델리게이트 바인딩 (이미 바인딩되어 있는지 확인하거나 Remove 후 Add 권장)
				WebManager->OnInventoryLoaded.RemoveDynamic(this, &URPGInventoryComponent::OnWebInventoryLoaded);
				WebManager->OnInventoryLoaded.AddDynamic(this, &URPGInventoryComponent::OnWebInventoryLoaded);

				// 데이터 요청
				FString CharacterID = PC->GetName();
				WebManager->LoadInventoryFromWeb(CharacterID);
			}
		}
	}
}

void URPGInventoryComponent::OnWebInventoryLoaded(const TArray<FItemSaveData>& LoadedData)
{
	if (LoadedData.Num() > 0)
	{
		RestoreInventoryFromData(LoadedData);
		Debug::Print(TEXT("Inventory Restored from Web Data! Item Count"), LoadedData.Num(), -1, FColor::Green);
	}
	else
	{
		Debug::Print(TEXT("No Web Data found or Empty Inventory."), FColor::Cyan);
	}
}

void URPGInventoryComponent::ConstructInventory()
{
	OwningController = Cast<ARPGPlayerController>(GetOwner());
	checkf(OwningController.IsValid(), TEXT("Inventory Component should have a Player Controller as Owner."));
	if (!OwningController->IsLocalController()) return;

	if (IsValid(InventoryMenuClass))
	{
		InventoryMenu = CreateWidget<URPGInventoryBase>(OwningController.Get(), InventoryMenuClass);
		if (InventoryMenu)
		{
			InventoryMenu->AddToViewport();
			DisplayInventory(bShowInventory);
		}
	}
	else
	{
		Debug::Print(TEXT("InventoryMenuClass is NOT set in BP!"), FColor::Red);
	}
}

void URPGInventoryComponent::DisplayInventory(bool bShow)
{
	if (!InventoryMenu) return;

	if (bShow)
	{
		InventoryMenu->SetVisibility(ESlateVisibility::Visible);

		FInputModeGameAndUI	InputMode;
		OwningController->SetInputMode(InputMode);
		OwningController->SetShowMouseCursor(true);
	}

	else
	{
		InventoryMenu->SetVisibility(ESlateVisibility::Hidden);
		
		FInputModeGameOnly InputMode;
		OwningController->SetInputMode(InputMode);
		OwningController->SetShowMouseCursor(false);
	}
}

void URPGInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	ARPGPlayerController* PC = OwningController.Get();
	if (!PC) PC = Cast<ARPGPlayerController>(GetOwner());

	// 클라이언트(소유자)인 경우에만 저장을 시도합니다.
	if (PC && PC->IsLocalController())
	{
		TArray<FItemSaveData> ConvertedData = GetInventorySaveData();

		// [NEW] 웹 서버로 저장 요청
		UGameInstance* GI = GetWorld()->GetGameInstance();
		if (GI)
		{
			UHttpWebManager* WebManager = GI->GetSubsystem<UHttpWebManager>();
			if (WebManager)
			{
				FString CharacterID = PC->GetName();
				WebManager->SaveInventoryToWeb(ConvertedData, CharacterID);
			}
		}
	}
}

void URPGInventoryComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

TArray<URPGItemBase*> URPGInventoryComponent::GetAllItems() const
{
	return InventoryList.GetAllItems();
}

TArray<FItemSaveData> URPGInventoryComponent::GetInventorySaveData()
{
	TArray<FItemSaveData> SaveData;

	// InventoryList(FastArray)에서 모든 아이템 가져오기
	TArray<URPGItemBase*> AllItems = InventoryList.GetAllItems();

	for (URPGItemBase* Item : AllItems)
	{
		if (IsValid(Item))
		{
			FItemSaveData NewData;
			// GameplayTag를 FName(ItemID)으로 변환하여 저장
			const FGameplayTag& Tag = Item->GetItemManifest().GetItemTag();
			NewData.ItemID = Tag.GetTagName();
			NewData.Quantity = Item->GetTotalQuantity();
			NewData.SlotIndex = Item->SlotIndex;
			NewData.Category = Tag.ToString(); // 태그 전체 문자열을 카테고리로 저장 (서버에서 필터링 용이)
			
			SaveData.Add(NewData);
		}
	}

	return SaveData;
}

void URPGInventoryComponent::RestoreInventoryFromData(const TArray<FItemSaveData>& SaveData)
{
	if (SaveData.Num() == 0) return;

	// 클라이언트에서 서버로 "이 데이터들로 복구해줘"라고 요청
	Server_RestoreInventory(SaveData);
}

void URPGInventoryComponent::Server_RestoreInventory_Implementation(const TArray<FItemSaveData>& SaveData)
{
	// 기존 인벤토리 초기화 (선택 사항)
	// InventoryList.Clear(); 

	for (const FItemSaveData& Data : SaveData)
	{
		FGameplayTag ItemTag = FGameplayTag::RequestGameplayTag(Data.ItemID);
		if (!ItemTag.IsValid()) continue;

		FItemManifest Manifest;
		// 싱글톤 패턴으로 직접 접근하여 데이터 획득
		if (UDataManager::Get()->GetItemManifestByTag(ItemTag, Manifest))
		{
			// 월드에 스폰하지 않고, 메모리에 아이템 객체만 생성
			URPGItemBase* NewItem = NewObject<URPGItemBase>(GetOwner());
			NewItem->SetItemManifest(Manifest);
			NewItem->SetTotalQuantity(Data.Quantity);
			NewItem->SlotIndex = Data.SlotIndex;

			InventoryList.AddEntry(NewItem);

			// [Fix] Standalone 모드 등에서 UI 갱신을 위해 델리게이트 브로드캐스트
			// 리플리케이션이 발생하지 않는 로컬 환경(Standalone)에서는 PostReplicatedAdd가 호출되지 않음
			if (GetOwner()->GetNetMode() == NM_Standalone || GetOwner()->GetNetMode() == NM_ListenServer)
			{
				OnItemAdded.Broadcast(NewItem);
			}
		}
	}

	Debug::Print(TEXT("Server: Inventory Restored via Singleton!"), SaveData.Num(), -1, FColor::Green);
}


void URPGInventoryComponent::TryAddItem(ARPGPickUpBase* InPickup)
{
	//OnInventoryIsFull.Broadcast();
	FSlotAvailabilityResult Result = InventoryMenu->HasSpaceForItem(InPickup);

	URPGItemBase* FoundItem = InventoryList.FindFirstItemType(InPickup->GetItemManifest().GetItemTag());
	Result.Item = FoundItem;

	UPlayerUIComponent* PlayerUIComponent = CachedPawnUIInterface->GetPlayerUIComponent();
	
	if (Result.TotalSpaceToFill==0)
	{
		if (IsValid(PlayerUIComponent))
		{
			FText Text = FText::FromString("Inventory is Full.");
			PlayerUIComponent->OnNoticeTextChanged.Broadcast(Text);
		}
	}
	
	if (Result.Item.IsValid() && Result.bStackable)
	{
		// Add stacks to an item that already exists in the inventory. We only want to update the stack count,
		// not create a new item of this type.
		OnQuantityChanged.Broadcast(Result);
		Server_AddStacksToItem(InPickup, Result.TotalSpaceToFill, Result.Remainder);
	}

	else if (Result.TotalSpaceToFill > 0)
	{
		// 빈 슬롯 찾기: FSlotAvailabilityResult 구조체 확인 필요
		int32 TargetSlot = -1;
		if (Result.SlotAvailabilities.Num() > 0)
		{
			TargetSlot = Result.SlotAvailabilities[0].Index;
		}

		// This item type doesn't exist in the inventory. Create a new one and update all pertinent slots.
		// [수정] TargetSlot 전달
		Server_AddNewItem(InPickup, Result.TotalSpaceToFill, TargetSlot);
	}

}

void URPGInventoryComponent::Server_AddNewItem_Implementation(ARPGPickUpBase* ItemPickup, int32 Quantity, int32 TargetSlotIndex)
{
	URPGItemBase* NewItem = InventoryList.AddEntry(ItemPickup);

	// [추가] 슬롯 인덱스 설정
	if (NewItem)
	{
		NewItem->SlotIndex = TargetSlotIndex;
		NewItem->SetTotalQuantity(Quantity);
	}

	if (GetOwner()->GetNetMode() == NM_ListenServer ||
		GetOwner()->GetNetMode() == NM_DedicatedServer ||
		GetOwner()->GetNetMode() == NM_Standalone)
	{
		/*URPGInventoryGrid* Grid = CreateWidget<URPGInventoryGrid>(InventoryMenu);
		if (Grid)
		{
			Grid->AddItem(NewItem);
		}*/
		OnItemAdded.Broadcast(NewItem);
	}

	// TODO: Tell the Item Component to destroy its owning actor.
	ItemPickup->PickedUp();
}

void URPGInventoryComponent::Server_AddStacksToItem_Implementation
	(ARPGPickUpBase* ItemPickup, int32 Quantity, int32 Remainder)
{
	const FGameplayTag& ItemTag
		= IsValid(ItemPickup) ? ItemPickup->GetItemManifest().GetItemTag() : FGameplayTag::EmptyTag;
	URPGItemBase* Item = InventoryList.FindFirstItemType(ItemTag);
	if (!IsValid(Item)) return;

	Item->SetTotalQuantity(Item->GetTotalQuantity() + Quantity);

	if (Remainder==0)
	{
		ItemPickup->PickedUp();
	}
	
	else if (FStackableFragment* StackableFragment
		=ItemPickup->GetItemManifest().GetFragmentOfTypeMutable<FStackableFragment>())
	{
		StackableFragment->SetQuantity(Remainder);
	}
}

void URPGInventoryComponent::Server_DropItem_Implementation(URPGItemBase* Item, int32 Quantity)
{
	if (!IsValid(Item)) return;

	const int32 NewQuantity = Item->GetTotalQuantity() - Quantity;
	
	// 먼저 드롭할 아이템을 스폰 (원본 아이템 수정 전)
	SpawnDroppedItem(Item, Quantity);
	
	// 그 후 인벤토리에서 제거 또는 수량 감소
	if (NewQuantity <= 0)
	{
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		Item->SetTotalQuantity(NewQuantity);
	}
}

void URPGInventoryComponent::Server_ConsumeItem_Implementation(URPGItemBase* Item)
{
	const int32 NewQuantity = Item->GetTotalQuantity() - 1;
	if (NewQuantity<=0)
	{
		InventoryList.RemoveEntry(Item);
	}
	else
	{
		Item->SetTotalQuantity(NewQuantity);
	}

	if (FConsumeModifier* ConsumableFragment=
		Item->GetItemManifestMutable().GetFragmentOfTypeMutable<FConsumeModifier>())
	{
		ConsumableFragment->OnConsume(OwningController.Get());
	}
}

void URPGInventoryComponent::Server_MoveItem_Implementation(URPGItemBase* Item, int32 NewSlotIndex)
{
	if (!IsValid(Item)) return;

	// Check if there is an item at the destination slot
	URPGItemBase* ItemAtDestination = nullptr;
	TArray<URPGItemBase*> AllItems = InventoryList.GetAllItems();

	for (URPGItemBase* ExistingItem : AllItems)
	{
		if (IsValid(ExistingItem) && ExistingItem->SlotIndex == NewSlotIndex && ExistingItem != Item)
		{
			ItemAtDestination = ExistingItem;
			break;
		}
	}

	if (ItemAtDestination)
	{
		// Swap: ItemAtDestination takes the old slot of Item
		ItemAtDestination->SlotIndex = Item->SlotIndex;
	}

	Item->SlotIndex = NewSlotIndex;
}

void URPGInventoryComponent::ToggleInventoryMenu()
{
	bShowInventory = !bShowInventory;
	DisplayInventory(bShowInventory);
}

void URPGInventoryComponent::AddRepSubObj(UObject* SubObj)
{
	if (IsUsingRegisteredSubObjectList() && IsReadyForReplication() && IsValid(SubObj))
	{
		AddReplicatedSubObject(SubObj);
	}
}

void URPGInventoryComponent::SpawnDroppedItem(URPGItemBase* Item, int32 Quantity)
{
	if (!IsValid(Item) || !OwningController.IsValid()) return;

	const APawn* OwningPawn = OwningController->GetPawn();
	if (!IsValid(OwningPawn)) return;

	FVector RotateForward = OwningPawn->GetActorForwardVector();
	RotateForward = RotateForward.RotateAngleAxis(
		FMath::FRandRange(DropSpawnAngleMin, DropSpawnAngleMax), FVector::UpVector);
	FVector SpawnLocation =
		OwningPawn->GetActorLocation() + RotateForward *
		FMath::FRandRange(DropSpawnDistanceMin, DropSpawnDistanceMax);
	SpawnLocation.Z -= RelativeSpawnElevation;
	const FRotator SpawnRotation = FRotator::ZeroRotator;

	FItemManifest& ItemManifest = Item->GetItemManifestMutable();
	
	// Stackable 아이템인 경우에만 수량을 임시로 설정
	// Non-stackable 아이템(장비)은 수량 변경 없이 스폰
	if (FStackableFragment* StackableFragment = ItemManifest.GetFragmentOfTypeMutable<FStackableFragment>())
	{
		StackableFragment->SetQuantity(Quantity);
	}
	
	ItemManifest.SpawnPickupActor(this, SpawnLocation, SpawnRotation);
}
