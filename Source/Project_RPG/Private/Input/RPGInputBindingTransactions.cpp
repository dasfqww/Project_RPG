#include "Input/RPGInputBindingTransactions.h"
#include "Input/RPGInputBindingRestoreView.h"
#include "UI/Option/RPGInputOptionsRuntime.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/UI/RPGSkillWindowComponent.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Settings/RPGGameUserSettings.h"
#include "RPGGameplayTags.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

void FRPGInputBindingTransactions::UpdateInputMappings()
{
	APlayerController* Controller = &Host.GetInputBindingController();
	ULocalPlayer* LocalPlayer = Controller->GetLocalPlayer();
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

void FRPGInputBindingTransactions::ApplyKeyMapping(FGameplayTag InTag, FKey NewKey)
{
	FText Error;
	if (!TryApplyKeyBinding(InTag, FKey(), NewKey, Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("Input binding rejected: %s"), *Error.ToString());
	}
}

bool FRPGInputBindingTransactions::TryApplyKeyBinding(FGameplayTag InTag, FKey DefaultKey, FKey NewKey, FText& OutError)
{
	return ApplyKeyBindingInternal(InTag, DefaultKey, NewKey, OutError, true);
}

bool FRPGInputBindingTransactions::TryApplyKeyBindingWithPolicy(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
	TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError)
{
	return ApplyKeyBindingInternal(InTag, DefaultKey, NewKey, OutError, true, &ValidatePolicy);
}


bool FRPGInputBindingTransactions::TryRestoreDefaultKeyBindings(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy, FText& OutError)
{
	return RestoreDefaultKeyBindingsInternal(ExpectedBindings, ValidatePolicy, nullptr, OutError);
}

bool FRPGInputBindingTransactions::TryRestoreDefaultKeyBindingsWithAdditional(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
	const FRPGInputBindingRestoreExtension& Additional, FText& OutError)
{
	return RestoreDefaultKeyBindingsInternal(ExpectedBindings, ValidatePolicy, &Additional, OutError);
}

bool FRPGInputBindingTransactions::RestoreDefaultKeyBindingsInternal(
	const TArray<FRPGKeyMapping>& ExpectedBindings,
	TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
	const FRPGInputBindingRestoreExtension* Additional, FText& OutError)
{
	APlayerController* Controller = &Host.GetInputBindingController();
	OutError = FText::GetEmpty();
	if (!IsInGameThread() || bApplyingKeyBinding || (ExpectedBindings.IsEmpty() && (!Additional || Additional->Contexts.IsEmpty())))
	{
		OutError = NSLOCTEXT("RPGInputOptions", "DefaultsBusy", "키 설정을 복원할 수 없습니다. 잠시 후 다시 시도해 주세요.");
		return false;
	}
	TStrongObjectPtr<APlayerController> KeepAlive(Controller);
	// Notify after the request guards are released, including failures after a live key change.
	bool bPrimaryMappingsChanged = false;
	const TWeakObjectPtr<ULocalPlayer> ChangedPlayer(Controller->GetLocalPlayer());
	const TWeakObjectPtr<UWorld> ChangedWorld(Controller->GetWorld());
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
	TStrongObjectPtr<UWorld> BindingWorld(Controller->GetWorld());
	TStrongObjectPtr<ULocalPlayer> BindingPlayer(Controller->GetLocalPlayer());
	TStrongObjectPtr<UDataAsset_InputConfig> RuntimeConfig(Host.GetInputBindingConfig());
	TStrongObjectPtr<const UDataAsset_InputConfig> Source(Host.GetSourceInputConfig());
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
		return IsValid(Controller) && !Controller->IsUnreachable() && !Controller->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& Controller->IsLocalController() && !Controller->IsActorBeingDestroyed() && BindingWorld.IsValid()
			&& !BindingWorld->bIsTearingDown && Controller->GetWorld() == BindingWorld.Get()
			&& BindingPlayer.IsValid() && Controller->GetLocalPlayer() == BindingPlayer.Get()
			&& BindingPlayer->GetPlayerController(nullptr) == Controller
			&& Subsystem.IsValid()
			&& BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() == Subsystem.Get()
			&& (!Additional || (IsValid(BindingPlayerInput.Get()) && !BindingPlayerInput->IsUnreachable()
				&& !BindingPlayerInput->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
				&& Subsystem->GetPlayerInput() == BindingPlayerInput.Get() && Controller->PlayerInput.Get() == BindingPlayerInput.Get()))
			&& Source.IsValid() && Host.GetSourceInputConfig() == Source.Get()
			&& SourceContext.IsValid() && Source->DefaultMappingContext == SourceContext.Get()
			&& RuntimeConfig.IsValid() && RuntimeConfig->GetOuter() == Controller
			&& Host.GetInputBindingConfig() == RuntimeConfig.Get() && RuntimeContext.IsValid()
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
		View.RebuiltContext = Host.GetRebuiltInputContext();
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
	if (bRestorePrimary && Host.GetInputQuickSlots()) { Host.GetInputQuickSlots()->ReleaseAllSkillInputs(); }
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
	if (bRestorePrimary && IsValid(Host.GetRebuiltInputContext()))
		FRPGQuickSlotInputBindings::RebuildSkillContext(RuntimeConfig.Get(), *Host.GetRebuiltInputContext());
	if (!ValidateCommittedBindings()) { return false; }
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Subsystem->RequestRebuildControlMappings(Options);
	if (!ValidateCommittedBindings()) { return false; }
	if (bRestorePrimary && HasSameOwner() && IsValid(Host.GetInputSkillWindow())) { Host.GetInputSkillWindow()->RefreshCloseKeys(); }
	if (!ValidateCommittedBindings()) { return false; }
	Settings->SaveSettings();
	// SaveSettings may invoke synchronous cloud/config callbacks; success requires the same committed state.
	return ValidateCommittedBindings();
}

bool FRPGInputBindingTransactions::ApplyKeyBindingInternal(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
	FText& OutError, bool bSaveSettings, TFunctionRef<bool(FText&)>* ValidatePolicy)
{
	APlayerController* Controller = &Host.GetInputBindingController();
	OutError = FText::GetEmpty();
	if (!IsInGameThread() || bApplyingKeyBinding)
	{
		OutError = NSLOCTEXT("RPGInputOptions", "BindingBusy", "An input binding change is already in progress.");
		return false;
	}
	TStrongObjectPtr<APlayerController> KeepAlive(Controller);
	// Notify after the request guards are released, including failures after a live key change.
	bool bPrimaryMappingsChanged = false;
	const TWeakObjectPtr<ULocalPlayer> ChangedPlayer(Controller->GetLocalPlayer());
	const TWeakObjectPtr<UWorld> ChangedWorld(Controller->GetWorld());
	ON_SCOPE_EXIT
	{
		if (bPrimaryMappingsChanged)
		{
			IRPGInputOptionsRuntime::NotifyMappingsChanged(ChangedPlayer.Get(), ChangedWorld.Get());
		}
	};
	TGuardValue<bool> ApplyingGuard(bApplyingKeyBinding, true);
	const TStrongObjectPtr<UWorld> BindingWorld(Controller->GetWorld());
	if (!Controller->IsLocalController() || Controller->IsActorBeingDestroyed() || !BindingWorld.IsValid()
		|| BindingWorld->bIsTearingDown || !InTag.IsValid() || !NewKey.IsValid())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "InvalidBinding", "A local player and a valid key are required.");
		return false;
	}
	if (InTag == RPGGameplayTags::InputTag_ShowSkillWindow && Host.GetInputSkillWindow()
		&& Host.GetInputSkillWindow()->RemapFallbackKey(NewKey))
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
	TStrongObjectPtr<UDataAsset_InputConfig> RuntimeConfig(Host.GetInputBindingConfig());
	TStrongObjectPtr<const UDataAsset_InputConfig> Source(Host.GetSourceInputConfig());
	TStrongObjectPtr<UInputMappingContext> RuntimeContext(
		RuntimeConfig.IsValid() ? RuntimeConfig->DefaultMappingContext : nullptr);
	TStrongObjectPtr<const UInputMappingContext> SourceContext(
		Source.IsValid() ? Source->DefaultMappingContext : nullptr);
	TStrongObjectPtr<ULocalPlayer> BindingPlayer(Controller->GetLocalPlayer());
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
		return IsValid(Controller) && !Controller->IsUnreachable() && !Controller->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& Controller->IsLocalController() && !Controller->IsActorBeingDestroyed() && BindingWorld.IsValid()
			&& !BindingWorld->bIsTearingDown && Controller->GetWorld() == BindingWorld.Get()
			&& BindingPlayer.IsValid() && Controller->GetLocalPlayer() == BindingPlayer.Get()
			&& BindingPlayer->GetPlayerController(nullptr) == Controller
			&& Subsystem.IsValid()
			&& BindingPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() == Subsystem.Get()
			&& Source.IsValid() && Host.GetSourceInputConfig() == Source.Get()
			&& SourceContext.IsValid() && Source->DefaultMappingContext == SourceContext.Get()
			&& RuntimeConfig.IsValid() && RuntimeConfig->GetOuter() == Controller
			&& Host.GetInputBindingConfig() == RuntimeConfig.Get() && RuntimeContext.IsValid()
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
	if (Host.GetInputQuickSlots()) { Host.GetInputQuickSlots()->ReleaseAllSkillInputs(); }
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
	if (IsValid(Host.GetRebuiltInputContext()))
		FRPGQuickSlotInputBindings::RebuildSkillContext(RuntimeConfig.Get(), *Host.GetRebuiltInputContext());
	if (!ValidateCommittedBindings()) { return false; }
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Subsystem->RequestRebuildControlMappings(Options);
	if (!ValidateCommittedBindings()) { return false; }
	if (HasSameOwner() && InTag == RPGGameplayTags::InputTag_ShowSkillWindow && IsValid(Host.GetInputSkillWindow()))
	{
		Host.GetInputSkillWindow()->RefreshCloseKeys();
	}
	if (!ValidateCommittedBindings()) { return false; }
	if (bSaveSettings) { Settings->SaveSettings(); }
	return ValidateCommittedBindings();
}

