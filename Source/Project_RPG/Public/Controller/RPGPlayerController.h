// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "GameplayTagContainer.h"
#include "GenericTeamAgentInterface.h"
#include "Interface/InteractionInterface.h"
#include "GameState/RPGBossBattleGameState.h"
#include "InputMappingContext.h"
#include "UObject/StrongObjectPtr.h"
#include "RPGPlayerController.generated.h"

USTRUCT()
struct FInteractionData
{
	GENERATED_BODY()
public:
	FInteractionData() :
		CurrentInteractable(nullptr),
		LastInteractionCheckTime(0.f)
	{

	}

	TObjectPtr<AActor> CurrentInteractable;

	float LastInteractionCheckTime;
};

class ARPGHUD;
class ARPGPlayer;
class UDataAsset_InputConfig;
struct FInputActionValue;
class URPGItemBase;
class ARPGPickUpBase;
class URPGInventoryProjectionWidget;
class URPGInventoryProjectionViewModel;
class URPGRewardMailWidget;
class UQuickSlotComponent;
class URPGItemCommandComponent;
class URPGWidgetBase;
class URPGDungeonResultActionsWidget;
class URPGDungeonPartyCombatWidget;
class URPGSkillWindowComponent;
class FRPGWindowInputState;
class UUserWidget;
class URPGInteractionProgressComponent;
struct FKeyEvent;
struct FRPGKeyMapping;
struct FRPGInputBindingRestoreView;
struct FRPGInputBindingRestoreExtension;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGBossBattleRetryRequestChanged);

/**
 * 
 */
UCLASS()
class PROJECT_RPG_API ARPGPlayerController : public APlayerController, public IGenericTeamAgentInterface
{
	GENERATED_BODY()
public:
	ARPGPlayerController();
	virtual void OnNetCleanup(UNetConnection* Connection) override;
	/** Exact socket only during Super::OnNetCleanup, including its synchronous Logout. */
	UNetConnection* GetConnectionBeingCleanedUp() const;

	// Ability input wrappers (made public so they can be bound by external code)
	void Input_AbilityInputPressed(FGameplayTag InInputTag);
	void Input_AbilityInputReleased(FGameplayTag InInputTag);

	virtual FGenericTeamId GetGenericTeamId()const;
	
	void UpdateInteractionWidget() const;

	void DropItem(URPGItemBase* ItemToDrop, const int32 QuantityToDrop);

	UFUNCTION(BlueprintCallable, Category = "Input")
	void UpdateInputMappings();

	UFUNCTION(BlueprintCallable, Category = "Input")
	void ApplyKeyMapping(FGameplayTag InTag, FKey NewKey);

	/** Changes one authored binding while preserving its modifiers and sibling mappings.
	 * Authored changes succeed only while the owner, mappings and saved values remain stable through callbacks.
	 */
	UFUNCTION(BlueprintCallable, Category = "Input")
	bool TryApplyKeyBinding(FGameplayTag InTag, FKey DefaultKey, FKey NewKey, FText& OutError);

	/** Apply an authored binding with a synchronous, read-only GameFeature policy.
	 * The policy runs before and after the existing input-release hook; it must not be retained or mutate bindings.
	 * The legacy fallback binding keeps its existing path and caller-side preflight.
	 */
	bool TryApplyKeyBindingWithPolicy(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
		TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError);

	/** Restore the complete set only while every authored binding still has its expected current key.
	 * ExpectedBindings.Key is the current key; DefaultKey is the key to restore.
	 * ValidatePolicy is required and runs synchronously before and after the input-release hook.
	 * The callback may only inspect its borrowed view; it must not retain the view or mutate mappings.
	 * Preserves mapping metadata and requests one settings save after validating the committed state.
	 * Returns false if rebuild/save callbacks change the owner, mappings, aliases or staged settings.
	 * INI saving has no disk-success result; an interrupted commit is not automatically rolled back.
	 */
	bool TryRestoreDefaultKeyBindings(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy, FText& OutError);

