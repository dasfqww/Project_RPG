// Fill out your copyright notice in the Description page of Project Settings.


#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillAction.h"
#include "DataTable/SkillData.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"

void URPGSkillDefinition::GetSkillDataForContext(AActor* InActor, const TArray<int32>& SelectedTripods, UTexture2D*& OutIcon, UAnimMontage*& OutMontage, TSubclassOf<URPGSkillAction>& OutActionClass) const
{
	// 1. 기본값 설정 (에셋 및 데이터 테이블 기준)
	OutIcon = SkillIcon;
	OutMontage = SkillMontage;
	OutActionClass = DefaultActionClass;

	if (!SkillDataHandle.IsNull())
	{
		if (FRPGSkillDataTable* Row = SkillDataHandle.GetRow<FRPGSkillDataTable>(TEXT("")))
		{
			if (Row->SkillIcon) OutIcon = Row->SkillIcon;
			// 데미지 등 수치 정보는 실행 시점에 별도 참조
		}
	}

	if (!InActor) return;

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InActor);
	if (!ASC) return;

	// 2. 트라이포드에 의한 데이터 오버라이드 (로직 변경 등)
	// SelectedTripods 배열은 [티어1 인덱스, 티어2 인덱스, 티어3 인덱스] 형태
	for (int32 TierIdx = 0; TierIdx < SelectedTripods.Num(); ++TierIdx)
	{
		int32 SelectedOptionIdx = SelectedTripods[TierIdx];
		
		if (TripodTiers.IsValidIndex(TierIdx))
		{
			const FRPGSkillTripodTier& Tier = TripodTiers[TierIdx];
			if (Tier.Options.IsValidIndex(SelectedOptionIdx))
			{
				const FRPGSkillTripodOption& Option = Tier.Options[SelectedOptionIdx];
				
				// 트라이포드에서 액션을 변경하라고 지정했다면 덮어씀
				if (Option.OverrideActionClass)
				{
					OutActionClass = Option.OverrideActionClass;
				}
				// 아이콘 등 비주얼도 필요시 여기서 덮어씀
			}
		}
	}

	// 3. 캐릭터 상태(변신 등)에 의한 데이터 최종 오버라이드
	for (const FSkillModeOverride& Override : ModeOverrides)
	{
		if (Override.RequiredStateTag.IsValid() && ASC->HasMatchingGameplayTag(Override.RequiredStateTag))
		{
			if (Override.NewIcon) OutIcon = Override.NewIcon;
			if (Override.NewMontage) OutMontage = Override.NewMontage;
			if (Override.NewActionClass) OutActionClass = Override.NewActionClass;
			
			break; // 첫 번째 일치하는 상태 우선
		}
	}
}