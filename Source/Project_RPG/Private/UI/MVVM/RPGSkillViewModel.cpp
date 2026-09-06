// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGSkillViewModel.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"

URPGSkillViewModel::URPGSkillViewModel()
{
}

void URPGSkillViewModel::BeginDestroy()
{
	UnbindSkillComponent();
	Super::BeginDestroy();
}

void URPGSkillViewModel::InitializeSkillData(URPGPlayerSkillComponent* InSkillComponent, const TArray<URPGSkillDefinition*>& InAllSkills)
{
	if (!InSkillComponent)
	{
		return;
	}

	if (SkillComponent != InSkillComponent)
	{
		UnbindSkillComponent();
		SkillComponent = InSkillComponent;
	}
	SkillComponent->OnSkillDataChanged.AddUniqueDynamic(
		this,
		&ThisClass::HandleSkillDataChanged);
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
	if (SkillComponent)
	{
		SkillComponent->TryLevelUpSkill(SkillTag);
	}
}

void URPGSkillViewModel::RequestSkillLevelDown(FGameplayTag SkillTag)
{
	if (SkillComponent)
	{
		SkillComponent->TryLevelDownSkill(SkillTag);
	}
}

void URPGSkillViewModel::RequestSkillLevelMax(FGameplayTag SkillTag)
{
	if (SkillComponent)
	{
		const URPGSkillDefinition* Definition =
			FindSkillDefinition(SkillTag);
		if (Definition)
		{
			SkillComponent->LevelUpToMax(
				SkillTag,
				Definition->MaxSkillLevel);
		}
	}
}

void URPGSkillViewModel::RequestSkillLevelMin(FGameplayTag SkillTag)
{
	if (SkillComponent)
	{
		SkillComponent->ResetSkillLevel(SkillTag);
	}
}

void URPGSkillViewModel::RefreshSkillData()
{
	if (!SkillComponent) return;

	int32 NewTotalSP = SkillComponent->GetTotalSP();
	if (TotalSP != NewTotalSP)
	{
		TotalSP = NewTotalSP;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(TotalSP);
	}

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

void URPGSkillViewModel::HandleSkillDataChanged(
	const FGameplayTag SkillTag)
{
	RefreshSkillData();
}

void URPGSkillViewModel::UnbindSkillComponent()
{
	if (SkillComponent)
	{
		SkillComponent->OnSkillDataChanged.RemoveDynamic(
			this,
			&ThisClass::HandleSkillDataChanged);
		SkillComponent = nullptr;
	}
}

const URPGSkillDefinition* URPGSkillViewModel::FindSkillDefinition(
	const FGameplayTag SkillTag) const
{
	for (const URPGSkillSlotViewModel* Slot : SkillSlots)
	{
		if (Slot && Slot->GetSkillTag() == SkillTag)
		{
			return Slot->GetSkillDefinition();
		}
	}
	return nullptr;
}
