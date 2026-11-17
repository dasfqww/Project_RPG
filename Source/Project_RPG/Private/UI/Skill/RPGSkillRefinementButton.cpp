// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillRefinementButton.h"

void URPGSkillRefinementButton::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Selection belongs to the slot view-model. CommonUI must not toggle it
	// again after the click command, or on focus, independently of that model.
	SetIsSelectable(false);
	SetShouldSelectUponReceivingFocus(false);
	SetIsInteractableWhenSelected(true);
	OnClicked().AddUObject(this, &ThisClass::HandleRefinementButtonClicked);
}

void URPGSkillRefinementButton::InitializeRefinement(int32 InTier, int32 InIndex, const FRPGSkillRefinementOption& InOption)
{
	TierIndex = InTier;
	OptionIndex = InIndex;
	RefinementOption = InOption;

	SetIsRefinementSelected(false);
	// 초기화 시 선택 상태는 기본 false로
	BP_OnRefinementInitialized(RefinementOption, false);
}

void URPGSkillRefinementButton::SetIsRefinementSelected(bool bInSelected)
{
	// Apply authoritative/predicted model state even for a non-toggleable
	// authored button. SetIsSelected(false) otherwise cannot clear selection.
	if (GetSelected() != bInSelected)
	{
		SetSelectedInternal(bInSelected, false);
	}
	BP_OnSelectionChanged(bInSelected);
}

void URPGSkillRefinementButton::HandleRefinementButtonClicked()
{
	if (OnRefinementClicked.IsBound())
	{
		OnRefinementClicked.Broadcast(TierIndex, OptionIndex);
	}
}

void URPGSkillRefinementButton::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();
	// 필요 시 텍스트 스타일 변경 로직 추가
}
