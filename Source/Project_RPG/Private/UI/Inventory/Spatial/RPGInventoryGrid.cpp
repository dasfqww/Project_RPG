// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Inventory/Spatial/RPGInventoryGrid.h"
#include "UI/GridSlot/RPGGridSlot.h"
#include "Component/RPGInventoryComponent.h"
#include "Item/RPGItemBase.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "RPGFunctionLibrary.h"
#include "Item/Manifest/RPGItemManifest.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "RPGGameplayTags.h"
#include "UI/RPGInventoryItemSlot.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"
#include "UI/PopUp/RPGItemPopUp.h"
#include "UI/PopUp/RPGItemSplitPopUp.h"

#include "RPGDebugHelper.h"

void URPGInventoryGrid::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	/*if (count >= 1) return;

	count++;*/

	ConstructGrid();
	UE_LOG(LogTemp, Warning, TEXT("NativeOnInitialized called on: %s"), *GetName());
	InventoryComponent = URPGFunctionLibrary::GetComponentFromPlayerController<URPGInventoryComponent>(GetOwningPlayer());
	//��������Ʈ �ߺ� ���..
	InventoryComponent->OnItemAdded.AddDynamic(this, &ThisClass::AddItem);
	InventoryComponent->OnQuantityChanged.AddDynamic(this, &ThisClass::AddQuantity);

	// [Fix] 이미 인벤토리에 있는 아이템들을 UI에 표시 (로드 후 UI가 열릴 경우 대비)
	TArray<URPGItemBase*> ExistingItems = InventoryComponent->GetAllItems();
	for (URPGItemBase* Item : ExistingItems)
	{
		if (IsValid(Item))
		{
			AddItem(Item);
		}
	}
}

void URPGInventoryGrid::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!HasHoverItem()) return;

	const FVector2D CanvasPos = URPGFunctionLibrary::GetWidgetPosition(CanvasPanel);
	const FVector2D MousePos = UWidgetLayoutLibrary	::GetMousePositionOnViewport(GetOwningPlayer());

	if (CursorExitedCanvas(CanvasPos, URPGFunctionLibrary::GetWidgetSize(CanvasPanel), MousePos))
	{
		return;
	}

	UpdateHoveredSlot(CanvasPos, MousePos);
}

//FReply URPGInventoryGrid::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, 
//	const FPointerEvent& InMouseEvent)
//{
//	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
//	{
//		Debug::Print("doubleclick");
//
//		return FReply::Handled();
//	}
//
//	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
//}

void URPGInventoryGrid::ConstructGrid()
{
	GridSlots.Reserve(Rows*Columns);

	for (int32 j = 0; j < Rows; j++)
	{
		for (int i = 0; i < Columns; i++)
		{
			URPGGridSlot* GridSlot = CreateWidget<URPGGridSlot>(this, GridSlotClass);
			CanvasPanel->AddChild(GridSlot);

			const FIntPoint TilePosition(i, j);
			GridSlot->SetTileIndex(URPGFunctionLibrary::GetIndexFromWidgetPosition(TilePosition, Columns));

			UCanvasPanelSlot* GridCPS = UWidgetLayoutLibrary::SlotAsCanvasSlot(GridSlot);
			GridCPS->SetSize(FVector2D(TileSize));
			GridCPS->SetPosition(TilePosition * TileSize);
			
			GridSlots.Add(GridSlot);

			GridSlot->OnGridSlotChanged.AddDynamic(this, &ThisClass::OnGridSlotChanged);
		}
	}
}

FSlotAvailabilityResult URPGInventoryGrid::HasSpaceForItem(const ARPGPickUpBase* ItemPickup)
{
	return HasSpaceForItem(ItemPickup->GetItemManifest());
}

FSlotAvailabilityResult URPGInventoryGrid::HasSpaceForItem(const URPGItemBase* Item)
{
	return HasSpaceForItem(Item->GetItemManifest());
}

FSlotAvailabilityResult URPGInventoryGrid::HasSpaceForItem(const FItemManifest& Manifest)
{
	FSlotAvailabilityResult Result;
	/*Result.TotalSpaceToFill = 7;
	Result.bStackable = true;
	
	FSlotAvailability SlotAvaility;
	SlotAvaility.AmountToFill = 2;
	SlotAvaility.Index = 0;
	Result.SlotAvailabilities.Add(MoveTemp(SlotAvaility));
	
	FSlotAvailability SlotAvaility2;
	SlotAvaility2.AmountToFill = 5;
	SlotAvaility2.Index = 1;
	Result.SlotAvailabilities.Add(MoveTemp(SlotAvaility2));*/

	/*const FStackableFragment* StackableFragment = Manifest.GetFragmentOfType<FStackableFragment>();
	Result.bStackable = StackableFragment != nullptr;

	const int32 MaxQuantity = StackableFragment ? StackableFragment->GetMaxQuantity() : 1;
	int32 AmountToFill = StackableFragment ? StackableFragment->GetQuantity() : 1;

	TSet<int32> CheckIndices;

	for(const auto& GridSlot :GridSlots)
	{ 
		if (AmountToFill == 0) break;

		if (IsIndexClaimed(CheckIndices, GridSlot->GetTileIndex())) continue;

		if (!IsInGridBounds(GridSlot->GetTileIndex(), GetItemDimensions(Manifest))) continue;

		const FGridFragment* GridFragment = Manifest.GetFragmentOfType<FGridFragment>();
		const FIntPoint Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);

		TSet<int32> TentativelyClaimed;
		if (!HasSpaceAtIndex(GridSlot, GetItemDimensions(Manifest), CheckIndices, 
			TentativelyClaimed, Manifest.GetItemTag(), MaxQuantity))
		{
			continue;
		}

		const int32 AmountToFillInSlot = DetermineFillAmountForSlot(Result.bStackable, MaxQuantity, 
			AmountToFill, GridSlot);
		if (AmountToFillInSlot == 0) continue;

		CheckIndices.Append(TentativelyClaimed);

		Result.TotalSpaceToFill += AmountToFillInSlot;
		Result.SlotAvailabilities.Emplace(
			FSlotAvailability{
				HasValidItem(GridSlot) ? GridSlot->GetUpperLeftIndex() : GridSlot->GetTileIndex(),
				Result.bStackable?AmountToFillInSlot:0,
				HasValidItem(GridSlot)
			}
		);

		AmountToFill -= AmountToFillInSlot;
		Result.Remainder = AmountToFill;

		if (AmountToFill == 0) return Result;
	}

	return Result;*/
	const FStackableFragment* StackableFragment = Manifest.GetFragmentOfType<FStackableFragment>();
	const bool bIsStackable = StackableFragment != nullptr;
	Result.bStackable = bIsStackable;
	int32 AmountToFill = bIsStackable ? StackableFragment->GetQuantity() : 1;
	Result.Remainder = AmountToFill;

	if (bIsStackable)
	{
		const int32 MaxQuantity = StackableFragment->GetMaxQuantity();

		// 1�ܰ�: ��ø ������ �������� ���, ���� ������ ���� ã���ϴ�.
		for (URPGGridSlot* GridSlot : GridSlots)
		{
			if (AmountToFill == 0) break;
			if (!GridSlot->GetInvenItem().IsValid() || !GridSlot->GetInvenItem()->IsStackable()) continue;

			// ������ �±װ� ����, ������ �� ���� �ʾҴ��� Ȯ��
			if (GridSlot->GetInvenItem()->GetItemManifest().GetItemTag() == Manifest.GetItemTag() && GridSlot->GetQuantity() < MaxQuantity)
			{
				const int32 SpaceInSlot = MaxQuantity - GridSlot->GetQuantity();
				const int32 AmountToAdd = FMath::Min(AmountToFill, SpaceInSlot);

				Result.SlotAvailabilities.Emplace(FSlotAvailability{ GridSlot->GetTileIndex(), AmountToAdd, true });
				AmountToFill -= AmountToAdd;
				Result.TotalSpaceToFill += AmountToAdd;
			}
		}
	}

	Result.Remainder = AmountToFill;
	if (AmountToFill == 0)
	{
		return Result; // ���� ���ÿ� ��� ä������ ����
	}

	// 2�ܰ�: ���� �����̳�, ��ø �Ұ����� �������� ���� �� ������ ã���ϴ�.
	const int32 AmountPerSlot = bIsStackable ? StackableFragment->GetMaxQuantity() : 1;
	for (URPGGridSlot* GridSlot : GridSlots)
	{
		if (AmountToFill == 0) break;

		if (!GridSlot->GetInvenItem().IsValid()) // ������ ����ִ°�?
		{
			const int32 AmountToAdd = FMath::Min(AmountToFill, AmountPerSlot);
			Result.SlotAvailabilities.Emplace(FSlotAvailability{ GridSlot->GetTileIndex(), AmountToAdd, false });
			AmountToFill -= AmountToAdd;
			Result.TotalSpaceToFill += AmountToAdd;
		}
	}

	Result.Remainder = AmountToFill;
	return Result;
}

