#include "RPGInputOptionsRuntime.h"

#include "Controller/RPGPlayerController.h"
#include "Settings/RPGGameUserSettings.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputMappingQuery.h"
#include "Input/RPGInputBindingRestoreView.h"
#include "Input/RPGAdditionalInputProvider.h"
#include "Features/IModularFeatures.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FAdditionalContextView
	{
		TWeakObjectPtr<const UObject> Provider;
		FRPGAdditionalInputContext Mapping;
	};

	bool IsLiveInputOptionsObject(const UObject* Object)
	{
		return IsValid(Object) && !Object->IsUnreachable()
			&& !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
	}

	void GatherAdditionalContexts(const ARPGPlayerController* Controller, TArray<FAdditionalContextView>& OutContexts)
	{
		OutContexts.Reset();
		if (!IsInGameThread() || !IsLiveInputOptionsObject(Controller) || Controller->IsActorBeingDestroyed()
			|| !Controller->IsLocalController()) { return; }
		TStrongObjectPtr<const ARPGPlayerController> KeepController(Controller);
		TStrongObjectPtr<ULocalPlayer> Player(Controller->GetLocalPlayer());
		TStrongObjectPtr<UWorld> World(Controller->GetWorld());
		TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem(
			Player.IsValid() ? Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr);
		TStrongObjectPtr<UEnhancedPlayerInput> PlayerInput(Subsystem.IsValid() ? Subsystem->GetPlayerInput() : nullptr);
		const auto IsCurrentPlayer = [&]()
		{
			return IsLiveInputOptionsObject(Controller) && !Controller->IsActorBeingDestroyed()
				&& IsLiveInputOptionsObject(Player.Get()) && Controller->GetLocalPlayer() == Player.Get()
				&& Player->GetPlayerController(nullptr) == Controller && IsLiveInputOptionsObject(World.Get())
				&& !World->bIsTearingDown && Player->GetWorld() == World.Get() && Controller->GetWorld() == World.Get()
				&& IsLiveInputOptionsObject(Subsystem.Get()) && IsLiveInputOptionsObject(PlayerInput.Get())
				&& Subsystem->GetPlayerInput() == PlayerInput.Get() && Controller->PlayerInput.Get() == PlayerInput.Get();
		};
		if (!IsCurrentPlayer()) { return; }
		IModularFeatures& Features = IModularFeatures::Get();
		const FName FeatureName = IRPGAdditionalInputProvider::GetFeatureName();
		const TArray<IRPGAdditionalInputProvider*> Providers =
			Features.GetModularFeatureImplementations<IRPGAdditionalInputProvider>(FeatureName);
		for (IRPGAdditionalInputProvider* Provider : Providers)
		{
			if (!IsCurrentPlayer()) { OutContexts.Reset(); return; }
			if (!Features.GetModularFeatureImplementations<IRPGAdditionalInputProvider>(FeatureName).Contains(Provider)) { continue; }
			TStrongObjectPtr<const UObject> Owner(Provider->GetInputOptionsOwner());
			if (!IsLiveInputOptionsObject(Owner.Get())) { continue; }
			TArray<FRPGAdditionalInputContext> Offered;
			Provider->GetInputOptionContexts(Player.Get(), Offered);
			if (!IsCurrentPlayer()) { OutContexts.Reset(); return; }
			if (!IsLiveInputOptionsObject(Owner.Get())
				|| !Features.GetModularFeatureImplementations<IRPGAdditionalInputProvider>(FeatureName).Contains(Provider)) { continue; }
			for (const FRPGAdditionalInputContext& Mapping : Offered)
			{
				if (!IsLiveInputOptionsObject(Mapping.SourceContext.Get()) || !IsLiveInputOptionsObject(Mapping.AppliedContext.Get())
					|| !Subsystem->HasMappingContext(Mapping.AppliedContext.Get())) { continue; }
				FAdditionalContextView& View = OutContexts.AddDefaulted_GetRef();
				View.Provider = Owner.Get();
				View.Mapping = Mapping;
			}
		}
		// Stable ordering also selects one representative when several feature actions share a registration.
		OutContexts.Sort([](const FAdditionalContextView& A, const FAdditionalContextView& B)
		{
			const FString ASource = GetPathNameSafe(A.Mapping.SourceContext.Get());
			const FString BSource = GetPathNameSafe(B.Mapping.SourceContext.Get());
			if (ASource != BSource) { return ASource < BSource; }
			const FString AApplied = GetPathNameSafe(A.Mapping.AppliedContext.Get());
			const FString BApplied = GetPathNameSafe(B.Mapping.AppliedContext.Get());
			return AApplied != BApplied ? AApplied < BApplied : GetPathNameSafe(A.Provider.Get()) < GetPathNameSafe(B.Provider.Get());
		});
		TMap<const UInputMappingContext*, TSet<const UInputMappingContext*>> Seen;
		OutContexts.RemoveAll([&](const FAdditionalContextView& View)
		{
			TSet<const UInputMappingContext*>& Applied = Seen.FindOrAdd(View.Mapping.SourceContext.Get());
			if (Applied.Contains(View.Mapping.AppliedContext.Get())) { return true; }
			Applied.Add(View.Mapping.AppliedContext.Get());
			return false;
		});
	}

	FKey ReadAdditionalBindingKey(const FRPGAdditionalInputContext& Context, const UInputAction* Action, FKey DefaultKey)
	{
		const UInputMappingContext* Source = Context.SourceContext.Get();
		const UInputMappingContext* Applied = Context.AppliedContext.Get();
		if (!IsLiveInputOptionsObject(Source) || !IsLiveInputOptionsObject(Applied) || !IsLiveInputOptionsObject(Action)
			|| !DefaultKey.IsValid() || Source->GetMappings().Num() != Applied->GetMappings().Num()) { return FKey(); }
		FKey Current;
		for (int32 Index = 0; Index < Source->GetMappings().Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = Source->GetMappings()[Index];
			const FEnhancedActionKeyMapping& Live = Applied->GetMappings()[Index];
			if (Authored.Action != Live.Action) { return FKey(); }
			if (Authored.Action == Action && Authored.Key == DefaultKey)
			{
				if (!Live.Key.IsValid() || (Current.IsValid() && Current != Live.Key)) { return FKey(); }
				Current = Live.Key;
			}
		}
		return Current;
	}

	void AppendAdditionalRows(const ARPGPlayerController* Controller, TArray<FRPGInputOptionBinding>& OutRows)
	{
		TArray<FAdditionalContextView> Contexts;
		GatherAdditionalContexts(Controller, Contexts);
		const UDataAsset_InputConfig* PrimaryConfig = Controller ? Controller->GetSourceInputConfig() : nullptr;
		for (const FAdditionalContextView& Context : Contexts)
		{
			const UInputMappingContext* Source = Context.Mapping.SourceContext.Get();
			if (!IsLiveInputOptionsObject(Source) || (PrimaryConfig && PrimaryConfig->DefaultMappingContext == Source)) { continue; }
			TMap<const UInputAction*, TSet<FKey>> Seen;
			for (const FEnhancedActionKeyMapping& Mapping : Source->GetMappings())
			{
				if (!IsLiveInputOptionsObject(Mapping.Action.Get()) || !Mapping.Key.IsValid()) { continue; }
				TSet<FKey>& Keys = Seen.FindOrAdd(Mapping.Action.Get());
				if (Keys.Contains(Mapping.Key)) { continue; }
				Keys.Add(Mapping.Key);
				FRPGInputOptionBinding& Row = OutRows.AddDefaulted_GetRef();
				Row.Action = Mapping.Action.Get();
				Row.DefaultKey = Mapping.Key;
				Row.CurrentKey = ReadAdditionalBindingKey(Context.Mapping, Mapping.Action.Get(), Mapping.Key);
				const FText Name = Mapping.GetDisplayName().IsEmpty()
					? FText::FromName(Mapping.Action->GetFName()) : Mapping.GetDisplayName();
				Row.DisplayName = FText::Format(NSLOCTEXT("RPGInputOptions", "BindingName", "{0} [{1}]"),
					Name, Mapping.Key.GetDisplayName());
				Row.bCanRemap = Context.Mapping.bCanRemap && Mapping.Action->IsAsset()
					&& !Mapping.Key.IsAnalog() && Row.CurrentKey.IsValid();
				Row.bIsAdditionalMapping = true;
				Row.AdditionalInputProvider = Context.Provider;
				Row.SourceMappingContext = Context.Mapping.SourceContext;
				Row.AppliedMappingContext = Context.Mapping.AppliedContext;
			}
		}
	}

	bool ValidateAdditionalKeyChange(ARPGPlayerController* Controller, const FRPGInputOptionBinding& Expected,
		FKey NewKey, FText& OutError)
	{
		OutError = FText::GetEmpty();
		if (!Expected.bIsAdditionalMapping || !Expected.bCanRemap || !Expected.AdditionalInputProvider.IsValid()
			|| !Expected.SourceMappingContext.IsValid() || !Expected.AppliedMappingContext.IsValid() || !Expected.Action.IsValid()
			|| !Expected.CurrentKey.IsValid())
		{
			OutError = NSLOCTEXT("RPGInputOptions", "AdditionalBindingChanged",
				"입력 설정이 변경되었습니다. 목록을 새로 열고 다시 시도해 주세요.");
			return false;
		}
		if (!NewKey.IsValid() || NewKey.IsAnalog() || (NewKey == EKeys::Escape && Expected.DefaultKey != EKeys::Escape))
		{
			OutError = NSLOCTEXT("RPGInputOptions", "UnsupportedKey", "Choose a button key. Escape cancels key selection.");
			return false;
		}
		if (!Expected.DefaultKey.IsValid() || Expected.DefaultKey.IsAnalog()
			|| Expected.DefaultKey.IsGamepadKey() != NewKey.IsGamepadKey())
		{
			OutError = NSLOCTEXT("RPGInputOptions", "DeviceMismatch", "Choose a key for the same input device.");
			return false;
		}
		TArray<FAdditionalContextView> Contexts;
		GatherAdditionalContexts(Controller, Contexts);
		const FAdditionalContextView* Context = Contexts.FindByPredicate([&](const FAdditionalContextView& View)
		{
			return View.Provider == Expected.AdditionalInputProvider && View.Mapping.SourceContext == Expected.SourceMappingContext
				&& View.Mapping.AppliedContext == Expected.AppliedMappingContext && View.Mapping.bCanRemap;
		});
		if (!Context || ReadAdditionalBindingKey(Context->Mapping, Expected.Action.Get(), Expected.DefaultKey) != Expected.CurrentKey)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "AdditionalBindingChanged",
				"입력 설정이 변경되었습니다. 목록을 새로 열고 다시 시도해 주세요.");
			return false;
		}
		if (Expected.CurrentKey == NewKey) { return true; }
		const UInputMappingContext* Source = Expected.SourceMappingContext.Get();
		const UInputMappingContext* Applied = Expected.AppliedMappingContext.Get();
		const auto ReportConflict = [&](const UInputAction* Other)
		{
			OutError = FText::Format(NSLOCTEXT("RPGInputOptions", "KeyInUse", "{0} is already assigned to {1}."),
				NewKey.GetDisplayName(), Other ? FText::FromName(Other->GetFName()) : FText::GetEmpty());
			return false;
		};
		for (int32 Index = 0; Index < Applied->GetMappings().Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = Source->GetMappings()[Index];
			const FEnhancedActionKeyMapping& Live = Applied->GetMappings()[Index];
			const bool bTarget = Authored.Action == Expected.Action.Get() && Authored.Key == Expected.DefaultKey;
			const bool bAuthoredSharedKey = NewKey == Expected.DefaultKey && Authored.Key == Expected.DefaultKey;
			if (!bTarget && !bAuthoredSharedKey && Live.Key == NewKey) { return ReportConflict(Live.Action.Get()); }
		}
		UEnhancedInputLocalPlayerSubsystem* Subsystem = Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		TArray<FMappingQueryIssue> Issues;
		if (!Subsystem || Subsystem->QueryMapKeyInActiveContextSet(Applied, Expected.Action.Get(), NewKey, Issues,
			EMappingQueryIssue::NoIssue) != EMappingQueryResult::MappingAvailable)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "NoLocalPlayer", "Input bindings are not ready.");
			return false;
		}
		for (const FMappingQueryIssue& Issue : Issues)
		{
			if (Issue.BlockingContext && Issue.BlockingContext != Applied
				&& Issue.BlockingAction && Issue.BlockingAction.Get() != Expected.Action.Get()) { return ReportConflict(Issue.BlockingAction.Get()); }
		}
		return true;
	}

	bool ApplyAdditionalBinding(ARPGPlayerController* Controller, const FRPGInputOptionBinding& Expected,
		FKey NewKey, FText& OutError)
	{
		if (!ValidateAdditionalKeyChange(Controller, Expected, NewKey, OutError)) { return false; }
		TStrongObjectPtr<ARPGPlayerController> KeepController(Controller);
		TStrongObjectPtr<const UObject> Owner(Expected.AdditionalInputProvider.Get());
		IModularFeatures& Features = IModularFeatures::Get();
		const TArray<IRPGAdditionalInputProvider*> Providers =
			Features.GetModularFeatureImplementations<IRPGAdditionalInputProvider>(IRPGAdditionalInputProvider::GetFeatureName());
		for (IRPGAdditionalInputProvider* Provider : Providers)
		{
			if (Provider->GetInputOptionsOwner() != Owner.Get()) { continue; }
			FRPGAdditionalInputBinding Request;
			Request.SourceContext = Expected.SourceMappingContext;
			Request.AppliedContext = Expected.AppliedMappingContext;
			Request.Action = Expected.Action;
			Request.DefaultKey = Expected.DefaultKey;
			Request.CurrentKey = Expected.CurrentKey;
			return Provider->TryApplyInputOptionBinding(Controller->GetLocalPlayer(), Request, NewKey,
				[Controller, Expected, NewKey](FText& PolicyError)
				{
					return ValidateAdditionalKeyChange(Controller, Expected, NewKey, PolicyError);
				}, OutError);
		}
		OutError = NSLOCTEXT("RPGInputOptions", "AdditionalBindingChanged",
			"입력 설정이 변경되었습니다. 목록을 새로 열고 다시 시도해 주세요.");
		return false;
	}

	bool ValidateDefaultRestoration(const FRPGInputBindingRestoreView& View, FText& OutError)
	{
		const auto NotReady = [&]()
		{
			OutError = NSLOCTEXT("RPGInputOptions", "DefaultsNotReady", "키 설정이 준비되지 않았습니다.");
			return false;
		};
		TArray<const FRPGInputBindingRestoreContextView*> Contexts;
		Contexts.Add(&View);
		for (const FRPGInputBindingRestoreContextView& Additional : View.AdditionalContexts) { Contexts.Add(&Additional); }
		TSet<const UInputMappingContext*> Seen;
		bool bHasTargets = false;
		for (const FRPGInputBindingRestoreContextView* Context : Contexts)
		{
			if (!Context->RuntimeContext || !Context->InputSubsystem || Context->InputSubsystem != View.InputSubsystem
				|| Context->DefaultMappings.Num() != Context->FinalKeys.Num() || Seen.Contains(Context->RuntimeContext)) { return NotReady(); }
			Seen.Add(Context->RuntimeContext);
			for (int32 Restored : Context->RestoredMappingIndices)
			{
				if (!Context->DefaultMappings.IsValidIndex(Restored) || !Context->DefaultMappings[Restored].Action
					|| !Context->FinalKeys[Restored].IsValid()) { return NotReady(); }
				bHasTargets = true;
			}
		}
		if (!bHasTargets) { return NotReady(); }
		const auto ReportConflict = [&](FKey Key)
		{
			OutError = FText::Format(NSLOCTEXT("RPGInputOptions", "DefaultsConflict",
				"기본 키 {0}이(가) 다른 명령에서 사용 중입니다. 해당 키를 변경한 후 다시 시도해 주세요."),
				Key.GetDisplayName());
			return false;
		};
		for (const FRPGInputBindingRestoreContextView* Context : Contexts)
		{
			for (int32 Restored : Context->RestoredMappingIndices)
			{
				for (const FRPGInputBindingRestoreContextView* OtherContext : Contexts)
				{
					for (int32 Other = 0; Other < OtherContext->FinalKeys.Num(); ++Other)
					{
						if ((OtherContext == Context && Other == Restored)
							|| OtherContext->FinalKeys[Other] != Context->FinalKeys[Restored]) { continue; }
						const bool bSameActionAcrossContexts = OtherContext != Context
							&& OtherContext->DefaultMappings[Other].Action == Context->DefaultMappings[Restored].Action;
						// Keep authored shared defaults; compare all other mappings against the complete final plan.
						if (!bSameActionAcrossContexts
							&& OtherContext->DefaultMappings[Other].Key != Context->DefaultMappings[Restored].Key)
						{
							return ReportConflict(Context->FinalKeys[Restored]);
						}
					}
				}
				TArray<FMappingQueryIssue> Issues;
				if (Context->InputSubsystem->QueryMapKeyInActiveContextSet(
					Context->RuntimeContext, Context->DefaultMappings[Restored].Action.Get(), Context->FinalKeys[Restored], Issues,
					EMappingQueryIssue::NoIssue) != EMappingQueryResult::MappingAvailable) { return NotReady(); }
				for (const FMappingQueryIssue& Issue : Issues)
				{
					// Participating contexts and their derived mappings were checked against their final keys above.
					const bool bPlanned = Contexts.ContainsByPredicate([&](const FRPGInputBindingRestoreContextView* Planned)
					{
						return Issue.BlockingContext && (Issue.BlockingContext == Planned->RuntimeContext
							|| Issue.BlockingContext == Planned->RebuiltContext);
					});
					if (!bPlanned && Issue.BlockingContext && Issue.BlockingAction
						&& Issue.BlockingAction != Context->DefaultMappings[Restored].Action)
					{
						return ReportConflict(Context->FinalKeys[Restored]);
					}
				}
			}
		}
		return true;
	}

	FText GetActionName(const UDataAsset_InputConfig* Config, const UInputAction* Action)
	{
		FText HiddenName;
		if (Config)
		{
			for (const TArray<FWarriorInputActionConfig>* Entries : { &Config->NativeInputActions, &Config->AbilityInputActions })
			{
				for (const FWarriorInputActionConfig& Entry : *Entries)
				{
					if (!Entry.IsValid() || Entry.InputAction.Get() != Action
						|| Config->FindInputActionByTag(Entry.InputTag) != Action) { continue; }
					const FText ActionName = Entry.InputActionName.IsEmpty()
						? FText::FromName(Action->GetFName()) : Entry.InputActionName;
					if (Entry.bShowInInputOptionMenu) { return ActionName; }
					if (HiddenName.IsEmpty()) { HiddenName = ActionName; }
				}
			}
		}
		if (!HiddenName.IsEmpty()) { return HiddenName; }
		return Action ? FText::FromName(Action->GetFName()) : FText::GetEmpty();
	}

	bool ValidateDisplayedBinding(ARPGPlayerController* Controller,
		const FRPGInputOptionBinding& Expected, FText& OutError)
	{
		const UDataAsset_InputConfig* Config = IsValid(Controller) ? Controller->GetSourceInputConfig() : nullptr;
		if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController()
			|| !Expected.bCanRemap || !Expected.InputTag.IsValid() || !Expected.DefaultKey.IsValid()
			|| !Expected.CurrentKey.IsValid() || !Expected.SourceConfig.IsValid() || !Expected.Action.IsValid()
			|| Config != Expected.SourceConfig.Get()
			|| Config->FindInputActionByTag(Expected.InputTag) != Expected.Action.Get()
			|| Controller->GetCurrentKeyForBinding(Expected.InputTag, Expected.DefaultKey) != Expected.CurrentKey)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "DisplayedBindingChanged",
				"키 설정이 변경되었습니다. 목록을 새로 열고 다시 시도해 주세요.");
			return false;
		}
		return true;
	}

	bool ValidateKeyChange(ARPGPlayerController* Controller, FGameplayTag InputTag,
		FKey DefaultKey, FKey NewKey, FText& OutError)
	{
		OutError = FText::GetEmpty();
		if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || !Controller->IsLocalController() || !Controller->GetLocalPlayer()
			|| !Controller->GetWorld() || Controller->GetWorld()->bIsTearingDown)
		{
			OutError = NSLOCTEXT("RPGInputOptions", "NoLocalPlayer", "Input bindings are not ready.");
			return false;
		}
		if (!NewKey.IsValid() || NewKey.IsAnalog() || (NewKey == EKeys::Escape && DefaultKey != EKeys::Escape))
		{
			OutError = NSLOCTEXT("RPGInputOptions", "UnsupportedKey", "Choose a button key. Escape cancels key selection.");
			return false;
		}
		if (DefaultKey.IsValid() && (DefaultKey.IsAnalog() || DefaultKey.IsGamepadKey() != NewKey.IsGamepadKey()))
		{
			OutError = NSLOCTEXT("RPGInputOptions", "DeviceMismatch", "Choose a key for the same input device.");
			return false;
		}
		const FKey CurrentKey = Controller->GetCurrentKeyForBinding(InputTag, DefaultKey);
		if (CurrentKey == NewKey) { return true; }
		const UDataAsset_InputConfig* Config = Controller->GetSourceInputConfig();
		const UInputMappingContext* Defaults = Config ? Config->DefaultMappingContext : nullptr;
		const UDataAsset_InputConfig* RuntimeConfig = Controller->GetInputConfig();
		const UInputMappingContext* RuntimeContext = RuntimeConfig ? RuntimeConfig->DefaultMappingContext : nullptr;
		const UInputAction* Target = Config ? Config->FindInputActionByTag(InputTag) : nullptr;

		FKey RequestedDefault = DefaultKey;
		if (!RequestedDefault.IsValid() && Defaults && Target)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Defaults->GetMappings())
			{
				if (Mapping.Action == Target && !Mapping.Key.IsGamepadKey() && !Mapping.Key.IsAnalog())
				{
					RequestedDefault = Mapping.Key;
					break;
				}
			}
		}
		auto ReportConflict = [&](const UInputAction* Other)
		{
			OutError = FText::Format(NSLOCTEXT("RPGInputOptions", "KeyInUse", "{0} is already assigned to {1}."),
				NewKey.GetDisplayName(), GetActionName(Config, Other));
			return false;
		};
		if (RuntimeContext)
		{
			for (int32 Index = 0; Index < RuntimeContext->GetMappings().Num(); ++Index)
			{
				const FEnhancedActionKeyMapping& Mapping = RuntimeContext->GetMappings()[Index];
				if (Mapping.Key != NewKey) { continue; }
				const bool bRequested = Defaults && Defaults->GetMappings().IsValidIndex(Index)
					&& Defaults->GetMappings()[Index].Action == Target
					&& Defaults->GetMappings()[Index].Key == RequestedDefault;
				if (!bRequested) { return ReportConflict(Mapping.Action); }
			}
		}
		UEnhancedInputLocalPlayerSubsystem* Subsystem =
			Controller->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		UEnhancedPlayerInput* PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
		if (PlayerInput)
		{
			// Read the current effective mappings through the engine's public, read-only view.
			for (const FEnhancedActionKeyMapping& Mapping : PlayerInput->GetEnhancedActionMappingsView())
			{
				if (Mapping.Key == NewKey && Mapping.Action != Target) { return ReportConflict(Mapping.Action); }
			}
		}
		return true;
	}
}

