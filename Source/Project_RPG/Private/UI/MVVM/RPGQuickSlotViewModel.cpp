// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Manager/DataManager.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "RPGGameplayTags.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Skill/QuickSlot/RPGSkillQuickSlotServices.h"

namespace
{
FText ResolveInputKeyText(
	const UQuickSlotComponent* Component, bool bIsSkillSlot, int32 SlotIndex)
{
	const APawn* OwnerPawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
	return FRPGQuickSlotInputBindings::ReadKeyLabel(OwnerPawn ? Cast<APlayerController>(OwnerPawn->GetController()) : nullptr,
		bIsSkillSlot, SlotIndex);
}
}

void URPGQuickSlotViewModel::Initialize(int32 InSlotIndex, UQuickSlotComponent* InComponent, bool bIsSkillSlot)
{
	UnbindFromComponent();

	const int32 SlotCount = InComponent
		? (bIsSkillSlot ? InComponent->GetMaxSkillSlots() : InComponent->GetMaxItemSlots())
		: 0;
	if (!InComponent || InSlotIndex < 0 || InSlotIndex >= SlotCount)
	{
		TargetSlotIndex = INDEX_NONE;
		SetInputKeyText(FText::GetEmpty());
		UpdateFromContent(FRPGQuickSlotContent());
		return;
	}

	TargetSlotIndex = InSlotIndex;
	LinkedComponent = InComponent;
	bIsSkillSlotViewModel = bIsSkillSlot;

	// 타입에 맞는 델리게이트 구독
	if (bIsSkillSlotViewModel)
	{
		InComponent->OnSkillSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
		SetInputKeyText(ResolveInputKeyText(
			InComponent, bIsSkillSlotViewModel, InSlotIndex));

		// 초기 데이터 반영
		if (const FRPGQuickSlotContent* Content = InComponent->GetSkillSlotContent(InSlotIndex))
		{
			UpdateFromContent(*Content);
		}
	}
	else
	{
		InComponent->OnItemSlotChanged.AddUniqueDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
		SetInputKeyText(ResolveInputKeyText(
			InComponent, bIsSkillSlotViewModel, InSlotIndex));

		// 초기 데이터 반영
		if (const FRPGQuickSlotContent* Content = InComponent->GetItemSlotContent(InSlotIndex))
		{
			UpdateFromContent(*Content);
		}
	}
}

void URPGQuickSlotViewModel::BeginDestroy()
{
	UnbindFromComponent();
	Super::BeginDestroy();
}

void URPGQuickSlotViewModel::UnbindFromComponent()
{
	CancelItemIconLoad();
	if (UQuickSlotComponent* Component = LinkedComponent.Get())
	{
		Component->OnSkillSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
		Component->OnItemSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
	}

	LinkedComponent.Reset();
}

void URPGQuickSlotViewModel::CancelItemIconLoad()
{
	if (ItemIconLoadHandle)
	{
		ItemIconLoadHandle->CancelHandle();
		ItemIconLoadHandle.Reset();
	}
	ItemIconReference.Reset();
}

void URPGQuickSlotViewModel::RefreshItemIcon(const TSoftObjectPtr<UTexture2D>& Icon)
{
	if (Icon == ItemIconReference && ItemIconLoadHandle) return;
	CancelItemIconLoad();
	ItemIconReference = Icon;
	UTexture2D* Loaded = Icon.Get();
	if (!Loaded && !Icon.IsNull())
	{
		const TWeakObjectPtr<URPGQuickSlotViewModel> WeakThis(this);
		ItemIconLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Icon.ToSoftObjectPath(),
			FStreamableDelegate::CreateLambda([WeakThis, Icon]()
			{
				if (auto* Self = WeakThis.Get(); Self && Self->LinkedComponent.IsValid() &&
					Self->ItemIconReference == Icon)
				{
					// Keep the handle alive while publishing the texture to the bound view.
					const auto Completed = MoveTemp(Self->ItemIconLoadHandle);
					Self->SetItemIcon(Icon.Get());
				}
			}));
	}
	SetItemIcon(Loaded);
}

