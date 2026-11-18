#pragma once

#include "Blueprint/UserWidget.h"
#include "RPGAchievementPanel.generated.h"

UCLASS()
class PROJECT_RPG_API URPGAchievementPanel : public UUserWidget
{
    GENERATED_BODY()
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};
