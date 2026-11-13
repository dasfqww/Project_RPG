// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGQuickSlotWidget.h"
#include "Item/RPGItemBase.h"
#include "Engine/GameInstance.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Manager/DataManager.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/QuickSlotDragDropOperation.h"
#include "Character/RPGPlayer.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "UI/Inventory/Projection/RPGItemIdDragDropOperation.h"
#include "UI/DragQuickSlotItemVisual.h"
#include "UI/DragItemVisual.h"
#include "UI/Inventory/Hover/RPGHoverItem.h"
#include "Controller/RPGPlayerController.h"
#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "UI/RPGDragDropOperation.h"
#include "UI/Skill/RPGSkillQuickSlotDragDrop.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "UI/Slots/SRPGSlotFrame.h"

#include "RPGGameplayTags.h"
#include "RPGDebugHelper.h"

namespace
{
FText ResolveQuickSlotInputKeyText(
	const UQuickSlotComponent* Component,
	bool bIsSkillSlot,
	int32 SlotIndex,
	const FText& FallbackText)
{
	const APawn* OwnerPawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
	return OwnerPawn ? FRPGQuickSlotInputBindings::ReadKeyLabel(
		Cast<APlayerController>(OwnerPawn->GetController()), bIsSkillSlot, SlotIndex) : FallbackText;
}
}

URPGQuickSlotWidget::URPGQuickSlotWidget()
	:SlotItem(nullptr),
	SlotIndex(0)
{
	
}

void URPGQuickSlotWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	SetInputKeyText(InputText);
	SetImageAlpha(SlotItemImage, 0.f);
	UpdateSkillCooldownView(0.0f);
}

void URPGQuickSlotWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (const auto Frame = SlotFrame.Pin()) Frame->ResetHover();
	BindToQuickSlot();
}

void URPGQuickSlotWidget::NativeDestruct()
{
	if (const auto Frame = SlotFrame.Pin()) Frame->ResetHover();
	UnbindFromQuickSlot();
	Super::NativeDestruct();
}

void URPGQuickSlotWidget::BeginDestroy()
{
	if (auto* Model = ItemCommandViewModel.Get()) Model->RemoveAllFieldValueChangedDelegates(this);
	ItemCommandViewModel.Reset();
	CancelItemIconLoad();
	Super::BeginDestroy();
}

void URPGQuickSlotWidget::CancelItemIconLoad()
{
	++ItemIconLoadGeneration;
	if (ItemIconLoadHandle)
	{
		ItemIconLoadHandle->CancelHandle();
		ItemIconLoadHandle.Reset();
	}
}

void URPGQuickSlotWidget::ApplyItemIcon(UTexture2D* Icon)
{
	if (SlotItemImage) SlotItemImage->SetBrushFromTexture(Icon);
	RefreshItemCommandState();
}

void URPGQuickSlotWidget::RefreshItemIcon(TSoftObjectPtr<UTexture2D> Icon)
{
	if (Icon == ItemIconReference && SlotItemImage &&
		(ItemIconLoadHandle || (Icon.Get() && SlotItemImage->GetBrush().GetResourceObject() == Icon.Get()))) return;
	CancelItemIconLoad();
	ItemIconReference = Icon;
	ApplyItemIcon(nullptr);
	if (!SlotItemImage || !SlotItemId.IsValid() || Icon.IsNull()) return;
	if (UTexture2D* Loaded = Icon.Get())
	{
		ApplyItemIcon(Loaded);
		return;
	}
	const auto Generation = ItemIconLoadGeneration;
	const FGuid ExpectedItemId = SlotItemId;
	const TWeakObjectPtr<URPGQuickSlotWidget> WeakThis(this);
	ItemIconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Icon.ToSoftObjectPath(),
		FStreamableDelegate::CreateLambda([WeakThis, Generation, ExpectedItemId, Icon]()
		{
			if (auto* Self = WeakThis.Get(); Self && !Self->bIsSkillSlot &&
				Self->ItemIconLoadGeneration == Generation && Self->SlotItemId == ExpectedItemId)
				Self->ApplyItemIcon(Icon.Get());
		}));
}

