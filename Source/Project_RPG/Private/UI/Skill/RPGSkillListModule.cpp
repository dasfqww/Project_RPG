// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillListModule.h"
#include "CommonListView.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"

void URPGSkillListModule::NativeConstruct()
{
	Super::NativeConstruct();

	if (SkillListView)
	{
		// 리스트 뷰의 선택 변경 이벤트 구독
		SkillListView->OnItemSelectionChanged().RemoveAll(this);
		SkillListView->OnItemSelectionChanged().AddUObject(this, &URPGSkillListModule::HandleItemSelectionChanged);
	}
}

void URPGSkillListModule::InitSkillList(const TArray<URPGSkillSlotViewModel*>& SkillSlots)
{
	if (SkillListView)
	{
		SkillListView->SetListItems(SkillSlots);
		if (!SkillSlots.IsEmpty())
		{
			SkillListView->SetSelectedItem(SkillSlots[0]);
		}
		else
		{
			// An already-empty list need not emit a selection change itself.
			OnSkillSelected.Broadcast(nullptr);
		}
	}
}

void URPGSkillListModule::HandleItemSelectionChanged(UObject* Item)
{
	// Clearing selection must also clear the detail panel and its subscriptions.
	OnSkillSelected.Broadcast(Cast<URPGSkillSlotViewModel>(Item));
}
