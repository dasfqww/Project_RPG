#pragma once
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Components/ComboBoxString.h"
#include "Components/Button.h"
#include "Riding/RPGRidingCollectionReader.h"
#include "Riding/RPGRidingRegistrationClient.h"
#include "RPGRidingCollectionWidget.generated.h"

class FRPGRidingPreferencesClient;
class URPGRidingPreferencesWidget;
class UButton;
class UCheckBox;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UComboBoxString;

UCLASS()
class PROJECT_RPG_API URPGRidingBrowseButton final : public UButton
{
    GENERATED_BODY()
public:
    FString MountId;
    TWeakObjectPtr<class URPGRidingCollectionWidget> Collection;
    UFUNCTION() void Browse();
};

/** Collection view and explicit registration consent. Closing never sends a mutation or mount command. */
UCLASS()
class PROJECT_RPG_API URPGRidingCollectionWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    using FPresenter = TFunction<FText(const FString&)>;
    void SetReadOwner(TSharedRef<FRPGRidingCollectionReader> InReader, FPresenter InPresenter, FPresenter InDescription = {});
    void SetMountAction(TFunction<bool(const FRPGRidingSelector&)> InAction) { MountAction = MoveTemp(InAction); }
    void SetRegistrationOwner(TSharedRef<FRPGRidingRegistrationClient> InOwner);
    void SetPreferencesOwner(TSharedRef<FRPGRidingPreferencesClient> InOwner);
    void OpenRegistration();
    void OpenPreferences();
    UFUNCTION() void CloseScreen();
    FSimpleMulticastDelegate OnClosed;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    friend struct FRPGRidingCollectionWidgetTestAccess;
    friend struct FRPGRidingRegistrationUiTestAccess;
    friend struct FRPGRidingPreferencesUiTestAccess;
    friend class URPGRidingBrowseButton;
    void Browse(const FString& MountId);
    UFUNCTION() void MountSelected();
    UFUNCTION() void BackToCollection();
    UPROPERTY(Transient) TObjectPtr<class UWidgetSwitcher> Pages;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PreferencesMountName;
    UPROPERTY(Transient) TObjectPtr<UButton> MountButton;
    FText MountFeedback;
    TFunction<bool(const FRPGRidingSelector&)> MountAction;
    UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RegistrationUnavailable;
    FSlateBrush MountPortrait(const FString& Id, float Size);
    TMap<FName,TSharedPtr<struct FSlateDynamicImageBrush>> PortraitBrushes;
    FString BrowsedMount;
    bool bSortByName = false;
    UFUNCTION() void ToggleSort();
    UFUNCTION() void FocusSearch();
    UPROPERTY(Transient) TObjectPtr<UTextBlock> SortLabel;
    void RefreshRegistration();
    UFUNCTION() void ReloadRegistration();
    UFUNCTION() void RegistrationSelection(FString Value, ESelectInfo::Type Type);
    UFUNCTION() void ReviewRegistration();
    UFUNCTION() void ConfirmRegistration();
    UFUNCTION() void DismissRegistration();
    UFUNCTION() void RecoverRegistration();
    UFUNCTION() void RetryRegistration();
    UFUNCTION() void StorageRegistration();
    UFUNCTION() void RefreshRegistrationAssets();
    TWeakPtr<FRPGRidingRegistrationClient> Registration;
    TWeakPtr<FRPGRidingPreferencesClient> PreferencesOwner;
    UPROPERTY(Transient) TObjectPtr<URPGRidingPreferencesWidget> PreferencesPanel;
    FDelegateHandle RegistrationChanged;
    TArray<FGuid> RegistrationIds;
    FGuid RegistrationSnapshot, RegistrationReview, SelectedRegistration;
    bool bRegistrationUpdating = false;
    void Subscribe();
    void Detach();
    void RefreshView();
    bool TickView(float);
    UFUNCTION() void Reload();
    UFUNCTION() void SearchChanged(const FText& Value);
    UFUNCTION() void FavoritesChanged(bool bChecked);
    TWeakPtr<FRPGRidingCollectionReader> Reader;
    FPresenter Presenter;
    FPresenter Description;
    FRPGRidingCollectionContext BoundContext;
    FDelegateHandle Changed;
    FTSTicker::FDelegateHandle TickHandle;
    FString RowsKey;
    bool bClosing = false, bUpdating = false;
    UPROPERTY(Transient) TObjectPtr<UButton> ReloadButton;
    UPROPERTY(Transient) TObjectPtr<UButton> CloseButton;
    UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Search;
    UPROPERTY(Transient) TObjectPtr<UCheckBox> FavoritesOnly;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Settings;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Empty;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> Rows;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RegistrationStatus;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> RegistrationGroup;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> RegistrationConsent;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> RegistrationRows;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationReload;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationReviewButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationConfirm;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationDismiss;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationRecover;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationRetry;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationStorage;
    UPROPERTY(Transient) TObjectPtr<UButton> RegistrationAssets;
};