void URPGInventoryGrid::AddItemToIndices(const FSlotAvailabilityResult& Result, URPGItemBase* NewItem)
{
	for (const auto& Availability : Result.SlotAvailabilities)
	{
		/*AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);
		UpdateGridSlots(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);*/

		// �̹� �������� �ִ� ����(���� ����)�� ������ ���ϴ� ���
		if (Availability.bItemAtIndex)
		{
			URPGGridSlot* TargetSlot = GridSlots[Availability.Index];
			URPGInventoryItemSlot* ItemSlotWidget = ItemsInSlot.FindChecked(Availability.Index);
			const int32 NewQuantity = TargetSlot->GetQuantity() + Availability.AmountToFill;
			TargetSlot->SetQuantity(NewQuantity);
			ItemSlotWidget->UpdateItemQuantity(NewQuantity);
		}
		// �� ���Կ� ���� �߰��ϴ� ���
		else
		{
			AddItemAtIndex(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);
			UpdateGridSlots(NewItem, Availability.Index, Result.bStackable, Availability.AmountToFill);
		}
	}
}

void URPGInventoryGrid::SetVisibleCursor()
{
	if (!IsValid(GetOwningPlayer())) return;

	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, GetCursorWidget());
}

void URPGInventoryGrid::SetOwningCanvas(UCanvasPanel* OwningCanvas)
{
	OwningCanvasPanel = OwningCanvas;
}

void URPGInventoryGrid::AddItem(URPGItemBase* Item)
{
	if (!MatchesCategory(Item)) return;

	//Debug::Print("InventoryGrid::AddItem");

	// [New] Check if the item has a specific assigned slot
	if (GridSlots.IsValidIndex(Item->SlotIndex))
	{
		// Try to place it at SlotIndex directly if possible
		// We only assume it's safe if the slot is currently empty (or if we trust the logic 100%)
		// But for robustness, check if slot is empty. 
		// If restoring from DB, slots should be empty.
		if (!GridSlots[Item->SlotIndex]->GetInvenItem().IsValid())
		{
			FSlotAvailabilityResult Result;
			Result.bStackable = Item->IsStackable();
			Result.TotalSpaceToFill = Item->GetTotalQuantity();
			Result.Remainder = 0;
			Result.SlotAvailabilities.Add(FSlotAvailability{ Item->SlotIndex, Item->GetTotalQuantity(), false });

			AddItemToIndices(Result, Item);
			return; 
		}
	}

	FSlotAvailabilityResult Result = HasSpaceForItem(Item);
	AddItemToIndices(Result, Item);
}

void URPGInventoryGrid::OnGridSlotChanged(int32 GridIndex, const FPointerEvent& MouseEvent, EGridSlotState SlotState)
{
	if (!IsValid(HoverItem)) return;

	if (SlotState==EGridSlotState::Selected)
	{
		if (!GridSlots.IsValidIndex(ItemDropIndex)) return;

		if (CurrentQueryResult.ValidItem.IsValid()
			&&GridSlots.IsValidIndex(CurrentQueryResult.UpperLeftIndex))
		{
			OnSlotItemClicked(CurrentQueryResult.UpperLeftIndex, MouseEvent);
			return;
		}

		auto GridSlot = GridSlots[ItemDropIndex];
		if (!GridSlot->GetInvenItem().IsValid())
		{
			Debug::Print("try put item");
			PutDownOnIndex(ItemDropIndex);
		}
	}

	else
	{
		URPGGridSlot* GridSlot = GridSlots[GridIndex];
		if (GridSlot->IsAvailiable())
		{
			GridSlot->SetSlotTexture(SlotState);
		}
	}
}

void URPGInventoryGrid::OnPopUpMenuDrop(int32 Index)
{
	URPGItemBase* ClickedItem = GridSlots[Index]->GetInvenItem().Get();
	if (!IsValid(ClickedItem)) return;

	const int32 QuantityToDrop = GridSlots[Index]->GetQuantity();

	// 서버에 드롭 요청 (인벤토리에서 아이템 삭제)
	InventoryComponent->Server_DropItem(ClickedItem, QuantityToDrop);
	
	// UI 업데이트 (인벤토리 그리드에서 제거)
	RemoveItemFromGrid(ClickedItem, Index);
}

void URPGInventoryGrid::OnPopUpMenuConsume(int32 Index)
{
	URPGItemBase* ClickedItem = GridSlots[Index]->GetInvenItem().Get();
	if (!IsValid(ClickedItem)) return;

	/*const int32 UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex();
	URPGGridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];
	const int32 NewQuantity = UpperLeftGridSlot->GetQuantity() - 1;

	UpperLeftGridSlot->SetQuantity(NewQuantity);
	ItemsInSlot.FindChecked(UpperLeftIndex)->UpdateItemQuantity(NewQuantity);*/

	// 1x1 �̹Ƿ� UpperLeftIndex�� �ڱ� �ڽ��� �ε����� �����ϴ�.
	URPGGridSlot* GridSlot = GridSlots[Index];
	const int32 NewQuantity = GridSlot->GetQuantity() - 1;

	GridSlot->SetQuantity(NewQuantity);
	ItemsInSlot.FindChecked(Index)->UpdateItemQuantity(NewQuantity);

	// TODO: Tell the server we're consuming an item
	InventoryComponent->Server_ConsumeItem(ClickedItem);

	if (NewQuantity <= 0)
	{
		RemoveItemFromGrid(ClickedItem, Index);
	}
}

