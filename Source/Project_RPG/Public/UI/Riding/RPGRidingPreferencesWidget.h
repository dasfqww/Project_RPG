#pragma once
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Containers/Ticker.h"
#include "Riding/RPGRidingPreferencesClient.h"
#include "RPGRidingPreferencesWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;

UCLASS()
class PROJECT_RPG_API URPGRidingPreferencesWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetOwner(TSharedRef<FRPGRidingPreferencesClient> InOwner);
    void DetachOwner();
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
private:
    friend struct FRPGRidingPreferencesUiTestAccess;
    void RefreshView();
    bool TickView(float);
    UFUNCTION() void Reload();
    UFUNCTION() void SelectChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void FavoriteChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void ModeChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void SpecificChanged(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void ToggleFavorite();
    UFUNCTION() void ClearAll();
    UFUNCTION() void ResetDraft();
    UFUNCTION() void Review();
    UFUNCTION() void Confirm();
    UFUNCTION() void Dismiss();
    UFUNCTION() void Recover();
    UFUNCTION() void Retry();
    UFUNCTION() void Storage();
    UFUNCTION() void Assets();
    TWeakPtr<FRPGRidingPreferencesClient> Owner;
    FDelegateHandle Change;
    FTSTicker::FDelegateHandle Tick;
    TArray<FString> MountIds;
    FString FavoriteTarget;
    FGuid Snapshot, Consent;
    bool bUpdating = false, bClosing = false;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Group;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Favorites;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ReviewText;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> SelectedBox;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> FavoriteBox;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> ModeBox;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> SpecificBox;
    UPROPERTY(Transient) TObjectPtr<UButton> ReloadButton;
    UPROPERTY(Transient) TObjectPtr<UButton> FavoriteButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> FavoriteButtonText;
    UPROPERTY(Transient) TObjectPtr<UButton> ClearButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ResetButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ReviewButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
    UPROPERTY(Transient) TObjectPtr<UButton> DismissButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RecoverButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> StorageButton;
    UPROPERTY(Transient) TObjectPtr<UButton> AssetsButton;
};
