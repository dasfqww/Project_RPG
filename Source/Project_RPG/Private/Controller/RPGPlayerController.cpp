// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/RPGPlayerController.h"
#include "Controller/RPGInputMappingClone.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Input/RPGInputBindingRestoreView.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/RPGInputComponent.h"
#include "RPGGameplayTags.h"
#include "RPGInputTags.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Inventory/Equipment/RPGEquipmentWidget.h"
#include "UI/Inventory/Projection/RPGInventoryProjectionWidget.h"
#include "UI/Inventory/Projection/RPGRewardMailWidget.h"
#include "UI/MVVM/RPGInventoryProjectionViewModel.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGDungeonPartyCombatComponent.h"
#include "UI/Content/RPGDungeonPartyCombatWidget.h"
#include "Component/RPGItemCommandComponent.h"
#include "Item/PickUp/RPGPickUpBase.h"
#include "Components/WidgetComponent.h"
#include "UI/RPGItemNameWidget.h"
#include "Item/RPGItemBase.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "UI/Skill/RPGSkillWindow.h"
#include "UI/RPGWindowInputState.h"
#include "UI/RPGWindowPlacement.h"
#include "Input/Events.h"
#include "UObject/StrongObjectPtr.h"
#include "Settings/RPGGameUserSettings.h"
#include "GameState/RPGBossBattleGameState.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "UI/RPGWidgetBase.h"
#include "UI/Content/RPGBossBattleInfoWidget.h"
#include "UI/RPGBossBattleRetryBinding.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGItemCommandE2EProbeComponent.h"
#include "Tests/RPGSkillNetworkE2EProbeComponent.h"
#include "Tests/RPGMovementSecurityE2EProbeComponent.h"
#include "Tests/RPGTargetDataWireProbe.h"
#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

#include "RPGDebugHelper.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "Widgets/Layout/Anchors.h"

ARPGPlayerController::ARPGPlayerController()
{
	PlayerTeamID = FGenericTeamId(0);
	BossVictoryWidgetClass = TSoftClassPtr<URPGWidgetBase>(FSoftObjectPath(TEXT(
		"/Game/Blueprints/UI/GameContents/BossBattle/WBP_VictoryWidget.WBP_VictoryWidget_C")));
	BossDefeatWidgetClass = TSoftClassPtr<URPGWidgetBase>(FSoftObjectPath(TEXT(
		"/Game/Blueprints/UI/GameContents/BossBattle/WBP_DefeatWidget.WBP_DefeatWidget_C")));
	InventoryProjectionWidgetClass =
		TSoftClassPtr<URPGInventoryProjectionWidget>(
			FSoftObjectPath(TEXT(
				"/Game/Blueprints/UI/Inventory/Projection/"
				"WBP_InventoryProjection.WBP_InventoryProjection_C")));
	EquipmentProjectionWidgetClass =
		TSoftClassPtr<URPGInventoryProjectionWidget>(
			FSoftObjectPath(TEXT(
				"/Game/Blueprints/UI/Equipment/Projection/"
				"WBP_EquipmentProjection.WBP_EquipmentProjection_C")));
	RewardMailWidgetClass = TSoftClassPtr<URPGRewardMailWidget>(FSoftObjectPath(TEXT(
		"/Game/Blueprints/UI/Inventory/Projection/WBP_RewardMail.WBP_RewardMail_C")));
	CreateDefaultSubobject<URPGInventoryProjectionComponent>(
		TEXT("InventoryProjection"));
	CreateDefaultSubobject<URPGDungeonPartyCombatComponent>(TEXT("DungeonPartyCombat"));
	ItemCommandComponent = CreateDefaultSubobject<URPGItemCommandComponent>(
		TEXT("ItemCommands"));
	SkillWindowComponent = CreateDefaultSubobject<URPGSkillWindowComponent>(TEXT("SkillWindow"));
	WindowInputState = MakeShared<FRPGWindowInputState>();
	SkillWindowComponent->SetWindowInputState(WindowInputState.ToSharedRef());
#if !UE_BUILD_SHIPPING
	CreateDefaultSubobject<URPGItemCommandE2EProbeComponent>(
		TEXT("ItemCommandE2EProbe"));
	CreateDefaultSubobject<URPGSkillNetworkE2EProbeComponent>(
		TEXT("SkillNetworkE2EProbe"));
	CreateDefaultSubobject<URPGMovementSecurityE2EProbeComponent>(
		TEXT("MovementSecurityE2EProbe"));
	CreateDefaultSubobject<URPGTargetDataWireProbeComponent>(TEXT("TargetDataWireProbe"));
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossMapE2E")))
	{
		CreateDefaultSubobject<URPGBossMapE2EProbeComponent>(TEXT("BossMapE2EProbe"));
	}
#endif
}

void ARPGPlayerController::UpdateInputMappings()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem) return;

	URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings();
	if (!UserSettings) return;

	// Restore legacy primary keys first, then per-binding overrides.
	// Work on a snapshot so callbacks cannot invalidate the saved-settings iterator.
	const TArray<FRPGKeyMapping> SavedMappings(UserSettings->CustomKeyMappings);
	for (const bool bBindingPass : { false, true })
	{
		for (const bool bSlotPass : { false, true })
		{
			for (const FRPGKeyMapping& Mapping : SavedMappings)
			{
				bool bSkill = false;
				int32 Index = INDEX_NONE;
				const bool bSlot = FRPGQuickSlotInputBindings::ParseTag(Mapping.InputTag, bSkill, Index);
				if (bSlot == bSlotPass && Mapping.DefaultKey.IsValid() == bBindingPass)
				{
					FText Error;
					ApplyKeyBindingInternal(Mapping.InputTag, Mapping.DefaultKey, Mapping.Key, Error, false);
				}
			}
		}
	}
}