void URPGQuickSlotWidget::BindToQuickSlot()
{
	UnbindFromQuickSlot();

	APawn* Pawn = GetOwningPlayerPawn();
	UQuickSlotComponent* Component = Pawn ? Pawn->FindComponentByClass<UQuickSlotComponent>() : nullptr;
	if (!Component)
	{
		ClearSlotItem();
		return;
	}

	LinkedQuickSlotComponent = Component;
	SetInputKeyText(ResolveQuickSlotInputKeyText(
		Component, bIsSkillSlot, SlotIndex, InputText));
	if (bIsSkillSlot)
	{
		SkillViewModel.Reset(NewObject<URPGQuickSlotViewModel>(this));
		SkillViewModel->Initialize(SlotIndex, Component, true);
		Component->OnSkillSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		if (const FRPGQuickSlotContent* Content =
			Component->GetSkillSlotContent(SlotIndex))
		{
			HandleSlotChanged(SlotIndex, *Content);
		}
	}
	else
	{
		BindItemCommandState();
		Component->OnItemSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		if (const FRPGQuickSlotContent* Content =
			Component->GetItemSlotContent(SlotIndex))
		{
			HandleSlotChanged(SlotIndex, *Content);
		}
	}
}

void URPGQuickSlotWidget::UnbindFromQuickSlot()
{
	if (auto* Model = ItemCommandViewModel.Get()) Model->RemoveAllFieldValueChangedDelegates(this);
	ItemCommandViewModel.Reset();
	CancelItemIconLoad();
	ItemIconReference.Reset();
	UpdateSkillCooldownView(0.0f);
	if (SkillViewModel.IsValid()) SkillViewModel->Initialize(INDEX_NONE, nullptr, false);
	SkillViewModel.Reset();
	CancelPendingItemClick();
	if (UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get())
	{
		Component->OnSkillSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
		Component->OnItemSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotWidget::HandleSlotChanged);
	}

	LinkedQuickSlotComponent.Reset();
}

void URPGQuickSlotWidget::BindItemCommandState()
{
	if (bIsSkillSlot || ItemCommandViewModel.IsValid()) return;
	auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	auto* Model = Controller ? Controller->GetInventoryProjectionViewModel() : nullptr;
	if (!Model) return;
	ItemCommandViewModel = Model;
	using Fields = URPGInventoryProjectionViewModel::FFieldNotificationClassDescriptor;
	const auto Delegate = INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
		this, &ThisClass::HandleItemCommandFieldChanged);
	for (const auto Field : {Fields::LoadState, Fields::InventoryItems, Fields::PendingItemCommandCount,
		Fields::bHasInventoryRecovery, Fields::bInventoryRecoveryInFlight,
		Fields::bInventoryRecoveryStorageBlocked, Fields::bInventoryAuthorityRevoked})
		Model->AddFieldValueChangedDelegate(Field, Delegate);
}

void URPGQuickSlotWidget::HandleItemCommandFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId)
{
	RefreshItemCommandState();
}

void URPGQuickSlotWidget::RefreshItemCommandState()
{
	if (bIsSkillSlot) return;
	auto* Model = ItemCommandViewModel.Get();
	using EState = ERPGItemSlotPresentationState;
	const EState State = Model && SlotItemId.IsValid()
		? Model->GetItemSlotPresentationState(SlotItemId, ERPGItemContainerType::Inventory)
		: EState::Unavailable;
	if (State != EState::Idle) CancelPendingItemClick();
	const bool bHasIcon = SlotItemId.IsValid() && SlotItemImage &&
		SlotItemImage->GetBrush().GetResourceObject();
	SetImageAlpha(SlotItemImage, bHasIcon ? (State == EState::Idle ? 1.f : 0.4f) : 0.f);
	FText Status;
	if (SlotItemId.IsValid())
	{
		switch (State)
		{
		case EState::Pending:
			Status = NSLOCTEXT("RPGQuickSlot", "ItemPending", "Waiting for the previous item action."); break;
		case EState::RecoveryRequired:
			Status = Model->GetCommandFeedbackText(); break;
		case EState::Locked:
			Status = NSLOCTEXT("RPGQuickSlot", "ItemLocked", "This item is locked."); break;
		case EState::Unavailable:
			Status = NSLOCTEXT("RPGQuickSlot", "ItemUnavailable", "This item is not available yet."); break;
		default: break;
		}
	}
	const FText Details = Model && SlotItemId.IsValid()
		? Model->GetItemPresentation(SlotItemId).GetDetailsText() : FText::GetEmpty();
	SetToolTipText(Details.IsEmpty() ? Status : Status.IsEmpty() ? Details
		: FText::Format(NSLOCTEXT("RPGQuickSlot", "ItemDetailsAndStatus", "{0}\n\n{1}"), Details, Status));
}