	/** Restore primary and prepared feature bindings with one rebuild/save. An empty primary set is supported.
	 * Feature callbacks are borrowed; collision policy also runs after commit/rebuild/save.
	 * No commit rollback is implied.
	 */
	bool TryRestoreDefaultKeyBindingsWithAdditional(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
		const FRPGInputBindingRestoreExtension& Additional, FText& OutError);

	UFUNCTION(BlueprintPure, Category = "Input")
	FKey GetCurrentKeyForBinding(FGameplayTag InTag, FKey DefaultKey) const;

	UFUNCTION(BlueprintCallable, Category = "Input")
	FKey GetCurrentKeyForTag(FGameplayTag InTag) const;
	const UDataAsset_InputConfig* GetInputConfig() const { return InputConfigDataAsset; }
	const UDataAsset_InputConfig* GetSourceInputConfig() const
	{
		return SourceInputConfig ? SourceInputConfig.Get() : InputConfigDataAsset;
	}

	/** Shared lock-on state consumed by independent skill targeting policies. */
	void SetSkillLockedTarget(AActor* InTarget);

	UFUNCTION(BlueprintPure, Category = "RPG|Skill|Targeting")
	AActor* GetSkillLockedTarget() const;

	/** Local UI entry point; the server validates encounter membership. */
	UFUNCTION(BlueprintCallable, Category = "Boss|Battle")
	void RequestBossBattleRestart();

	UFUNCTION(BlueprintCallable, Category = "Boss|Battle")
	bool TryRequestBossBattleRestart();

	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	bool CanRequestBossBattleRestart() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	bool IsBossBattleRestartPending() const;

	UFUNCTION(BlueprintPure, Category = "Boss|Battle")
	FString GetBossBattleRestartErrorCode() const { return BossBattleRestartError; }

	UPROPERTY(BlueprintAssignable, Category = "Boss|Battle")
	FRPGBossBattleRetryRequestChanged OnBossBattleRetryRequestChanged;

	/** Local UI entry; backend terminal state is checked before leaving the server. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool RequestDungeonReturnToLobby();

	/** Opt-in developer combat confirmation; does not directly send an abandonment POST. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Online|Dungeon")
	bool ShowDungeonAbandonmentConfirmation();
	void CloseDungeonAbandonmentConfirmation(URPGDungeonResultActionsWidget* ExpectedWidget);

	/** No client-supplied identity or generation; server closes only this owned socket. */
	UFUNCTION(Server, Reliable)
	void ServerRequestDungeonLobbyDisconnect();

	/** Void dispatcher adapter for the authored online result ExitButton. */
	UFUNCTION()
	void RequestDungeonReturnFromResult();

	UFUNCTION(Server, Reliable)
	void ServerRequestBossBattleRestart(uint32 RequestId, int32 ExpectedBattleRevision);

	UFUNCTION(Client, Reliable)
	void ClientBossBattleRestartResult(uint32 RequestId, bool bAccepted);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ShowInventoryProjection();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ShowEquipmentProjection();

