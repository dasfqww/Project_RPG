#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGExperienceLoadingScreen.generated.h"

/** Shared loading presentation available before any Experience feature is ready. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGExperienceLoadingScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    URPGExperienceLoadingScreen(const FObjectInitializer& ObjectInitializer);
    static TSharedRef<SWidget> MakeMapTravelScreen(double ShownTime);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    bool IsExperienceFailed() const;
    FText GetTitleText() const;
    FText GetMessageText() const;
};