void FRPGInputOptionsRuntime::BuildRows(const ARPGPlayerController* Controller,
	const UDataAsset_InputConfig* PreviewConfig, TArray<FRPGInputOptionBinding>& OutRows) const
{
	OutRows.Reset();
	if (Controller && (!IsValid(Controller) || Controller->IsActorBeingDestroyed())) { return; }
	const UDataAsset_InputConfig* Config = Controller ? Controller->GetSourceInputConfig() : PreviewConfig;
	if (IsValid(Config) && IsValid(Config->DefaultMappingContext))
	{
		TMap<const UInputAction*, TSet<FKey>> SeenBindings;
		for (const TArray<FWarriorInputActionConfig>* Entries : { &Config->NativeInputActions, &Config->AbilityInputActions })
		{
			for (const FWarriorInputActionConfig& Entry : *Entries)
			{
				if (!Entry.IsValid() || !Entry.bShowInInputOptionMenu) { continue; }
				const UInputAction* Action = Config->FindInputActionByTag(Entry.InputTag);
				// A row's label, authored mapping and live tag lookup must refer to the same action.
				if (!IsValid(Action) || Action != Entry.InputAction.Get()) { continue; }
				TSet<FKey>& SeenKeys = SeenBindings.FindOrAdd(Action);
				for (const FEnhancedActionKeyMapping& Mapping : Config->DefaultMappingContext->GetMappings())
				{
					if (Mapping.Action != Action || !Mapping.Key.IsValid() || SeenKeys.Contains(Mapping.Key)) { continue; }
					SeenKeys.Add(Mapping.Key);
					FRPGInputOptionBinding& Row = OutRows.AddDefaulted_GetRef();
					Row.InputTag = Entry.InputTag;
					Row.DefaultKey = Mapping.Key;
					Row.CurrentKey = Controller
						? Controller->GetCurrentKeyForBinding(Entry.InputTag, Mapping.Key) : Mapping.Key;
					const FText ActionName = Entry.InputActionName.IsEmpty()
						? FText::FromName(Action->GetFName()) : Entry.InputActionName;
					Row.DisplayName = FText::Format(NSLOCTEXT("RPGInputOptions", "BindingName", "{0} [{1}]"),
						ActionName, Mapping.Key.GetDisplayName());
					Row.bCanRemap = !Mapping.Key.IsAnalog() && Row.CurrentKey.IsValid()
						&& (!Controller || (Controller->GetInputConfig() && Controller->GetInputConfig() != Config
							&& Controller->GetInputConfig()->GetOuter() == Controller));
					Row.SourceConfig = Config;
					Row.Action = Action;
				}
			}
		}
	}
	AppendAdditionalRows(Controller, OutRows);
}

