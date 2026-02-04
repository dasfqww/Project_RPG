#include "UI/Option/RPGInputMappingRow.h"
#include "Components/TextBlock.h"
#include "Components/InputKeySelector.h"
#include "Components/Image.h"
#include "DataAsset/Input/DataAsset_KeyIconConfig.h"
#include "Controller/RPGPlayerController.h"

void URPGInputMappingRow::InitializeRow(const FGameplayTag& InTag, const FText& InActionName, const FKey& InCurrentKey, const UDataAsset_KeyIconConfig* InIconConfig)
{
	ActionTag = InTag;
	IconConfig = InIconConfig;

	if (ActionNameText)
	{
		ActionNameText->SetText(InActionName);
	}

	if (KeySelector)
	{
		KeySelector->SetSelectedKey(FInputChord(InCurrentKey));
	}

	// 초기 아이콘 설정
	UpdateKeyIcon(InCurrentKey);
}

void URPGInputMappingRow::NativeConstruct()
{
	Super::NativeConstruct();

	if (KeySelector)
	{
		KeySelector->OnKeySelected.AddDynamic(this, &URPGInputMappingRow::HandleKeySelected);
	}
}

void URPGInputMappingRow::HandleKeySelected(FInputChord SelectedKey)
{
	ARPGPlayerController* RPGPC = Cast<ARPGPlayerController>(GetOwningPlayer());
	if (!RPGPC) return;

	// 플레이어 컨트롤러를 통해 키 매핑 업데이트
	RPGPC->ApplyKeyMapping(ActionTag, SelectedKey.Key);

	// 아이콘 갱신
	UpdateKeyIcon(SelectedKey.Key);
}

void URPGInputMappingRow::UpdateKeyIcon(const FKey& InKey)
{
	// KeyIconImage가 있고, 데이터 에셋이 있다면 아이콘 변경 시도
	if (KeyIconImage && IconConfig)
	{
		if (UTexture2D* FoundIcon = IconConfig->FindIconForKey(InKey))
		{
			KeyIconImage->SetBrushFromTexture(FoundIcon);
			KeyIconImage->SetVisibility(ESlateVisibility::HitTestInvisible); // 이미지는 보이게
			
			// 선택적으로 텍스트 숨김 처리 등을 할 수 있음
			// KeySelector->SetVisibility(ESlateVisibility::Hidden); // 이렇게 하면 클릭이 안될 수 있으니 주의
		}
		else
		{
			// 아이콘이 없으면 기본 텍스트가 보이도록 이미지 숨김? 
			// 혹은 기본 빈 이미지 사용
			KeyIconImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}