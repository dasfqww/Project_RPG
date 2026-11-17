#pragma once

#include "UI/RPGWidgetBase.h"
#include "UI/MVVM/RPGGreatSwordIdentityViewModel.h"
#include "RPGGreatSwordIdentityWidget.generated.h"

class UWidgetAnimation;

/** Identity View: owns layout/paint and local possession, not gameplay state. */
UCLASS(meta=(DisableNativeTick))
class PROJECT_RPG_API URPGGreatSwordIdentityWidget : public URPGWidgetBase
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="UI|Identity") void SetIdentitySource(UAbilitySystemComponent* Source);
    UFUNCTION(BlueprintPure, Category="UI|Identity") FRPGGreatSwordIdentityPresentation GetDisplayedIdentity() const;
    UFUNCTION(BlueprintPure, Category="UI|Identity") FText GetIdentityText() const;
    UFUNCTION(BlueprintPure, Category="UI|Identity") URPGGreatSwordIdentityViewModel* GetIdentityViewModel() const { return ViewModel; }
protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void BeginDestroy() override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Cull,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bEnabled) const override;
    UPROPERTY(EditDefaultsOnly, Category="Identity") bool bAutoBindOwningPawn = true;
    // Retain the authored bindings and animation for existing module compatibility.
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UUserWidget> WBP_IdentityGaugeBar;
    UPROPERTY(Transient, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> InterpUp;
private:
    friend struct FRPGGreatSwordIdentityWidgetTestAccess;
    URPGGreatSwordIdentityViewModel& EnsureViewModel();
    void UnbindController();
    void Present();
    UFUNCTION() void OnOwningPawnChanged(APawn* OldPawn, APawn* NewPawn);
    UPROPERTY(Transient) TObjectPtr<URPGGreatSwordIdentityViewModel> ViewModel;
    TWeakObjectPtr<APlayerController> Controller;
    FDelegateHandle PresentationChanged;
    bool bConstructed = false;
    bool bLastBerserk = false;
};