FKey FRPGInputOptionsRuntime::GetAdditionalBindingKey(const ARPGPlayerController* Controller,
	const FRPGInputOptionBinding& Binding) const
{
	if (!Binding.bIsAdditionalMapping || !Binding.AdditionalInputProvider.IsValid()
		|| !Binding.SourceMappingContext.IsValid() || !Binding.AppliedMappingContext.IsValid() || !Binding.Action.IsValid()) { return FKey(); }
	TArray<FAdditionalContextView> Contexts;
	GatherAdditionalContexts(Controller, Contexts);
	for (const FAdditionalContextView& Context : Contexts)
	{
		if (Context.Provider == Binding.AdditionalInputProvider && Context.Mapping.SourceContext == Binding.SourceMappingContext
			&& Context.Mapping.AppliedContext == Binding.AppliedMappingContext)
		{
			return ReadAdditionalBindingKey(Context.Mapping, Binding.Action.Get(), Binding.DefaultKey);
		}
	}
	return FKey();
}

bool FRPGInputOptionsRuntime::MatchesFilter(const FRPGInputOptionBinding& Binding,
	const FText& SearchText, ERPGInputOptionDeviceFilter DeviceFilter, bool bChangedOnly) const
{
	// Classify by the authored device so a binding stays in the same device group.
	const FKey DeviceKey = Binding.DefaultKey.IsValid() ? Binding.DefaultKey : Binding.CurrentKey;
	if (DeviceFilter == ERPGInputOptionDeviceFilter::KeyboardMouse && DeviceKey.IsGamepadKey()) { return false; }
	if (DeviceFilter == ERPGInputOptionDeviceFilter::Gamepad && !DeviceKey.IsGamepadKey()) { return false; }
	if (bChangedOnly && (!Binding.DefaultKey.IsValid() || !Binding.CurrentKey.IsValid()
		|| Binding.DefaultKey == Binding.CurrentKey)) { return false; }

	TArray<FString> Terms;
	SearchText.ToString().ParseIntoArrayWS(Terms);
	if (Terms.IsEmpty()) { return true; }
	const FString Searchable = Binding.DisplayName.ToString() + TEXT(" ") + Binding.InputTag.ToString()
		+ TEXT(" ") + Binding.CurrentKey.GetDisplayName().ToString() + TEXT(" ") + Binding.CurrentKey.ToString()
		+ TEXT(" ") + Binding.DefaultKey.GetDisplayName().ToString() + TEXT(" ") + Binding.DefaultKey.ToString();
	for (const FString& Term : Terms)
	{
		if (!Searchable.Contains(Term, ESearchCase::IgnoreCase)) { return false; }
	}
	return true;
}

