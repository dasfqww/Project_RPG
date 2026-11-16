#pragma once
#include "Blueprint/UserWidget.h"
#include "RPGSoloRaidPanel.generated.h"

class URPGSoloRaidSubsystem;

/** Native, development-only workbench UI; no asset or production controller changes. */
UCLASS()
class PROJECT_RPG_API URPGSoloRaidPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetWorkbench(URPGSoloRaidSubsystem* Value);
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
    TWeakObjectPtr<URPGSoloRaidSubsystem> Workbench;
};
