#pragma once

#include "Blueprint/UserWidget.h"
#include "RPGPlayerNoticeWidget.generated.h"

class UPlayerUIComponent;
class UTextBlock;

/** Latest notice replaces the previous one and receives its own display lifetime. */
UCLASS(meta=(DisableNativeTick))
class PROJECT_RPG_API URPGPlayerNoticeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="UI|Notice") void SetNoticeSource(UPlayerUIComponent* Source);
    UFUNCTION(BlueprintCallable, Category="UI|Notice") void ShowNotice(const FText& Text);
    UFUNCTION(BlueprintCallable, Category="UI|Notice") void ClearNotice();
    UFUNCTION(BlueprintPure, Category="UI|Notice") FText GetDisplayedNotice() const { return DisplayedNotice; }
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void BeginDestroy() override;
    UPROPERTY(EditDefaultsOnly, Category="Notice", meta=(ClampMin="0.01")) float DisplayDuration = 3.f;
    UPROPERTY(EditDefaultsOnly, Category="Notice") bool bAutoBindOwningPawn = true;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> NoticeText;
    UFUNCTION(BlueprintImplementableEvent, Category="UI|Notice") void OnNoticePresentationChanged();
private:
    friend struct FRPGPlayerNoticeWidgetTestAccess;
    void UnbindSource();
    void UnbindController();
    void Present();
    UFUNCTION() void OnNoticeReceived(FText Text);
    UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
    UFUNCTION() void OnSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    TWeakObjectPtr<UPlayerUIComponent> Source;
    TWeakObjectPtr<AActor> SourceActor;
    TWeakObjectPtr<APlayerController> BoundController;
    UPROPERTY(Transient) FText DisplayedNotice;
    FTimerHandle ExpiryTimer;
    uint64 Revision = 0;
    bool bConstructed = false;
};
