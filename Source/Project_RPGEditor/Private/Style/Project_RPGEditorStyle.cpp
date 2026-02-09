#include "Style/Project_RPGEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/SlateGameResources.h"
#include "Framework/Application/SlateApplication.h"

TSharedPtr<FSlateStyleSet> FProject_RPGEditorStyle::StyleInstance = nullptr;

void FProject_RPGEditorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FProject_RPGEditorStyle::Shutdown()
{
	if (StyleInstance.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
		ensure(StyleInstance.IsUnique());
		StyleInstance.Reset();
	}
}

void FProject_RPGEditorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FProject_RPGEditorStyle::Get()
{
	return *StyleInstance;
}

FName FProject_RPGEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("Project_RPGEditorStyle"));
	return StyleSetName;
}

TSharedRef<FSlateStyleSet> FProject_RPGEditorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));

	// Content/Editor/Slate 경로가 실제 존재하는지 확인 후 설정 필요
	Style->SetContentRoot(FPaths::ProjectContentDir() / TEXT("Editor/Slate"));
	Style->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	return Style;
}