bool URPGInventoryGrid::MatchesCategory(const URPGItemBase* Item) const
{
	return Item->GetItemManifest().GetItemCategory() == ItemCategory;
}

//FVector2D URPGInventoryGrid::GetDrawSize(const FGridFragment* GridFragment) const
//{
//	const float IconTileWidth = TileSize - GridFragment->GetGridPadding() * 2;
//	
//	return GridFragment->GetGridSize()* IconTileWidth;
//}

FVector2D URPGInventoryGrid::GetDrawSize() const
{
	// �е��� ������ ���� �ֽ��ϴ�.
	//constexpr float Padding = 2.f;
	return FVector2D(TileSize);
}

void URPGInventoryGrid::SetItemSlotImage(const URPGInventoryItemSlot* ItemSlot, 
	const FImageFragment* ImageFragment) const
{
	if (!ItemSlot || !ImageFragment) return;

	FSlateBrush Brush;
	Brush.SetResourceObject(ImageFragment->GetIcon());
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.ImageSize = GetDrawSize(); // �̹� �ܼ�ȭ�� GetDrawSize() ȣ��

	// URPGInventoryItemSlot Ŭ������ SetImageBrush �Լ��� �ִٰ� �����մϴ�.
	ItemSlot->SetImageBrush(Brush);
}

//void URPGInventoryGrid::SetItemSlotImage(const URPGInventoryItemSlot* ItemSlot,
//	const FGridFragment* GridFragment, const FImageFragment* ImageFragment) const
//{
//	FSlateBrush Brush;
//	Brush.SetResourceObject(ImageFragment->GetIcon());
//	Brush.DrawAs = ESlateBrushDrawType::Image;
//	//Brush.ImageSize = GetDrawSize(GridFragment);
//	Brush.ImageSize = GetDrawSize();
//	ItemSlot->SetImageBrush(Brush);
//}

void URPGInventoryGrid::AddItemAtIndex(URPGItemBase* Item, const int32 Index,
	const bool bStackable, const int32 StackAmount)
{
	/*const FGridFragment* GridFragment =
		GetFragment<FGridFragment>(Item, RPGGameplayTags::Fragment_GridFragment);*/
	const FImageFragment* ImageFragment =
		GetFragment<FImageFragment>(Item, RPGGameplayTags::Fragment_IconFragment);
	//if (!GridFragment || !ImageFragment)return;
	if (!ImageFragment)return;

	//URPGInventoryItemSlot* ItemSlot = CreateSlotItem(Item, bStackable, StackAmount, GridFragment, ImageFragment, Index);
	URPGInventoryItemSlot* ItemSlot =
		CreateSlotItem(Item, bStackable, StackAmount, ImageFragment, Index);

	if (IsValid(ItemSlot))
	{
		UE_LOG(LogTemp, Log, TEXT("ItemSlot Widget sucess - Index: %d, Item: %s"),
			Index, *Item->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ItemSlot fail - Index: %d, Item: %s"),
			Index, *Item->GetName());
	}

	AddItemSlotToCanvas(Index, ItemSlot);

	ItemsInSlot.Add(Index, ItemSlot);
}

//URPGInventoryItemSlot* URPGInventoryGrid::CreateSlotItem(URPGItemBase* Item, const bool bStackable, 
//	const int32 StackAmount,const FGridFragment* GridFragment,
//	const FImageFragment* ImageFragment, const int32 Index)
//{
//	URPGInventoryItemSlot* SlotItem = CreateWidget<URPGInventoryItemSlot>(GetOwningPlayer(), ItemSlotClass);
//	SlotItem->SetItemReference(Item);
//	SetItemSlotImage(SlotItem, GridFragment, ImageFragment);
//	SlotItem->SetGridIndex(Index);
//	SlotItem->SetIsStackable(bStackable);
//
//	const int32 ItemQuantity = bStackable ? StackAmount : 0;
//	SlotItem->UpdateItemQuantity(ItemQuantity);
//	SlotItem->OnSlotItemClicked.AddDynamic(this, &ThisClass::OnSlotItemClicked);
//
//	return SlotItem;
//}

URPGInventoryItemSlot* URPGInventoryGrid::CreateSlotItem(URPGItemBase* Item, const bool bStackable,
	const int32 StackAmount, const FImageFragment* ImageFragment, const int32 Index)
{
	URPGInventoryItemSlot* SlotItem = 
		CreateWidget<URPGInventoryItemSlot>(GetOwningPlayer(), ItemSlotClass);
	if (!SlotItem) return nullptr;

	SlotItem->SetItemReference(Item);
	SetItemSlotImage(SlotItem, ImageFragment); // GridFragment �Ķ���� ����
	SlotItem->SetGridIndex(Index);
	SlotItem->SetIsStackable(bStackable);

	const int32 ItemQuantity = bStackable ? StackAmount : 0;
	SlotItem->UpdateItemQuantity(ItemQuantity);
	SlotItem->OnSlotItemClicked.AddDynamic(this, &ThisClass::OnSlotItemClicked);

	return SlotItem;

}

//void URPGInventoryGrid::AddItemSlotToCanvas(const int32 Index, const FGridFragment* GridFragment,
//	URPGInventoryItemSlot* ItemSlot) const
//{
//	CanvasPanel->AddChild(ItemSlot);
//	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemSlot);
//	CanvasSlot->SetSize(GetDrawSize());
//	const FVector2D DrawPos = URPGFunctionLibrary::GetPositionFromWidgetIndex(Index, Columns)* TileSize;
//	constexpr float Padding = 2.f;
//	const FVector2D DrawPosWithPadding = DrawPos + FVector2D(Padding);
//	CanvasSlot->SetPosition(DrawPosWithPadding);
//
//	UE_LOG(LogTemp, Log, TEXT("ItemSlot SetPosition - Index: %d, Pos: X=%.1f Y=%.1f"),
//		Index, DrawPosWithPadding.X, DrawPosWithPadding.Y);
//}

void URPGInventoryGrid::AddItemSlotToCanvas(const int32 Index, URPGInventoryItemSlot* ItemSlot) const
{
	CanvasPanel->AddChild(ItemSlot);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemSlot);
	CanvasSlot->SetSize(GetDrawSize()); // GridFragment �Ķ���� ����
	const FVector2D DrawPos = URPGFunctionLibrary::GetPositionFromWidgetIndex(Index, Columns) * TileSize;
	//constexpr float Padding = 2.f;
	const FVector2D DrawPosWithPadding = DrawPos;
	CanvasSlot->SetPosition(DrawPosWithPadding);
}

