#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class IAssetTypeActions;

class FProject_RPGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

protected:
	void RegisterMenus();
	void OnCheckContentClicked();

private:
	TArray<TSharedPtr<IAssetTypeActions>> RegisteredAssetTypeActions;
};