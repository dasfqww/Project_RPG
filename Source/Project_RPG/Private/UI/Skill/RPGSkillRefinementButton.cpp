// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Skill/RPGSkillRefinementButton.h"
#include "UI/Tooltip/RPGSkillTooltip.h"

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

void URPGSkillRefinementButton::InitializeRefinement(int32 InTier, int32 InIndex,
	const FRPGSkillRefinementOption& InOption, const int32 InRequiredSkillLevel)
{
	TierIndex = InTier;
	OptionIndex = InIndex;
	RefinementOption = InOption;
	RequiredSkillLevel = InRequiredSkillLevel;

	if (GetSelected()) SetSelectedInternal(false, false);
	BP_OnSelectionChanged(false);
	// 초기화 시 선택 상태는 기본 false로
	BP_OnRefinementInitialized(RefinementOption, false);
	// The existing Blueprint initializes a plain text tip along with the icon.
	// Restore the shared card after that event and read the now-resolved icon.
	URPGSkillTooltip::UpdateFor(*this,
		FRPGSkillTooltipPresentation::ForRefinement(RefinementOption, RequiredSkillLevel, false, true));
}

void URPGSkillRefinementButton::SetRefinementState(bool bInSelected, bool bUnlocked)
{
	// Apply authoritative/predicted model state even for a non-toggleable
	// authored button. SetIsSelected(false) otherwise cannot clear selection.
	if (GetSelected() != bInSelected)
	{
		SetSelectedInternal(bInSelected, false);
	}
	BP_OnSelectionChanged(bInSelected);
	URPGSkillTooltip::UpdateFor(*this,
		FRPGSkillTooltipPresentation::ForRefinement(RefinementOption, RequiredSkillLevel, bInSelected, bUnlocked));
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
