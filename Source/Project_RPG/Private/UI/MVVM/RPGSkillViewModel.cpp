// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGSkillViewModel.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"

URPGSkillViewModel::URPGSkillViewModel()
{
}

void URPGSkillViewModel::InitializeSkillData(URPGPlayerSkillComponent* InSkillComponent, const TArray<URPGSkillDefinition*>& InAllSkills)
{
	if (!InSkillComponent)
	{
		return;
	}

	SkillComponent = InSkillComponent;
	SkillSlots.Empty();

	// 1. 전달받은 모든 스킬 정의(Definition)를 순회하며 슬롯 VM 생성
	for (URPGSkillDefinition* SkillDef : InAllSkills)
	{
		if (!SkillDef) continue;

		URPGSkillSlotViewModel* NewSlotVM = NewObject<URPGSkillSlotViewModel>(this);
		NewSlotVM->SetSkillDefinition(SkillDef);
		NewSlotVM->SetOwnerComponent(SkillComponent);
		
		// 초기 상태 동기화 (레벨 등)
		FRPGSkillSaveData SaveData = SkillComponent->GetSkillSaveData(SkillDef->SkillTag);
		NewSlotVM->RefreshFromSaveData(SaveData);

		SkillSlots.Add(NewSlotVM);
	}

	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SkillSlots);

	// 2. 전체 포인트 정보 갱신
	RefreshSkillData();
}

void URPGSkillViewModel::RequestSkillLevelUp(FGameplayTag SkillTag)
{
	if (SkillComponent && SkillComponent->TryLevelUpSkill(SkillTag))
	{
		RefreshSkillData();
	}
}

void URPGSkillViewModel::RequestSkillLevelDown(FGameplayTag SkillTag)
{
	if (SkillComponent && SkillComponent->TryLevelDownSkill(SkillTag))
	{
		RefreshSkillData();
	}
}

void URPGSkillViewModel::RequestSkillLevelMax(FGameplayTag SkillTag)
{
	if (SkillComponent)
	{
		bool bChanged = false;
		while (SkillComponent->TryLevelUpSkill(SkillTag))
		{
			bChanged = true;
		}

		if (bChanged)
		{
			RefreshSkillData();
		}
	}
}

void URPGSkillViewModel::RequestSkillLevelMin(FGameplayTag SkillTag)
{
	if (SkillComponent)
	{
		bool bChanged = false;
		while (SkillComponent->TryLevelDownSkill(SkillTag))
		{
			bChanged = true;
		}

		if (bChanged)
		{
			RefreshSkillData();
		}
	}
}

void URPGSkillViewModel::RefreshSkillData()
{
	if (!SkillComponent) return;

	// SP 정보 갱신
	int32 NewRemainingSP = SkillComponent->GetRemainingSP();
	if (RemainingSP != NewRemainingSP)
	{
		RemainingSP = NewRemainingSP;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RemainingSP);
	}

	// 각 슬롯의 상태(레벨 등)도 갱신
	for (URPGSkillSlotViewModel* SlotVM : SkillSlots)
	{
		if (!SlotVM) continue;

		FGameplayTag Tag = SlotVM->GetSkillTag();
		if (Tag.IsValid())
		{
			FRPGSkillSaveData Data = SkillComponent->GetSkillSaveData(Tag);
			SlotVM->RefreshFromSaveData(Data);
		}
	}
}