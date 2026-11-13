#pragma once
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Riding/RPGRidingCollectionReader.h"
#include "RPGRidingCollectionWidget.generated.h"

class UButton;
class UCheckBox;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;

/** Read-only persistent collection view. Closing never sends a mutation or mount command. */
UCLASS()
class PROJECT_RPG_API URPGRidingCollectionWidget final : public UUserWidget
{
    GENERATED_BODY()
public:
    using FPresenter = TFunction<FText(const FString&)>;
    void SetReadOwner(TSharedRef<FRPGRidingCollectionReader> InReader, FPresenter InPresenter);
    UFUNCTION() void CloseScreen();
    FSimpleMulticastDelegate OnClosed;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
private:
    friend struct FRPGRidingCollectionWidgetTestAccess;
    void Subscribe();
    void Detach();
    void RefreshView();
    bool TickView(float);
    UFUNCTION() void Reload();
    UFUNCTION() void SearchChanged(const FText& Value);
    UFUNCTION() void FavoritesChanged(bool bChecked);
    TWeakPtr<FRPGRidingCollectionReader> Reader;
    FPresenter Presenter;
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
};
