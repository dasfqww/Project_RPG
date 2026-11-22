// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Skill/RPGSkillRefinementBoard.h"
#include "UI/Skill/RPGSkillRefinementButton.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"

void URPGSkillRefinementBoard::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 버튼들을 티어별 배열로 정리
	RefinementTiers.Empty();
	RefinementTiers.Add({ Btn_1_1, Btn_1_2, Btn_1_3, Btn_1_4 }); // Tier 0
	RefinementTiers.Add({ Btn_2_1, Btn_2_2, Btn_2_3 }); // Tier 1
	RefinementTiers.Add({ Btn_3_1, Btn_3_2 });          // Tier 2

	// 모든 버튼에 클릭 이벤트 연결
	for (int32 TierIdx = 0; TierIdx < RefinementTiers.Num(); ++TierIdx)
	{
		for (int32 OptIdx = 0; OptIdx < RefinementTiers[TierIdx].Num(); ++OptIdx)
		{
			if (URPGSkillRefinementButton* Btn = RefinementTiers[TierIdx][OptIdx])
			{
				Btn->OnRefinementClicked.AddUObject(this, &ThisClass::OnRefinementBtnClicked);
			}
		}
	}
}

void URPGSkillRefinementBoard::InitializeBoard(URPGSkillSlotViewModel* InSlotVM)
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnRefinementIndicesChanged.RemoveAll(this);
		CurrentSlotVM->RemoveAllFieldValueChangedDelegates(this);
	}

	CurrentSlotVM = InSlotVM;

	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnRefinementIndicesChanged.AddUObject(this, &URPGSkillRefinementBoard::UpdateRefinementSelection);
		CurrentSlotVM->AddFieldValueChangedDelegate(
			URPGSkillSlotViewModel::FFieldNotificationClassDescriptor::SkillLevel,
			INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateWeakLambda(
				this, [this](UObject*, UE::FieldNotification::FFieldId) { RefreshSelection(); }));
	}
	UpdateButtons();
	RefreshSelection();
}

void URPGSkillRefinementBoard::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (IsDesignTime())
	{
		// 디자인 모드에서 더미 데이터 세팅 (레이아웃 확인용)
		TArray<TArray<URPGSkillRefinementButton*>> DesignTiers;
		DesignTiers.Add({ Btn_1_1, Btn_1_2, Btn_1_3, Btn_1_4 });
		DesignTiers.Add({ Btn_2_1, Btn_2_2, Btn_2_3 });
		DesignTiers.Add({ Btn_3_1, Btn_3_2 });

		for (int32 i = 0; i < DesignTiers.Num(); ++i)
		{
			for (int32 j = 0; j < DesignTiers[i].Num(); ++j)
			{
				if (URPGSkillRefinementButton* Btn = DesignTiers[i][j])
				{
					FRPGSkillRefinementOption DummyOption;
					DummyOption.OptionName = FText::FromString(FString::Printf(TEXT("Tier %d Opt %d"), i + 1, j + 1));
					Btn->InitializeRefinement(i, j, DummyOption);
					Btn->SetVisibility(ESlateVisibility::Visible);
				}
			}
		}
	}
}

void URPGSkillRefinementBoard::NativeDestruct()
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnRefinementIndicesChanged.RemoveAll(this);
		CurrentSlotVM->RemoveAllFieldValueChangedDelegates(this);
	}
	CurrentSlotVM.Reset();
	Super::NativeDestruct();
}

void URPGSkillRefinementBoard::UpdateButtons()
{
	const URPGSkillDefinition* SkillDef = CurrentSlotVM.IsValid()
		? CurrentSlotVM->GetSkillDefinition() : nullptr;
	const TArray<FRPGSkillRefinementTier>* DataTiers = SkillDef ? &SkillDef->RefinementTiers : nullptr;

	for (int32 TierIdx = 0; TierIdx < RefinementTiers.Num(); ++TierIdx)
	{
		const TArray<URPGSkillRefinementButton*>& UIButtons = RefinementTiers[TierIdx];
		
		// 데이터가 있는 티어인지 확인
		if (DataTiers && DataTiers->IsValidIndex(TierIdx))
		{
			const FRPGSkillRefinementTier& TierData = (*DataTiers)[TierIdx];
			
			for (int32 OptIdx = 0; OptIdx < UIButtons.Num(); ++OptIdx)
			{
				URPGSkillRefinementButton* Btn = UIButtons[OptIdx];
				if (!Btn) continue;

				// 해당 옵션 데이터가 있으면 활성화 및 초기화
				if (TierData.Options.IsValidIndex(OptIdx))
				{
					Btn->SetVisibility(ESlateVisibility::Visible);
				Btn->InitializeRefinement(TierIdx, OptIdx, TierData.Options[OptIdx], TierData.RequiredSkillLevel);
				}
				else
				{
					// 데이터가 없으면 버튼 숨김
					Btn->SetVisibility(ESlateVisibility::Collapsed);
				}
			}
		}
		else
		{
			// 티어 자체가 데이터에 없으면 해당 티어 버튼 모두 숨김
			for (URPGSkillRefinementButton* Btn : UIButtons)
			{
				if (Btn) Btn->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void URPGSkillRefinementBoard::RefreshSelection()
{
	if (!CurrentSlotVM.IsValid()) return;

	for (int32 TierIdx = 0; TierIdx < RefinementTiers.Num(); ++TierIdx)
	{
		for (URPGSkillRefinementButton* Btn : RefinementTiers[TierIdx])
		{
			if (Btn && Btn->GetVisibility() == ESlateVisibility::Visible)
			{
				bool bSelected = CurrentSlotVM->IsRefinementSelected(TierIdx, Btn->GetOptionIndex());
				const URPGSkillDefinition* Definition = CurrentSlotVM->GetSkillDefinition();
				const bool bUnlocked = Definition && Definition->IsRefinementSelectionAllowed(
					CurrentSlotVM->SkillLevel, TierIdx, Btn->GetOptionIndex());
				Btn->SetRefinementState(bSelected, bUnlocked);
				Btn->SetIsEnabled(bUnlocked);
			}
		}
	}
}

void URPGSkillRefinementBoard::OnRefinementBtnClicked(int32 Tier, int32 Index)
{
	if (CurrentSlotVM.IsValid())
	{
		// Resolve the UI toggle here; the model/server still receive an explicit
		// selection so repeated commands cannot accidentally toggle it again.
		const int32 RequestedIndex = CurrentSlotVM->IsRefinementSelected(Tier, Index)
			? INDEX_NONE : Index;
		CurrentSlotVM->RequestRefinementSelection(Tier, RequestedIndex);
	}
}

void URPGSkillRefinementBoard::UpdateRefinementSelection(const TArray<int32>& NewIndices)
{
	RefreshSelection();
}
