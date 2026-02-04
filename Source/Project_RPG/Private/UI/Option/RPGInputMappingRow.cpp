#include "UI/Option/RPGInputMappingRow.h"
#include "Components/TextBlock.h"
#include "Components/InputKeySelector.h"
#include "Controller/RPGPlayerController.h"

void URPGInputMappingRow::InitializeRow(const FGameplayTag& InTag, const FText& InActionName, const FKey& InCurrentKey)
{
	ActionTag = InTag;
	
	if (ActionNameText)
	{
		ActionNameText->SetText(InActionName);
	}
	
	if (KeySelector)
	{
		KeySelector->SetSelectedKey(InCurrentKey);
	}
}

void URPGInputMappingRow::NativeConstruct()
{
	Super::NativeConstruct();

	if (KeySelector)
	{
		KeySelector->OnKeySelected.RemoveAll(this);
		KeySelector->OnKeySelected.AddDynamic(this, &URPGInputMappingRow::HandleKeySelected);
	}
}

void URPGInputMappingRow::HandleKeySelected(FInputChord SelectedKey)
{
	if (ARPGPlayerController* RPGPC = Cast<ARPGPlayerController>(GetOwningPlayer()))
	{
		// 엔진 구조체인 SelectedKey에서 Key 멤버를 가져와 적용
		RPGPC->ApplyKeyMapping(ActionTag, SelectedKey.Key);
	}
}
