// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/URPGSkillListSlot.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"

void UURPGSkillListSlot::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	URPGSkillSlotViewModel* ViewModel = Cast<URPGSkillSlotViewModel>(ListItemObject);
	if (ViewModel)
	{
		// C++ 로직 처리 (필요하다면)
		// ...

		// BP 이벤트 호출 (디자이너가 ViewBinding 등을 확인/디버깅 하거나 추가 연출을 할 수 있도록)
		OnSlotViewModelSet(ViewModel);
	}
}