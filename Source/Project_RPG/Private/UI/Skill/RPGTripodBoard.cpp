// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Skill/RPGTripodBoard.h"
#include "UI/Skill/RPGTripodButton.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "Skill/RPGSkillDefinition.h"

void URPGTripodBoard::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 버튼들을 티어별 배열로 정리
	TripodTiers.Empty();
	TripodTiers.Add({ Btn_1_1, Btn_1_2, Btn_1_3 }); // Tier 0
	TripodTiers.Add({ Btn_2_1, Btn_2_2, Btn_2_3 }); // Tier 1
	TripodTiers.Add({ Btn_3_1, Btn_3_2 });          // Tier 2

	// 모든 버튼에 클릭 이벤트 연결
	for (int32 TierIdx = 0; TierIdx < TripodTiers.Num(); ++TierIdx)
	{
		for (int32 OptIdx = 0; OptIdx < TripodTiers[TierIdx].Num(); ++OptIdx)
		{
			if (URPGTripodButton* Btn = TripodTiers[TierIdx][OptIdx])
			{
				Btn->OnTripodClicked.AddUObject(this, &ThisClass::OnTripodBtnClicked);
			}
		}
	}
}

void URPGTripodBoard::InitializeBoard(URPGSkillSlotViewModel* InSlotVM)
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnTripodIndicesChanged.RemoveAll(this);
	}

	CurrentSlotVM = InSlotVM;

	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnTripodIndicesChanged.AddUObject(this, &URPGTripodBoard::UpdateTripodSelection);
		UpdateButtons();
		RefreshSelection();
	}
}

void URPGTripodBoard::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (IsDesignTime())
	{
		// 디자인 모드에서 더미 데이터 세팅 (레이아웃 확인용)
		TArray<TArray<URPGTripodButton*>> DesignTiers;
		DesignTiers.Add({ Btn_1_1, Btn_1_2, Btn_1_3 });
		DesignTiers.Add({ Btn_2_1, Btn_2_2, Btn_2_3 });
		DesignTiers.Add({ Btn_3_1, Btn_3_2 });

		for (int32 i = 0; i < DesignTiers.Num(); ++i)
		{
			for (int32 j = 0; j < DesignTiers[i].Num(); ++j)
			{
				if (URPGTripodButton* Btn = DesignTiers[i][j])
				{
					FRPGSkillTripodOption DummyOption;
					DummyOption.OptionName = FText::FromString(FString::Printf(TEXT("Tier %d Opt %d"), i + 1, j + 1));
					Btn->InitializeTripod(i, j, DummyOption);
					Btn->SetVisibility(ESlateVisibility::Visible);
				}
			}
		}
	}
}

void URPGTripodBoard::NativeDestruct()
{
	if (CurrentSlotVM.IsValid())
	{
		CurrentSlotVM->OnTripodIndicesChanged.RemoveAll(this);
	}
	Super::NativeDestruct();
}

void URPGTripodBoard::UpdateButtons()
{
	if (!CurrentSlotVM.IsValid()) return;

	URPGSkillDefinition* SkillDef = CurrentSlotVM->GetSkillDefinition();
	if (!SkillDef) return;

	const TArray<FRPGSkillTripodTier>& DataTiers = SkillDef->TripodTiers;

	for (int32 TierIdx = 0; TierIdx < TripodTiers.Num(); ++TierIdx)
	{
		const TArray<URPGTripodButton*>& UIButtons = TripodTiers[TierIdx];
		
		// 데이터가 있는 티어인지 확인
		if (DataTiers.IsValidIndex(TierIdx))
		{
			const FRPGSkillTripodTier& TierData = DataTiers[TierIdx];
			
			for (int32 OptIdx = 0; OptIdx < UIButtons.Num(); ++OptIdx)
			{
				URPGTripodButton* Btn = UIButtons[OptIdx];
				if (!Btn) continue;

				// 해당 옵션 데이터가 있으면 활성화 및 초기화
				if (TierData.Options.IsValidIndex(OptIdx))
				{
					Btn->SetVisibility(ESlateVisibility::Visible);
					Btn->InitializeTripod(TierIdx, OptIdx, TierData.Options[OptIdx]);
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
			for (URPGTripodButton* Btn : UIButtons)
			{
				if (Btn) Btn->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
}

void URPGTripodBoard::RefreshSelection()
{
	if (!CurrentSlotVM.IsValid()) return;

	for (int32 TierIdx = 0; TierIdx < TripodTiers.Num(); ++TierIdx)
	{
		for (URPGTripodButton* Btn : TripodTiers[TierIdx])
		{
			if (Btn && Btn->GetVisibility() == ESlateVisibility::Visible)
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
