#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"
#include "RPGInputMappingRow.generated.h"

class UTextBlock;
class UInputKeySelector;

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
	void InitializeRow(const FGameplayTag& InTag, const FText& InActionName, const FKey& InCurrentKey);

protected:
	virtual void NativeConstruct() override;

	// 엔진의 델리게이트 서명(FInputKeySelected)을 받기 위한 함수
	// 구조체 이름이 엔진과 충돌할 수 있으므로, .cpp에서 정확한 타입을 처리합니다.
	UFUNCTION()
	void HandleKeySelected(FInputChord SelectedKey);

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ActionNameText;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UInputKeySelector> KeySelector;

	FGameplayTag ActionTag;
};
