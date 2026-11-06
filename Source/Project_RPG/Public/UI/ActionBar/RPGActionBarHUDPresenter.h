#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Components/SlateWrapperTypes.h"
#include "RPGActionBarHUDPresenter.generated.h"
class UUserWidget;
class UWidget;
class URPGActionBarContext;
class URPGActionBarWidget;

/** Replaces only two authored canvas regions, retaining their layout and original widgets. */
UCLASS()
class PROJECT_RPG_API URPGActionBarHUDPresenter : public UObject
{
    GENERATED_BODY()
public:
    bool Bind(UUserWidget* Root, URPGActionBarContext* InContext);
    void Unbind();
private:
    void Refresh();
    void RefreshOnce();
    void Restore();
    URPGActionBarWidget* CreateReplacement(UWidget* Original, TSubclassOf<URPGActionBarWidget> Class);
    UPROPERTY(Transient) TObjectPtr<UUserWidget> HUDRoot;
    UPROPERTY(Transient) TObjectPtr<URPGActionBarContext> Context;
    UPROPERTY(Transient) TObjectPtr<UWidget> BaseLeft;
    UPROPERTY(Transient) TObjectPtr<UWidget> BaseCenter;
    UPROPERTY(Transient) TObjectPtr<URPGActionBarWidget> Left;
    UPROPERTY(Transient) TObjectPtr<URPGActionBarWidget> Center;
    ESlateVisibility LeftVisibility = ESlateVisibility::SelfHitTestInvisible;
    ESlateVisibility CenterVisibility = ESlateVisibility::SelfHitTestInvisible;
    FDelegateHandle ChangedHandle;
    bool bOverridden = false;
    bool bRefreshing = false;
    bool bRefreshPending = false;
};
