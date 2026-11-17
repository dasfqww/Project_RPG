// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "Type/RPGStructTypes.h"
#include "RPGTripodButton.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTripodButtonClicked, int32 /*Tier*/, int32 /*Index*/);

/**
 * 연마 효과 개별 버튼
 */
UCLASS(meta = (DisplayName = "연마 효과 버튼"))
class PROJECT_RPG_API URPGTripodButton : public UCommonButtonBase
{
	GENERATED_BODY()
	
public:
	void InitializeTripod(int32 InTier, int32 InIndex, const FRPGSkillTripodOption& InOption);
	void SetIsTripodSelected(bool bInSelected);

	FOnTripodButtonClicked OnTripodClicked;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnCurrentTextStyleChanged() override; // CommonButtonBase override

	UPROPERTY(BlueprintReadOnly, Category = "스킬 연마")
	int32 TierIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "스킬 연마")
	int32 OptionIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "스킬 연마", meta = (DisplayName = "연마 효과"))
	FRPGSkillTripodOption TripodOption;

	// UI 바인딩용 (아이콘, 텍스트 설정)
	UFUNCTION(BlueprintImplementableEvent, Category = "스킬 연마", meta = (DisplayName = "연마 효과 초기화"))
	void BP_OnTripodInitialized(const FRPGSkillTripodOption& Option, bool bIsSelected);
	
	UFUNCTION(BlueprintImplementableEvent, Category = "스킬 연마")
	void BP_OnSelectionChanged(bool bIsSelected);

private:
	void HandleTripodButtonClicked();

public:
	FORCEINLINE int32 GetOptionIndex() { return OptionIndex; }
};