	/** Fixed P shortcut, separate from the legacy bag action mapped to I. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ToggleEquipmentProjection();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ShowRewardMail();

	/** One owner-scoped presentation model across bag, equipment and mail windows. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|UI")
	URPGInventoryProjectionViewModel* GetInventoryProjectionViewModel();

	/** Local item input shares reservations with every inventory window, even while closed. */
	bool TryConsumeInventoryItem(FGuid ItemId, FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Skill|UI")
	void ToggleSkillWindow();

	/** Window preview routing keeps UI hotkeys available while a child has focus. */
	bool HandlePlayerWindowKeyDown(UUserWidget* SourceWindow, const FKeyEvent& Event);

protected:
	virtual void SetupInputComponent() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void OnRep_Pawn() override;
	virtual void ClientRestart_Implementation(APawn* NewPawn) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	//void PerformInteractionCheck_LineTrace();
	void PerformInteractionCheck_Around();
	
	void FoundInteractable(AActor* NewInteractable);
	void NoInteractableFound();
	void BeginInteract();
	void EndInteract();
	void Interact();
	
	// 퀵슬롯 입력 처리
	UFUNCTION()
	void PressSkillSlot(int32 SlotIndex);
	void ReleaseSkillSlot(int32 SlotIndex);

	UFUNCTION()
	void UseItemSlot(int32 SlotIndex);

	void RefreshControlledPawnReferences();
	friend struct FRPGQuickSlotProjectionTestAccess;
	void EnableCameraZoom();
	void UpdateBossBattlePresentation();
	void UpdateDungeonPartyCombatPresentation();
	void UpdateBossMechanicPresentation(const FRPGBossBattleSnapshot& Snapshot);
	void EnsureDungeonResultActions(const FRPGBossBattleSnapshot& Snapshot);

	UFUNCTION()
	void HandleBossBattleSnapshotChanged(const FRPGBossBattleSnapshot& Snapshot);

private:
	friend struct FRPGDungeonConnectionTestAccess;
	friend struct FRPGBossBattleRetryRequestTestAccess;
	friend struct FRPGDungeonAbandonmentTestAccess;
	friend struct FRPGItemWindowTestAccess;
	void UpdateBossBattleRetryRequest();
	void ClearBossBattleRetryRequest(const FString& ErrorCode, bool bNotify = true);
	uint32 BossBattleRetrySerial = 0;
	uint32 PendingBossBattleRetryId = 0;
	int32 PendingBossBattleRetryRevision = INDEX_NONE;
	TWeakObjectPtr<UWorld> PendingBossBattleRetryWorld;
	double BossBattleRetryDeadline = 0.0;
	double NextServerBossBattleRetryTime = 0.0;
	FString BossBattleRestartError;
	TWeakObjectPtr<UNetConnection> ConnectionBeingCleanedUp;
	UPROPERTY()
	TObjectPtr<ARPGHUD> HUD;

	/** Projection-based inventory presentation. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Inventory")
	TSoftClassPtr<URPGInventoryProjectionWidget>
		InventoryProjectionWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Inventory")
	TSoftClassPtr<URPGInventoryProjectionWidget>
		EquipmentProjectionWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Inventory")
	TSoftClassPtr<URPGRewardMailWidget> RewardMailWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<URPGInventoryProjectionWidget> InventoryProjectionWidget;

	UPROPERTY(Transient)
	TObjectPtr<URPGInventoryProjectionWidget> EquipmentProjectionWidget;

	UPROPERTY(Transient)
	TObjectPtr<URPGRewardMailWidget> RewardMailWidget;

	UPROPERTY(Transient)
	TObjectPtr<URPGInventoryProjectionViewModel> InventoryProjectionViewModel;
	bool bItemPresentationEnded = false;

	/** Existing result panels; each client owns its own instance. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Boss Battle")
	TSoftClassPtr<URPGWidgetBase> BossVictoryWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI|Boss Battle")
	TSoftClassPtr<URPGWidgetBase> BossDefeatWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<URPGWidgetBase> BossResultWidget;

	/** Local remote-client presentation; no gameplay authority or replicated widget state. */
	UPROPERTY(Transient)
	TObjectPtr<URPGWidgetBase> BossMechanicWidget;

	UPROPERTY(Transient)
	TObjectPtr<URPGDungeonResultActionsWidget> DungeonResultActionsWidget;
	UPROPERTY(Transient)
	TObjectPtr<URPGDungeonResultActionsWidget> GameplayAbandonmentWidget;
	/** Optional passive party HUD. Unset/invalid classes keep the native fallback. */
	UPROPERTY(EditDefaultsOnly, Category = "UI|Dungeon Party", meta = (AllowAbstract = "false"))
	TSubclassOf<URPGDungeonPartyCombatWidget> DungeonPartyCombatWidgetClass;
	UPROPERTY(Transient)
	TObjectPtr<URPGDungeonPartyCombatWidget> DungeonPartyCombatWidget;
	double NextPartyCombatRefreshTime = 0.0;

	TWeakObjectPtr<ARPGBossBattleGameState> ObservedBossBattleGameState;
	int32 LastPresentedBossBattleRevision = INDEX_NONE;

	bool EnsureInventoryProjectionWidget(bool bEquipment);
	void DisplayInventoryProjection(URPGInventoryProjectionWidget* Widget);
	void CloseProjectionWindow(URPGInventoryProjectionWidget* Widget);
	void FocusProjectionWindow(URPGInventoryProjectionWidget* Widget);
	bool IsProjectionWindow(const UUserWidget* Widget) const;
	bool CloseTopPlayerWindow();
	bool ToggleInventoryProjection();
	void HandleProjectionWindowClosed(URPGInventoryProjectionWidget* Widget);
	TSharedPtr<FRPGWindowInputState> WindowInputState;

	UPROPERTY(VisibleAnywhere, Category = "Character|Interaction")
	TScriptInterface<IInteractionInterface> TargetInteractable;

	UPROPERTY(EditDefaultsOnly, Category = "Pickup")
	TSubclassOf<ARPGPickUpBase> PickupClass;

	// 퀵슬롯 컴포넌트
	UPROPERTY(VisibleAnywhere, Category = "QuickSlot")
	TObjectPtr<UQuickSlotComponent> QuickSlotComponent;

	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<URPGItemCommandComponent> ItemCommandComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RPG|Skill|UI", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<URPGSkillWindowComponent> SkillWindowComponent;

	UPROPERTY(EditDefaultsOnly)
	float PickupCheckRadius = 40.f;

	float InteractionCheckFequency=0.1f;

	float InteractionCheckDistance=225.f;

	FTimerHandle TimerHandle_Interaction;

	FInteractionData InteractionData;

	FTimerHandle ZoomDelayHandle;

	bool bCanZoom=true;

	UPROPERTY()
		ARPGPlayer* PlayerCharacter;

	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> SkillLockedTarget;

	FGenericTeamId PlayerTeamID;

#pragma region Inputs

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "CharacterData", meta = (AllowPrivateAccess = "true"))
		UDataAsset_InputConfig* InputConfigDataAsset;
	UPROPERTY(Transient)
	TObjectPtr<UDataAsset_InputConfig> SourceInputConfig;
	bool bApplyingKeyBinding = false;