void URPGQuickSlotViewModel::HandleSlotChanged(int32 SlotIndex, const FRPGQuickSlotContent& NewContent)
{
	if (SlotIndex == TargetSlotIndex)
	{
		UpdateFromContent(NewContent);
	}
}

void URPGQuickSlotViewModel::UpdateFromContent(const FRPGQuickSlotContent& InContent)
{
	CooldownRemaining = CooldownDuration = 0.0f;
	if (!InContent.ItemId.IsValid() && InContent.AbilityTag.IsValid() && bIsSkillSlotViewModel)
	{
		CancelItemIconLoad();
		const FRPGSkillQuickSlotPresentation Skill = LinkedComponent.IsValid()
			? LinkedComponent->ReadSkillPresentation(InContent.AbilityTag) : FRPGSkillQuickSlotPresentation();
		CooldownRemaining = Skill.CooldownRemaining;
		CooldownDuration = Skill.CooldownDuration;
		SetItemIcon(Skill.Icon);
		SetIsSlotActive(Skill.bIsGranted);
		FNumberFormattingOptions Format;
		Format.SetUseGrouping(false)
			.SetRoundingMode(ERoundingMode::ToPositiveInfinity)
			.SetMinimumFractionalDigits(0)
			.SetMaximumFractionalDigits(CooldownRemaining < 1.0f ? 1 : 0);
		SetQuantityText(Skill.bIsGranted && GetCooldownFraction() > 0.0f
			? FText::AsNumber(CooldownRemaining, &Format) : FText::GetEmpty());
		return;
	}
	if (InContent.ItemId.IsValid())
	{
		FRPGInventoryProjectionEntry Entry;
		UQuickSlotComponent* Component = LinkedComponent.Get();
		if (Component && Component->FindAssignedItem(TargetSlotIndex, Entry))
		{
			UWorld* World = Component->GetWorld();
			UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
			UDataManager* DataManager = Instance ? Instance->GetSubsystem<UDataManager>() : nullptr;
			FRPGItemDefinitionView View;
			const bool bHasView = DataManager &&
				DataManager->GetItemDefinitionViewCatalog().TryFindDefinitionView(Entry.GetDefinitionId(), View);
			RefreshItemIcon(bHasView ? View.Icon : TSoftObjectPtr<UTexture2D>());
			SetQuantityText(FText::AsNumber(Entry.GetQuantity()));
			SetIsSlotActive(true);
			return;
		}
	}

	CancelItemIconLoad();
	UQuickSlotComponent* Component = LinkedComponent.Get();
	const bool bLegacyAbilitySlot = !InContent.ItemId.IsValid() && InContent.AbilityTag.IsValid();
	SetItemIcon(bLegacyAbilitySlot && Component ? Component->GetSkillIcon(InContent.AbilityTag) : nullptr);
	SetQuantityText(FText::GetEmpty());
	SetIsSlotActive(bLegacyAbilitySlot);
}

void URPGQuickSlotViewModel::RefreshSkillState()
{
	UQuickSlotComponent* Component = LinkedComponent.Get();
	if (!bIsSkillSlotViewModel || !Component) return;
	SetInputKeyText(ResolveInputKeyText(Component, true, TargetSlotIndex));
	const FRPGQuickSlotContent* Content = Component->GetSkillSlotContent(TargetSlotIndex);
	UpdateFromContent(Content ? *Content : FRPGQuickSlotContent());
}

void URPGQuickSlotViewModel::SetItemIcon(UTexture2D* InIcon)
{
	UE_MVVM_SET_PROPERTY_VALUE(ItemIcon, InIcon);
}

void URPGQuickSlotViewModel::SetQuantityText(FText InText)
{
	UE_MVVM_SET_PROPERTY_VALUE(QuantityText, InText);
}

void URPGQuickSlotViewModel::SetIsSlotActive(bool bInActive)
{
	UE_MVVM_SET_PROPERTY_VALUE(bIsSlotActive, bInActive);
}

void URPGQuickSlotViewModel::SetInputKeyText(FText InText)
{
	UE_MVVM_SET_PROPERTY_VALUE(InputKeyText, InText);
}
