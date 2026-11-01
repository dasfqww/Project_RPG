#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGOptionsShortcutHint.generated.h"

/** Informational hint for the owning player's currently mapped options shortcuts. */
UCLASS()
class RPGOPTIONSUIRUNTIME_API URPGOptionsShortcutHint : public UUserWidget
{
	GENERATED_BODY()
public:
	URPGOptionsShortcutHint(const FObjectInitializer& ObjectInitializer);

	/** Empty while the owning player has no live options shortcut or mapped button key. */
	UFUNCTION(BlueprintPure, Category = "UI|Options")
	FText GetShortcutText() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};