void URPGQuickSlotWidget::HandleSlotChanged(
	int32 ChangedSlotIndex, const FRPGQuickSlotContent& Content)
{
	if (ChangedSlotIndex != SlotIndex)
	{
		return;
	}
	if (bIsSkillSlot)
	{
		CancelItemIconLoad();
		ItemIconReference.Reset();
		RefreshSkillSlotView();
		return;
	}
	if (PressedItemId.IsValid() && Content.ItemId != PressedItemId)
	{
		CancelPendingItemClick();
	}

	if (Content.ItemId.IsValid())
	{
		FRPGInventoryProjectionEntry Entry;
		UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get();
		if (!Component || !Component->FindAssignedItem(SlotIndex, Entry))
		{
			ClearSlotItem();
			return;
		}
		SlotItem = nullptr;
		if (SlotItemId != Entry.GetItemId()) CancelItemIconLoad();
		SlotItemId = Entry.GetItemId();
		UDataManager* DataManager = GetWorld() && GetWorld()->GetGameInstance()
			? GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
		FRPGItemDefinitionView View;
		const bool bMatchingView = DataManager &&
			DataManager->GetItemDefinitionViewCatalog().TryFindDefinitionView(Entry.GetDefinitionId(), View) &&
			View.IsValid() && View.Snapshot.DefinitionId == Entry.GetDefinitionId() &&
			View.Snapshot.DefinitionVersion == Entry.GetDefinitionVersion();
		RefreshItemIcon(bMatchingView ? View.Icon : TSoftObjectPtr<UTexture2D>());
		if (SlotItemCountText) SlotItemCountText->SetText(FText::AsNumber(Entry.GetQuantity()));
		RefreshItemCommandState();
		return;
	}

	SlotItem = nullptr;
	SlotItemId.Invalidate();
	CancelItemIconLoad();
	ItemIconReference.Reset();
	if (Content.AbilityTag.IsValid())
	{
		UTexture2D* SkillIcon = LinkedQuickSlotComponent.IsValid()
			? LinkedQuickSlotComponent->GetSkillIcon(Content.AbilityTag)
			: nullptr;
		if (SlotItemImage)
		{
			SlotItemImage->SetBrushFromTexture(SkillIcon);
		}
		if (SlotItemCountText)
		{
			SlotItemCountText->SetText(FText::GetEmpty());
		}
		SetImageAlpha(SlotItemImage, SkillIcon ? 1.f : 0.f);
		return;
	}

	ClearSlotItem();
}

void URPGQuickSlotWidget::RefreshSkillSlotView()
{
	if (!SkillViewModel.IsValid()) return;
	SkillViewModel->RefreshSkillState();
	UTexture2D* Icon = SkillViewModel->GetIcon();
	if (SlotItemImage && SlotItemImage->GetBrush().GetResourceObject() != Icon)
		SlotItemImage->SetBrushFromTexture(Icon);
	SetImageAlpha(SlotItemImage, Icon && SkillViewModel->IsSlotActive() ? 1.f : 0.f);
	if (SlotItemCountText) SlotItemCountText->SetText(SkillViewModel->GetQuantityLabel());
	SetInputKeyText(SkillViewModel->GetKeyLabel());
	UpdateSkillCooldownView(SkillViewModel->IsSlotActive()
		? SkillViewModel->GetCooldownFraction() : 0.0f);
}

