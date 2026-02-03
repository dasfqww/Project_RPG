// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGTripodButton.h"

void URPGTripodButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	OnClicked().AddUObject(this, &ThisClass::HandleTripodButtonClicked);
}

void URPGTripodButton::InitializeTripod(int32 InTier, int32 InIndex, const FRPGSkillTripodOption& InOption)
{
	TierIndex = InTier;
	OptionIndex = InIndex;
	TripodOption = InOption;

	// 초기화 시 선택 상태는 기본 false로
	BP_OnTripodInitialized(TripodOption, false);
}

void URPGTripodButton::SetIsTripodSelected(bool bInSelected)
{
	// CommonButtonBase의 SetIsSelected 사용 가능하지만,
	// 여기서는 명시적인 시각적 처리를 위해 BP 이벤트 호출
	Super::SetIsSelected(bInSelected);
	BP_OnSelectionChanged(bInSelected);
}

void URPGTripodButton::HandleTripodButtonClicked()
{
	if (OnTripodClicked.IsBound())
	{
		OnTripodClicked.Broadcast(TierIndex, OptionIndex);
	}
}

void URPGTripodButton::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();
	// 필요 시 텍스트 스타일 변경 로직 추가
}