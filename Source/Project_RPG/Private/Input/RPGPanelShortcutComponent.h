#pragma once

#include "Components/ActorComponent.h"
#include "Input/RPGPanelShortcutConfig.h"
#include "RPGPanelShortcutComponent.generated.h"

class IInputProcessor;

/** Installs the local controller's panel shortcuts while it owns a game viewport. */
UCLASS()
class URPGPanelShortcutComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGPanelShortcutComponent();
	void Start(TFunction<bool(ERPGPanelShortcutAction)> Dispatch);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Stop();

	UPROPERTY(EditDefaultsOnly, Category = "Shortcuts")
	TSoftObjectPtr<URPGPanelShortcutConfig> ConfigAsset;

	TSharedPtr<IInputProcessor> Processor;
};
