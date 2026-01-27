// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/PopUp/RPGItemSplitPopUp.h"
#include "Components/Slider.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/SizeBox.h"

void URPGItemSplitPopUp::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	AcceptButton->OnClicked.AddDynamic(this, &ThisClass::OnSplitAccepted);
	SplitSlider->OnValueChanged.AddDynamic(this, &ThisClass::OnSliderValueChanged);
}

int32 URPGItemSplitPopUp::GetSplitAmount() const
{
	return FMath::Floor(SplitSlider->GetValue());
}

void URPGItemSplitPopUp::OnSplitAccepted()
{
	if (OnSplit.ExecuteIfBound(GetSplitAmount(), GridIndex))
	{
		RemoveFromParent();
	}
}

void URPGItemSplitPopUp::OnSliderValueChanged(float Value)
{
	SplitAmountText->SetText(FText::AsNumber(FMath::Floor(Value)));
}

FVector2D URPGItemSplitPopUp::GetBoxSize() const
{
	return FVector2D(RootSizeBox->GetWidthOverride(), RootSizeBox->GetHeightOverride());
}

void URPGItemSplitPopUp::CollapseSplitButton()
{
	AcceptButton->SetVisibility(ESlateVisibility::Collapsed);
	SplitSlider->SetVisibility(ESlateVisibility::Collapsed);
	SplitAmountText->SetVisibility(ESlateVisibility::Collapsed);
}

void URPGItemSplitPopUp::SetSliderParams(const float Max, const float Value) const
{
	SplitSlider->SetMaxValue(Max);
	SplitSlider->SetMinValue(1);
	SplitSlider->SetValue(Value);
	SplitAmountText->SetText(FText::AsNumber(FMath::Floor(Value)));
}