void ARPGPlayerController::ApplyKeyMapping(FGameplayTag InTag, FKey NewKey)
{
	FText Error;
	if (!TryApplyKeyBinding(InTag, FKey(), NewKey, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Input binding rejected: %s"), *Error.ToString());
	}
}

bool ARPGPlayerController::TryApplyKeyBinding(FGameplayTag InTag, FKey DefaultKey, FKey NewKey, FText& OutError)
{
	return ApplyKeyBindingInternal(InTag, DefaultKey, NewKey, OutError, true);
}

bool ARPGPlayerController::TryApplyKeyBindingWithPolicy(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
	TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError)
{
	return ApplyKeyBindingInternal(InTag, DefaultKey, NewKey, OutError, true, &ValidatePolicy);
}


bool ARPGPlayerController::TryRestoreDefaultKeyBindings(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy, FText& OutError)
{
	return RestoreDefaultKeyBindingsInternal(ExpectedBindings, ValidatePolicy, nullptr, OutError);
}

bool ARPGPlayerController::TryRestoreDefaultKeyBindingsWithAdditional(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
	const FRPGInputBindingRestoreExtension& Additional, FText& OutError)
{
	return RestoreDefaultKeyBindingsInternal(ExpectedBindings, ValidatePolicy, &Additional, OutError);
}

bool ARPGPlayerController::RestoreDefaultKeyBindingsInternal(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
	const FRPGInputBindingRestoreExtension* Additional, FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsInGameThread() || bApplyingKeyBinding || (ExpectedBindings.IsEmpty() && (!Additional || Additional->Contexts.IsEmpty())))
	{
		OutError = NSLOCTEXT("RPGInputOptions", "DefaultsBusy", "키 설정을 복원할 수 없습니다. 잠시 후 다시 시도해 주세요.");
		return false;
	}
	TStrongObjectPtr<ARPGPlayerController> KeepAlive(this);
	// Notify after the request guards are released, including failures after a live key change.
	bool bPrimaryMappingsChanged = false;
	const TWeakObjectPtr<ULocalPlayer> ChangedPlayer(GetLocalPlayer());
	const TWeakObjectPtr<UWorld> ChangedWorld(GetWorld());
	ON_SCOPE_EXIT
	{
		if (bPrimaryMappingsChanged)
		{
			IRPGInputOptionsRuntime::NotifyMappingsChanged(ChangedPlayer.Get(), ChangedWorld.Get());
		}
	};
	TGuardValue<bool> ApplyingGuard(bApplyingKeyBinding, true);
	// Copy before settings lookup can invoke engine initialization callbacks.
	const TArray<FRPGKeyMapping> RequestedBindings(ExpectedBindings);
	TStrongObjectPtr<UWorld> BindingWorld(GetWorld());
	TStrongObjectPtr<ULocalPlayer> BindingPlayer(GetLocalPlayer());
	TStrongObjectPtr<UDataAsset_InputConfig> RuntimeConfig(InputConfigDataAsset);
	TStrongObjectPtr<const UDataAsset_InputConfig> Source(GetSourceInputConfig());
	TStrongObjectPtr<UInputMappingContext> RuntimeContext(
		RuntimeConfig.IsValid() ? RuntimeConfig->DefaultMappingContext : nullptr);
	TStrongObjectPtr<const UInputMappingContext> SourceContext(
		Source.IsValid() ? Source->DefaultMappingContext : nullptr);
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem(BindingPlayer.IsValid()
		? BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr);
	TStrongObjectPtr<UEnhancedPlayerInput> BindingPlayerInput(
		Additional && Subsystem.IsValid() ? Subsystem->GetPlayerInput() : nullptr);
	TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
	if (!IsValid(Settings.Get()) || Settings->bApplyingAdditionalInputBinding
		|| !Settings->CanSaveAdditionalInputBindings())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "DefaultsBusy", "키 설정을 복원할 수 없습니다. 잠시 후 다시 시도해 주세요.");
		return false;
	}
	TGuardValue<bool> AdditionalGuard(Settings->bApplyingAdditionalInputBinding, Additional != nullptr);
	const bool bRestorePrimary = !RequestedBindings.IsEmpty();
	const auto HasSameOwner = [&]()
	{
		return IsValid(this) && !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& IsLocalController() && !IsActorBeingDestroyed() && BindingWorld.IsValid()
			&& !BindingWorld->bIsTearingDown && GetWorld() == BindingWorld.Get()
			&& BindingPlayer.IsValid() && GetLocalPlayer() == BindingPlayer.Get()
			&& BindingPlayer->GetPlayerController(nullptr) == this
			&& Subsystem.IsValid()
			&& BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() == Subsystem.Get()
			&& (!Additional || (IsValid(BindingPlayerInput.Get()) && !BindingPlayerInput->IsUnreachable()
				&& !BindingPlayerInput->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				&& Subsystem->GetPlayerInput() == BindingPlayerInput.Get() && PlayerInput.Get() == BindingPlayerInput.Get()))
			&& Source.IsValid() && GetSourceInputConfig() == Source.Get()
			&& SourceContext.IsValid() && Source->DefaultMappingContext == SourceContext.Get()
			&& RuntimeConfig.IsValid() && RuntimeConfig->GetOuter() == this
			&& InputConfigDataAsset == RuntimeConfig.Get() && RuntimeContext.IsValid()
			&& RuntimeConfig->DefaultMappingContext == RuntimeContext.Get()
			&& RuntimeContext->GetOuter() == RuntimeConfig.Get() && RuntimeContext.Get() != SourceContext.Get()
			&& IsValid(Settings.Get()) && !Settings->IsUnreachable()
			&& !Settings->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get()
			&& Settings->CanSaveAdditionalInputBindings();
	};
	if (!HasSameOwner())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "DefaultsNotReady", "키 설정이 준비되지 않았습니다.");
		return false;
	}

	// Snapshot identities/keys; shared assets and mapping metadata are never rewritten.
	const TArray<FEnhancedActionKeyMapping> Defaults(SourceContext->GetMappings());
	const TArray<FEnhancedActionKeyMapping> Previous(RuntimeContext->GetMappings());
	if (Defaults.Num() != Previous.Num())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
		return false;
	}
	TArray<FKey> FinalKeys;
	TArray<FKey> ExpectedLiveKeys;
	for (int32 Index = 0; Index < Defaults.Num(); ++Index)
	{
		if (Defaults[Index].Action != Previous[Index].Action)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
			return false;
		}
		FinalKeys.Add(Previous[Index].Key);
		ExpectedLiveKeys.Add(Previous[Index].Key);
	}
	TSet<int32> RestoredIndices;
	for (const FRPGKeyMapping& Binding : RequestedBindings)
	{
		const UInputAction* Action = Source->FindNativeInputActionByTag(Binding.InputTag);
		if (!Action) { Action = Source->FindAbilityInputActionByTag(Binding.InputTag); }
		bool bFound = false;
		bool bCurrentMatches = true;
		if (Action && Binding.InputTag.IsValid() && Binding.DefaultKey.IsValid()
			&& !Binding.DefaultKey.IsAnalog() && Binding.Key.IsValid() && Binding.Key != Binding.DefaultKey)
		{
			for (int32 Index = 0; Index < Defaults.Num(); ++Index)
			{
				if (Defaults[Index].Action != Action || Defaults[Index].Key != Binding.DefaultKey) { continue; }
				if (Previous[Index].Key != Binding.Key) { bCurrentMatches = false; break; }
				bFound = true;
				RestoredIndices.Add(Index);
				FinalKeys[Index] = Binding.DefaultKey;
			}
		}
		if (!bFound || !bCurrentMatches)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "DefaultsChanged", "키 설정이 변경되었습니다. 복원을 다시 요청해 주세요.");
			return false;
		}
	}

	TMap<const UInputAction*, TArray<FGameplayTag>> RestoredActionTags;
	for (int32 Index : RestoredIndices)
	{
		const UInputAction* Action = Defaults[Index].Action.Get();
		if (!RestoredActionTags.Contains(Action))
		{
			Source->GetInputTagsForAction(Action, RestoredActionTags.FindOrAdd(Action));
		}
	}

	const auto ValidateLayout = [&]()
	{
		if (!HasSameOwner() || SourceContext->GetMappings().Num() != Defaults.Num()
			|| RuntimeContext->GetMappings().Num() != Previous.Num())
		{
			OutError = NSLOCTEXT("RPGInputOptions", "BindingOwnerChanged", "The input owner changed during the request.");
			return false;
		}
		for (int32 Index = 0; Index < Defaults.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = SourceContext->GetMappings()[Index];
			const FEnhancedActionKeyMapping& Live = RuntimeContext->GetMappings()[Index];
			if (Authored.Action != Defaults[Index].Action || Authored.Key != Defaults[Index].Key
				|| Live.Action != Previous[Index].Action || Live.Key != ExpectedLiveKeys[Index])
			{
				OutError = NSLOCTEXT("RPGInputOptions", "DefaultsChanged", "키 설정이 변경되었습니다. 복원을 다시 요청해 주세요.");
				return false;
			}
		}
		for (const auto& ActionTags : RestoredActionTags)
		{
			TArray<FGameplayTag> CurrentTags;
			Source->GetInputTagsForAction(ActionTags.Key, CurrentTags);
			if (CurrentTags != ActionTags.Value)
			{
				OutError = NSLOCTEXT("RPGInputOptions", "DefaultsChanged", "키 설정이 변경되었습니다. 복원을 다시 요청해 주세요.");
				return false;
			}
		}
		return !Additional || Additional->Validate(OutError);
	};

	const TArray<int32> TargetIndices = RestoredIndices.Array();
	const auto MakePolicyView = [&]()
	{
		FRPGInputBindingRestoreView View;
		View.DefaultMappings = MakeArrayView(Defaults);
		View.FinalKeys = MakeArrayView(FinalKeys);
		View.RestoredMappingIndices = MakeArrayView(TargetIndices);
		View.RuntimeContext = RuntimeContext.Get();
		View.RebuiltContext = SkillQuickSlotInputContext.Get();
		View.InputSubsystem = Subsystem.Get();
		if (Additional) { View.AdditionalContexts = Additional->Contexts; }
		return View;
	};

	const auto ValidateCommittedBindings = [&]()
	{
		if (!ValidateLayout()) { return false; }
		if (Additional)
		{
			const FRPGInputBindingRestoreView View = MakePolicyView();
			if (!ValidatePolicy(View, OutError) || !ValidateLayout()) { return false; }
		}
		for (int32 Index : RestoredIndices)
		{
			for (const FGameplayTag& Tag : RestoredActionTags.FindChecked(Defaults[Index].Action.Get()))
			{
				if (!Settings->MatchesKeyBinding(Tag, Defaults[Index].Key, FinalKeys[Index]))
				{
					OutError = NSLOCTEXT("RPGInputOptions", "SavedBindingChanged",
						"저장할 키 설정이 변경되었습니다. 현재 키를 확인한 뒤 다시 시도해 주세요.");
					return false;
				}
			}
		}
		return true;
	};

	const auto ValidatePlan = [&]()
	{
		if (!ValidateLayout()) { return false; }
		const FRPGInputBindingRestoreView View = MakePolicyView();
		if (!ValidatePolicy(View, OutError)) { return false; }
		// A caller-supplied policy may run synchronous callbacks; validate ownership and layout again.
		return ValidateLayout();
	};
	if (!ValidatePlan()) { return false; }
	// Use the existing release/rebuild hooks once for the complete request.
	if (bRestorePrimary && QuickSlotComponent) { QuickSlotComponent->ReleaseAllSkillInputs(); }
	if (!ValidatePlan()) { return false; }

	for (int32 Index : RestoredIndices)
	{
		bPrimaryMappingsChanged |= RuntimeContext->GetMappings()[Index].Key != FinalKeys[Index];
		RuntimeContext->GetMapping(Index).Key = FinalKeys[Index];
		ExpectedLiveKeys[Index] = FinalKeys[Index];
	}
	// Persist effective aliases only; shadowed entries must not overwrite another action's saved binding.
	for (int32 Index : RestoredIndices)
	{
		for (const FGameplayTag& Tag : RestoredActionTags.FindChecked(Defaults[Index].Action.Get()))
		{
			Settings->SetKeyBinding(Tag, Defaults[Index].Key, Defaults[Index].Key);
		}
	}
	if (Additional && !Additional->Commit(OutError)) { return false; }
	if (bRestorePrimary && SkillQuickSlotInputContext.IsValid())
		FRPGQuickSlotInputBindings::RebuildSkillContext(RuntimeConfig.Get(), *SkillQuickSlotInputContext);
	if (!ValidateCommittedBindings()) { return false; }
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Subsystem->RequestRebuildControlMappings(Options);
	if (!ValidateCommittedBindings()) { return false; }
	if (bRestorePrimary && HasSameOwner() && IsValid(SkillWindowComponent)) { SkillWindowComponent->RefreshCloseKeys(); }
	if (!ValidateCommittedBindings()) { return false; }
	Settings->SaveSettings();
	// SaveSettings may invoke synchronous cloud/config callbacks; success requires the same committed state.
	return ValidateCommittedBindings();
}