void URPGInventoryGrid::UpdateGridSlots(URPGItemBase* NewItem, const int32 Index,
	bool bStackableItem, const int32 Quantity)
{
	check(GridSlots.IsValidIndex(Index));

	URPGGridSlot* GridSlot = GridSlots[Index];
	GridSlot->SetInvenItem(NewItem);
	// GridSlot->SetUpperLeftIndex(Index); // UpperLeftIndex ���� ����
	GridSlot->SetSlotTexture(EGridSlotState::Occupied);
	GridSlot->SetAvailable(false);

	if (bStackableItem)
	{
		GridSlots[Index]->SetQuantity(Quantity);
	}

	else
	{
		GridSlots[Index]->SetQuantity(1);
	}
	/*const FGridFragment* GridFragment 
		= GetFragment<FGridFragment>(NewItem, RPGGameplayTags::Fragment_GridFragment);
	const FIntPoint Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);

	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots, Index, Dimensions, Columns, 
		[&](URPGGridSlot* GridSlot)
	{
		GridSlot->SetInvenItem(NewItem);
		GridSlot->SetUpperLeftIndex(Index);
		GridSlot->SetSlotTexture(EGridSlotState::Occupied);
		GridSlot->SetAvailable(false);
	});*/
}

//bool URPGInventoryGrid::IsIndexClaimed(const TSet<int32>& CheckedIndices, const int32 Index) const
//{
//	return CheckedIndices.Contains(Index);
//}
//
//bool URPGInventoryGrid::HasSpaceAtIndex(const URPGGridSlot* GridSlot, const FIntPoint& Dimension,
//	const TSet<int32>& CheckedIndices, TSet<int32>& OutTentativelyClaimed, 
//	const FGameplayTag& ItemTag, const int32 MaxQuantity)
//{
//	bool bHasSpaceAtIndex = true;
//
//	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots, GridSlot->GetTileIndex(), Dimension, Columns, 
//		[&](const URPGGridSlot* SubGridSlot)
//	{
//			if (CheckSlotConstraints(GridSlot, SubGridSlot,CheckedIndices, 
//				OutTentativelyClaimed, ItemTag, MaxQuantity))
//			{
//				OutTentativelyClaimed.Add(SubGridSlot->GetTileIndex());
//			}
//			else
//			{
//				bHasSpaceAtIndex = false;
//			}
//	});
//
//	return bHasSpaceAtIndex;
//}
//
//FIntPoint URPGInventoryGrid::GetItemDimensions(const FItemManifest& Manifest) const
//{
//	const FGridFragment* GridFragment = Manifest.GetFragmentOfType<FGridFragment>(); 
//	return GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);
//}
//
//bool URPGInventoryGrid::CheckSlotConstraints(const URPGGridSlot* GridSlot, const URPGGridSlot* SubGridSlot, 
//	const TSet<int32>& CheckedIndices, TSet<int32>& OutTentativelyClaimed, 
//	const FGameplayTag& ItemTag, const int32 MaxQuantity) const
//{
//	if(IsIndexClaimed(CheckedIndices, SubGridSlot->GetTileIndex()))
//		return false;
//
//	if (!HasValidItem(SubGridSlot))
//	{
//		OutTentativelyClaimed.Add(SubGridSlot->GetTileIndex());
//		return true;
//	}
//
//	if (!IsUpperLeftSlot(GridSlot, SubGridSlot)) return false;
//
//	const URPGItemBase* SubItem = SubGridSlot->GetInvenItem().Get();
//	if (!SubItem->IsStackable()) return false;
//
//	if (!DoesItemTagMatch(SubItem, ItemTag)) return false;
//
//	if (GridSlot->GetQuantity() >= MaxQuantity) return false;
//
//	return true;
//}

bool URPGInventoryGrid::HasValidItem(const URPGGridSlot* GridSlot) const
{
	return GridSlot->GetInvenItem().IsValid();
}

//bool URPGInventoryGrid::IsUpperLeftSlot(const URPGGridSlot* GridSlot, const URPGGridSlot* SubGridSlot) const
//{
//	return SubGridSlot->GetUpperLeftIndex() == GridSlot->GetTileIndex();
//}

bool URPGInventoryGrid::DoesItemTagMatch(const URPGItemBase* SubItem, const FGameplayTag& ItemTag) const
{
	return SubItem->GetItemManifest().GetItemTag().MatchesTagExact(ItemTag);
}

//bool URPGInventoryGrid::IsInGridBounds(const int32 StartIndex, const FIntPoint& ItemDimension)
//{
//	if (StartIndex < 0 || StartIndex >= GridSlots.Num()) return false;
//	
//	const int32 EndColumn = (StartIndex % Columns) + ItemDimension.X;
//	const int32 EndRow = (StartIndex / Columns) + ItemDimension.Y;
//	return EndColumn <= Columns && EndRow <= Rows;
//}
//
//int32 URPGInventoryGrid::DetermineFillAmountForSlot(const bool bStackable, const int32 MaxQuantity, 
//	const int32 AmountToFill, const URPGGridSlot* GridSlot) const
//{
//	const int32 SpaceInSlot = MaxQuantity - GetQuantity(GridSlot);
//
//	return bStackable ? FMath::Min(AmountToFill, SpaceInSlot) : 1;
//}

//int32 URPGInventoryGrid::GetQuantity(const URPGGridSlot* GridSlot) const
//{
//	int32 CurrentSlotQuantity = GridSlot->GetQuantity();
//
//	//const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex();
//	if (const int32 UpperLeftIndex = GridSlot->GetUpperLeftIndex(); UpperLeftIndex != INDEX_NONE)
//	{
//		URPGGridSlot* UpperLeftGridSlot = GridSlots[UpperLeftIndex];
//		CurrentSlotQuantity = UpperLeftGridSlot->GetQuantity();
//	}
//
//	return CurrentSlotQuantity;
//}


bool URPGInventoryGrid::IsCtrlRightClick(const FPointerEvent& MouseEvent) const
{
	return MouseEvent.GetEffectingButton() == EKeys::RightMouseButton &&
		MouseEvent.IsControlDown();
}

bool URPGInventoryGrid::IsLeftClick(const FPointerEvent& MouseEvent) const
{
	return MouseEvent.GetEffectingButton()==EKeys::LeftMouseButton;
}

bool URPGInventoryGrid::IsRightClick(const FPointerEvent& MouseEvent) const
{
	return MouseEvent.GetEffectingButton() == EKeys::RightMouseButton;
}

void URPGInventoryGrid::PickUp(URPGItemBase* ClickedInvenItem, const int32 GridIndex)
{
	AssignHoverItem(ClickedInvenItem, GridIndex, GridIndex);

	//GridSlots[GridIndex]->SetSlotTexture(EGridSlotState::GrayedOut);
	//RemoveItemFromGrid(ClickedInvenItem, GridIndex);
}

void URPGInventoryGrid::AssignAndSetHoverItem(URPGItemBase* Item, 
	const int32 GridIndex, const int32 PrevGridIndex, URPGItemPopUp* InItemPopUp)
{
	AssignHoverItem(Item, GridIndex, PrevGridIndex);
	InItemPopUp->SetHoverItem(HoverItem);
}

