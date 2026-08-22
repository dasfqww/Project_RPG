// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "Item/RPGItemBase.h"
#include "Item/Fragment/RPGItemFragment.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Controller/RPGPlayerController.h"
#include "RPGGameplayTags.h"

namespace
{
FText ResolveInputKeyText(
	const UQuickSlotComponent* Component, bool bIsSkillSlot, int32 SlotIndex)
{
	const TCHAR* SlotType = bIsSkillSlot ? TEXT("Skill") : TEXT("Item");
	const FString TagName = FString::Printf(
		TEXT("InputTag.Quick%s.%d"),
		SlotType,
		SlotIndex + 1);
	const FGameplayTag InputTag =
		FGameplayTag::RequestGameplayTag(FName(*TagName), false);

	const APawn* OwnerPawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
	const ARPGPlayerController* PlayerController =
		OwnerPawn ? Cast<ARPGPlayerController>(OwnerPawn->GetController()) : nullptr;
	if (PlayerController && InputTag.IsValid())
	{
		const FKey Key = PlayerController->GetCurrentKeyForTag(InputTag);
		if (Key.IsValid())
		{
			return Key.GetDisplayName(false);
		}
	}

	if (!bIsSkillSlot)
	{
		return FText::Format(
			NSLOCTEXT("RPGQuickSlot", "FunctionKeyFormat", "F{0}"),
			FText::AsNumber(SlotIndex + 1));
	}

	static const TCHAR* SkillKeyFallbacks[] = {
		TEXT("Q"), TEXT("E"), TEXT("R"), TEXT("F"),
		TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4")
	};
	return SlotIndex < UE_ARRAY_COUNT(SkillKeyFallbacks)
		? FText::FromString(SkillKeyFallbacks[SlotIndex])
		: FText::AsNumber(SlotIndex + 1);
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
		InComponent->OnQuickSlotQuantityChanged.AddUniqueDynamic(
			this, &URPGQuickSlotViewModel::HandleQuantityChanged);

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
	if (UQuickSlotComponent* Component = LinkedComponent.Get())
	{
		Component->OnSkillSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
		Component->OnItemSlotChanged.RemoveDynamic(
			this, &URPGQuickSlotViewModel::HandleSlotChanged);
		Component->OnQuickSlotQuantityChanged.RemoveDynamic(
			this, &URPGQuickSlotViewModel::HandleQuantityChanged);
	}

	LinkedComponent.Reset();
}

void URPGQuickSlotViewModel::HandleSlotChanged(int32 SlotIndex, const FRPGQuickSlotContent& NewContent)
{
	if (SlotIndex == TargetSlotIndex)
	{
		UpdateFromContent(NewContent);
	}
}

void URPGQuickSlotViewModel::HandleQuantityChanged(URPGItemBase* Item, int32 NewQuantity)
{
	if (bIsSkillSlotViewModel) return; // 스킬 뷰모델은 수량 변화 무시

	if (LinkedComponent.IsValid())
	{
		const FRPGQuickSlotContent* CurrentContent = LinkedComponent->GetItemSlotContent(TargetSlotIndex);
		if (CurrentContent && CurrentContent->Item == Item)
		{
			if (NewQuantity > 0)
			{
				SetQuantityText(FText::AsNumber(NewQuantity));
			}
			else
			{
				UpdateFromContent(FRPGQuickSlotContent()); // 비우기
			}
		}
	}
}

void URPGQuickSlotViewModel::UpdateFromContent(const FRPGQuickSlotContent& InContent)
{
	SetItemIcon(nullptr);

	if (InContent.IsEmpty())
	{
		SetQuantityText(FText::GetEmpty());
		SetIsSlotActive(false);
		return;
	}

	if (InContent.Item)
	{
		// 아이템인 경우
		if (const FImageFragment* ImageFragment =
			GetFragment<FImageFragment>(
				InContent.Item, RPGGameplayTags::Fragment_IconFragment))
		{
			SetItemIcon(ImageFragment->GetIcon());
		}

		SetQuantityText(FText::AsNumber(InContent.Item->GetTotalQuantity()));
		SetIsSlotActive(true);
	}
	else if (InContent.AbilityTag.IsValid())
	{
		SetItemIcon(
			LinkedComponent.IsValid()
				? LinkedComponent->GetSkillIcon(InContent.AbilityTag)
				: nullptr);
		SetQuantityText(FText::GetEmpty());
		SetIsSlotActive(true);
	}
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
