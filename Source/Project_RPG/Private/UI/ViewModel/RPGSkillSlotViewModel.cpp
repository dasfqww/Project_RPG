// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"

void URPGSkillSlotViewModel::SetSkillDefinition(URPGSkillDefinition* InDefinition)
{
	SkillDefinition = InDefinition;
	if (SkillDefinition)
	{
		// Definition에서 데이터 추출 (DataTable 연동 로직 포함)
		SkillName = SkillDefinition->SkillName;
		
		// Icon의 경우 Context(캐릭터 상태)에 따라 다를 수 있지만, 
		// 스킬창에서는 보통 기본 아이콘을 보여줍니다.
		SkillIcon = SkillDefinition->SkillIcon;

		// 데이터 변경 알림 (MVVM 핵심)
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SkillName);
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SkillIcon);
	}
}

void URPGSkillSlotViewModel::SetOwnerComponent(URPGPlayerSkillComponent* InComp)
{
	OwnerSkillComponent = InComp;
}

void URPGSkillSlotViewModel::RefreshFromSaveData(const FRPGSkillSaveData& Data)
{
	bool bLevelChanged = (SkillLevel != Data.SkillLevel);

	if (bLevelChanged)
	{
		SkillLevel = Data.SkillLevel;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SkillLevel);
	}

	if (CurrentTripodIndices != Data.SelectedTripodIndices)
	{
		CurrentTripodIndices = Data.SelectedTripodIndices;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(CurrentTripodIndices);
		OnTripodIndicesChanged.Broadcast(CurrentTripodIndices);
	}

	// 다음 레벨업 비용 계산
	if (OwnerSkillComponent.IsValid())
	{
		int32 NewCost = OwnerSkillComponent->GetRequiredSPForLevel(SkillLevel + 1);
		
		// 만약 최대 레벨이라면 비용을 0으로 표시하거나 숨김 처리
		if (SkillDefinition && SkillLevel >= SkillDefinition->MaxSkillLevel)
		{
			NewCost = 0;
		}

		if (NextLevelCost != NewCost)
		{
			NextLevelCost = NewCost;
			UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(NextLevelCost);
			OnNextLevelCostChanged.Broadcast(NextLevelCost);
		}
	}
}

FGameplayTag URPGSkillSlotViewModel::GetSkillTag() const
{
	if (SkillDefinition)
	{
		return SkillDefinition->SkillTag;
	}
	return FGameplayTag();
}

void URPGSkillSlotViewModel::RequestTripodSelection(int32 Tier, int32 OptionIndex)
{
	if (OwnerSkillComponent.IsValid() && SkillDefinition)
	{
		FGameplayTag SkillTag = SkillDefinition->SkillTag;
		if (OwnerSkillComponent->SelectTripod(SkillTag, Tier, OptionIndex))
		{
			// 성공 시 데이터 갱신
			FRPGSkillSaveData NewData = OwnerSkillComponent->GetSkillSaveData(SkillTag);
			RefreshFromSaveData(NewData);
		}
	}
}

bool URPGSkillSlotViewModel::IsTripodSelected(int32 Tier, int32 OptionIndex) const
{
	if (CurrentTripodIndices.IsValidIndex(Tier))
	{
		return CurrentTripodIndices[Tier] == OptionIndex;
	}
	return false;
}