bool ARPGPlayerController::ApplyKeyBindingInternal(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
	FText& OutError, bool bSaveSettings, TFunctionRef<bool(FText&)>* ValidatePolicy)
{
	OutError = FText::GetEmpty();
	if (!IsInGameThread() || bApplyingKeyBinding)
	{
		OutError = NSLOCTEXT("RPGInputOptions", "BindingBusy", "An input binding change is already in progress.");
		return false;
	}
	TStrongObjectPtr<ARPGPlayerController> KeepAlive(this);
	// Notify after the request guards are released, including failures after a live key change.
	bool bPrimaryMappingsChanged = false;
	const TWeakObjectPtr<ULocalPlayer> ChangedPlayer(GetLocalPlayer());
	const TWeakObjectPtr<UWorld> ChangedWorld(GetWorld());
	ON_SCOPE_EXIT
	{
		if (bPrimaryMappingsChanged)
		{
			IRPGInputOptionsRuntime::NotifyMappingsChanged(ChangedPlayer.Get(), ChangedWorld.Get());
		}
	};
	TGuardValue<bool> ApplyingGuard(bApplyingKeyBinding, true);
	const TStrongObjectPtr<UWorld> BindingWorld(GetWorld());
	if (!IsLocalController() || IsActorBeingDestroyed() || !BindingWorld.IsValid()
		|| BindingWorld->bIsTearingDown || !InTag.IsValid() || !NewKey.IsValid())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "InvalidBinding", "A local player and a valid key are required.");
		return false;
	}
	if (InTag == RPGGameplayTags::InputTag_ShowSkillWindow && SkillWindowComponent
		&& SkillWindowComponent->RemapFallbackKey(NewKey))
	{
		if (bSaveSettings)
		{
			if (URPGGameUserSettings* Settings = URPGGameUserSettings::GetRPGGameUserSettings())
			{
				Settings->SetKeyMapping(InTag, NewKey);
				Settings->SaveSettings();
			}
		}
		return true;
	}
	TStrongObjectPtr<UDataAsset_InputConfig> RuntimeConfig(InputConfigDataAsset);
	TStrongObjectPtr<const UDataAsset_InputConfig> Source(GetSourceInputConfig());
	TStrongObjectPtr<UInputMappingContext> RuntimeContext(
		RuntimeConfig.IsValid() ? RuntimeConfig->DefaultMappingContext : nullptr);
	TStrongObjectPtr<const UInputMappingContext> SourceContext(
		Source.IsValid() ? Source->DefaultMappingContext : nullptr);
	TStrongObjectPtr<ULocalPlayer> BindingPlayer(GetLocalPlayer());
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem(BindingPlayer.IsValid()
		? BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr);
	TStrongObjectPtr<URPGGameUserSettings> Settings(
		bSaveSettings ? URPGGameUserSettings::GetRPGGameUserSettings() : nullptr);
	if (Settings.IsValid() && Settings->bApplyingAdditionalInputBinding)
	{
		OutError = NSLOCTEXT("RPGInputOptions", "BindingBusy", "An input binding change is already in progress.");
		return false;
	}
	const auto HasSameOwner = [&]()
	{
		return IsValid(this) && !IsUnreachable() && !HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& IsLocalController() && !IsActorBeingDestroyed() && BindingWorld.IsValid()
			&& !BindingWorld->bIsTearingDown && GetWorld() == BindingWorld.Get()
			&& BindingPlayer.IsValid() && GetLocalPlayer() == BindingPlayer.Get()
			&& BindingPlayer->GetPlayerController(nullptr) == this
			&& Subsystem.IsValid()
			&& BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() == Subsystem.Get()
			&& Source.IsValid() && GetSourceInputConfig() == Source.Get()
			&& SourceContext.IsValid() && Source->DefaultMappingContext == SourceContext.Get()
			&& RuntimeConfig.IsValid() && RuntimeConfig->GetOuter() == this
			&& InputConfigDataAsset == RuntimeConfig.Get() && RuntimeContext.IsValid()
			&& RuntimeConfig->DefaultMappingContext == RuntimeContext.Get()
			&& RuntimeContext->GetOuter() == RuntimeConfig.Get() && RuntimeContext.Get() != SourceContext.Get()
			&& (!bSaveSettings || (IsValid(Settings.Get()) && !Settings->IsUnreachable()
				&& !Settings->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				&& URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get()));
	};
	if (!HasSameOwner())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "NotReady", "Input bindings are not ready.");
		return false;
	}
	UInputAction* TargetAction = Source->FindNativeInputActionByTag(InTag);
	if (!TargetAction) { TargetAction = Source->FindAbilityInputActionByTag(InTag); }
	if (!TargetAction)
	{
		OutError = NSLOCTEXT("RPGInputOptions", "MissingAction", "This input action is unavailable.");
		return false;
	}
	TStrongObjectPtr<UInputAction> KeepTargetAlive(TargetAction);
	const TArray<FEnhancedActionKeyMapping> Defaults(SourceContext->GetMappings());
	const TArray<FEnhancedActionKeyMapping> Previous(RuntimeContext->GetMappings());
	if (Defaults.Num() != Previous.Num())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
		return false;
	}
	if (!DefaultKey.IsValid())
	{
		for (const FEnhancedActionKeyMapping& Mapping : Defaults)
		{
			if (Mapping.Action == TargetAction && !Mapping.Key.IsGamepadKey() && !Mapping.Key.IsAnalog())
			{
				DefaultKey = Mapping.Key;
				break;
			}
		}
		if (!DefaultKey.IsValid())
		{
			for (const FEnhancedActionKeyMapping& Mapping : Defaults)
			{
				if (Mapping.Action == TargetAction) { DefaultKey = Mapping.Key; break; }
			}
		}
	}
	TArray<int32> Indices;
	for (int32 Index = 0; Index < Defaults.Num(); ++Index)
	{
		if (Defaults[Index].Action != TargetAction || Defaults[Index].Key != DefaultKey) { continue; }
		if (!RuntimeContext->GetMappings().IsValidIndex(Index)
			|| RuntimeContext->GetMappings()[Index].Action != TargetAction)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
			return false;
		}
		Indices.Add(Index);
	}
	if (Indices.IsEmpty())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "MissingBinding", "This key binding is unavailable.");
		return false;
	}
	TArray<FKey> ExpectedLiveKeys;
	for (const FEnhancedActionKeyMapping& Mapping : Previous) { ExpectedLiveKeys.Add(Mapping.Key); }
	TArray<FGameplayTag> BindingTags;
	Source->GetInputTagsForAction(TargetAction, BindingTags);
	const auto ValidateLayout = [&]()
	{
		if (!HasSameOwner())
		{
			OutError = NSLOCTEXT("RPGInputOptions", "BindingOwnerChanged", "The input owner changed during the request.");
			return false;
		}
		const UInputAction* CurrentAction = Source->FindNativeInputActionByTag(InTag);
		if (!CurrentAction) { CurrentAction = Source->FindAbilityInputActionByTag(InTag); }
		TArray<FGameplayTag> CurrentTags;
		Source->GetInputTagsForAction(TargetAction, CurrentTags);
		if (CurrentAction != TargetAction || CurrentTags != BindingTags
			|| SourceContext->GetMappings().Num() != Defaults.Num()
			|| RuntimeContext->GetMappings().Num() != Previous.Num())
		{
			OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
			return false;
		}
		for (int32 Index = 0; Index < Defaults.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = SourceContext->GetMappings()[Index];
			const FEnhancedActionKeyMapping& Live = RuntimeContext->GetMappings()[Index];
			if (Authored.Action != Defaults[Index].Action || Authored.Key != Defaults[Index].Key
				|| Live.Action != Previous[Index].Action || Live.Key != ExpectedLiveKeys[Index])
			{
				OutError = NSLOCTEXT("RPGInputOptions", "ChangedLayout", "The input mapping layout has changed.");
				return false;
			}
		}
		return true;
	};

	const auto ValidateCommittedBindings = [&]()
	{
		if (!ValidateLayout()) { return false; }
		if (bSaveSettings)
		{
			for (const FGameplayTag& Tag : BindingTags)
			{
				if (!Settings->MatchesKeyBinding(Tag, DefaultKey, NewKey))
				{
					OutError = NSLOCTEXT("RPGInputOptions", "SavedBindingChanged",
						"저장할 키 설정이 변경되었습니다. 현재 키를 확인한 뒤 다시 시도해 주세요.");
					return false;
				}
			}
		}
		return true;
	};

	const auto ValidatePlan = [&]()
	{
		if (!ValidateLayout()) { return false; }
		if (ValidatePolicy && !(*ValidatePolicy)(OutError)) { return false; }
		// Never commit a snapshot invalidated by a synchronous policy callback.
		return ValidateLayout();
	};
	if (!ValidatePlan()) { return false; }
	// Keep the existing skill-input release and rebuild hooks around the key change.
	if (QuickSlotComponent) { QuickSlotComponent->ReleaseAllSkillInputs(); }
	if (!ValidatePlan()) { return false; }
	for (int32 Index : Indices)
	{
		bPrimaryMappingsChanged |= RuntimeContext->GetMappings()[Index].Key != NewKey;
		RuntimeContext->GetMapping(Index).Key = NewKey;
		ExpectedLiveKeys[Index] = NewKey;
	}
	if (bSaveSettings && Settings.IsValid())
	{
		// Stage all aliases before mapping rebuild callbacks can request another settings save.
		for (const FGameplayTag& Tag : BindingTags) { Settings->SetKeyBinding(Tag, DefaultKey, NewKey); }
	}
	if (SkillQuickSlotInputContext.IsValid())
		FRPGQuickSlotInputBindings::RebuildSkillContext(RuntimeConfig.Get(), *SkillQuickSlotInputContext);
	if (!ValidateCommittedBindings()) { return false; }
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Subsystem->RequestRebuildControlMappings(Options);
	if (!ValidateCommittedBindings()) { return false; }
	if (HasSameOwner() && InTag == RPGGameplayTags::InputTag_ShowSkillWindow && IsValid(SkillWindowComponent))
	{
		SkillWindowComponent->RefreshCloseKeys();
	}
	if (!ValidateCommittedBindings()) { return false; }
	if (bSaveSettings) { Settings->SaveSettings(); }
	return ValidateCommittedBindings();
}

