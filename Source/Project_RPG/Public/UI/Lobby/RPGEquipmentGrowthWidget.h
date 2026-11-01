#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "UI/Lobby/RPGEquipmentGrowthModel.h"
#include "RPGEquipmentGrowthWidget.generated.h"

class UButton;
class UTextBlock;
class URPGOnlineLobbyWidget;
class URPGEquipmentMutationSubsystem;

/** Current-item selectors and explicit confirmation over the existing durable commands. */
UCLASS()
class PROJECT_RPG_API URPGEquipmentGrowthWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetLobby(URPGOnlineLobbyWidget* InLobby);
	void RefreshContext();
	void SetScreenOpen(bool bOpen);
	void OpenScreen();
	bool IsBusy() const;
	bool HasUnresolvedCommand() const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
private:
	friend class FRPGEquipmentGrowthWidgetTest;
	bool ReadScope(FRPGEquipmentMutationScope& Out) const;
	bool IsCurrentContext() const;
	bool CanRequest(bool bWrite, bool bRetry=false) const;
	FRPGEquipmentMutationView View() const;
	FRPGEquipmentGrowthPreview Preview() const;
	void RebuildChoices(const FRPGEquipmentMutationView& Current);
	void Draw();
	void EnsureOpened();
	UFUNCTION() void HandleChanged();
	UFUNCTION() void SelectedOperation(FString Option, ESelectInfo::Type Type);
	UFUNCTION() void SelectedSource(FString Option, ESelectInfo::Type Type);
	UFUNCTION() void SelectedTarget(FString Option, ESelectInfo::Type Type);
	UFUNCTION() void Reload();
	UFUNCTION() void Prepare();
	UFUNCTION() void Confirm();
	UFUNCTION() void Cancel();
	UFUNCTION() void Retry();
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> OperationBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> SourceBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> TargetBox;
	UPROPERTY(Transient) TObjectPtr<UComboBoxString> CraftBox;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PreviewText;
	UPROPERTY(Transient) TObjectPtr<UButton> ReloadButton;
	UPROPERTY(Transient) TObjectPtr<UButton> PrepareButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ConfirmButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CancelButton;
	UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
	TWeakObjectPtr<URPGOnlineLobbyWidget> Lobby;
	TWeakObjectPtr<URPGEquipmentMutationSubsystem> Mutations;
	TWeakObjectPtr<UWorld> ContextWorld;
	TWeakObjectPtr<APlayerController> ContextController;
	FRPGEquipmentMutationScope Scope;
	TArray<FRPGEquipmentGrowthChoice> Options;
	FRPGEquipmentGrowthPreview Confirmation;
	FGuid SourceId, TargetId, LastRefreshRequest;
	FText LocalMessage;
	bool bAttached=false, bScreenOpen=false, bOpened=false, bConfirming=false;
	bool bUpdating=false, bRefreshing=false, bHandlingChange=false;
};