FKey FRPGInputBindingTransactions::GetCurrentKeyForBinding(FGameplayTag InTag, FKey DefaultKey) const
{
	if (!DefaultKey.IsValid()) { return GetCurrentKeyForTag(InTag); }
	const UDataAsset_InputConfig* Source = Host.GetSourceInputConfig();
	const UInputMappingContext* Defaults = Source ? Source->DefaultMappingContext : nullptr;
	const UInputMappingContext* Current = Host.GetInputBindingConfig() ? Host.GetInputBindingConfig()->DefaultMappingContext : nullptr;
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

FKey FRPGInputBindingTransactions::GetCurrentKeyForTag(FGameplayTag InTag) const
{
	APlayerController* Controller = &Host.GetInputBindingController();
	// Labels follow the live action mapping, including old/new tags sharing an
	// action. A saved value for one alias may no longer be the effective key.
	if (Host.GetInputBindingConfig() && Host.GetInputBindingConfig()->GetOuter() == Controller)
	{
		UInputAction* TargetAction = Host.GetInputBindingConfig()->FindNativeInputActionByTag(InTag);
		if (!TargetAction) TargetAction = Host.GetInputBindingConfig()->FindAbilityInputActionByTag(InTag);

		if (TargetAction && Host.GetInputBindingConfig()->DefaultMappingContext)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Host.GetInputBindingConfig()->DefaultMappingContext->GetMappings())
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
	if (Host.GetInputBindingConfig() && Host.GetInputBindingConfig()->GetOuter() != Controller)
	{
		const UInputAction* Action = Host.GetInputBindingConfig()->FindNativeInputActionByTag(InTag);
		if (!Action) Action = Host.GetInputBindingConfig()->FindAbilityInputActionByTag(InTag);
		if (Action && Host.GetInputBindingConfig()->DefaultMappingContext)
		{
			for (const auto& Mapping : Host.GetInputBindingConfig()->DefaultMappingContext->GetMappings())
				if (Mapping.Action == Action) return Mapping.Key;
		}
	}

	if (InTag == RPGGameplayTags::InputTag_ShowSkillWindow && Host.GetInputSkillWindow())
	{
		return Host.GetInputSkillWindow()->GetToggleKey();
	}
	return FKey();
}