	bool RestoreDefaultKeyBindingsInternal(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
		const FRPGInputBindingRestoreExtension* Additional, FText& OutError);
	bool ApplyKeyBindingInternal(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
		FText& OutError, bool bSaveSettings, TFunctionRef<bool(FText&)>* ValidatePolicy = nullptr);
	TStrongObjectPtr<UInputMappingContext> SkillQuickSlotInputContext;

	FVector2D SwitchDirection = FVector2D::ZeroVector;

	// --- [자동화 규칙] 함수 이름은 'Input_태그명'이어야 합니다. ---

	UFUNCTION()
	void Input_Move(const FInputActionValue& InputActionValue);

	UFUNCTION()
	void Input_Look(const FInputActionValue& InputActionValue);

	UFUNCTION()
	void Input_CameraZoom(const FInputActionValue& InputActionValue);

	/** SwitchTarget 전용 래퍼 (Triggered/Completed 통합 처리) */
	UFUNCTION()
	void Input_SwitchTarget(const FInputActionValue& InputActionValue);

	UFUNCTION()
	void Input_PickUp_Items(const FInputActionValue& InputActionValue);

	/** 일시정지 메뉴 토글 */
	UFUNCTION()
	void Input_ToggleMenu(const FInputActionValue& Value);

	/** 장비창 토글 (태그: InputTag.ShowEquipmentWidget) */
	UFUNCTION()
	void Input_ShowEquipmentWidget(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleOptionMenu();

	void ShowEquipmentWidget();
#pragma endregion

public:
	FORCEINLINE bool IsInteracting() const { return GetWorldTimerManager().IsTimerActive(TimerHandle_Interaction); };
private:
	UPROPERTY(VisibleAnywhere, Category="Interaction")
	TObjectPtr<URPGInteractionProgressComponent> InteractionProgress;
};