void URPGQuickSlotWidget::UpdateSkillCooldownView(const float RemainingFraction)
{
	if (!bIsSkillSlot) return;
	UImage* CooldownImage = Cast<UImage>(GetWidgetFromName(TEXT("SkillCooldownImage")));
	if (!CooldownImage) return; // Existing item/legacy WBPs do not need this image.
	if (RemainingFraction <= 0.0f)
	{
		CooldownImage->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}
	if (UMaterialInstanceDynamic* Material = CooldownImage->GetDynamicMaterial())
	{
		// The image owns a per-widget MID, never a shared cooldown or UI timer.
		Material->SetScalarParameterValue(TEXT("ProgressBarFillPercent"), RemainingFraction);
		CooldownImage->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		CooldownImage->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void URPGQuickSlotWidget::NativeTick(const FGeometry& Geometry, const float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (!bIsSkillSlot || !LinkedQuickSlotComponent.IsValid()) return;
	SkillRefreshElapsed += DeltaTime;
	if (SkillRefreshElapsed >= 0.1f)
	{
		SkillRefreshElapsed = 0.f;
		RefreshSkillSlotView();
	}
}

TSharedRef<SWidget> URPGQuickSlotWidget::RebuildWidget()
{
	const TSharedRef<SWidget> Content = Super::RebuildWidget();
	const TWeakObjectPtr<URPGQuickSlotWidget> WeakThis(this);
	const auto Frame = SNew(SRPGSlotFrame).Appearance_Lambda([WeakThis] {
		FRPGSlotAppearance Appearance;
		if (const auto* Self = WeakThis.Get())
		{
			Appearance.HighlightColor = Self->HoverHighlightColor;
			Appearance.HighlightWidth = Self->HoverHighlightWidth;
			Appearance.FadeInDuration = Self->HoverHighlightFadeDuration;
		}
		return Appearance;
	})[Content];
	SlotFrame = Frame;
	return Frame;
}

FReply URPGQuickSlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	FReply Reply = Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	if (Reply.IsEventHandled())
	{
		return Reply;
	}
	if (bIsSkillSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		// Keep the clear gesture in UI. An unhandled press lets the game viewport
		// capture the pointer, so this slot never receives the matching release.
		return FReply::Handled();
	}
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Reply;
	}
	if (bIsSkillSlot)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	CancelPendingItemClick();
	UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get();
	const APlayerController* Controller = GetOwningPlayer();
	const FRPGQuickSlotContent* Content = Component ? Component->GetItemSlotContent(SlotIndex) : nullptr;
	if (!GetIsEnabled() || !Controller || !Component || Component->GetOwner() != Controller->GetPawn() ||
		!Content || !SlotItemId.IsValid() || Content->ItemId != SlotItemId ||
		!InGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition()))
	{
		return FReply::Handled();
	}

	const auto* Model = ItemCommandViewModel.Get();
	if (!Model || Model->GetItemSlotPresentationState(SlotItemId, ERPGItemContainerType::Inventory) !=
		ERPGItemSlotPresentationState::Idle)
	{
		// A blocked press must not turn into a use when recovery finishes before release.
		// Keep shortcut drag/drop routing available without arming a consumption click.
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}

	PressedItemId = SlotItemId;
	PressedQuickSlotComponent = Component;
	PressedSlotIndex = SlotIndex;
	PressedUserIndex = InMouseEvent.GetUserIndex();
	PressedPointerIndex = InMouseEvent.GetPointerIndex();
	ItemClickScreenOrigin = InMouseEvent.GetScreenSpacePosition();
	return FReply::Handled().CaptureMouse(TakeWidget()).DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
}

FReply URPGQuickSlotWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{
	const FReply Reply = Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	if (bIsSkillSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && LinkedQuickSlotComponent.IsValid())
	{
		LinkedQuickSlotComponent->ClearSlot(true, SlotIndex);
		return FReply::Handled();
	}
	if (!PressedItemId.IsValid() || InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton ||
		InMouseEvent.GetUserIndex() != PressedUserIndex ||
		InMouseEvent.GetPointerIndex() != PressedPointerIndex)
	{
		return Reply;
	}

	const UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get();
	const FRPGQuickSlotContent* Content = Component ? Component->GetItemSlotContent(SlotIndex) : nullptr;
	const bool bUseItem = !bIsSkillSlot && GetIsEnabled() && IsVisible() &&
		SlotIndex == PressedSlotIndex && SlotItemId == PressedItemId &&
		Component && Component == PressedQuickSlotComponent.Get() &&
		Content && Content->ItemId == PressedItemId &&
		InGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition()) &&
		(!FSlateApplication::IsInitialized() || !FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(
			InMouseEvent, ItemClickScreenOrigin));
	// Clear before the command: synchronous projection/delegate changes cannot reuse the press.
	CancelPendingItemClick();
	if (bUseItem)
	{
		UseAssignedItem();
	}
	return FReply::Handled();
}

