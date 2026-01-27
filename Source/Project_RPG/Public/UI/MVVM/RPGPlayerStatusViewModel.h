// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "RPGPlayerStatusViewModel.generated.h"

/**
 * 플레이어의 핵심 상태(HP, MP, Identity)를 UI에 전달하는 ViewModel
 */
UCLASS()
class PROJECT_RPG_API URPGPlayerStatusViewModel : public URPGViewModelBase
{
	GENERATED_BODY()

public:
	URPGPlayerStatusViewModel();

	/** UI 바인딩용 속성들 (FieldNotify를 통해 값이 변하면 UI가 즉시 갱신됨) */
	
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	float HealthPercent;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	float ManaPercent;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	float IdentityPercent;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	FLinearColor IdentityColor;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	TSoftObjectPtr<UTexture2D> IdentityIcon;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Status")
	TSoftObjectPtr<UMaterialInterface> IdentityGaugeMaterial;

	/** 초기화: 로아식 직업 데이터를 읽어와서 UI 초기화 */
	void InitializeFromParams(const struct FRPGPlayerIdentityData& IdentityData);

protected:
	/** PlayerUIComponent의 델리게이트와 연결될 함수들 */
	UFUNCTION()
	void OnHealthChanged(float NewPercent);

	UFUNCTION()
	void OnManaChanged(float NewPercent);

	UFUNCTION()
	void OnIdentityChanged(float NewPercent);

public:
	/** 로직 연결 함수 */
	void SetUIComponent(class UPlayerUIComponent* UIComponent);
};