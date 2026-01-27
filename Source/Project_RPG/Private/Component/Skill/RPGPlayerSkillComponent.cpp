// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "RPGDebugHelper.h"

URPGPlayerSkillComponent::URPGPlayerSkillComponent()
{
}

bool URPGPlayerSkillComponent::TryLevelUpSkill(FGameplayTag SkillTag)
{
	FRPGSkillSaveData& Data = SkillDataMap.FindOrAdd(SkillTag);

	if (Data.SkillLevel >= 12) return false; // 최대 레벨 제한

	int32 NextLevel = Data.SkillLevel + 1;
	int32 Cost = GetRequiredSPForLevel(NextLevel);

	if (GetRemainingSP() >= Cost)
	{
		Data.SkillLevel = NextLevel;
		UsedSP += Cost;
		Debug::Print(SkillTag.ToString() + TEXT(" Level Up! Current: "), Data.SkillLevel);
		return true;
	}

	Debug::Print(TEXT("Not enough SP for Level "), NextLevel);
	return false;
}

bool URPGPlayerSkillComponent::TryLevelDownSkill(FGameplayTag SkillTag)
{
	if (!SkillDataMap.Contains(SkillTag)) return false;

	FRPGSkillSaveData& Data = SkillDataMap[SkillTag];
	if (Data.SkillLevel <= 1) return false;

	int32 Refund = GetRequiredSPForLevel(Data.SkillLevel);
	Data.SkillLevel--;
	UsedSP -= Refund;

	// 레벨이 낮아지면 해당 티어의 트라이포드 선택도 취소해야 함 (로아 규칙)
	if (Data.SkillLevel < 10) Data.SelectedTripodIndices[2] = -1;
	if (Data.SkillLevel < 7)  Data.SelectedTripodIndices[1] = -1;
	if (Data.SkillLevel < 4)  Data.SelectedTripodIndices[0] = -1;

	return true;
}

void URPGPlayerSkillComponent::LevelUpToMax(FGameplayTag SkillTag, int32 TargetGoalLevel)
{
	if (!SkillDataMap.Contains(SkillTag))
	{
		// 데이터가 없으면 새로 생성해서 시작
		SkillDataMap.Add(SkillTag, FRPGSkillSaveData());
	}

	FRPGSkillSaveData& Data = SkillDataMap[SkillTag];
	int32 CurrentLevel = Data.SkillLevel;

	// 1. 현재 레벨이 목표보다 낮으면 -> 레벨업 시도
	if (CurrentLevel < TargetGoalLevel)
	{
		for (int32 i = CurrentLevel; i < TargetGoalLevel; ++i)
		{
			if (!TryLevelUpSkill(SkillTag)) break; // SP 부족 시 중단
		}
	}
	// 2. 현재 레벨이 목표보다 높으면 -> 레벨다운 시도
	else if (CurrentLevel > TargetGoalLevel)
	{
		for (int32 i = CurrentLevel; i > TargetGoalLevel; --i)
		{
			if (!TryLevelDownSkill(SkillTag)) break;
		}
	}
}

void URPGPlayerSkillComponent::ResetSkillLevel(FGameplayTag SkillTag)
{
	// 1레벨이 될 때까지 레벨다운 반복 (SP 환급 및 트라이포드 해제 자동 처리됨)
	while (TryLevelDownSkill(SkillTag))
	{
		// 반복
	}
}

bool URPGPlayerSkillComponent::SelectTripod(FGameplayTag SkillTag, int32 TierIndex, int32 OptionIndex)
{
	if (!SkillDataMap.Contains(SkillTag)) return false;
	FRPGSkillSaveData& Data = SkillDataMap[SkillTag];

	// 티어별 해금 레벨 체크 (로아 규칙: 4, 7, 10레벨)
	int32 RequiredLevel = (TierIndex == 0) ? 4 : (TierIndex == 1) ? 7 : 10;
	
	if (Data.SkillLevel < RequiredLevel)
	{
		Debug::Print(TEXT("Skill Level too low for Tier "), TierIndex + 1);
		return false;
	}

	if (TierIndex >= 0 && TierIndex < 3)
	{
		Data.SelectedTripodIndices[TierIndex] = OptionIndex;
		Debug::Print(TEXT("Tripod Selected! Tier: "), TierIndex + 1);
		return true;
	}

	return false;
}

FRPGSkillSaveData URPGPlayerSkillComponent::GetSkillSaveData(FGameplayTag SkillTag) const
{
	if (SkillDataMap.Contains(SkillTag))
	{
		return SkillDataMap[SkillTag];
	}
	return FRPGSkillSaveData(); // 기본값 (1레벨, 선택 없음)
}

int32 URPGPlayerSkillComponent::GetRequiredSPForLevel(int32 TargetLevel) const
{
	// 로아식 SP 소모 테이블 (예시)
	// 2~4렙: 1 / 5~7렙: 2 / 8~10렙: 4 / 11~12렙: 6
	if (TargetLevel <= 4) return 1;
	if (TargetLevel <= 7) return 2;
	if (TargetLevel <= 10) return 4;
	return 6;
}