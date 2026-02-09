#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateStyle.h"

/**
 * 에디터에서 사용할 아이콘 및 스타일 정의 (Lyra의 FGameEditorStyle 참고)
 */
class FProject_RPGEditorStyle
{
public:
	static void Initialize();
	static void Shutdown();
	static void ReloadTextures();

	static const ISlateStyle& Get();
	static FName GetStyleSetName();

private:
	static TSharedRef<class FSlateStyleSet> Create();

	static TSharedPtr<class FSlateStyleSet> StyleInstance;
};
