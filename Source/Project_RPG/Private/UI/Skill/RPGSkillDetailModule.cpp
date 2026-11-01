// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillDetailModule.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "UI/Skill/RPGTripodBoard.h"

void URPGSkillDetailModule::SetSelectedSkill(URPGSkillSlotViewModel* InSkillSlotVM)
{
	CurrentSkillVM = InSkillSlotVM;
	if (URPGTripodBoard* Board = Cast<URPGTripodBoard>(GetRootWidget()))
	{
		Board->InitializeBoard(CurrentSkillVM);
	}

	if (CurrentSkillVM)
	{
		// 뷰모델에서 정의(Definition)나 태그를 가져올 수 있다면 함께 전달
		OnSkillSelected(CurrentSkillVM, CurrentSkillVM->GetSkillDefinition()); 
	}
	else
	{
		OnNoSkillSelected();
	}
}