FKey ARPGPlayerController::GetCurrentKeyForBinding(FGameplayTag InTag, FKey DefaultKey) const
{
	if (!DefaultKey.IsValid()) { return GetCurrentKeyForTag(InTag); }
	const UDataAsset_InputConfig* Source = GetSourceInputConfig();
	const UInputMappingContext* Defaults = Source ? Source->DefaultMappingContext : nullptr;
	const UInputMappingContext* Current = InputConfigDataAsset ? InputConfigDataAsset->DefaultMappingContext : nullptr;
	const UInputAction* Action = Source ? Source->FindNativeInputActionByTag(InTag) : nullptr;
	if (!Action && Source) { Action = Source->FindAbilityInputActionByTag(InTag); }
	if (!Action || !Defaults || !Current) { return FKey(); }
	for (int32 Index = 0; Index < Defaults->GetMappings().Num(); ++Index)
	{
		const FEnhancedActionKeyMapping& Mapping = Defaults->GetMappings()[Index];
		if (Mapping.Action == Action && Mapping.Key == DefaultKey
			&& Current->GetMappings().IsValidIndex(Index) && Current->GetMappings()[Index].Action == Action)
		{
			return Current->GetMappings()[Index].Key;
		}
	}
	return FKey();
}

FKey ARPGPlayerController::GetCurrentKeyForTag(FGameplayTag InTag) const
{
	// Labels follow the live action mapping, including old/new tags sharing an
	// action. A saved value for one alias may no longer be the effective key.
	if (InputConfigDataAsset && InputConfigDataAsset->GetOuter() == this)
	{
		UInputAction* TargetAction = InputConfigDataAsset->FindNativeInputActionByTag(InTag);
		if (!TargetAction) TargetAction = InputConfigDataAsset->FindAbilityInputActionByTag(InTag);

		if (TargetAction && InputConfigDataAsset->DefaultMappingContext)
		{
			for (const FEnhancedActionKeyMapping& Mapping : InputConfigDataAsset->DefaultMappingContext->GetMappings())
			{
				if (Mapping.Action == TargetAction) return Mapping.Key;
			}
		}
	}
	if (const URPGGameUserSettings* UserSettings = URPGGameUserSettings::GetRPGGameUserSettings())
	{
		const FKey CustomKey = UserSettings->GetKeyMapping(InTag);
		if (CustomKey.IsValid()) return CustomKey;
	}
	if (InputConfigDataAsset && InputConfigDataAsset->GetOuter() != this)
	{
		const UInputAction* Action = InputConfigDataAsset->FindNativeInputActionByTag(InTag);
		if (!Action) Action = InputConfigDataAsset->FindAbilityInputActionByTag(InTag);
		if (Action && InputConfigDataAsset->DefaultMappingContext)
		{
			for (const auto& Mapping : InputConfigDataAsset->DefaultMappingContext->GetMappings())
				if (Mapping.Action == Action) return Mapping.Key;
		}
	}

	if (InTag == RPGGameplayTags::InputTag_ShowSkillWindow && SkillWindowComponent)
	{
		return SkillWindowComponent->GetToggleKey();
	}
	return FKey();
}

FGenericTeamId ARPGPlayerController::GetGenericTeamId() const
{
	return PlayerTeamID;
}

void ARPGPlayerController::SetSkillLockedTarget(AActor* InTarget)
{
	SkillLockedTarget = IsValid(InTarget) ? InTarget : nullptr;
}

AActor* ARPGPlayerController::GetSkillLockedTarget() const
{
	return SkillLockedTarget.IsValid() ? SkillLockedTarget.Get() : nullptr;
}

void ARPGPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	SkillLockedTarget.Reset();
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::OnRep_Pawn()
{
	Super::OnRep_Pawn();
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::ClientRestart_Implementation(APawn* NewPawn)
{
	Super::ClientRestart_Implementation(NewPawn);
	// ClientRestart can set Pawn directly, bypassing OnRep_Pawn. The engine
	// assigns Pawn.Controller only after AcknowledgePossession, so bind here
	// after Super, once both sides of the owning-client relationship are ready.
	RefreshControlledPawnReferences();
}

void ARPGPlayerController::PerformInteractionCheck_Around()
{
	if (!PlayerCharacter) return;
	FVector Center = PlayerCharacter->GetActorLocation();
	TArray<FOverlapResult> OverlapResults;
	FCollisionObjectQueryParams ObjectQueryParams(ECollisionChannel::ECC_WorldDynamic);
	FCollisionShape CollisionShape;
	CollisionShape.SetSphere(PickupCheckRadius);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(PlayerCharacter);

	bool bHasOverlapped = GetWorld()->OverlapMultiByObjectType(OverlapResults, Center, FQuat::Identity, ObjectQueryParams, CollisionShape, QueryParams);

	if (bHasOverlapped)
	{
		AActor* ClosestInteractable = nullptr;
		float MinDistanceSquared = FLT_MAX; 

		for (const FOverlapResult& Result : OverlapResults)
		{
			AActor* OverlappedActor = Result.GetActor();
			if (!OverlappedActor) continue;
						
			if (OverlappedActor->GetClass()->ImplementsInterface(UInteractionInterface::StaticClass()))
			{
				float DistanceSquared = FVector::DistSquared(PlayerCharacter->GetActorLocation(), OverlappedActor->GetActorLocation());
				if (DistanceSquared < MinDistanceSquared)
				{
					MinDistanceSquared = DistanceSquared;
					ClosestInteractable = OverlappedActor;
				}
			}
		}

		if (ClosestInteractable)
		{
			if (InteractionData.CurrentInteractable != ClosestInteractable) FoundInteractable(ClosestInteractable);
			return;
		}
	}
	NoInteractableFound();
}

void ARPGPlayerController::FoundInteractable(AActor* NewInteractable)
{
	if (IsInteracting()) EndInteract();
	
	if (InteractionData.CurrentInteractable)
	{
		TargetInteractable = InteractionData.CurrentInteractable;
		TargetInteractable->EndFocus();
	}

	InteractionData.CurrentInteractable = NewInteractable;
	TargetInteractable = NewInteractable;
	TargetInteractable->BeginFocus();
}

void ARPGPlayerController::NoInteractableFound()
{
	if (IsInteracting()) GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);

	if (InteractionData.CurrentInteractable)
	{
		if (IsValid(TargetInteractable.GetObject())) TargetInteractable->EndFocus();
		if (HUD) HUD->HideInteractionWidget();
		InteractionData.CurrentInteractable = nullptr;
		TargetInteractable = nullptr;
	}
}

void ARPGPlayerController::BeginInteract()
{
	PerformInteractionCheck_Around();
	if (InteractionData.CurrentInteractable && IsValid(TargetInteractable.GetObject()))
	{
		TargetInteractable->BeginInteract();
		if (FMath::IsNearlyZero(TargetInteractable->InteractableData.InteractionDuration, 0.1f)) Interact();
		else
		{
			GetWorldTimerManager().SetTimer(TimerHandle_Interaction, this, &ThisClass::Interact, TargetInteractable->InteractableData.InteractionDuration, false);
		}
	}
}

void ARPGPlayerController::EndInteract()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);
	if (IsValid(TargetInteractable.GetObject())) TargetInteractable->EndInteract();
}

void ARPGPlayerController::Interact()
{
	GetWorldTimerManager().ClearTimer(TimerHandle_Interaction);
	if (IsValid(TargetInteractable.GetObject())) TargetInteractable->Interact(this);
}

void ARPGPlayerController::PressSkillSlot(const int32 SlotIndex)
{
	if (WindowInputState->HasOpenWindows()) return;
	// Key remapping can introduce conflicts after SetupInputComponent bound the
	// delegates. Recheck the live data before either conflicting action fires.
	if (!FRPGQuickSlotInputBindings::Read(InputConfigDataAsset).ContainsByPredicate(
		[SlotIndex](const auto& Binding) { return Binding.bIsSkillSlot && Binding.SlotIndex == SlotIndex; })) return;
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->BeginUseSkillSlot(SlotIndex);
	}
}

void ARPGPlayerController::ReleaseSkillSlot(const int32 SlotIndex)
{
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->EndUseSkillSlot(SlotIndex);
	}
}

void ARPGPlayerController::UseItemSlot(int32 SlotIndex)
{
	if (WindowInputState->HasOpenWindows()) return;
	if (APawn* PlayerPawn = GetPawn())
	{
		if (UQuickSlotComponent* QuickSlotComp = PlayerPawn->FindComponentByClass<UQuickSlotComponent>())
			QuickSlotComp->UseItemSlot(SlotIndex, this);
	}
}