void URPGInventoryGrid::AssignHoverItem(URPGItemBase* Item, const int32 GridIndex, const int32 PrevGridIndex)
{
	if (!IsValid(HoverItem))
	{
		HoverItem = CreateWidget<URPGHoverItem>(GetOwningPlayer(), HoverItemClass);
	}

	/*const FGridFragment* GridFragment =
		GetFragment<FGridFragment>(Item, RPGGameplayTags::Fragment_GridFragment);*/
	const FImageFragment* ImageFragment =
		GetFragment<FImageFragment>(Item, RPGGameplayTags::Fragment_IconFragment);
	//if (!GridFragment || !ImageFragment) return;
	if (!ImageFragment) return;

	const FVector2D DrawSize = GetDrawSize();

	FSlateBrush IconBrush;
	IconBrush.SetResourceObject(ImageFragment->GetIcon());
	IconBrush.DrawAs = ESlateBrushDrawType::Image;
	IconBrush.ImageSize = DrawSize * UWidgetLayoutLibrary::GetViewportScale(this);

	HoverItem->SetImageBrush(IconBrush);
	//HoverItem->SetGridDimensions(GridFragment->GetGridSize());
	HoverItem->SetInvenItem(Item);
	HoverItem->SetIsStackable(Item->IsStackable());
	HoverItem->SetPrevGridIndex(PrevGridIndex);

	if (HoverItem->IsStackable())
	{
		HoverItem->UpdateQuantity(GridSlots[GridIndex]->GetQuantity());
	}

	GetOwningPlayer()->SetMouseCursorWidget(EMouseCursor::Default, HoverItem);
}

void URPGInventoryGrid::RemoveItemFromGrid(URPGItemBase* Item, const int32 GridIndex)
{
	/*const FGridFragment* GridFragment =
		GetFragment<FGridFragment>(Item, RPGGameplayTags::Fragment_GridFragment);
	if (!GridFragment)return;*/

	if (!GridSlots.IsValidIndex(GridIndex)) return;

	URPGGridSlot* GridSlot = GridSlots[GridIndex];
	GridSlot->SetInvenItem(nullptr);
	//GridSlot->SetUpperLeftIndex(INDEX_NONE);
	GridSlot->SetSlotTexture(EGridSlotState::Unoccupied);
	GridSlot->SetAvailable(true);
	GridSlot->SetQuantity(0);

	/*URPGFunctionLibrary::ForeachGridSlot2D(GridSlots, GridIndex, GridFragment->GetGridSize(),
		Columns, [&](URPGGridSlot* GridSlot)
	{
			GridSlot->SetInvenItem(nullptr);
			GridSlot->SetUpperLeftIndex(INDEX_NONE);
			GridSlot->SetSlotTexture(EGridSlotState::Unoccupied);
			GridSlot->SetAvailable(true);
			GridSlot->SetQuantity(0);
	});*/

	if (ItemsInSlot.Contains(GridIndex))
	{
		TObjectPtr<URPGInventoryItemSlot> FoundSlotItem;
		ItemsInSlot.RemoveAndCopyValue(GridIndex, FoundSlotItem);
		FoundSlotItem->RemoveFromParent();
	}
}

//void URPGInventoryGrid::UpdateTileParameters(const FVector2D& CanvasPos, const FVector2D& MousePos)
//{
//	if (!bMouseWithinCanvas) return;
//
//	const FIntPoint HoveredTileCoords = CalculateHoveredCoordinates(CanvasPos, MousePos);
//
//	LastTileParams= TileParams;
//	TileParams.TileCoords = HoveredTileCoords;
//	TileParams.TileIndex = URPGFunctionLibrary::GetIndexFromWidgetPosition(HoveredTileCoords, Columns);
//
//	TileParams.TileQuadrant = CalculateTileQuadrant(CanvasPos, MousePos);
//
//	OnTileParametersUpdated(TileParams);
//}

//FIntPoint URPGInventoryGrid::CalculateHoveredCoordinates(const FVector2D& CanvasPos, const FVector2D& MousePos) const
//{
//	return FIntPoint{
//		static_cast<int32>(FMath::FloorToInt((MousePos.X - CanvasPos.X) / TileSize)),
//		static_cast<int32>(FMath::FloorToInt((MousePos.Y - CanvasPos.Y) / TileSize))
//	};
//}
//
//ETileQuadrant URPGInventoryGrid::CalculateTileQuadrant(const FVector2D& CanvasPos, const FVector2D& MousePos) const
//{
//	const float TileLocalX = FMath::Fmod(MousePos.X - CanvasPos.X, TileSize);
//	const float TileLocalY = FMath::Fmod(MousePos.Y - CanvasPos.Y, TileSize);
//
//	const bool bIsTop = TileLocalY < TileSize / 2.f; 
//	const bool bIsLeft = TileLocalX < TileSize / 2.f;
//
//	ETileQuadrant HoveredTileQuadrant = ETileQuadrant::None;
//	if (bIsTop && bIsLeft) HoveredTileQuadrant = ETileQuadrant::TopLeft;
//	else if (bIsTop && !bIsLeft) HoveredTileQuadrant = ETileQuadrant::TopRight;
//	else if (!bIsTop && bIsLeft) HoveredTileQuadrant = ETileQuadrant::BottomLeft;
//	else if (!bIsTop && !bIsLeft) HoveredTileQuadrant = ETileQuadrant::BottomRight;
//
//	return HoveredTileQuadrant;
//}
//
//void URPGInventoryGrid::OnTileParametersUpdated(const FTileParameters& Params)
//{
//	if (!IsValid(HoverItem)) return;
//
//	const FIntPoint Dimensions = HoverItem->GetGridDimensions();
//
//	const FIntPoint StartingCoord = 
//		CalculateStartingCoordinate(Params.TileCoords, Dimensions, Params.TileQuadrant);
//	ItemDropIndex = URPGFunctionLibrary::GetIndexFromWidgetPosition(StartingCoord, Columns);
//
//	CurrentQueryResult = CheckHoverPosition(StartingCoord, Dimensions);
//
//	if (CurrentQueryResult.bHasSpace)
//	{
//		HighlightSlots(ItemDropIndex, Dimensions);
//		return;
//	}
//	UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
//
//	if (CurrentQueryResult.ValidItem.IsValid() 
//		&& GridSlots.IsValidIndex(CurrentQueryResult.UpperLeftIndex))
//	{
//		// TODO: There's a single item in this space. We can swap or add stacks.
//		const FGridFragment* GridFragment = 
//			GetFragment<FGridFragment>(CurrentQueryResult.ValidItem.Get(),
//				RPGGameplayTags::Fragment_GridFragment);
//		if (!GridFragment) return;
//
//		ChangeHoverType(CurrentQueryResult.UpperLeftIndex,
//			GridFragment->GetGridSize(), EGridSlotState::GrayedOut);
//	}
//}
//
//FIntPoint URPGInventoryGrid::CalculateStartingCoordinate(const FIntPoint& Coord, 
//	const FIntPoint& Dimensions, const ETileQuadrant Quadrant) const
//{
//	const int32 HasEvenWidth = Dimensions.X % 2 == 0 ? 1 : 0;
//	const int32 HasEvenHeight = Dimensions.Y % 2 == 0 ? 1 : 0;
//
//	FIntPoint StartingCoord;
//	switch (Quadrant)
//	{
//	case ETileQuadrant::TopLeft:
//		StartingCoord.X = Coord.X - FMath::FloorToInt(0.5f * Dimensions.X);
//		StartingCoord.Y = Coord.Y - FMath::FloorToInt(0.5f * Dimensions.Y);
//		break;
//	case ETileQuadrant::TopRight:
//		StartingCoord.X = Coord.X - FMath::FloorToInt(0.5f * Dimensions.X)+HasEvenWidth;
//		StartingCoord.Y = Coord.Y - FMath::FloorToInt(0.5f * Dimensions.Y);
//		break;
//	case ETileQuadrant::BottomLeft:
//		StartingCoord.X = Coord.X - FMath::FloorToInt(0.5f * Dimensions.X);
//		StartingCoord.Y = Coord.Y - FMath::FloorToInt(0.5f * Dimensions.Y)+HasEvenHeight;
//		break;
//	case ETileQuadrant::BottomRight:
//		StartingCoord.X = Coord.X - FMath::FloorToInt(0.5f * Dimensions.X) + HasEvenWidth;
//		StartingCoord.Y = Coord.Y - FMath::FloorToInt(0.5f * Dimensions.Y) + HasEvenHeight;
//		break;
//
//	default:
//		break;
//	}
//	return StartingCoord;
//}
//
//FSpaceQueryResult URPGInventoryGrid::CheckHoverPosition(const FIntPoint& Pos, 
//	const FIntPoint& Dimensions)
//{
//	FSpaceQueryResult Result;
//
//	if (!IsInGridBounds(URPGFunctionLibrary::GetIndexFromWidgetPosition(Pos, Columns), Dimensions))
//		return Result;
//
//	Result.bHasSpace = true;
//
//	TSet<int32> OccupiedUpperLeftIndices;
//	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots,
//		URPGFunctionLibrary::GetIndexFromWidgetPosition(Pos, Columns),Dimensions, Columns,
//	[&](const URPGGridSlot* GridSlot)
//	{
//		if (GridSlot->GetInvenItem().IsValid())
//		{
//			OccupiedUpperLeftIndices.Add(GridSlot->GetUpperLeftIndex());
//			Result.bHasSpace = false;
//		}
//	});
//
//	if (OccupiedUpperLeftIndices.Num()==1)
//	{
//		const int32 Index = *OccupiedUpperLeftIndices.CreateConstIterator();
//		Result.ValidItem = GridSlots[Index]->GetInvenItem();
//		Result.UpperLeftIndex = GridSlots[Index]->GetUpperLeftIndex();
//	}
//
//	return Result;
//}