FReply URPGQuickSlotWidget::NativeOnMouseButtonDoubleClick(
	const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if ((!bIsSkillSlot && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) ||
		(bIsSkillSlot && InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton))
	{
		// Slate dispatches the second press as a double-click; keep commands on release.
		return NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	}
	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

FReply URPGQuickSlotWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (PressedItemId.IsValid() && InMouseEvent.GetUserIndex() == PressedUserIndex &&
		InMouseEvent.GetPointerIndex() == PressedPointerIndex)
	{
		if (!InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) ||
			!InGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition()) ||
			(FSlateApplication::IsInitialized() && FSlateApplication::Get().HasTraveledFarEnoughToTriggerDrag(
				InMouseEvent, ItemClickScreenOrigin)))
		{
			CancelPendingItemClick();
		}
		return FReply::Handled();
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

void URPGQuickSlotWidget::CancelPendingItemClick()
{
	const bool bHadItemPress = PressedItemId.IsValid();
	const int32 UserIndex = PressedUserIndex;
	const uint32 PointerIndex = PressedPointerIndex;
	PressedItemId.Invalidate();
	PressedQuickSlotComponent.Reset();
	PressedSlotIndex = INDEX_NONE;
	PressedUserIndex = INDEX_NONE;
	if (bHadItemPress && FSlateApplication::IsInitialized())
	{
		const TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetUser(UserIndex);
		if (User && User->GetPointerCaptor(PointerIndex) == GetCachedWidget())
		{
			User->ReleaseCapture(PointerIndex);
		}
	}
}

void URPGQuickSlotWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	CancelPendingItemClick();
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
}

void URPGQuickSlotWidget::NativeOnDeactivated()
{
	if (const auto Frame = SlotFrame.Pin()) Frame->ResetHover();
	CancelPendingItemClick();
	Super::NativeOnDeactivated();
}

void URPGQuickSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	CancelPendingItemClick();
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
	UQuickSlotComponent* Slots = LinkedQuickSlotComponent.Get();
	const FRPGQuickSlotContent* Content = bIsSkillSlot && Slots ? Slots->GetSkillSlotContent(SlotIndex) : nullptr;
	if (Content && Content->AbilityTag.IsValid())
	{
		OutOperation = FRPGSkillQuickSlotDragDrop::Create(this, Slots, Content->AbilityTag,
			Slots->GetSkillIcon(Content->AbilityTag), SlotIndex);
	}
}

void URPGQuickSlotWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	CancelPendingItemClick();
	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
}

bool URPGQuickSlotWidget::NativeOnDrop(
	const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	CancelPendingItemClick();
	if (bIsSkillSlot)
	{
		const URPGDragDropOperation* Drag = Cast<URPGDragDropOperation>(InOperation);
		UQuickSlotComponent* Slots = LinkedQuickSlotComponent.Get();
		return Drag && Slots && Slots->GetOwner() == GetOwningPlayerPawn() &&
			FRPGSkillQuickSlotDragDrop::Drop(*Drag, *Slots, SlotIndex);
	}

	const URPGItemIdDragDropOperation* Drag =
		Cast<URPGItemIdDragDropOperation>(InOperation);
	UQuickSlotComponent* QuickSlots = LinkedQuickSlotComponent.Get();
	APlayerController* Controller = GetOwningPlayer();
	const URPGInventoryProjectionComponent* Projection = Controller
		? Controller->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	FRPGInventoryProjectionEntry Entry;
	if (QuickSlots && Controller && Projection && Drag && !Drag->bSplitStack && !Drag->bChooseSplitQuantity &&
		QuickSlots->GetOwner() == Controller->GetPawn() &&
		SlotIndex >= 0 && SlotIndex < QuickSlots->GetMaxItemSlots() &&
		Drag->ItemId.IsValid() &&
		Projection->FindProjectedItem(Drag->ItemId, Entry) &&
		Entry.GetContainerType() == ERPGItemContainerType::Inventory &&
		Entry.GetQuantity() > 0)
	{
		QuickSlots->SetItemSlotById(SlotIndex, Entry.GetItemId());
		return true;
	}
	return false;
}