void FRPGInputOptionsRuntime::BuildDefaultRestoration(const ARPGPlayerController* Controller,
	TArray<FRPGInputOptionBinding>& OutBindings) const
{
	OutBindings.Reset();
	if (!IsValid(Controller) || !Controller->IsLocalController() || Controller->IsActorBeingDestroyed()
		|| !Controller->GetLocalPlayer() || !Controller->GetWorld() || Controller->GetWorld()->bIsTearingDown) { return; }
	BuildRows(Controller, nullptr, OutBindings);
	OutBindings.RemoveAll([](const FRPGInputOptionBinding& Binding)
	{
		return !Binding.bCanRemap || !Binding.DefaultKey.IsValid() || !Binding.CurrentKey.IsValid()
			|| Binding.CurrentKey == Binding.DefaultKey;
	});
}

bool FRPGInputOptionsRuntime::TryRestoreDefaults(ARPGPlayerController* Controller,
	const TArray<FRPGInputOptionBinding>& ExpectedBindings, FText& OutError) const
{
	OutError = FText::GetEmpty();
	const TArray<FRPGInputOptionBinding> Requested(ExpectedBindings);
	const auto Changed = [&](FText& Error)
	{
		Error = NSLOCTEXT("RPGInputOptions", "DefaultsChanged", "키 설정이 변경되었습니다. 복원을 다시 요청해 주세요.");
		return false;
	};
	if (!IsInGameThread() || !IsLiveInputOptionsObject(Controller)) { return Changed(OutError); }
	TStrongObjectPtr<ARPGPlayerController> KeepController(Controller);
	const auto MatchesRequested = [&]()
	{
		TArray<FRPGInputOptionBinding> Current;
		BuildDefaultRestoration(Controller, Current);
		if (Requested.IsEmpty() || Current.Num() != Requested.Num()) { return false; }
		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			const FRPGInputOptionBinding& A = Current[Index];
			const FRPGInputOptionBinding& B = Requested[Index];
			if (A.bCanRemap != B.bCanRemap || A.SourceConfig != B.SourceConfig || A.Action != B.Action
				|| A.InputTag != B.InputTag || A.DefaultKey != B.DefaultKey || A.CurrentKey != B.CurrentKey
				|| A.bIsAdditionalMapping != B.bIsAdditionalMapping || A.AdditionalInputProvider != B.AdditionalInputProvider
				|| A.SourceMappingContext != B.SourceMappingContext || A.AppliedMappingContext != B.AppliedMappingContext) { return false; }
		}
		return true;
	};
	if (!MatchesRequested()) { return Changed(OutError); }

	struct FProviderRequests
	{
		TWeakObjectPtr<const UObject> Owner;
		TArray<FRPGAdditionalInputBinding> Bindings;
	};
	TArray<FRPGKeyMapping> PrimaryBindings;
	TArray<FProviderRequests> Groups;
	for (const FRPGInputOptionBinding& Binding : Requested)
	{
		if (!Binding.bIsAdditionalMapping)
		{
			FRPGKeyMapping& Request = PrimaryBindings.AddDefaulted_GetRef();
			Request.InputTag = Binding.InputTag;
			Request.DefaultKey = Binding.DefaultKey;
			Request.Key = Binding.CurrentKey;
			continue;
		}
		FProviderRequests* Group = Groups.FindByPredicate([&](const FProviderRequests& Existing)
			{ return Existing.Owner == Binding.AdditionalInputProvider; });
		if (!Group)
		{
			Group = &Groups.AddDefaulted_GetRef();
			Group->Owner = Binding.AdditionalInputProvider;
		}
		FRPGAdditionalInputBinding& Request = Group->Bindings.AddDefaulted_GetRef();
		Request.SourceContext = Binding.SourceMappingContext;
		Request.AppliedContext = Binding.AppliedMappingContext;
		Request.Action = Binding.Action;
		Request.DefaultKey = Binding.DefaultKey;
		Request.CurrentKey = Binding.CurrentKey;
	}
	TArray<TUniquePtr<IRPGAdditionalInputRestorePlan>> Plans;
	TArray<FRPGInputBindingRestoreContextView> AdditionalViews;
	for (const FProviderRequests& Group : Groups)
	{
		TStrongObjectPtr<const UObject> Owner(Group.Owner.Get());
		if (!IsLiveInputOptionsObject(Owner.Get())) { return Changed(OutError); }
		bool bPrepared = false;
		const TArray<IRPGAdditionalInputProvider*> Providers = IModularFeatures::Get()
			.GetModularFeatureImplementations<IRPGAdditionalInputProvider>(IRPGAdditionalInputProvider::GetFeatureName());
		for (IRPGAdditionalInputProvider* Provider : Providers)
		{
			if (Provider->GetInputOptionsOwner() != Owner.Get()) { continue; }
			TUniquePtr<IRPGAdditionalInputRestorePlan> Plan = Provider->PrepareInputOptionRestoration(
				Controller->GetLocalPlayer(), MakeArrayView(Group.Bindings), OutError);
			if (!Plan) { return false; }
			const int32 OldCount = AdditionalViews.Num();
			Plan->AppendViews(AdditionalViews);
			if (AdditionalViews.Num() == OldCount) { return Changed(OutError); }
			Plans.Add(MoveTemp(Plan));
			bPrepared = true;
			break;
		}
		if (!bPrepared) { return Changed(OutError); }
	}
	bool bCommitted = false;
	const auto ValidateAdditional = [&](FText& Error)
	{
		for (const TUniquePtr<IRPGAdditionalInputRestorePlan>& Plan : Plans)
		{
			if (!Plan->Validate(Error)) { return false; }
		}
		if (!bCommitted) { return MatchesRequested() || Changed(Error); }
		TArray<FRPGInputOptionBinding> Remaining;
		BuildDefaultRestoration(Controller, Remaining);
		return Remaining.IsEmpty() || Changed(Error);
	};
	const auto CommitAdditional = [&](FText& Error)
	{
		for (const TUniquePtr<IRPGAdditionalInputRestorePlan>& Plan : Plans) { if (!Plan->Validate(Error)) { return false; } }
		for (const TUniquePtr<IRPGAdditionalInputRestorePlan>& Plan : Plans) { if (!Plan->Commit(Error)) { return false; } }
		bCommitted = true;
		return true;
	};
	const FRPGInputBindingRestoreExtension Additional { MakeArrayView(AdditionalViews), ValidateAdditional, CommitAdditional };
	return Controller->TryRestoreDefaultKeyBindingsWithAdditional(PrimaryBindings, ValidateDefaultRestoration, Additional, OutError);
}

