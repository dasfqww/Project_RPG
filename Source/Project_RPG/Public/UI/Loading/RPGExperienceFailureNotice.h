#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RPGExperienceFailureNotice.generated.h"

/** A passive per-player failure notice; it does not acquire gameplay input or focus. */
UCLASS()
class PROJECT_RPG_API URPGExperienceFailureNotice : public UUserWidget
{
    GENERATED_BODY()
public:
    URPGExperienceFailureNotice(const FObjectInitializer& ObjectInitializer);
    void SetNoticeMessage(const FText& InMessage) { NoticeMessage = InMessage; }

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

private:
    UPROPERTY(Transient)
    FText NoticeMessage;
};