void ARPGPlayerController::EnableCameraZoom()
{
	bCanZoom = true;
}

void ARPGPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ThisClass::ToggleEquipmentProjection).bConsumeInput = true;
	if (!InputConfigDataAsset || !GetLocalPlayer()) return;
	const auto ObservePartyInputSetup = [this](const TCHAR* Phase)
	{
#if !UE_BUILD_SHIPPING
		if (!FParse::Param(FCommandLine::Get(), TEXT("RPGPartyHudInputE2E"))) return;
		const auto* Context = InputConfigDataAsset ? InputConfigDataAsset->DefaultMappingContext : nullptr;
		const auto* Source = GetSourceInputConfig();
		const auto* SourceContext = Source ? Source->DefaultMappingContext : nullptr;
		UE_LOG(LogTemp, Display, TEXT("PARTY_HUD_INPUT SETUP Phase=%s Primary=%d Source=%d NeedsPostLoad=%d"),
			Phase, Context ? Context->GetMappings().Num() : -1, SourceContext ? SourceContext->GetMappings().Num() : -1,
			Context && Context->HasAnyFlags(RF_NeedPostLoad));
#endif
	};
	ObservePartyInputSetup(TEXT("before-copy"));
	if (InputConfigDataAsset->GetOuter() != this)
	{
		SourceInputConfig = InputConfigDataAsset;
		InputConfigDataAsset = DuplicateObject<UDataAsset_InputConfig>(InputConfigDataAsset, this);
		InputConfigDataAsset->SetFlags(RF_Transient);
		ObservePartyInputSetup(TEXT("after-config-copy"));
		if (InputConfigDataAsset->DefaultMappingContext)
		{
			const UInputMappingContext* Authored = InputConfigDataAsset->DefaultMappingContext;
			InputConfigDataAsset->DefaultMappingContext = DuplicateObject<UInputMappingContext>(
				InputConfigDataAsset->DefaultMappingContext, InputConfigDataAsset);
			InputConfigDataAsset->DefaultMappingContext->SetFlags(RF_Transient);
			RPGInputMappingClone::RestoreMissingRecords(*Authored, *InputConfigDataAsset->DefaultMappingContext);
			ObservePartyInputSetup(TEXT("after-context-copy"));
		}
	}

	auto* SubSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (SubSystem && InputConfigDataAsset->DefaultMappingContext)
		SubSystem->AddMappingContext(InputConfigDataAsset->DefaultMappingContext, 0);
	ObservePartyInputSetup(TEXT("after-register"));

	URPGInputComponent* RPGInputComponent = CastChecked<URPGInputComponent>(InputComponent);

	// =========================================================================
	// [완전 자동화] 이제부터는 데이터 에셋에 액션/태그만 추가하면 자동으로 연결됩니다.
	// =========================================================================
	
	// 1. 모든 Native 입력 자동 바인딩 (리플렉션 이용)
	// 규칙: 태그 InputTag.Move -> 함수 Input_Move
	RPGInputComponent->BindNativeInputActions(InputConfigDataAsset, this);
	SkillWindowComponent->BindInput(RPGInputComponent, InputConfigDataAsset);

	// Slot/action assignments come from input data; neither skills nor keys are fixed here.
	for (const FRPGQuickSlotInputBinding& Binding : FRPGQuickSlotInputBindings::Read(InputConfigDataAsset))
	{
		if (Binding.bIsSkillSlot)
		{
			RPGInputComponent->BindAction(Binding.Action, ETriggerEvent::Started, this, &ThisClass::PressSkillSlot, Binding.SlotIndex);
			RPGInputComponent->BindAction(Binding.Action, ETriggerEvent::Completed, this, &ThisClass::ReleaseSkillSlot, Binding.SlotIndex);
			RPGInputComponent->BindAction(Binding.Action, ETriggerEvent::Canceled, this, &ThisClass::ReleaseSkillSlot, Binding.SlotIndex);
		}
		else RPGInputComponent->BindAction(Binding.Action, ETriggerEvent::Started, this, &ThisClass::UseItemSlot, Binding.SlotIndex);
	}

	// 3. GAS 어빌리티 바인딩 (이미 자동화 구조)
	RPGInputComponent->BindAbilityInputAction(InputConfigDataAsset, this, &ThisClass::Input_AbilityInputPressed, &ThisClass::Input_AbilityInputReleased);

	UpdateInputMappings();
	ObservePartyInputSetup(TEXT("after-saved-bindings"));
	if (SubSystem)
	{
		if (!SkillQuickSlotInputContext.IsValid())
			SkillQuickSlotInputContext.Reset(NewObject<UInputMappingContext>(this, NAME_None, RF_Transient));
		FRPGQuickSlotInputBindings::RebuildSkillContext(InputConfigDataAsset, *SkillQuickSlotInputContext);
		// Base input remains 0, weapon input 1 and target-lock input 3.
		// Only registered skill actions override the weapon's old fixed hotkeys.
		SubSystem->AddMappingContext(SkillQuickSlotInputContext.Get(), 2);
	}
	ObservePartyInputSetup(TEXT("after-skill-context"));
}

UNetConnection* ARPGPlayerController::GetConnectionBeingCleanedUp() const
{
	return ConnectionBeingCleanedUp.Get();
}

void ARPGPlayerController::OnNetCleanup(UNetConnection* Connection)
{
	// Engine clears Player/NetConnection before Destroy -> GameMode::Logout.
	// Scope this identity to the actual cleanup stack; never cache a lost socket.
	TGuardValue<TWeakObjectPtr<UNetConnection>> CleanupScope(ConnectionBeingCleanedUp,
		TWeakObjectPtr<UNetConnection>(Connection));
	Super::OnNetCleanup(Connection);
}

void ARPGPlayerController::BeginPlay()
{
	Super::BeginPlay();
	HUD = Cast<ARPGHUD>(GetHUD());
	RefreshControlledPawnReferences();
	UpdateBossBattlePresentation();
}

void ARPGPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bItemPresentationEnded = true;
	ClearBossBattleRetryRequest(FString(), false);
	CloseDungeonAbandonmentConfirmation(GameplayAbandonmentWidget);
	if (SkillWindowComponent) SkillWindowComponent->CloseWindow();
	CloseProjectionWindow(InventoryProjectionWidget);
	CloseProjectionWindow(EquipmentProjectionWidget);
	CloseProjectionWindow(RewardMailWidget);
	if (InventoryProjectionViewModel)
	{
		InventoryProjectionViewModel->InitializeFromPlayerController(nullptr);
		InventoryProjectionViewModel = nullptr;
	}
	WindowInputState->Reset();
	if (QuickSlotComponent) QuickSlotComponent->ReleaseAllSkillInputs();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (auto* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			Subsystem && InputConfigDataAsset && InputConfigDataAsset->GetOuter() == this)
		{
			if (SkillQuickSlotInputContext.IsValid()) Subsystem->RemoveMappingContext(SkillQuickSlotInputContext.Get());
			Subsystem->RemoveMappingContext(InputConfigDataAsset->DefaultMappingContext);
		}
	}
	SkillQuickSlotInputContext.Reset();
	if (ObservedBossBattleGameState.IsValid())
	{
		ObservedBossBattleGameState->OnBattleSnapshotChanged.RemoveDynamic(
			this, &ThisClass::HandleBossBattleSnapshotChanged);
	}
	ObservedBossBattleGameState.Reset();
	if (BossMechanicWidget)
	{
		BossMechanicWidget->RemoveFromParent();
		BossMechanicWidget = nullptr;
	}
	if (BossResultWidget)
	{
		BossResultWidget->RemoveFromParent();
		BossResultWidget = nullptr;
	}
	if (DungeonResultActionsWidget)
	{
		DungeonResultActionsWidget->RemoveFromParent();
		DungeonResultActionsWidget = nullptr;
	}
	if (DungeonPartyCombatWidget)
	{
		DungeonPartyCombatWidget->RemoveFromParent();
		DungeonPartyCombatWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void ARPGPlayerController::RefreshControlledPawnReferences()
{
	if (SkillWindowComponent) SkillWindowComponent->CloseWindow();
	if (QuickSlotComponent) QuickSlotComponent->ReleaseAllSkillInputs();
	PlayerCharacter = Cast<ARPGPlayer>(GetPawn());
	QuickSlotComponent = GetPawn() ? GetPawn()->FindComponentByClass<UQuickSlotComponent>() : nullptr;

	if (QuickSlotComponent)
	{
		QuickSlotComponent->BindInventoryProjection(
			FindComponentByClass<URPGInventoryProjectionComponent>());
	}
	if (ItemCommandComponent)
	{
		ItemCommandComponent->HandlePawnChanged();
	}
}

void ARPGPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsLocalController())
	{
		UpdateBossBattlePresentation();
		UpdateDungeonPartyCombatPresentation();
	}
}

