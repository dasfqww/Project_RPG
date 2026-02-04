#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "RPGInputMappingRow.generated.h"

class UTextBlock;
class UInputKeySelector;
class UImage;
class UDataAsset_KeyIconConfig;

/**
 * 엔진의 FInputKeySelected 구조체가 인식이 안 되는 경우를 대비해 
 * 여기서 직접 정의하거나, 델리게이트 서명을 위해 선언합니다.
 */
USTRUCT(BlueprintType)
struct FRPGInputKeySelected
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Key")
	FKey Key;
};

/**
 * 키 바인딩 설정의 개별 항목을 나타내는 위젯
 */
UCLASS()
class PROJECT_RPG_API URPGInputMappingRow : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	// 초기화 함수에 IconConfig 추가
	void InitializeRow(const FGameplayTag& InTag, const FText& InActionName, const FKey& InCurrentKey, const UDataAsset_KeyIconConfig* InIconConfig);

protected:
	virtual void NativeConstruct() override;

	// 엔진의 델리게이트 서명(FInputKeySelected)을 받기 위한 함수
	UFUNCTION()
	void HandleKeySelected(FInputChord SelectedKey);

	// 아이콘 업데이트 헬퍼
	void UpdateKeyIcon(const FKey& InKey);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ActionNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputKeySelector> KeySelector;

	// 키 아이콘을 표시할 이미지 위젯 (WBP에서 이 이름으로 Image 추가 필요)
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> KeyIconImage;

	UPROPERTY()
	TObjectPtr<const UDataAsset_KeyIconConfig> IconConfig;

	FGameplayTag ActionTag;
};