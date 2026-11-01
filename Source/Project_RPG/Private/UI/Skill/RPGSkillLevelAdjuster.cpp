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
		TargetSlotViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	if (MainViewModel)
	{
		MainViewModel->RemoveAllFieldValueChangedDelegates(this);
	}

	MainViewModel = InMainVM;
	TargetSlotViewModel = InSlotVM;

	if (TargetSlotViewModel)
	{
		// NextLevelCost 변경 감지 (Native Delegate)
		TargetSlotViewModel->OnNextLevelCostChanged.AddUObject(this, &ThisClass::OnCostChanged);
		TargetSlotViewModel->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::SkillLevel,
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
				this, [this](UObject*, UE::FieldNotification::FFieldId) { UpdateRequireLevelText(); }));
	}
	if (MainViewModel)
	{
		MainViewModel->AddFieldValueChangedDelegate(
			URPGSkillViewModel::FFieldNotificationClassDescriptor::RemainingSP,
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
				this, [this](UObject*, UE::FieldNotification::FFieldId) { UpdateRequireLevelText(); }));
	}
	UpdateRequireLevelText();
}

void URPGSkillLevelAdjuster::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Btn_IncreaseLevel)
	{
		Btn_IncreaseLevel->OnClicked.AddDynamic(this, &ThisClass::OnIncreaseLevelClicked);
	}

	if (Btn_DecreaseLevel)
	{
		Btn_DecreaseLevel->OnClicked.AddDynamic(this, &ThisClass::OnDecreaseLevelClicked);
	}
}

void URPGSkillLevelAdjuster::NativeDestruct()
{
	if (TargetSlotViewModel)
	{
		TargetSlotViewModel->OnNextLevelCostChanged.RemoveAll(this);
		TargetSlotViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	if (MainViewModel)
	{
		MainViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	TargetSlotViewModel = nullptr;
	MainViewModel = nullptr;
	Super::NativeDestruct();
}

void URPGSkillLevelAdjuster::OnCostChanged(int32 NewCost)
{
	UpdateRequireLevelText();
}

void URPGSkillLevelAdjuster::UpdateRequireLevelText()
{
	const int32 Cost = TargetSlotViewModel ? TargetSlotViewModel->NextLevelCost : 0;
	if (RequireLevelText)
	{
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
	if (UCommonTextBlock* Text = Cast<UCommonTextBlock>(GetWidgetFromName(TEXT("CurrentSkillLevelText"))))
	{
		Text->SetText(TargetSlotViewModel ? FText::AsNumber(TargetSlotViewModel->SkillLevel) : FText::GetEmpty());
	}
	if (Btn_IncreaseLevel)
	{
		Btn_IncreaseLevel->SetIsEnabled(MainViewModel && TargetSlotViewModel
			&& Cost > 0 && MainViewModel->RemainingSP >= Cost);
	}
	if (Btn_DecreaseLevel)
	{
		Btn_DecreaseLevel->SetIsEnabled(MainViewModel && TargetSlotViewModel
			&& TargetSlotViewModel->SkillLevel > 1);
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