void ARPGPlayerController::UpdateBossBattlePresentation()
{
	if (!IsLocalController() || !GetWorld())
	{
		return;
	}
	UpdateBossBattleRetryRequest();
	if (BossResultWidget)
		if (UWidget* Retry = BossResultWidget->GetWidgetFromName(TEXT("RetryButton")))
			Retry->SetIsEnabled(CanRequestBossBattleRestart());
	ARPGBossBattleGameState* BattleGameState = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	UpdateBossMechanicPresentation(BattleGameState ? BattleGameState->GetBattleSnapshot() : FRPGBossBattleSnapshot());
	if (ObservedBossBattleGameState.Get() == BattleGameState)
	{
		if (BattleGameState) EnsureDungeonResultActions(BattleGameState->GetBattleSnapshot());
		return;
	}
	if (ObservedBossBattleGameState.IsValid())
	{
		ObservedBossBattleGameState->OnBattleSnapshotChanged.RemoveDynamic(
			this, &ThisClass::HandleBossBattleSnapshotChanged);
	}
	ObservedBossBattleGameState = BattleGameState;
	LastPresentedBossBattleRevision = INDEX_NONE;
	if (BattleGameState)
	{
		BattleGameState->OnBattleSnapshotChanged.AddUniqueDynamic(
			this, &ThisClass::HandleBossBattleSnapshotChanged);
		HandleBossBattleSnapshotChanged(BattleGameState->GetBattleSnapshot());
	}
}

void ARPGPlayerController::UpdateBossMechanicPresentation(const FRPGBossBattleSnapshot& Snapshot)
{
	// The legacy GameMode creates the listen/standalone HUD. Remote clients have no GameMode.
	const bool bNeedsWidget = IsLocalController() && GetNetMode() == NM_Client && GetLocalPlayer() &&
		GetWorld() && !GetWorld()->bIsTearingDown && !IsActorBeingDestroyed() &&
		Snapshot.BattleState == EBossBattleState::InProgress;
	if (!bNeedsWidget)
	{
		if (BossMechanicWidget)
		{
			BossMechanicWidget->RemoveFromParent();
			BossMechanicWidget = nullptr;
		}
		return;
	}
	if (!BossMechanicWidget)
	{
		// Native presentation reads only replicated snapshots; no client GameMode Blueprint calls.
		BossMechanicWidget = CreateWidget<URPGBossBattleInfoWidget>(this, URPGBossBattleInfoWidget::StaticClass());
		if (BossMechanicWidget) BossMechanicWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	if (BossMechanicWidget && !BossMechanicWidget->IsInViewport())
	{
		BossMechanicWidget->AddToPlayerScreen(10);
	}
}

void ARPGPlayerController::HandleBossBattleSnapshotChanged(const FRPGBossBattleSnapshot& Snapshot)
{
	if (!IsLocalController() || Snapshot.Revision == LastPresentedBossBattleRevision)
	{
		return;
	}
	LastPresentedBossBattleRevision = Snapshot.Revision;
	UpdateBossMechanicPresentation(Snapshot);
	CloseDungeonAbandonmentConfirmation(GameplayAbandonmentWidget);
	ClearBossBattleRetryRequest(FString());
	if (DungeonResultActionsWidget)
	{
		DungeonResultActionsWidget->RemoveFromParent();
		DungeonResultActionsWidget = nullptr;
	}
	if (BossResultWidget)
	{
		BossResultWidget->RemoveFromParent();
		BossResultWidget = nullptr;
		SetInputMode(FInputModeGameOnly());
		SetShowMouseCursor(false);
	}
	const TSoftClassPtr<URPGWidgetBase>* ResultClass = nullptr;
	if (Snapshot.BattleState == EBossBattleState::Victory)
	{
		ResultClass = &BossVictoryWidgetClass;
	}
	else if (Snapshot.BattleState == EBossBattleState::Defeated)
	{
		ResultClass = &BossDefeatWidgetClass;
	}
	if (!ResultClass)
	{
		return;
	}
	const TSubclassOf<URPGWidgetBase> LoadedClass = ResultClass->LoadSynchronous();
	if (!LoadedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("Boss result widget class is unavailable on this client."));
		return;
	}
	BossResultWidget = CreateWidget<URPGWidgetBase>(this, LoadedClass);
	if (BossResultWidget)
	{
		if (Snapshot.BattleState == EBossBattleState::Defeated)
		{
			if (!RPGBossBattleUI::BindRetryButton(
				BossResultWidget->GetWidgetFromName(TEXT("RetryButton")), this))
			{
				UE_LOG(LogTemp, Warning, TEXT("Boss defeat widget has no supported RetryButton click event."));
			}
		}
		BossResultWidget->AddToViewport(100);
		EnsureDungeonResultActions(Snapshot);
		FInputModeGameAndUI InputMode;
		InputMode.SetWidgetToFocus(DungeonResultActionsWidget
			? DungeonResultActionsWidget->TakeWidget() : BossResultWidget->TakeWidget());
		SetInputMode(InputMode);
		SetShowMouseCursor(true);
	}
}

void ARPGPlayerController::EnsureDungeonResultActions(const FRPGBossBattleSnapshot& Snapshot)
{
	if (!IsLocalController() || !BossResultWidget || DungeonResultActionsWidget
		|| (Snapshot.BattleState != EBossBattleState::Victory && Snapshot.BattleState != EBossBattleState::Defeated)) return;
	URPGDungeonSessionSubsystem* Flow = GetGameInstance()
		? GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	// Offline retry/exit behavior remains unchanged. Late-arriving client flow/identity
	// can attach the online controls without needing a new battle snapshot revision.
	if (!Flow || Flow->GetSelectedCharacterId().IsEmpty()
		|| Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()) return;
	DungeonResultActionsWidget = CreateWidget<URPGDungeonResultActionsWidget>(this,
		URPGDungeonResultActionsWidget::StaticClass());
	if (!DungeonResultActionsWidget) return;
	DungeonResultActionsWidget->InitializeResult(Snapshot.BattleState == EBossBattleState::Victory);
	DungeonResultActionsWidget->SetIsFocusable(true);
	DungeonResultActionsWidget->AddToViewport(110);
	if (UWidget* Exit = BossResultWidget->GetWidgetFromName(TEXT("ExitButton")))
	{
		if (!RPGBossBattleUI::BindReturnButton(Exit, this))
		{
			Exit->SetIsEnabled(false); // An unsupported legacy click must not bypass settlement.
			UE_LOG(LogTemp, Warning, TEXT("Boss result ExitButton has no supported return dispatcher."));
		}
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(DungeonResultActionsWidget->TakeWidget());
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
}

void ARPGPlayerController::RequestDungeonReturnFromResult()
{
	const ARPGPlayerState* State = GetPlayerState<ARPGPlayerState>();
	const URPGDungeonSessionSubsystem* Flow = GetGameInstance()
		? GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	if (IsLocalController() && State && Flow && State->HasAuthenticatedCharacter()
		&& State->GetBackendCharacterId().Equals(Flow->GetSelectedCharacterId(), ESearchCase::CaseSensitive)
		&& State->GetBackendDungeonSessionId().Equals(Flow->GetCurrentDungeonSession().DungeonSessionId, ESearchCase::CaseSensitive))
	{
		// A legacy exit click still performs an authenticated fresh backend read;
		// Pending/InProgress cannot schedule a local OpenLevel.
		RequestDungeonReturnToLobby();
	}
}

void ARPGPlayerController::RequestBossBattleRestart()
{
	TryRequestBossBattleRestart();
}

bool ARPGPlayerController::IsBossBattleRestartPending() const
{
	return PendingBossBattleRetryId != 0 && PendingBossBattleRetryWorld.Get() == GetWorld();
}

bool ARPGPlayerController::CanRequestBossBattleRestart() const
{
	const UWorld* World = GetWorld();
	if (!IsLocalController() || !World || World->bIsTearingDown || IsBossBattleRestartPending()
		|| IsDungeonGameplayRevoked(this) || IsDungeonGameplayRevoked(GetPawn())) return false;
	const auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
	if (!Battle) return false;
	const EBossBattleState State = Battle->GetBattleSnapshot().BattleState;
	if (State != EBossBattleState::Defeated && State != EBossBattleState::Victory) return false;
	const UGameInstance* Instance = GetGameInstance();
	const auto* Flow = Instance ? Instance->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	const auto* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
	const auto* Authentication = Instance ? Instance->GetSubsystem<URPGSteamAuthSubsystem>() : nullptr;
	const auto* AuthenticatedState = GetPlayerState<ARPGPlayerState>();
	// Direct-IP/offline multiplayer is not a persistent dungeon merely because it is a client.
	const bool bPersistent = (Web && Web->IsConfiguredForDungeonServer()) || (World->GetNetMode() == NM_Client
		&& ((AuthenticatedState && AuthenticatedState->HasAuthenticatedCharacter()) || (Flow && !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty())));
	if (!bPersistent) return true;
	if (State != EBossBattleState::Defeated || !Flow || !Web || !Web->HasAccessToken()
		|| !AuthenticatedState || !AuthenticatedState->HasAuthenticatedCharacter() || Flow->IsRequestInFlight()
		|| (Authentication && Authentication->IsLoginRequestInFlight())
		|| Flow->HasUnresolvedDungeonAbandonment()
		|| Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby
		|| !Web->GetAuthenticatedSteamId().Equals(AuthenticatedState->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive)) return false;
	const FRPGDungeonSession Session = Flow->GetCurrentDungeonSession();
	const FRPGDungeonRewardDeliveryNotice Notice = AuthenticatedState->GetRewardDeliveryNotice();
	return !Session.DungeonSessionId.IsEmpty() && Session.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
		&& Flow->GetSelectedCharacterId().Equals(AuthenticatedState->GetBackendCharacterId(), ESearchCase::CaseSensitive)
		&& Session.DungeonSessionId.Equals(AuthenticatedState->GetBackendDungeonSessionId(), ESearchCase::CaseSensitive)
		&& Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
			{ return Member.CharacterId.Equals(AuthenticatedState->GetBackendCharacterId(), ESearchCase::CaseSensitive); })
		&& !(Notice.bApplied && Notice.DungeonSessionId.Equals(Session.DungeonSessionId, ESearchCase::CaseSensitive));
}