//void URPGQuickSlotWidget::NativeOnDragCancelled(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
//{
//	Super::NativeOnDragCancelled(InDragDropEvent, InOperation);
//
//	ClearSlotItem();
//	SetImageAlpha(SlotItemImage, 0.f);
//}
//
//void URPGQuickSlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, 
//	const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
//{
//	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);
//
//	if (DragVisualClass)
//	{
//		UDragItemVisual* DragVisual = CreateWidget<UDragItemVisual>(this, DragVisualClass);
//		//DragVisual->ItemIcon->SetBrushFromTexture(SlotItem->AssetData.Icon);
//		//DragVisual->ItemBorder->SetBrushColor(ItemBorder->GetBrushColor());
//
//		UQuickSlotDragDropOperation* DragSlotItemOperation = NewObject<UQuickSlotDragDropOperation>();
//		DragSlotItemOperation->SourceItem = SlotItem;
//		DragSlotItemOperation->SourceInventory = SlotItem->OwningInventory;
//		DragSlotItemOperation->SourceQuickSlot = this;
//		DragSlotItemOperation->SourceQuickSlotIndex = SlotIndex;
//
//		DragSlotItemOperation->DefaultDragVisual = DragVisual;
//		DragSlotItemOperation->Pivot = EDragPivot::CenterCenter;
//
//		OutOperation = DragSlotItemOperation;
//	}
//}
//
//bool URPGQuickSlotWidget::NativeOnDrop(const FGeometry& InGeometry,
//	const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
//{
//	// 드래그된 아이템을 슬롯에 넣는 처리
//	const UItemDragDropOperation* ItemDragDrop = Cast<UItemDragDropOperation>(InOperation);
//	if (ItemDragDrop && ItemDragDrop->SourceItem)
//	{
//		
//		 
//		//URPGQuickSlotWidget* SourceWidget = Cast<URPGQuickSlotWidget>(ItemDragDrop->SourceQuickSlot);
//		//if (!SourceWidget || SourceWidget == this) return false; // 같은 슬롯이면 무시
//
//		SetImageAlpha(SlotItemImage, 1.f);
//		//SetSlotItem(ItemDragDrop->SourceItem, ItemDragDrop->SourceItem->Quantity);
//
//		ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
//		if (Player)
//		{
//			UQuickSlotComponent* QuickSlotComponent = Player->GetQuickSlotComponent();
//			if (QuickSlotComponent)
//			{
//				QuickSlotComponent->SetQuickSlotItem(SlotIndex, ItemDragDrop->SourceItem);
//			}
//		}
//
//		return true;
//	}
//
//	//TODO:퀵슬롯 이미지 및 수량 스왑 구현
//	//퀵슬롯 컴포넌트에서 구현해보자.
//	const UQuickSlotDragDropOperation* QuickSlotDragDrop = Cast<UQuickSlotDragDropOperation>(InOperation);
//	if (QuickSlotDragDrop&&QuickSlotDragDrop->SourceInventory&&QuickSlotDragDrop->SourceQuickSlot)
//	{
//		//여기서 스왑 기능을 실행한다.
//		URPGQuickSlotWidget* SourceQuickSlot = Cast<URPGQuickSlotWidget>(QuickSlotDragDrop->SourceQuickSlot);
//		if (!SourceQuickSlot || SourceQuickSlot == this) return false; // 같은 슬롯이면 무시
//
//		ARPGPlayer* Player = Cast<ARPGPlayer>(GetOwningPlayerPawn());
//		if (Player)
//		{
//			UQuickSlotComponent* QuickSlotComponent = Player->GetQuickSlotComponent();
//			if (QuickSlotComponent)
//			{
//				URPGItemBase* SourceItem = SourceQuickSlot->GetSlotItem();
//
//				URPGItemBase* TargetItem = GetSlotItem();
//
//				QuickSlotComponent->SwapQuickSlotItems(SourceQuickSlot->SlotIndex, SlotIndex);
//
//				SourceQuickSlot->SetSlotItem(TargetItem, 0);
//				SetSlotItem(SourceItem, 0);
//			}
//		}
//	}
//
//	return false;
//}

