// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillListModule.h"
#include "Components/ListView.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"

void URPGSkillListModule::NativeConstruct()
{
	Super::NativeConstruct();

	if (SkillListView)
	{
		// 리스트 뷰의 선택 변경 이벤트 구독
		SkillListView->OnItemSelectionChanged().AddUObject(this, &URPGSkillListModule::HandleItemSelectionChanged);
	}
}

void URPGSkillListModule::InitSkillList(const TArray<URPGSkillSlotViewModel*>& SkillSlots)
{
	if (SkillListView)
	{
		SkillListView->SetListItems(SkillSlots);
	}
}

void URPGSkillListModule::HandleItemSelectionChanged(UObject* Item)
{
	URPGSkillSlotViewModel* SelectedVM = Cast<URPGSkillSlotViewModel>(Item);
	if (SelectedVM)
	{
		OnSkillSelected.Broadcast(SelectedVM);
	}
}