bool ARPGPlayerController::TryRequestBossBattleRestart()
{
	UpdateBossBattleRetryRequest();
	if (!CanRequestBossBattleRestart()) return false;
	if (++BossBattleRetrySerial == 0) ++BossBattleRetrySerial;
	const uint32 RequestId = BossBattleRetrySerial;
	PendingBossBattleRetryId = RequestId;
	PendingBossBattleRetryRevision = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot().Revision;
	PendingBossBattleRetryWorld = GetWorld();
	BossBattleRetryDeadline = FPlatformTime::Seconds() + 10.0;
	BossBattleRestartError.Reset();
	OnBossBattleRetryRequestChanged.Broadcast();
	ServerRequestBossBattleRestart(RequestId, PendingBossBattleRetryRevision);
	return true;
}

void ARPGPlayerController::ClearBossBattleRetryRequest(const FString& ErrorCode, const bool bNotify)
{
	const bool bChanged = PendingBossBattleRetryId != 0 || BossBattleRestartError != ErrorCode;
	PendingBossBattleRetryId = 0;
	PendingBossBattleRetryRevision = INDEX_NONE;
	PendingBossBattleRetryWorld.Reset();
	BossBattleRetryDeadline = 0.0;
	BossBattleRestartError = ErrorCode;
	if (bNotify && bChanged) OnBossBattleRetryRequestChanged.Broadcast();
}

void ARPGPlayerController::UpdateBossBattleRetryRequest()
{
	if (PendingBossBattleRetryId == 0) return;
	const auto* Battle = GetWorld() ? GetWorld()->GetGameState<ARPGBossBattleGameState>() : nullptr;
	if (PendingBossBattleRetryWorld.Get() != GetWorld() || !Battle
		|| Battle->GetBattleSnapshot().Revision != PendingBossBattleRetryRevision)
		ClearBossBattleRetryRequest(FString());
	else if (FPlatformTime::Seconds() >= BossBattleRetryDeadline)
		ClearBossBattleRetryRequest(TEXT("boss_retry_timed_out"));
}

void ARPGPlayerController::ServerRequestBossBattleRestart_Implementation(const uint32 RequestId, const int32 ExpectedBattleRevision)
{
	bool bAccepted = false;
	const double Now = FPlatformTime::Seconds();
	if (RequestId != 0 && ExpectedBattleRevision >= 0 && Now >= NextServerBossBattleRetryTime)
	{
		NextServerBossBattleRetryTime = Now + 1.0;
		if (auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>() : nullptr)
			bAccepted = Mode->TryRestartBattleForParticipant(this, ExpectedBattleRevision);
	}
	ClientBossBattleRestartResult(RequestId, bAccepted);
}

void ARPGPlayerController::ClientBossBattleRestartResult_Implementation(const uint32 RequestId, const bool bAccepted)
{
	if (!IsLocalController() || !IsBossBattleRestartPending() || RequestId != PendingBossBattleRetryId) return;
	// Acceptance is not a local battle transition: retain the lock until the new server snapshot arrives.
	if (!bAccepted) ClearBossBattleRetryRequest(TEXT("boss_retry_rejected"));
}

bool ARPGPlayerController::RequestDungeonReturnToLobby()
{
	UGameInstance* GameInstance = GetGameInstance();
	URPGDungeonSessionSubsystem* DungeonFlow = GameInstance
		? GameInstance->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	return IsLocalController() && DungeonFlow && DungeonFlow->ReturnToLobby(this);
}

void ARPGPlayerController::ServerRequestDungeonLobbyDisconnect_Implementation()
{
	if (UWorld* World = GetWorld())
		if (ARPGGameModeBase* Mode = World->GetAuthGameMode<ARPGGameModeBase>())
			Mode->RequestPlannedDungeonLobbyDisconnect(this);
}

// --- 핸들러 구현부 (이름 규칙: Input_태그명) ---

void ARPGPlayerController::Input_Move(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter) PlayerCharacter->Move(InputActionValue);
}

void ARPGPlayerController::Input_Look(const FInputActionValue& InputActionValue)
{
	if (PlayerCharacter) PlayerCharacter->Look(InputActionValue);
}

void ARPGPlayerController::Input_CameraZoom(const FInputActionValue& InputActionValue)
{
	if (WindowInputState->HasOpenWindows()) return;
	if (PlayerCharacter && bCanZoom)
	{		
		PlayerCharacter->HandleCameraZoom(InputActionValue);
		bCanZoom = false;
		GetWorld()->GetTimerManager().SetTimer(ZoomDelayHandle, this, &ThisClass::EnableCameraZoom, 0.5f, false);
	}
}

void ARPGPlayerController::Input_SwitchTarget(const FInputActionValue& InputActionValue)
{
	if (WindowInputState->HasOpenWindows()) return;
	// Triggered와 Completed를 모두 처리할 수 있음
	if (InputActionValue.Get<FVector2D>() != FVector2D::ZeroVector)
	{
		SwitchDirection = InputActionValue.Get<FVector2D>();
	}
	else // Completed 시점 (Value가 0이 됨)
	{
		FGameplayEventData Data;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(PlayerCharacter,
			SwitchDirection.X > 0.f ? RPGGameplayTags::Player_Event_SwitchTarget_Right : RPGGameplayTags::Player_Event_SwitchTarget_Left, Data);
	}
}

void ARPGPlayerController::Input_PickUp_Items(const FInputActionValue& InputActionValue)
{
	if (WindowInputState->HasOpenWindows() && InputActionValue.Get<bool>()) return;
	if (InputActionValue.Get<bool>()) BeginInteract();
	else EndInteract();
}

void ARPGPlayerController::Input_ToggleMenu(const FInputActionValue& Value)
{
	if (GameplayAbandonmentWidget)
	{
		CloseDungeonAbandonmentConfirmation(GameplayAbandonmentWidget);
		return;
	}
	if (CloseTopPlayerWindow()) return;
	if (ShowDungeonAbandonmentConfirmation()) return;
	if (HUD) HUD->ToggleMenu();
}

bool ARPGPlayerController::ShowDungeonAbandonmentConfirmation()
{
	const auto* Flow = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	if (!IsLocalController() || IsActorBeingDestroyed() || !GetLocalPlayer() || !GetWorld() || GetWorld()->bIsTearingDown
		|| !Flow || !Flow->CanQueueDungeonAbandonmentFromGameplay(this)) return false;
	if (GameplayAbandonmentWidget) { WindowInputState->FocusWindow(*GameplayAbandonmentWidget); return true; }
	TStrongObjectPtr<URPGDungeonResultActionsWidget> Widget(CreateWidget<URPGDungeonResultActionsWidget>(this));
	if (!Widget.IsValid()) return false;
	Widget->InitializeGameplayAbandonment();
	Widget->SetIsFocusable(true);
	GameplayAbandonmentWidget = Widget.Get();
	if (!WindowInputState->BeginWindow(*this, *Widget, 120))
	{
		CloseDungeonAbandonmentConfirmation(Widget.Get());
		return false;
	}
	// Window/focus delegates may synchronously tear down this controller or open
	// a replacement. Never attach the old widget or clear the replacement's input.
	if (GameplayAbandonmentWidget != Widget.Get() || IsActorBeingDestroyed() || !GetWorld() || GetWorld()->bIsTearingDown
		|| !IsValid(Flow) || !Flow->CanQueueDungeonAbandonmentFromGameplay(this)
		|| !Widget->AddToPlayerScreen(120) || GameplayAbandonmentWidget != Widget.Get()
		|| !Widget->OpenGameplayAbandonmentConfirmation())
	{
		CloseDungeonAbandonmentConfirmation(Widget.Get());
		return false;
	}
	WindowInputState->FocusWindow(*Widget);
	return GameplayAbandonmentWidget == Widget.Get();
}

void ARPGPlayerController::CloseDungeonAbandonmentConfirmation(URPGDungeonResultActionsWidget* ExpectedWidget)
{
	if (!ExpectedWidget || GameplayAbandonmentWidget != ExpectedWidget) return;
	TStrongObjectPtr<URPGDungeonResultActionsWidget> KeepAlive(ExpectedWidget);
	GameplayAbandonmentWidget = nullptr;
	WindowInputState->EndWindow(*ExpectedWidget);
	ExpectedWidget->RemoveFromParent();
}

void ARPGPlayerController::Input_ShowEquipmentWidget(const FInputActionValue& Value)
{
	ToggleInventory();
}

void ARPGPlayerController::Input_AbilityInputPressed(FGameplayTag InInputTag)
{
	if (WindowInputState->HasOpenWindows()) return;
	if (FRPGQuickSlotInputBindings::OwnsAbilityInput(InputConfigDataAsset, InInputTag)) return;
	if (PlayerCharacter) PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputPressed(InInputTag);
}

void ARPGPlayerController::Input_AbilityInputReleased(FGameplayTag InInputTag)
{
	if (FRPGQuickSlotInputBindings::OwnsAbilityInput(InputConfigDataAsset, InInputTag)) return;
	if (PlayerCharacter) PlayerCharacter->GetRPGAbilitySystemComponent()->OnAbilityInputReleased(InInputTag);
}