bool FRPGInputOptionsRuntime::TryApply(ARPGPlayerController* Controller, FGameplayTag InputTag,
	FKey DefaultKey, FKey NewKey, FText& OutError) const
{
	if (!ValidateKeyChange(Controller, InputTag, DefaultKey, NewKey, OutError)) { return false; }
	if (Controller->GetCurrentKeyForBinding(InputTag, DefaultKey) == NewKey) { return true; }
	return Controller->TryApplyKeyBindingWithPolicy(InputTag, DefaultKey, NewKey,
		[Controller, InputTag, DefaultKey, NewKey](FText& PolicyError)
		{
			return ValidateKeyChange(Controller, InputTag, DefaultKey, NewKey, PolicyError);
		}, OutError);
}

bool FRPGInputOptionsRuntime::TryApplyBinding(ARPGPlayerController* Controller,
	const FRPGInputOptionBinding& ExpectedBinding, FKey NewKey, FText& OutError) const
{
	OutError = FText::GetEmpty();
	const FRPGInputOptionBinding Expected(ExpectedBinding);
	if (Expected.bIsAdditionalMapping)
	{
		return ApplyAdditionalBinding(Controller, Expected, NewKey, OutError);
	}
	// Only explicitly empty identities use the compatibility path; expired identities must fail.
	if (Expected.SourceConfig.IsExplicitlyNull() && Expected.Action.IsExplicitlyNull())
	{
		return TryApply(Controller, Expected.InputTag, Expected.DefaultKey, NewKey, OutError);
	}
	if (!ValidateDisplayedBinding(Controller, Expected, OutError)
		|| !ValidateKeyChange(Controller, Expected.InputTag, Expected.DefaultKey, NewKey, OutError)) { return false; }
	if (Expected.CurrentKey == NewKey) { return true; }
	return Controller->TryApplyKeyBindingWithPolicy(Expected.InputTag, Expected.DefaultKey, NewKey,
		[Controller, Expected, NewKey](FText& PolicyError)
		{
			return ValidateDisplayedBinding(Controller, Expected, PolicyError)
				&& ValidateKeyChange(Controller, Expected.InputTag, Expected.DefaultKey, NewKey, PolicyError);
		}, OutError);
}