void URPGInventoryGrid::UpdateHoveredSlot(const FVector2D& CanvasPos, const FVector2D& MousePos)
{
	const FIntPoint HoveredCoords = FIntPoint(
		FMath::FloorToInt((MousePos.X - CanvasPos.X) / TileSize),
		FMath::FloorToInt((MousePos.Y - CanvasPos.Y) / TileSize)
	);

	const int32 NewHoveredIndex = 
		URPGFunctionLibrary::GetIndexFromWidgetPosition(HoveredCoords, Columns);

	if (LastHoveredIndex != NewHoveredIndex)
	{
		// ������ ���̶���Ʈ�� ������ ������� �ǵ����ϴ�.
		if (GridSlots.IsValidIndex(LastHoveredIndex))
		{
			GridSlots[LastHoveredIndex]->SetSlotTexture(EGridSlotState::Unoccupied);
		}

		// ���� ���̶���Ʈ�� ������ ǥ���մϴ�.
		if (GridSlots.IsValidIndex(NewHoveredIndex))
		{
			GridSlots[NewHoveredIndex]->SetSlotTexture(EGridSlotState::Selected);
			ItemDropIndex = NewHoveredIndex; // �������� �������� ��ġ ����
		}

		LastHoveredIndex = NewHoveredIndex;
	}
}

bool URPGInventoryGrid::CursorExitedCanvas(const FVector2D& BoundaryPos,
	const FVector2D& BoundarySize, const FVector2D& Location)
{
	bLastMouseWithinCanvas = bMouseWithinCanvas;
	bMouseWithinCanvas = URPGFunctionLibrary::IsWithinBounds(BoundaryPos, BoundarySize, Location);
	if (!bMouseWithinCanvas && bLastMouseWithinCanvas)
	{
		
		return true;
	}

	return false;
}

//void URPGInventoryGrid::HighlightSlots(const int32 Index, const FIntPoint& Dimensions)
//{
//	if (!bMouseWithinCanvas) return;
//	UnHighlightSlots(LastHighlightedIndex, LastHighlightedDimensions);
//	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots,
//		Index, Dimensions, Columns,
//		[&](URPGGridSlot* GridSlot)
//		{
//			GridSlot->SetSlotTexture(EGridSlotState::Occupied);
//		});
//	LastHighlightedDimensions = Dimensions;
//	LastHighlightedIndex = Index;
//}

void URPGInventoryGrid::HighlightSlots(const int32 Index)
{
	if (!bMouseWithinCanvas || !GridSlots.IsValidIndex(Index)) return;

	// ������ ���̶���Ʈ�� ������ ���� ���� ������ �ʿ��� �� �ֽ��ϴ�.
	// UnHighlightSlots(LastHighlightedIndex); 

	GridSlots[Index]->SetSlotTexture(EGridSlotState::Occupied); // �Ǵ� Selected
	LastHighlightedIndex = Index;
}

//void URPGInventoryGrid::UnHighlightSlots(const int32 Index, const FIntPoint& Dimensions)
//{
//	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots,
//		Index, Dimensions, Columns,
//		[&](URPGGridSlot* GridSlot)
//		{
//			if (GridSlot->IsAvailiable())
//			{
//				GridSlot->SetSlotTexture(EGridSlotState::Unoccupied);
//			}
//			else
//				GridSlot->SetSlotTexture(EGridSlotState::Occupied);
//		});
//}

void URPGInventoryGrid::UnHighlightSlots(const int32 Index)
{
	if (!GridSlots.IsValidIndex(Index)) return;

	if (GridSlots[Index]->IsAvailiable())
	{
		GridSlots[Index]->SetSlotTexture(EGridSlotState::Unoccupied);
	}
	else
		GridSlots[Index]->SetSlotTexture(EGridSlotState::Occupied);
}

void URPGInventoryGrid::ChangeHoverType(const int32 Index,
	const FIntPoint& Dimensions, EGridSlotState SlotState)
{
	UnHighlightSlots(LastHighlightedIndex);
	URPGFunctionLibrary::ForeachGridSlot2D(GridSlots,
		Index, Dimensions, Columns,
		[State = SlotState](URPGGridSlot* GridSlot)
		{
			switch (State)
			{
			case EGridSlotState::Occupied:
				GridSlot->SetSlotTexture(EGridSlotState::Occupied);
				break;
			case EGridSlotState::Unoccupied:
				GridSlot->SetSlotTexture(EGridSlotState::Unoccupied);
				break;
			case EGridSlotState::GrayedOut:
				GridSlot->SetSlotTexture(EGridSlotState::GrayedOut);
				break;
			case EGridSlotState::Selected:
				GridSlot->SetSlotTexture(EGridSlotState::Selected);
				break;
			}
		});

	LastHighlightedIndex = Index;
	LastHighlightedDimensions = Dimensions;
}

