#include "Project_RPGEditor.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "Project_RPGEditor"

void FProject_RPGEditorModule::StartupModule()
{
	// 에디터 메뉴 등록 대기
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProject_RPGEditorModule::RegisterMenus));
}

void FProject_RPGEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
}

void FProject_RPGEditorModule::RegisterMenus()
{
	// 여기에 나중에 Lyra처럼 툴바 버튼이나 메뉴 항목을 추가합니다.
	FToolMenuOwnerScoped OwnerScoped(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FProject_RPGEditorModule, Project_RPGEditor)
