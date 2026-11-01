// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGSkillViewModel.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"

URPGSkillViewModel::URPGSkillViewModel()
{
}

void URPGSkillViewModel::BeginDestroy()
{
	UnbindSkillComponent();
	Super::BeginDestroy();
}

void URPGSkillViewModel::InitializeSkillData(URPGPlayerSkillComponent* InSkillComponent, const TArray<URPGSkillDefinition*>& /*InAllSkills*/)
{
	if (SkillComponent != InSkillComponent)
	{
		UnbindSkillComponent();
		SkillComponent = InSkillComponent;
	}
	if (SkillComponent)
	{
		SkillComponent->OnSkillDataChanged.AddUniqueDynamic(
			this,
			&ThisClass::HandleSkillDataChanged);
	}
	// List/detail widgets may still retain an old slot while entries are being
	// replaced. It must no longer be able to issue commands to the old owner.
	for (URPGSkillSlotViewModel* Slot : SkillSlots)
	{
		if (Slot) Slot->SetOwnerComponent(nullptr);
	}
	SkillSlots.Empty();
	PresentedClassDefinition = SkillComponent ? SkillComponent->GetPlayerClassDefinition() : nullptr;

	// The catalog discovers assets; only the verified class chooses the exact definitions to present.
	TArray<URPGSkillDefinition*> ClassSkills;
	if (const auto* Definition = PresentedClassDefinition.Get())
		for (const FRPGPlayerClassSkill& Entry : Definition->Skills) ClassSkills.Add(Entry.Definition);
	for (URPGSkillDefinition* SkillDef : ClassSkills)
	{
		if (!SkillComponent || !SkillDef || !SkillComponent->IsSkillInCurrentClass(SkillDef->SkillTag)) continue;

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
	int32 NewTotalSP = SkillComponent ? SkillComponent->GetTotalSP() : 0;
	if (TotalSP != NewTotalSP)
	{
		TotalSP = NewTotalSP;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(TotalSP);
	}

	// SP 정보 갱신
	int32 NewRemainingSP = SkillComponent ? SkillComponent->GetRemainingSP() : 0;
	if (RemainingSP != NewRemainingSP)
	{
		RemainingSP = NewRemainingSP;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(RemainingSP);
	}
	if (!SkillComponent) return;

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
	URPGPlayerClassDefinition* CurrentClass = SkillComponent ? SkillComponent->GetPlayerClassDefinition() : nullptr;
	if (PresentedClassDefinition.Get() != CurrentClass)
	{
		TArray<URPGSkillDefinition*> ClassSkills;
		if (CurrentClass)
			for (const FRPGPlayerClassSkill& Entry : CurrentClass->Skills) ClassSkills.Add(Entry.Definition);
		InitializeSkillData(SkillComponent, ClassSkills);
		return;
	}
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
