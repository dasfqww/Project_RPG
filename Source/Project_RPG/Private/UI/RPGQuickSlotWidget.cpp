// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/RPGQuickSlotWidget.h"
#include "Riding/RPGMountDefinition.h"
#include "UI/Riding/RPGMountQuickSlots.h"
#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/ItemQuickSlot/RPGItemQuickSlotBar.h"
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
#include "Component/Skill/RPGPlayerSkillComponent.h"
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
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillRuntimeTypes.h"
#include "UI/Tooltip/RPGSkillTooltip.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "UI/Slots/SRPGSlotFrame.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"

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
	if (SlotItemImage && SlotItemImage->GetBrush().GetResourceObject() != Icon)
		SlotItemImage->SetBrushFromTexture(Icon);
	RefreshItemCommandState();
}

void URPGQuickSlotWidget::RefreshItemIcon(TSoftObjectPtr<UTexture2D> Icon)
{
	UTexture2D* Loaded = Icon.Get();
	if (Icon == ItemIconReference && SlotItemImage &&
		(ItemIconLoadHandle || (Loaded && SlotItemImage->GetBrush().GetResourceObject() == Loaded)))
	{
		RefreshItemCommandState();
		return;
	}
	CancelItemIconLoad();
	ItemIconReference = Icon;
	if (!SlotItemImage || !SlotItemId.IsValid() || Icon.IsNull() || Loaded)
	{
		ApplyItemIcon(SlotItemId.IsValid() ? Loaded : nullptr);
		return;
	}
	ApplyItemIcon(nullptr);
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
	if (bIsSkillSlot) URPGSkillTooltip::UpdateFor(*this, {});
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
	if (auto* Overlay = Cast<UTextBlock>(GetWidgetFromName(TEXT("ItemStatusText"))))
	{
		Overlay->SetText(!SlotItemId.IsValid() ? (SlotIndex == UQuickSlotComponent::PotionSlotIndex
			? NSLOCTEXT("ItemQuickSlots", "HealthPlaceholder", "HP") : FText::FromString(TEXT("+")))
			: State == EState::Pending ? NSLOCTEXT("ItemQuickSlots", "Pending", "대기")
			: State == EState::RecoveryRequired ? NSLOCTEXT("ItemQuickSlots", "Recovery", "확인") : FText::GetEmpty());
	}
	SetToolTipText(!VisibleMountId.IsEmpty()
		? NSLOCTEXT("ItemQuickSlots", "MountUse", "탈것 · 단축키 또는 클릭으로 탑승 · 우클릭으로 슬롯 해제")
		: !SlotItemId.IsValid() ? (SlotIndex == UQuickSlotComponent::PotionSlotIndex
		? NSLOCTEXT("ItemQuickSlots", "PotionEmpty", "회복 물약 · 클릭하여 등록\nX 또는 클릭으로 사용 · 우클릭으로 변경/해제")
		: NSLOCTEXT("ItemQuickSlots", "ItemEmpty", "소모품 · 클릭하여 등록\n단축키 또는 클릭으로 사용 · 우클릭으로 변경/해제"))
		: Details.IsEmpty() ? Status : Status.IsEmpty() ? Details
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
		RefreshSkillSlotView(true);
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
		if (SlotItemCountText) SlotItemCountText->SetText(FText::AsNumber(Entry.GetQuantity()));
		RefreshItemIcon(bMatchingView ? View.Icon : TSoftObjectPtr<UTexture2D>());
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

void URPGQuickSlotWidget::RefreshSkillSlotView(const bool bRefreshTooltip)
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

	if (bRefreshTooltip)
	{
		const UQuickSlotComponent* QuickSlots = LinkedQuickSlotComponent.Get();
		const FRPGQuickSlotContent* Content = QuickSlots ? QuickSlots->GetSkillSlotContent(SlotIndex) : nullptr;
		APawn* Pawn = GetOwningPlayerPawn();
		const auto* Skills = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
		const auto* PlayerClass = Skills ? Skills->GetPlayerClassDefinition() : nullptr;
		const FRPGPlayerClassSkill* Skill = PlayerClass && Content && Content->AbilityTag.IsValid()
			? PlayerClass->FindSkill(Content->AbilityTag) : nullptr;
		FRPGSkillSaveData Selection;
		if (Skill && Skill->Definition && Skills->TryGetSkillSaveData(Content->AbilityTag, Selection))
		{
			FRPGSkillRuntimeSpec Spec;
			Skill->Definition->BuildRuntimeSpec(Pawn, Selection, Spec);
			URPGSkillTooltip::UpdateFor(*this,
				FRPGSkillTooltipPresentation::ForSkill(*Skill->Definition, Spec, true, Pawn));
		}
		else
		{
			URPGSkillTooltip::UpdateFor(*this, {});
		}
	}
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
	if (!LinkedQuickSlotComponent.IsValid()) return;
	SkillRefreshElapsed += DeltaTime;
	if (SkillRefreshElapsed >= 0.1f)
	{
		SkillRefreshElapsed = 0.f;
		if (bIsSkillSlot) RefreshSkillSlotView(IsHovered());
		else
		{
			SetInputKeyText(ResolveQuickSlotInputKeyText(LinkedQuickSlotComponent.Get(), false, SlotIndex, InputText));
			RefreshMountIcon();
		}
	}
}

void URPGQuickSlotWidget::RefreshMountIcon()
{
	if (!MountImage) return;
	const FString Current = RPGMountQuickSlots::Read(GetOwningPlayer(), SlotIndex);
	if (VisibleMountId != Current)
	{
		VisibleMountId = Current;
		MountIconLoad.Reset();
		MountImage->SetBrushFromTexture(nullptr);
		const bool bMount = !Current.IsEmpty();
		const auto ItemVisibility = bMount ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible;
		SlotItemImage->SetVisibility(ItemVisibility);
		SlotItemCountText->SetVisibility(ItemVisibility);
		if (auto* Status = GetWidgetFromName(TEXT("ItemStatusText"))) Status->SetVisibility(ItemVisibility);
		MountImage->SetVisibility(bMount ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
		RefreshItemCommandState();
		if (bMount && UAssetManager::IsInitialized())
		{
			auto& Assets = UAssetManager::Get();
			const auto Path = Assets.GetPrimaryAssetPath(
				FPrimaryAssetId(URPGMountDefinition::PrimaryAssetType, FName(*Current)));
			if (Path.IsValid()) MountIconLoad = Assets.GetStreamableManager().RequestAsyncLoad(Path);
		}
	}
	if (!MountIconLoad || !MountIconLoad->HasLoadCompleted()) return;
	const auto Completed = MoveTemp(MountIconLoad);
	if (const auto* Def = Cast<URPGMountDefinition>(Completed->GetLoadedAsset()))
	{
		if (Def->MountId == Current && !Def->Icon.IsNull())
			MountIconLoad = UAssetManager::GetStreamableManager().RequestAsyncLoad(Def->Icon.ToSoftObjectPath());
	}
	else if (auto* Texture = Cast<UTexture2D>(Completed->GetLoadedAsset()))
	{
		MountImage->SetBrushFromTexture(Texture);
	}
}

TSharedRef<SWidget> URPGQuickSlotWidget::RebuildWidget()
{
	if (!bIsSkillSlot && (!WidgetTree || !WidgetTree->RootWidget)) BuildNativeItemCell();
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
	if (!bIsSkillSlot && GetIsEnabled() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FString Mount = RPGMountQuickSlots::Read(GetOwningPlayer(), SlotIndex);
		if (!Mount.IsEmpty())
		{
			CancelPendingItemClick();
			PressedMountId = Mount;
			PressedUserIndex = InMouseEvent.GetUserIndex();
			PressedPointerIndex = InMouseEvent.GetPointerIndex();
			ItemClickScreenOrigin = InMouseEvent.GetScreenSpacePosition();
			return FReply::Handled().CaptureMouse(TakeWidget());
		}
	}
	if (!bIsSkillSlot && GetIsEnabled() && LinkedQuickSlotComponent.IsValid() &&
		(InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton ||
		 (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && !SlotItemId.IsValid())))
	{
		CancelPendingItemClick();
		OpenItemPicker(InMouseEvent.GetScreenSpacePosition());
		return FReply::Handled();
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
	if (!PressedMountId.IsEmpty() && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton &&
		InMouseEvent.GetUserIndex() == PressedUserIndex && InMouseEvent.GetPointerIndex() == PressedPointerIndex)
	{
		const bool bCall = GetIsEnabled() && IsVisible() &&
			InGeometry.IsUnderLocation(InMouseEvent.GetScreenSpacePosition()) &&
			PressedMountId == RPGMountQuickSlots::Read(GetOwningPlayer(), SlotIndex);
		CancelPendingItemClick();
		if (bCall)
			if (auto* PC = Cast<ARPGPlayerController>(GetOwningPlayer())) PC->CallQuickSlotMount(SlotIndex);
		return FReply::Handled();
	}
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
	if ((PressedItemId.IsValid() || !PressedMountId.IsEmpty()) && InMouseEvent.GetUserIndex() == PressedUserIndex &&
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
	const bool bHadItemPress = PressedItemId.IsValid() || !PressedMountId.IsEmpty();
	const int32 UserIndex = PressedUserIndex;
	const uint32 PointerIndex = PressedPointerIndex;
	PressedItemId.Invalidate();
	PressedMountId.Reset();
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

	if (const auto* Library = InOperation ? Cast<URPGRidingCollectionWidget>(InOperation->GetOuter()) : nullptr)
		return Library->AssignHudSlot(InOperation, this, SlotIndex);

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
		Entry.GetQuantity() > 0 && QuickSlots->IsItemAllowedInSlot(SlotIndex, Entry))
	{
		QuickSlots->SetItemSlotById(SlotIndex, Entry.GetItemId());
		RPGMountQuickSlots::Assign(Controller, SlotIndex, {});
		return true;
	}
	return false;
}

void URPGQuickSlotWidget::ConfigureItemSlot(const int32 Index)
{
	bIsSkillSlot = false;
	SlotIndex = Index;
	SetIsFocusable(false);
	SetVisibility(ESlateVisibility::Visible);
}

void URPGQuickSlotWidget::BuildNativeItemCell()
{
	if (!WidgetTree) WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	auto* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ItemCellSize"));
	Size->SetWidthOverride(52.f); Size->SetHeightOverride(52.f);
	WidgetTree->RootWidget = Size;
	auto* Border = WidgetTree->ConstructWidget<UBorder>();
	Border->SetBrushColor(FLinearColor(.36f, .27f, .16f, 1.f)); Border->SetPadding(FMargin(1.f));
	Size->AddChild(Border);
	auto* Inner = WidgetTree->ConstructWidget<UBorder>();
	Inner->SetBrushColor(FLinearColor(.035f, .026f, .021f, .98f)); Inner->SetPadding(FMargin(3.f));
	Border->AddChild(Inner);
	auto* Overlay = WidgetTree->ConstructWidget<UOverlay>(); Inner->AddChild(Overlay);
	SlotItemImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("SlotItemImage"));
	Overlay->AddChildToOverlay(SlotItemImage)->SetPadding(FMargin(3.f));
	SetImageAlpha(SlotItemImage, 0.f);
	if (RPGMountQuickSlots::IsMountSlot(SlotIndex))
	{
		MountImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MountSlotImage"));
		MountImage->SetVisibility(ESlateVisibility::Hidden);
		Overlay->AddChildToOverlay(MountImage)->SetPadding(FMargin(3.f));
	}
	InputKeyText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InputKeyText"));
	InputKeyText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	InputKeyText->SetColorAndOpacity(FSlateColor(FLinearColor(.95f, .82f, .61f)));
	auto* KeySlot = Overlay->AddChildToOverlay(InputKeyText); KeySlot->SetHorizontalAlignment(HAlign_Left); KeySlot->SetVerticalAlignment(VAlign_Top);
	SlotItemCountText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SlotItemCountText"));
	SlotItemCountText->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 12));
	auto* CountSlot = Overlay->AddChildToOverlay(SlotItemCountText); CountSlot->SetHorizontalAlignment(HAlign_Right); CountSlot->SetVerticalAlignment(VAlign_Bottom);
	auto* Status = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ItemStatusText"));
	Status->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
	auto* StatusSlot = Overlay->AddChildToOverlay(Status); StatusSlot->SetHorizontalAlignment(HAlign_Center); StatusSlot->SetVerticalAlignment(VAlign_Center);
	// Decorative children must not intercept slot presses or inventory drops.
	Border->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void URPGQuickSlotWidget::OpenItemPicker(const FVector2D& ScreenPosition)
{
	const auto* PC = GetOwningPlayer();
	const auto* Slots = LinkedQuickSlotComponent.Get();
	const auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
	const auto* Data = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UDataManager>() : nullptr;
	if (!PC || !Slots || Slots->GetOwner() != PC->GetPawn() || !Projection || !Data || !FSlateApplication::IsInitialized()) return;
	const TWeakObjectPtr<URPGQuickSlotWidget> Weak(this);
	const TWeakObjectPtr<UQuickSlotComponent> ExpectedSlots(LinkedQuickSlotComponent);
	const int32 ExpectedIndex = SlotIndex;
	const bool bMountAssigned = !RPGMountQuickSlots::Read(PC, SlotIndex).IsEmpty();
	const auto Assign = [Weak, ExpectedSlots, ExpectedIndex](FGuid Id)
	{
		auto* Self = Weak.Get(); auto* Current = ExpectedSlots.Get();
		if (!Self || !Current || Self->LinkedQuickSlotComponent != ExpectedSlots || Self->SlotIndex != ExpectedIndex
			|| !Self->GetOwningPlayer() || Self->GetOwningPlayerPawn() != Current->GetOwner()) return;
		Current->SetItemSlotById(ExpectedIndex, Id);
		RPGMountQuickSlots::Assign(Self->GetOwningPlayer(), ExpectedIndex, {});
	};
	FMenuBuilder Menu(true, nullptr);
	Menu.BeginSection(NAME_None, NSLOCTEXT("ItemQuickSlots", "Register", "아이템 등록"));
	if (RPGMountQuickSlots::IsMountSlot(SlotIndex))
		Menu.AddMenuEntry(NSLOCTEXT("ItemQuickSlots", "MountCollection", "탈것 보관함"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Weak]
			{
				const auto* Self = Weak.Get();
				const auto* Controller = Self ? Self->GetOwningPlayer() : nullptr;
				const auto* HUD = Controller ? Cast<ARPGHUD>(Controller->GetHUD()) : nullptr;
				if (auto* Bar = HUD ? HUD->GetItemQuickSlotBar() : nullptr) Bar->OpenMountCollection();
			})));
	if (bMountAssigned)
		Menu.AddMenuEntry(NSLOCTEXT("ItemQuickSlots", "ClearMount", "탈것 슬롯 해제"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Weak, ExpectedIndex]
			{
				if (const auto* Self = Weak.Get()) RPGMountQuickSlots::Assign(Self->GetOwningPlayer(), ExpectedIndex, {});
			})));
	else if (SlotItemId.IsValid())
		Menu.AddMenuEntry(NSLOCTEXT("ItemQuickSlots", "Clear", "슬롯 해제"), FText::GetEmpty(), FSlateIcon(),
			FUIAction(FExecuteAction::CreateLambda([Assign] { Assign(FGuid()); })));
	int32 Eligible = 0;
	for (const auto& Entry : Projection->GetProjectedItems())
	{
		FRPGItemDefinitionView View; FRPGItemActionPolicy Policy;
		if (!Slots->IsItemAllowedInSlot(SlotIndex, Entry)
			|| !Data->GetItemActionPolicyCatalog().TryFindActionPolicy(Entry.GetDefinitionId(), Policy)
			|| !Policy.IsValid() || !Policy.IsConsumable() || Policy.DefinitionVersion != Entry.GetDefinitionVersion()
			|| !Data->GetItemDefinitionViewCatalog().TryFindDefinitionView(Entry.GetDefinitionId(), View)
			|| !View.IsValid() || View.Snapshot.DefinitionVersion != Entry.GetDefinitionVersion()) continue;
		++Eligible;
		Menu.AddMenuEntry(FText::Format(NSLOCTEXT("ItemQuickSlots", "ItemQuantity", "{0}  ×{1}"), View.DisplayName, FText::AsNumber(Entry.GetQuantity())),
			View.Description, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([Assign, Id=Entry.GetItemId()] { Assign(Id); })));
	}
	if (!Eligible) Menu.AddMenuEntry(NSLOCTEXT("ItemQuickSlots", "EmptyBag", "등록할 소모품이 없습니다"), FText::GetEmpty(), FSlateIcon(),
		FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
	Menu.EndSection();
	FSlateApplication::Get().PushMenu(TakeWidget(), FWidgetPath(),
		SNew(SBox).WidthOverride(280.f).MaxDesiredHeight(300.f)[SNew(SScrollBox) + SScrollBox::Slot()[Menu.MakeWidget()]],
		ScreenPosition, FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
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
	if (!bIsSkillSlot) RefreshItemCommandState();
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