void URPGQuickSlotWidget::SetSlotItem(URPGItemBase* NewItem, int32 ItemCount)
{
	if (!IsValid(NewItem) || ItemCount <= 0)
	{
		ClearSlotItem();
		return;
	}

	const UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get();
	const FRPGQuickSlotContent* Content = Component ? Component->GetItemSlotContent(SlotIndex) : nullptr;
	if (Content && Content->ItemId == NewItem->GetInstanceId())
	{
		HandleSlotChanged(SlotIndex, *Content);
	}
	else
	{
		ClearSlotItem();
	}
}

void URPGQuickSlotWidget::ClearSlotItem()
{
	CancelItemIconLoad();
	ItemIconReference.Reset();
	UpdateSkillCooldownView(0.0f);
	CancelPendingItemClick();
	SlotItem = nullptr;
	SlotItemId.Invalidate();

	if (SlotItemImage)
	{
		SlotItemImage->SetBrushFromTexture(nullptr);
	}

	// 아이템 수량 텍스트 업데이트
	if (SlotItemCountText)
	{
		SlotItemCountText->SetText(FText::GetEmpty());
	}

	SetImageAlpha(SlotItemImage, 0.f);
	if (!bIsSkillSlot) SetToolTipText(FText::GetEmpty());
}

void URPGQuickSlotWidget::SetInputKeyText(FText InText)
{
	if (InputKeyText)
	{
		InputKeyText->SetText(InText);
	}
}

//void URPGQuickSlotWidget::UpdateSlotUI()
//{
//	if (SlotItem)
//	{
//		SetSlotItem(SlotItem, SlotItem->Quantity);
//	}
//
//	else
//	{
//		// 아이템이 없다면 슬롯을 비운다.
//		if (SlotItemImage) SlotItemImage->SetBrushFromTexture(nullptr);
//		if (SlotItemCountText) SlotItemCountText->SetText(FText::FromString(TEXT("")));
//	}
//}

void URPGQuickSlotWidget::SetImageAlpha(UImage* InImage, float InAlpha)
{
	if (!InImage)
	{
		return;
	}

	FLinearColor NewColor = InImage->GetColorAndOpacity();
	NewColor.A = InAlpha;
	InImage->SetColorAndOpacity(NewColor);
}

void URPGQuickSlotWidget::UseSlotItem(URPGItemBase* UseItem)
{
	if (UseItem && (!IsValid(UseItem) || UseItem->GetInstanceId() != SlotItemId))
	{
		return;
	}
	UseAssignedItem();
}

void URPGQuickSlotWidget::UseAssignedItem()
{
	if (bIsSkillSlot || !SlotItemId.IsValid()) return;
	UQuickSlotComponent* Component = LinkedQuickSlotComponent.Get();
	const APlayerController* Controller = GetOwningPlayer();
	const FRPGQuickSlotContent* Content = Component ? Component->GetItemSlotContent(SlotIndex) : nullptr;
	if (Component && Controller && Component->GetOwner() == Controller->GetPawn() &&
		Content && Content->ItemId == SlotItemId)
	{
		Component->UseItemSlot(SlotIndex, GetOwningPlayer());
	}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemQuantity(int32 ItemCount)
{
	if (SlotItemId.IsValid() && SlotItemCountText)
	{
		SlotItemCountText->SetText(
			ItemCount > 0 ? FText::AsNumber(ItemCount) : FText::GetEmpty());
	}
}

void URPGQuickSlotWidget::UpdateQuickSlotItemImageAlpha()
{
	RefreshItemCommandState();
}
