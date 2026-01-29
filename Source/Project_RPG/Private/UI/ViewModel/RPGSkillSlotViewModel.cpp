// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"

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

void URPGSkillSlotViewModel::RefreshFromSaveData(const FRPGSkillSaveData& Data)
{
	if (SkillLevel != Data.SkillLevel)
	{
		SkillLevel = Data.SkillLevel;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(SkillLevel);
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