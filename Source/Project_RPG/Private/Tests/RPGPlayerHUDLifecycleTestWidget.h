#pragma once
#include "Blueprint/UserWidget.h"
#include "RPGPlayerHUDLifecycleTestWidget.generated.h"

/** Unsaved stand-in for authored initialization/Construct/Destruct callbacks. */
UCLASS(Transient, NotBlueprintable)
class URPGPlayerHUDLifecycleTestWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<APawn> InitialPawn;
    static TFunction<void(UUserWidget*)> OnInitialize;
    static TFunction<void(UUserWidget*)> OnConstruct;
    static TFunction<void(UUserWidget*)> OnDestruct;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
