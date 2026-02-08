#include "UI/Option/RPGInputOptionMenu.h"
#include "Components/ScrollBox.h"
#include "UI/Option/RPGInputMappingRow.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Controller/RPGPlayerController.h"
#include "InputMappingContext.h" // 추가: IMC 접근을 위해 필요

void URPGInputOptionMenu::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshMappings();
}

void URPGInputOptionMenu::NativePreConstruct()
{
	Super::NativePreConstruct();
	// 에디터 디자인 타임에도 미리보기를 위해 호출
	RefreshMappings();
}

void URPGInputOptionMenu::RefreshMappings()
{
	// 필수 에셋이 없으면 중단 (특히 에디터에서 아직 할당 안 했을 때)
	if (!MappingScrollBox || !RowWidgetClass || !InputConfig) return;

	MappingScrollBox->ClearChildren();

	// 현재 위젯이 디자인 타임(에디터 미리보기)인지 확인
	bool bIsDesignTime = IsDesignTime();
	ARPGPlayerController* RPGPC = Cast<ARPGPlayerController>(GetOwningPlayer());

	// 게임 중인데 PC가 없으면 중단, 디자인 타임이면 PC 없어도 진행
	if (!bIsDesignTime && !RPGPC) return;

	auto AddRow = [&](const FWarriorInputActionConfig& Config)
	{
		if (!Config.InputTag.IsValid()||!Config.bShowInInputOptionMenu) return;

		URPGInputMappingRow* RowWidget = CreateWidget<URPGInputMappingRow>(this, RowWidgetClass);
		if (RowWidget)
		{
			FKey DisplayKey;

			if (bIsDesignTime)
			{
				// 에디터에서는 IMC에서 '첫 번째' 매핑된 키를 찾아서 보여줌 (기본값)
				if (InputConfig->DefaultMappingContext)
				{
					for (const FEnhancedActionKeyMapping& Mapping : InputConfig->DefaultMappingContext->GetMappings())
					{
						if (Mapping.Action == Config.InputAction)
						{
							DisplayKey = Mapping.Key;
							break; // 첫 번째 키만 찾으면 종료
						}
					}
				}
			}

			else if (RPGPC)
			{
				// 게임 실행 중: 플레이어의 실제 설정값을 가져옴
				DisplayKey = RPGPC->GetCurrentKeyForTag(Config.InputTag);
			}

			FText ActionName = Config.InputActionName;
			
			// KeyIconConfig는 .h에서 추가했으므로 사용 가능
			RowWidget->InitializeRow(Config.InputTag, ActionName, DisplayKey, KeyIconConfig);
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
