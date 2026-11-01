#pragma once

#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Progression/RPGCharacterProgressionSnapshot.h"
#include "RPGCharacterConfigurationWidget.generated.h"

class UButton;
class UTextBlock;
class UVerticalBox;
class URPGOnlineLobbyWidget;
class URPGCharacterConfigurationWidget;

/** Per-control event binding; carries indices, never authority or a saved record. */
UCLASS()
class URPGCharacterConfigurationBinding final : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UComboBoxString> Box;
	TWeakObjectPtr<URPGCharacterConfigurationWidget> Editor;
	int32 Kind = 0, Row = 0, Tier = 0;
	UFUNCTION() void Selected(FString Option, ESelectInfo::Type SelectionType);
};

/** Local lobby draft. Only a confirmed backend result becomes the saved configuration. */
UCLASS()
class PROJECT_RPG_API URPGCharacterConfigurationWidget final : public UUserWidget
{
	GENERATED_BODY()
public:
	void SetLobby(URPGOnlineLobbyWidget* InLobby);
	void RefreshContext();
	bool IsRequestInFlight() const { return bRequestInFlight; }
	bool HasUnresolvedSave() const { return PendingCommand.RequestId.IsValid(); }
	/** Read-only view of the confirmed record for this live account/character. */
	const FRPGCharacterProgressionSnapshot* GetConfirmedProgression() const;
	/** One initial read per context; failures keep the explicit reload action. */
	void EnsureInitialLoad();
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool LoadSavedConfiguration();
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool SaveConfiguration();
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool ResetDraft();
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool SetSkillLevel(FGameplayTag Skill, int32 Level);
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool SetTripodChoice(FGameplayTag Skill, int32 Tier, const FString& PersistenceId);
	/** Selecting an already slotted skill moves/swaps it; empty slots stay empty. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") bool SetCombatSlot(int32 Index, FGameplayTag Skill);
	/** Explicit local draft action. Keeps occupied slots, skill levels and SP unchanged. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Progression|Lobby") int32 EquipUnslottedSkills();
	UFUNCTION(BlueprintPure, Category = "RPG|Progression|Lobby") bool CanEquipUnslottedSkills() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Progression|Lobby") bool HasUnsavedChanges() const;
	UFUNCTION(BlueprintPure, Category = "RPG|Progression|Lobby") FText GetSkillPreview(FGameplayTag Skill) const;
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
private:
	friend class FRPGMenuFrontendTest;
	friend class URPGCharacterConfigurationBinding;
	void HandleChoice(const URPGCharacterConfigurationBinding& Binding, int32 Choice);
	void HandleResult(uint64 Serial, bool bWrite, FRPGCharacterProgressionResult Result);
	void RebuildChoices();
	void RefreshControls();
	void RefreshLobby();
	bool CanRequest(bool bWrite, bool bRetry = false) const;
	bool CanEdit() const;
	bool IsCurrentContext() const;
	int32 UsedDraftSP() const;
	void AddChoice(int32 Kind, int32 Row, int32 Tier, const FText& Label, const TArray<FString>& Options);
	UFUNCTION() void ClickLoad();
	UFUNCTION() void ClickSave();
	UFUNCTION() void ClickReset();
	UFUNCTION() void ClickEquipUnslotted();
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SummaryText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ResultText;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Choices;
	UPROPERTY(Transient) TObjectPtr<UButton> LoadButton;
	UPROPERTY(Transient) TObjectPtr<UButton> SaveButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ResetButton;
	UPROPERTY(Transient) TObjectPtr<UButton> EquipUnslottedButton;
	UPROPERTY(Transient) TArray<TObjectPtr<URPGCharacterConfigurationBinding>> Bindings;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> PreviewTexts;
	TWeakObjectPtr<URPGOnlineLobbyWidget> Lobby;
	TWeakObjectPtr<UWorld> ContextWorld;
	TWeakObjectPtr<APlayerController> ContextController;
	FString CharacterId, SteamId;
	FText Message;
	FRPGCharacterProgressionSnapshot Saved;
	TArray<FRPGSavedProgressionSkill> DraftSkills;
	TArray<FGameplayTag> DraftSlots;
	FRPGCharacterSkillConfiguration PendingCommand;
	uint64 RequestSerial = 0;
	bool bAttached = false, bRequestInFlight = false, bLoaded = false, bNeedsReload = false, bUpdating = false;
	bool bInitialReadAttempted = false;
	bool bProgressionReadConfirmed = false;
};