void URPGInventoryGrid::PutDownOnIndex(const int32 Index)
{
	const int32 PrevIndex = HoverItem->GetPrevGridIndex();
	
	if (GridSlots.IsValidIndex(PrevIndex) && PrevIndex != Index)
	{
		RemoveItemFromGrid(HoverItem->GetInvenItem(), PrevIndex);
	}

	// [Fix] 아이템 데이터의 슬롯 인덱스 업데이트
	if (URPGItemBase* Item = HoverItem->GetInvenItem())
	{
		Item->SlotIndex = Index;
	
		InventoryComponent->Server_MoveItem(Item, Index);		
	}

	AddItemAtIndex(HoverItem->GetInvenItem(), Index, HoverItem->IsStackable(), HoverItem->GetQuantity());
	UpdateGridSlots(HoverItem->GetInvenItem(), Index, HoverItem->IsStackable(), HoverItem->GetQuantity());
	ClearHoverItem();
}

void URPGInventoryGrid::ClearHoverItem()
{
	if (!IsValid(HoverItem)) return;

	HoverItem->SetInvenItem(nullptr);
	HoverItem->SetIsStackable(false);
	HoverItem->SetPrevGridIndex(INDEX_NONE);
	HoverItem->UpdateQuantity(0);
	HoverItem->SetImageBrush(FSlateNoResource());

	HoverItem->RemoveFromParent();
	HoverItem = nullptr;

	SetVisibleCursor();
}

bool URPGInventoryGrid::IsSameStackable(const URPGItemBase* ClickedInventoryItem) const
{
	const bool bIsSameItem = ClickedInventoryItem == HoverItem->GetInvenItem();
	const bool bIsStackable = ClickedInventoryItem->IsStackable();

	return bIsSameItem&&bIsStackable&&HoverItem->
		GetItemTag().MatchesTagExact(ClickedInventoryItem->GetItemManifest().GetItemTag());
}

void URPGInventoryGrid::SwapWithHoverItem(URPGItemBase* ClickedInventoryItem, const int32 ClickedGridIndex)
{
	if (!IsValid(HoverItem)) return;

	// 1. �ӽ� ������ ���� ��� �ִ� ������(HoverItem)�� ������ �����մϴ�.
	// �� ������ HoverItem ��� �������� ���� �����ɴϴ�.
	URPGItemBase* HoveredItem = HoverItem->GetInvenItem();
	const int32 HoveredQuantity = HoverItem->GetQuantity();
	const bool bHoveredIsStackable = HoverItem->IsStackable();
	const int32 PrevHoveredIndex = HoverItem->GetPrevGridIndex(); // ���� �߿�!

	// ���� Ŭ���� �������� ������ �̸� ������ �Ӵϴ�.
	// RemoveItemFromGrid�� ȣ���ϸ� GridSlot���� ������ ������ �� ���� �Ǳ� �����Դϴ�.
	const int32 ClickedItemQuantity = GridSlots[ClickedGridIndex]->GetQuantity();
	const bool bClickedIsStackable = ClickedInventoryItem->IsStackable();
	
	InventoryComponent->Server_MoveItem(HoveredItem, ClickedGridIndex);
	InventoryComponent->Server_MoveItem(ClickedInventoryItem, PrevHoveredIndex);
	
	HoveredItem->SlotIndex = ClickedGridIndex;
	ClickedInventoryItem->SlotIndex = PrevHoveredIndex;

	// 2. ������ ��� �ִ� �������� �ִ� �ڸ��� ���� ���ϴ�.
	RemoveItemFromGrid(HoveredItem, PrevHoveredIndex);

	// 3. ���� Ŭ���� �������� �ִ� �ڸ��� ���ϴ�.
	RemoveItemFromGrid(ClickedInventoryItem, ClickedGridIndex);

	// 4. ������ ��� �ִ� ������(HoveredItem)�� ���� Ŭ���� �������� ��ġ(ClickedGridIndex)�� �����ϴ�.
	AddItemAtIndex(HoveredItem, ClickedGridIndex, bHoveredIsStackable, HoveredQuantity);
	UpdateGridSlots(HoveredItem, ClickedGridIndex, bHoveredIsStackable, HoveredQuantity);

	// 5. ���� Ŭ���ߴ� ������(ClickedInventoryItem)�� ������ ��� �ִ� �������� ��ġ(PrevHoveredIndex)�� �����ϴ�.
	AddItemAtIndex(ClickedInventoryItem, PrevHoveredIndex, bClickedIsStackable, ClickedItemQuantity);
	UpdateGridSlots(ClickedInventoryItem, PrevHoveredIndex, bClickedIsStackable, ClickedItemQuantity);

	// 6. ��� ������ �������Ƿ� HoverItem�� �����ϰ� ���콺 Ŀ���� ������� �ǵ����ϴ�.
	ClearHoverItem();
}

bool URPGInventoryGrid::ShouldSwapQuantity(const int32 SpaceInClickedSlot,
	const int32 HoveredQuantity, const int32 MaxQuantity) const
{
	return SpaceInClickedSlot == 0 && HoveredQuantity < MaxQuantity;
}

void URPGInventoryGrid::SwapQuantity(const int32 ClickedQuantity, 
	const int32 HoveredQuantity, const int32 Index)
{
	URPGGridSlot* GridSlot = GridSlots[Index];
	GridSlot->SetQuantity(HoveredQuantity);

	URPGInventoryItemSlot* ClickedSlotItem = ItemsInSlot.FindChecked(Index);
	ClickedSlotItem->UpdateItemQuantity(HoveredQuantity);

	HoverItem->UpdateQuantity(ClickedQuantity);
}

bool URPGInventoryGrid::ShouldConsumeHoverItemQuantity(const int32 HoveredQuantity,
	const int32 SpaceClickedSlot) const
{
	return SpaceClickedSlot>=HoveredQuantity;
}

void URPGInventoryGrid::ConsumeHoverItemQuantity(const int32 ClickedQuantity, 
	const int32 HoveredQuantity, const int32 Index)
{
	const int32 AmountToTransfer = HoveredQuantity;
	const int32 NewClickedQuantity = ClickedQuantity + AmountToTransfer;

	GridSlots[Index]->SetQuantity(NewClickedQuantity);
	ItemsInSlot.FindChecked(Index)->UpdateItemQuantity(NewClickedQuantity);
	ClearHoverItem();
	SetVisibleCursor();

	/*const FGridFragment* GridFragment
		= GridSlots[Index]->GetInvenItem()->GetItemManifest().GetFragmentOfType<FGridFragment>();
	const FIntPoint Dimensions = GridFragment ? GridFragment->GetGridSize() : FIntPoint(1, 1);
	HighlightSlots(Index, Dimensions);*/

	if (GridSlots.IsValidIndex(Index))
	{
		GridSlots[Index]->SetSlotTexture(EGridSlotState::Occupied);
	}
}

