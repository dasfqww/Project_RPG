#include "UI/Option/RPGInputOptionMenu.h"
#include "Components/ScrollBox.h"
#include "UI/Option/RPGInputMappingRow.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Controller/RPGPlayerController.h"

void URPGInputOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshMappings();
}

void URPGInputOptionMenu::RefreshMappings()
{
	if (!MappingScrollBox || !RowWidgetClass || !InputConfig) return;

	MappingScrollBox->ClearChildren();

	ARPGPlayerController* RPGPC = Cast<ARPGPlayerController>(GetOwningPlayer());
	if (!RPGPC) return;

	// 중복 방지를 위해 람다로 생성 로직 통합
	auto AddRow = [&](const FWarriorInputActionConfig& Config)
	{
		if (!Config.InputTag.IsValid()) return;

		URPGInputMappingRow* RowWidget = CreateWidget<URPGInputMappingRow>(this, RowWidgetClass);
		if (RowWidget)
		{
			FKey CurrentKey = RPGPC->GetCurrentKeyForTag(Config.InputTag);
			FText ActionName = FText::FromName(Config.InputTag.GetTagName());
			
			RowWidget->InitializeRow(Config.InputTag, ActionName, CurrentKey);
			MappingScrollBox->AddChild(RowWidget);
		}
	};

	// 기본 입력 액션들 추가
	for (const FWarriorInputActionConfig& Config : InputConfig->NativeInputActions)
	{
		AddRow(Config);
	}

	// 어빌리티(스킬) 입력 액션들 추가
	for (const FWarriorInputActionConfig& Config : InputConfig->AbilityInputActions)
	{
		AddRow(Config);
	}
}