#pragma once
#include "Blueprint/UserWidget.h"
#include "RPGBurnStatusWidget.generated.h"
class URPGCombatControlComponent;
class UTextBlock;

/** Small noninteractive status label shared by Player/NPC screen-space widgets. */
UCLASS(Transient)
class URPGBurnStatusWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetControl(URPGCombatControlComponent* InControl);
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
private:
	TWeakObjectPtr<URPGCombatControlComponent> Control;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
};
