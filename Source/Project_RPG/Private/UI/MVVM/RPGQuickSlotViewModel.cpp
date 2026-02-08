// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGQuickSlotViewModel.h"
#include "Item/RPGItemBase.h"
#include "Component/UI/QuickSlotComponent.h"
#include "RPGFunctionLibrary.h"
#include "RPGGameplayTags.h"

void URPGQuickSlotViewModel::Initialize(int32 InSlotIndex, UQuickSlotComponent* InComponent, bool bIsSkillSlot)
{
	if (!InComponent) return;

	TargetSlotIndex = InSlotIndex;
	LinkedComponent = InComponent;
	bIsSkillSlotViewModel = bIsSkillSlot;

	// 타입에 맞는 델리게이트 구독
	if (bIsSkillSlotViewModel)
	{
		InComponent->OnSkillSlotChanged.AddDynamic(this, &URPGQuickSlotViewModel::HandleSlotChanged);
		
		// 스킬 슬롯 키 텍스트 설정 (Q, E, R, F, 1, 2, 3, 4)
		TArray<FText> SkillKeys = { FText::FromString("Q"), FText::FromString("E"), FText::FromString("R"), FText::FromString("F"), 
									 FText::FromString("1"), FText::FromString("2"), FText::FromString("3"), FText::FromString("4") };
		if (SkillKeys.IsValidIndex(InSlotIndex)) SetInputKeyText(SkillKeys[InSlotIndex]);

		// 초기 데이터 반영
		if (const FRPGQuickSlotContent* Content = InComponent->GetSkillSlotContent(InSlotIndex))
		{
			UpdateFromContent(*Content);
		}
	}
	else
	{
		InComponent->OnItemSlotChanged.AddDynamic(this, &URPGQuickSlotViewModel::HandleSlotChanged);
		InComponent->OnQuickSlotQuantityChanged.AddDynamic(this, &URPGQuickSlotViewModel::HandleQuantityChanged);

		// 아이템 슬롯 키 텍스트 설정 (F1 ~ F8)
		SetInputKeyText(FText::FromString(FString::Printf(TEXT("F%d"), InSlotIndex + 1)));

		// 초기 데이터 반영
		if (const FRPGQuickSlotContent* Content = InComponent->GetItemSlotContent(InSlotIndex))
		{
			UpdateFromContent(*Content);
		}
	}
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
	if (InContent.IsEmpty())
	{
		SetItemIcon(nullptr);
		SetQuantityText(FText::GetEmpty());
		SetIsSlotActive(false);
		return;
	}

	if (InContent.Item)
	{
		// 아이템인 경우
		// SetItemIcon(InContent.Item->GetIcon()); // 아이템 아이콘 설정
		SetQuantityText(FText::AsNumber(InContent.Item->GetTotalQuantity()));
		SetIsSlotActive(true);
	}
	else if (InContent.AbilityTag.IsValid())
	{
		// 스킬인 경우
		// 스킬 태그를 통해 아이콘을 가져오는 로직 (데이터 에셋 등에서 조회 필요)
		// UTexture2D* SkillIcon = URPGFunctionLibrary::GetIconForSkillTag(GetWorld(), InContent.AbilityTag);
		// SetItemIcon(SkillIcon);
		
		SetQuantityText(FText::GetEmpty()); // 스킬은 개수 표시 안함
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