bool URPGInventoryGrid::ShouldFillInStack(const int32 SpaceInClickedSlot, const int32 HoveredQuantity) const
{
	return SpaceInClickedSlot < HoveredQuantity;
}

void URPGInventoryGrid::FillInQuantity(const int32 FillAmount, const int32 Remainder, const int32 Index)
{
	URPGGridSlot* GridSlot = GridSlots[Index];
	const int32 NewQuantity = GridSlot->GetQuantity() + FillAmount;

	GridSlot->SetQuantity(NewQuantity);

	URPGInventoryItemSlot* ClickedSlottedItem=ItemsInSlot.FindChecked(Index);
	ClickedSlottedItem->UpdateItemQuantity(NewQuantity);

	HoverItem->UpdateQuantity(Remainder);
}

void URPGInventoryGrid::CreateItemPopUp(const int32 GridIndex)
{
	URPGItemBase* ClickedItem = GridSlots[GridIndex]->GetInvenItem().Get();
	if (!IsValid(ClickedItem)) return;
	if (IsValid(GridSlots[GridIndex]->GetItemPopUp())) return;

	ItemPopUp = CreateWidget<URPGItemPopUp>(this, ItemPopUpClass);
	GridSlots[GridIndex]->SetItemPopUp(ItemPopUp);

	OwningCanvasPanel->AddChild(ItemPopUp);
	UCanvasPanelSlot* CanvasSlot = UWidgetLayoutLibrary::SlotAsCanvasSlot(ItemPopUp);
	const FVector2D MousePosition = UWidgetLayoutLibrary::GetMousePositionOnViewport(GetOwningPlayer());
	CanvasSlot->SetPosition(MousePosition-ItemPopUpOffset);
	CanvasSlot->SetSize(ItemPopUp->GetBoxSize());


	const int32 SliderMax = GridSlots[GridIndex]->GetQuantity() - 1;
	if (ClickedItem->IsStackable()&&SliderMax>0)
	{
		ItemPopUp->OnAssignHoverItem.BindDynamic(this, &ThisClass::AssignAndSetHoverItem);
		//ItemPopUp->SetHoverItem(HoverItem);
		ItemPopUp->SetSliderMax(SliderMax);
		ItemPopUp->SetGridSlots(GridSlots);
		ItemPopUp->SetItemsInSlot(ItemsInSlot);
	}
	else
	{
		ItemPopUp->CollapseSplitButton();
	}

	ItemPopUp->OnDrop.BindDynamic(this, &ThisClass::OnPopUpMenuDrop);

	if (ClickedItem	->IsConsumable())
	{
		ItemPopUp->OnConsume.BindDynamic(this, &ThisClass::OnPopUpMenuConsume);
	}

	else
	{
		ItemPopUp->CollapseConsumeButton();
	}
}

void URPGInventoryGrid::DropItem()
{
	if (!IsValid(HoverItem)) return;
	if (!IsValid(HoverItem->GetInvenItem())) return;

	InventoryComponent->Server_DropItem(HoverItem->GetInvenItem(), HoverItem->GetQuantity());

	ClearHoverItem();
	SetVisibleCursor();
}

bool URPGInventoryGrid::HasHoverItem() const
{
	return IsValid(HoverItem);
}

UUserWidget* URPGInventoryGrid::GetCursorWidget()
{
	if (!IsValid(GetOwningPlayer())) return nullptr;

	if (!IsValid(CursorWidget))
	{
		CursorWidget = CreateWidget<UUserWidget>(GetOwningPlayer(), CursorWidgetClass);
	}

	return CursorWidget;
}

void URPGInventoryGrid::AddQuantity(const FSlotAvailabilityResult& Result)
{
	if (!MatchesCategory(Result.Item.Get())) return;

	for (const auto& Availability : Result.SlotAvailabilities)
	{
		if (Availability.bItemAtIndex)
		{
			const auto& GridSlot = GridSlots[Availability.Index];
			const auto& ItemInSlot = ItemsInSlot.FindChecked(Availability.Index);
			ItemInSlot->UpdateItemQuantity(GridSlot->GetQuantity() + Availability.AmountToFill);
			GridSlot->SetQuantity(GridSlot->GetQuantity() + Availability.AmountToFill);
		}
		
		else
		{
			AddItemAtIndex(Result.Item.Get(), Availability.Index, 
				Result.bStackable, Availability.AmountToFill);
			UpdateGridSlots(Result.Item.Get(), Availability.Index,
				Result.bStackable, Availability.AmountToFill);
		}
	}
}

void URPGInventoryGrid::OnSlotItemClicked(int32 GridIndex, const FPointerEvent& MouseEvent)
{
	//Debug::Print("Clicked on item at index : %d", GridIndex);
	URPGFunctionLibrary::ItemUnhovered(GetOwningPlayer());

	check(GridSlots.IsValidIndex(GridIndex));
	URPGItemBase* ClickedInvenItem = GridSlots[GridIndex]->GetInvenItem().Get();

	if (!IsValid(HoverItem)&&IsLeftClick(MouseEvent))
	{
		PickUp(ClickedInvenItem, GridIndex);
		return;
	}

	if (IsCtrlRightClick(MouseEvent))
	{
		//Debug::Print("ctrlRightClick..");
		CreateItemPopUp(GridIndex);
		return;
	}

	if (IsRightClick(MouseEvent))
	{
		Debug::Print("RightClick..");
		return;
	}

	if (IsSameStackable(ClickedInvenItem))
	{
		const int32 ClickedQuantity = GridSlots[GridIndex]->GetQuantity();
		const FStackableFragment* StackableFragment 
			= ClickedInvenItem->GetItemManifest().GetFragmentOfType<FStackableFragment>();
		const int32 MaxQuantity = StackableFragment->GetMaxQuantity();
		const int32 SpaceInClickedSlot = MaxQuantity - ClickedQuantity;
		const int32 HoveredQuantity = HoverItem->GetQuantity();

		if (ShouldSwapQuantity(SpaceInClickedSlot, HoveredQuantity, MaxQuantity))
		{
			SwapQuantity(ClickedQuantity, HoveredQuantity, GridIndex);
			return;
		}

		if (ShouldConsumeHoverItemQuantity(HoveredQuantity, SpaceInClickedSlot))
		{
			ConsumeHoverItemQuantity(ClickedQuantity, HoveredQuantity, GridIndex);
			return;
		}

		if (ShouldFillInStack(SpaceInClickedSlot, HoveredQuantity))
		{
			FillInQuantity(SpaceInClickedSlot, HoveredQuantity - SpaceInClickedSlot, GridIndex);
			return;
		}

		if (SpaceInClickedSlot==0)
		{
			return;
		}		
	}

	SwapWithHoverItem(ClickedInvenItem, GridIndex);
}
