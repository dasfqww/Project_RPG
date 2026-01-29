// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillDetailModule.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"

void URPGSkillDetailModule::SetSelectedSkill(URPGSkillSlotViewModel* InSkillSlotVM)
{
	CurrentSkillVM = InSkillSlotVM;

	if (CurrentSkillVM)
	{
		// 뷰모델에서 정의(Definition)나 태그를 가져올 수 있다면 함께 전달
		// 현재 SlotVM 구조상 Definition에 직접 접근하는 public getter가 없으므로 
		// 필요한 데이터는 VM 프로퍼티나 별도 메서드로 전달해야 함.
		// 여기서는 VM 자체를 넘겨 Blueprint에서 처리하도록 함.
		OnSkillSelected(CurrentSkillVM, nullptr); 
	}
	else
	{
		OnNoSkillSelected();
	}
}