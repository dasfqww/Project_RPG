#pragma once

#include "CoreMinimal.h"
#include "UI/RPGWidgetBase.h"
#include "RPGInputOptionMenu.generated.h"

class UScrollBox;
class URPGInputMappingRow;
class UDataAsset_InputConfig;

/**
 * 키 바인딩 설정 전체 리스트를 보여주는 메뉴 위젯
 */
UCLASS()
class PROJECT_RPG_API URPGInputOptionMenu : public URPGWidgetBase
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativePreConstruct() override;

	// UI 리스트 새로고침
	UFUNCTION(BlueprintCallable, Category = "Input")
	void RefreshMappings();

protected:
	// 개별 항목으로 생성할 위젯 클래스 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TSubclassOf<URPGInputMappingRow> RowWidgetClass;

	// 입력 설정 데이터 에셋 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UDataAsset_InputConfig> InputConfig;

	// 키 아이콘 데이터 에셋 (블루프린트에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<class UDataAsset_KeyIconConfig> KeyIconConfig;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UScrollBox> MappingScrollBox;
};