void ARPGPlayerController::ToggleInventory()
{
	ToggleInventoryProjection();
}

void ARPGPlayerController::ToggleSkillWindow()
{
	if (SkillWindowComponent) SkillWindowComponent->ToggleWindow();
}

bool ARPGPlayerController::HandlePlayerWindowKeyDown(UUserWidget* SourceWindow, const FKeyEvent& Event)
{
	if (!SourceWindow || !WindowInputState->ContainsWindow(*SourceWindow) || (!IsProjectionWindow(SourceWindow)
		&& (!SkillWindowComponent || SourceWindow != SkillWindowComponent->GetWindow()))) return false;
	const FKey Key = Event.GetKey();
	const bool bEquipmentToggle = Key == EKeys::P;
	const bool bSkillToggle = SkillWindowComponent && SkillWindowComponent->GetToggleKeys().Contains(Key);
	bool bInventoryToggle = false;
	if (const UInputAction* Action = InputConfigDataAsset
		? InputConfigDataAsset->FindNativeInputActionByTag(RPGGameplayTags::InputTag_ShowEquipmentWidget) : nullptr)
	{
		const UEnhancedInputLocalPlayerSubsystem* Subsystem = GetLocalPlayer()
			? GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		bInventoryToggle = Subsystem && Subsystem->QueryKeysMappedToAction(Action).Contains(Key);
	}
	if (!bSkillToggle && !bInventoryToggle && !bEquipmentToggle && Key != EKeys::Escape) return false;
	// A routed event is consumed so Enhanced Input cannot toggle it a second time.
	if (Event.IsRepeat()) return true;
	if (GameplayAbandonmentWidget) return true;
	if (bEquipmentToggle) ToggleEquipmentProjection();
	else if (bSkillToggle) ToggleSkillWindow();
	else if (bInventoryToggle) ToggleInventory();
	else CloseTopPlayerWindow();
	return true;
}

URPGInventoryProjectionViewModel* ARPGPlayerController::GetInventoryProjectionViewModel()
{
	if (bItemPresentationEnded || !IsLocalController()) return nullptr;
	if (!InventoryProjectionViewModel)
		InventoryProjectionViewModel = NewObject<URPGInventoryProjectionViewModel>(this);
	if (!InventoryProjectionViewModel->IsBoundToPlayerController(this))
		InventoryProjectionViewModel->InitializeFromPlayerController(this);
	return InventoryProjectionViewModel;
}

bool ARPGPlayerController::TryConsumeInventoryItem(const FGuid ItemId, FGuid& OutRequestId)
{
	OutRequestId.Invalidate();
	URPGInventoryProjectionViewModel* Model = GetInventoryProjectionViewModel();
	return Model && Model->RequestConsume(ItemId, OutRequestId);
}

bool ARPGPlayerController::EnsureInventoryProjectionWidget(const bool bEquipment)
{
	if (bItemPresentationEnded || !IsLocalController()) return false;
	TObjectPtr<URPGInventoryProjectionWidget>& Widget = bEquipment
		? EquipmentProjectionWidget
		: InventoryProjectionWidget;
	if (Widget)
	{
		return true;
	}
	const TSubclassOf<URPGInventoryProjectionWidget> WidgetClass =
		(bEquipment
			? EquipmentProjectionWidgetClass
			: InventoryProjectionWidgetClass).LoadSynchronous();
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Missing %s projection widget class."),
			bEquipment ? TEXT("equipment") : TEXT("inventory"));
		return false;
	}

	Widget =
		CreateWidget<URPGInventoryProjectionWidget>(
			this,
			WidgetClass);
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Could not create %s projection widget."),
			bEquipment ? TEXT("equipment") : TEXT("inventory"));
		return false;
	}

	Widget->AddToViewport(20);
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	return true;
}

void ARPGPlayerController::DisplayInventoryProjection(
	URPGInventoryProjectionWidget* Widget)
{
	if (!Widget || !IsProjectionWindow(Widget) || bItemPresentationEnded || !IsLocalController()
		|| Widget->IsRemovingFromParent()) return;
	if (WindowInputState->ContainsWindow(*Widget))
	{
		WindowInputState->FocusWindow(*Widget);
		return;
	}

	const TStrongObjectPtr<URPGInventoryProjectionWidget> KeepAlive(Widget);
	Widget->OnWindowClosed.RemoveAll(this);
	Widget->OnWindowClosed.AddUObject(this, &ThisClass::HandleProjectionWindowClosed, Widget);
	Widget->OnWindowInteraction.RemoveAll(this);
	Widget->OnWindowInteraction.AddUObject(this, &ThisClass::FocusProjectionWindow, Widget);
	Widget->SetIsFocusable(true);
	if (!WindowInputState->BeginWindow(*this, *Widget)) return;
	if (bItemPresentationEnded || !WindowInputState->ContainsWindow(*Widget)) return;
	if (!Widget->IsInViewport()) Widget->AddToViewport(20);
	Widget->SetVisibility(ESlateVisibility::Visible);
	Widget->ActivateWidget();
	if (!bItemPresentationEnded && WindowInputState->ContainsWindow(*Widget) && Widget->IsActivated())
	{
		RPGWindowPlacement::Apply(*Widget, Widget == EquipmentProjectionWidget
			? ERPGWindowSide::Left : Widget == RewardMailWidget ? ERPGWindowSide::Center : ERPGWindowSide::Right);
		WindowInputState->FocusWindow(*Widget);
	}
}

bool ARPGPlayerController::IsProjectionWindow(const UUserWidget* Widget) const
{
	return Widget && (Widget == InventoryProjectionWidget || Widget == EquipmentProjectionWidget || Widget == RewardMailWidget);
}

void ARPGPlayerController::FocusProjectionWindow(URPGInventoryProjectionWidget* Widget)
{
	if (!bItemPresentationEnded && Widget && Widget->IsActivated()) WindowInputState->FocusWindow(*Widget);
}

void ARPGPlayerController::CloseProjectionWindow(URPGInventoryProjectionWidget* Widget)
{
	if (!Widget || !WindowInputState->ContainsWindow(*Widget)) return;
	const TStrongObjectPtr<URPGInventoryProjectionWidget> KeepAlive(Widget);
	Widget->OnWindowClosed.RemoveAll(this);
	Widget->OnWindowInteraction.RemoveAll(this);
	Widget->SetVisibility(ESlateVisibility::Collapsed);
	Widget->DeactivateWidget();
	// End last: focus callbacks may reopen a window. Never overwrite that state.
	WindowInputState->EndWindow(*Widget);
}

bool ARPGPlayerController::CloseTopPlayerWindow()
{
	UUserWidget* Top = WindowInputState->GetTopWindow();
	if (IsProjectionWindow(Top))
	{
		CloseProjectionWindow(Cast<URPGInventoryProjectionWidget>(Top));
		return true;
	}
	if (Top && SkillWindowComponent && Top == SkillWindowComponent->GetWindow())
	{
		SkillWindowComponent->CloseWindow();
		return true;
	}
	return false;
}

void ARPGPlayerController::HandleProjectionWindowClosed(URPGInventoryProjectionWidget* Widget)
{
	CloseProjectionWindow(Widget);
}

bool ARPGPlayerController::ToggleInventoryProjection()
{
	if (bItemPresentationEnded || GameplayAbandonmentWidget) return false;
	if (InventoryProjectionWidget && WindowInputState->ContainsWindow(*InventoryProjectionWidget))
	{
		CloseProjectionWindow(InventoryProjectionWidget);
		return true;
	}
	if (!EnsureInventoryProjectionWidget(false))
	{
		return false;
	}

	DisplayInventoryProjection(InventoryProjectionWidget);
	return true;
}

void ARPGPlayerController::ShowInventoryProjection()
{
	if (EnsureInventoryProjectionWidget(false))
	{
		DisplayInventoryProjection(InventoryProjectionWidget);
	}
}

void ARPGPlayerController::ShowEquipmentProjection()
{
	if (EnsureInventoryProjectionWidget(true))
	{
		DisplayInventoryProjection(EquipmentProjectionWidget);
	}
}

void ARPGPlayerController::ToggleEquipmentProjection()
{
	if (bItemPresentationEnded || GameplayAbandonmentWidget) return;
	if (EquipmentProjectionWidget && WindowInputState->ContainsWindow(*EquipmentProjectionWidget))
	{
		CloseProjectionWindow(EquipmentProjectionWidget);
		return;
	}
	ShowEquipmentProjection();
}

void ARPGPlayerController::ShowRewardMail()
{
	if (bItemPresentationEnded || !IsLocalController())
	{
		return;
	}
	if (!RewardMailWidget)
	{
		TSubclassOf<URPGRewardMailWidget> WidgetClass = RewardMailWidgetClass.IsNull()
			? URPGRewardMailWidget::StaticClass() : RewardMailWidgetClass.LoadSynchronous();
		if (!WidgetClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("Reward mail presentation asset is unavailable; using the native layout."));
			WidgetClass = URPGRewardMailWidget::StaticClass();
		}
		RewardMailWidget = CreateWidget<URPGRewardMailWidget>(this, WidgetClass);
		if (!RewardMailWidget)
		{
			return;
		}
		RewardMailWidget->AddToViewport(20);
		RewardMailWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
	DisplayInventoryProjection(RewardMailWidget);
}

void ARPGPlayerController::ToggleOptionMenu()
{
	if (HUD) HUD->ToggleOptionMenu();
}

void ARPGPlayerController::ShowEquipmentWidget()
{
}
