// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillLevelAdjuster.h"
#include "UI/MVVM/RPGSkillViewModel.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"

void URPGSkillLevelAdjuster::InitializeAdjuster(URPGSkillViewModel* InMainVM, URPGSkillSlotViewModel* InSlotVM)
{
	MainViewModel = InMainVM;
	TargetSlotViewModel = InSlotVM;
}

void URPGSkillLevelAdjuster::OnIncreaseLevelClicked()
{
	if (MainViewModel && TargetSlotViewModel)
	{
		FGameplayTag Tag = TargetSlotViewModel->GetSkillTag();
		if (Tag.IsValid())
		{
			MainViewModel->RequestSkillLevelUp(Tag);
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
			MainViewModel->RequestSkillLevelDown(Tag);
		}
	}
}