// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Skill/RPGTripodBoard.h"
#include "UI/Skill/RPGTripodButton.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"
#include "Components/VerticalBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"

void URPGTripodBoard::InitializeBoard(URPGSkillSlotViewModel* InSlotVM)
{
	// 기존 바인딩 해제
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnTripodIndicesChanged.RemoveAll(this);
	}

	CurrentSlotVM = InSlotVM;

	if (CurrentSlotVM.IsValid())
	{
		// 커스텀 델리게이트 바인딩 (컴파일 에러 걱정 없음)
		CurrentSlotVM->OnTripodIndicesChanged.AddUObject(this, &URPGTripodBoard::UpdateTripodSelection);

		// 버튼 재생성 및 초기 선택 상태 반영
		CreateButtons();
		RefreshSelection();
	}
}

void URPGTripodBoard::NativeDestruct()
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->RemoveAllFieldValueChangedDelegates(this);
	}
	Super::NativeDestruct();
}

void URPGTripodBoard::CreateButtons()
{
	if (!TripodContainer || !CurrentSlotVM.IsValid() || !TripodButtonClass)
	{
		return;
	}

	URPGSkillDefinition* SkillDef = CurrentSlotVM->GetSkillDefinition();
	if (!SkillDef)
	{
		return;
	}

	TripodContainer->ClearChildren();
	CreatedButtons.Empty();

	const TArray<FRPGSkillTripodTier>& Tiers = SkillDef->TripodTiers;

	for (int32 TierIdx = 0; TierIdx < Tiers.Num(); ++TierIdx)
	{
		const FRPGSkillTripodTier& TierData = Tiers[TierIdx];
		UHorizontalBox* RowBox = NewObject<UHorizontalBox>(this);
		TArray<URPGTripodButton*>& ButtonList = CreatedButtons.FindOrAdd(TierIdx);
		
		for (int32 OptIdx = 0; OptIdx < TierData.Options.Num(); ++OptIdx)
		{
			const FRPGSkillTripodOption& Option = TierData.Options[OptIdx];
			URPGTripodButton* NewBtn = CreateWidget<URPGTripodButton>(this, TripodButtonClass);
			if (NewBtn)
			{
				NewBtn->InitializeTripod(TierIdx, OptIdx, Option);
				NewBtn->OnTripodClicked.AddUObject(this, &ThisClass::OnTripodBtnClicked);
				
				UHorizontalBoxSlot* HSlot = RowBox->AddChildToHorizontalBox(NewBtn);
				if (HSlot)
				{
					HSlot->SetPadding(FMargin(5.0f, 0.0f));
					HSlot->SetVerticalAlignment(VAlign_Center);
				}
				ButtonList.Add(NewBtn);
			}
		}

		TripodContainer->AddChildToVerticalBox(RowBox);
		USpacer* Spacer = NewObject<USpacer>(this);
		Spacer->SetSize(FVector2D(0.f, 10.f));
		TripodContainer->AddChildToVerticalBox(Spacer);
	}
}

void URPGTripodBoard::RefreshSelection()
{
	if (!CurrentSlotVM.IsValid())
	{
		return;
	}

	for (auto& Pair : CreatedButtons)
	{
		int32 TierIdx = Pair.Key;
		const TArray<URPGTripodButton*>& Buttons = Pair.Value;

		for (URPGTripodButton* Btn : Buttons)
		{
			if (Btn)
			{
				bool bSelected = CurrentSlotVM->IsTripodSelected(TierIdx, Btn->GetOptionIndex());
				Btn->SetIsTripodSelected(bSelected);
			}
		}
	}
}

void URPGTripodBoard::OnTripodBtnClicked(int32 Tier, int32 Index)
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->RequestTripodSelection(Tier, Index);
	}
}

void URPGTripodBoard::UpdateTripodSelection(const TArray<int32>& NewIndices)
{
	RefreshSelection();
}