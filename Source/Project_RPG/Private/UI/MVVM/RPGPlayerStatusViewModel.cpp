// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MVVM/RPGPlayerStatusViewModel.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Type/RPGStructTypes.h"

URPGPlayerStatusViewModel::URPGPlayerStatusViewModel()
	: HealthPercent(1.0f)
	, ManaPercent(1.0f)
	, IdentityPercent(0.0f)
	, IdentityColor(FLinearColor::Gray)
{
}

void URPGPlayerStatusViewModel::InitializeFromParams(const FRPGPlayerIdentityData& IdentityData)
{
	// 직업 에셋에서 UI용 시각 데이터 로드
	IdentityColor = IdentityData.GaugeColor;
	IdentityIcon = IdentityData.IdentityIcon;
	IdentityGaugeMaterial = IdentityData.IdentityGaugeMaterial;

	// 값이 변했음을 UI에 알림
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IdentityColor);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IdentityIcon);
	UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IdentityGaugeMaterial);
}

void URPGPlayerStatusViewModel::SetUIComponent(UPlayerUIComponent* UIComponent)
{
	if (!UIComponent) return;

	// PlayerUIComponent의 델리게이트를 ViewModel의 함수와 바인딩
	UIComponent->OnCurrentHealthChanged.AddDynamic(this, &URPGPlayerStatusViewModel::OnHealthChanged);
	UIComponent->OnCurrentManaChanged.AddDynamic(this, &URPGPlayerStatusViewModel::OnManaChanged);
	UIComponent->OnCurrentIdentityGaugeChanged.AddDynamic(this, &URPGPlayerStatusViewModel::OnIdentityChanged);
}

void URPGPlayerStatusViewModel::OnHealthChanged(float NewPercent)
{
	if (HealthPercent != NewPercent)
	{
		HealthPercent = NewPercent;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(HealthPercent);
	}
}

void URPGPlayerStatusViewModel::OnManaChanged(float NewPercent)
{
	if (ManaPercent != NewPercent)
	{
		ManaPercent = NewPercent;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(ManaPercent);
	}
}

void URPGPlayerStatusViewModel::OnIdentityChanged(float NewPercent)
{
	if (IdentityPercent != NewPercent)
	{
		IdentityPercent = NewPercent;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(IdentityPercent);
	}
}