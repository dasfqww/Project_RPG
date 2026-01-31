// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillLevelAdjuster.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "UI/Button/RPGCustomButton.h"
#include "CommonTextBlock.h"
#include "Framework/Application/SlateApplication.h"

void URPGSkillLevelAdjuster::InitializeAdjuster(URPGSkillViewModel* InMainVM, URPGSkillSlotViewModel* InSlotVM)
{
	// 기존 바인딩 해제
	if (TargetSlotViewModel)
	{
		TargetSlotViewModel->OnNextLevelCostChanged.RemoveAll(this);
	}

	MainViewModel = InMainVM;
	TargetSlotViewModel = InSlotVM;

	if (TargetSlotViewModel)
	{
		// NextLevelCost 변경 감지 (Native Delegate)
		TargetSlotViewModel->OnNextLevelCostChanged.AddUObject(this, &ThisClass::OnCostChanged);

		UpdateRequireLevelText();
	}
}

void URPGSkillLevelAdjuster::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	/*if (Btn_IncreaseLevel)
	{
		Btn_IncreaseLevel->OnClicked().AddUObject(this, &ThisClass::OnIncreaseLevelClicked);
	}

	if (Btn_DecreaseLevel)
	{
		Btn_DecreaseLevel->OnClicked().AddUObject(this, &ThisClass::OnDecreaseLevelClicked);
	}*/
}

void URPGSkillLevelAdjuster::NativeDestruct()
{
	if (TargetSlotViewModel)
	{
		TargetSlotViewModel->OnNextLevelCostChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void URPGSkillLevelAdjuster::OnCostChanged(int32 NewCost)
{
	UpdateRequireLevelText();
}

void URPGSkillLevelAdjuster::UpdateRequireLevelText()
{
	if (RequireLevelText && TargetSlotViewModel)
	{
		int32 Cost = TargetSlotViewModel->NextLevelCost;
		if (Cost > 0)
		{
			RequireLevelText->SetText(FText::AsNumber(Cost));
			RequireLevelText->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			// 비용이 0이면(만렙 등) 숨김
			RequireLevelText->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void URPGSkillLevelAdjuster::OnIncreaseLevelClicked()
{
	if (MainViewModel && TargetSlotViewModel)
	{
		FGameplayTag Tag = TargetSlotViewModel->GetSkillTag();
		if (Tag.IsValid())
		{
			if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
			{
				MainViewModel->RequestSkillLevelMax(Tag);
			}
			else
			{
				MainViewModel->RequestSkillLevelUp(Tag);
			}
		}
	}
}

void URPGSkillLevelAdjuster::OnDecreaseLevelClicked()
{
	if (MainViewModel && TargetSlotViewModel)
	{
		FGameplayTag Tag = TargetSlotViewModel->GetSkillTag();
		if (Tag.IsValid())
		{
			if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
			{
				MainViewModel->RequestSkillLevelMin(Tag);
			}
			else
			{
				MainViewModel->RequestSkillLevelDown(Tag);
			}
		}
	}
}