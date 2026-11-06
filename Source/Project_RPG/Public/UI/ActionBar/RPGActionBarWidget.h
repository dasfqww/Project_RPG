#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateBrush.h"
#include "RPGActionBarWidget.generated.h"

class URPGActionBarContext;

/** Default native bar; authored subclasses can use the same revision-bound input API. */
UCLASS(Blueprintable)
class PROJECT_RPG_API URPGActionBarWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void BindContext(URPGActionBarContext* InContext);
    void UnbindContext();
    UFUNCTION(BlueprintPure) URPGActionBarContext* GetActionBarContext() const { return Context; }
    UFUNCTION(BlueprintCallable) void PressAction(FName ActionId);
    UFUNCTION(BlueprintCallable) void ReleaseAction(FName ActionId);
    UFUNCTION(BlueprintImplementableEvent) void OnActionBarBound();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
private:
    UPROPERTY(Transient) TObjectPtr<URPGActionBarContext> Context;
    TArray<FSlateBrush> IconBrushes;
    TSet<FName> PointerActions;
    uint64 BindingRevision = 0;
    FName SourceFor(FName ActionId) const;
};
