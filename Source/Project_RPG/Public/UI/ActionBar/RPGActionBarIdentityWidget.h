#pragma once
#include "UI/ActionBar/RPGActionBarWidget.h"
#include "RPGActionBarIdentityWidget.generated.h"

/** Native central display; custom profiles may still supply an authored subclass. */
UCLASS()
class PROJECT_RPG_API URPGActionBarIdentityWidget : public URPGActionBarWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
