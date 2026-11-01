#include "GameFeatureAction_RPGInputMapping.h"

#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Settings/RPGGameUserSettings.h"
#include "Input/RPGInputBindingRestoreView.h"
#include "Features/IModularFeatures.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#if WITH_EDITORONLY_DATA
#include "AssetRegistry/AssetBundleData.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogRPGFeatureInput, Log, All);

namespace
{
	bool IsLiveFeatureInputObject(const UObject* Object)
	{
		return IsValid(Object) && !Object->IsUnreachable()
			&& !Object->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
	}

	FName PlayerMappingBaseName(const UWorld* World, const UEnhancedPlayerInput* PlayerInput,
		const UInputMappingContext* Source)
	{
		// A non-numeric suffix keeps MakeUniqueObjectName's number separate from the source identity.
		return FName(*FString::Printf(TEXT("RPGFeatureMapping_%u_%u_%u_Context"),
			World->GetUniqueID(), PlayerInput->GetUniqueID(), Source->GetUniqueID()));
	}

	UInputMappingContext* FindActivePlayerMappingCopy(ULocalPlayer* Player, UWorld* World,
		UEnhancedPlayerInput* PlayerInput, UInputMappingContext* Source,
		UEnhancedInputLocalPlayerSubsystem* Subsystem)
	{
		const FString BaseName = PlayerMappingBaseName(World, PlayerInput, Source).ToString();
		TArray<TWeakObjectPtr<UInputMappingContext>> Candidates;
		// Only collect direct children under this player while the UObject hash is locked.
		ForEachObjectWithOuter(Player, [&](UObject* Object)
		{
			if (!IsLiveFeatureInputObject(Object) || !Object->HasAnyFlags(RF_Transient)
				|| Object->GetClass() != Source->GetClass() || Object->GetFName().GetPlainNameString() != BaseName) { return; }
			if (UInputMappingContext* Candidate = Cast<UInputMappingContext>(Object)) { Candidates.Add(Candidate); }
		}, EGetObjectsFlags::None);
		// Subsystem queries and object creation stay outside the hash enumeration.
		for (const TWeakObjectPtr<UInputMappingContext>& WeakCandidate : Candidates)
		{
			TStrongObjectPtr<UInputMappingContext> Candidate(WeakCandidate.Get());
			if (IsLiveFeatureInputObject(Candidate.Get()) && Subsystem->HasMappingContext(Candidate.Get())) { return Candidate.Get(); }
		}
		return nullptr;
	}

	UInputMappingContext* GetOrCreatePlayerMappingCopy(ULocalPlayer* Player, UWorld* World,
		UEnhancedPlayerInput* PlayerInput, UInputMappingContext* Source,
		UEnhancedInputLocalPlayerSubsystem* Subsystem)
	{
		if (UInputMappingContext* Existing = FindActivePlayerMappingCopy(Player, World, PlayerInput, Source, Subsystem))
		{
			return Existing;
		}
		FObjectDuplicationParameters Parameters(Source, Player);
		Parameters.DestName = MakeUniqueObjectName(Player, Source->GetClass(), PlayerMappingBaseName(World, PlayerInput, Source));
		Parameters.FlagMask &= ~(RF_Public | RF_Standalone);
		Parameters.ApplyFlags |= RF_Transient;
		Parameters.bAssignExternalPackages = false;
		TStrongObjectPtr<UInputMappingContext> Copy(Cast<UInputMappingContext>(StaticDuplicateObjectEx(Parameters)));
		if (!IsLiveFeatureInputObject(Copy.Get())) { return nullptr; }
		// Duplication callbacks may have let another action acquire the first active copy.
		if (UInputMappingContext* Existing = FindActivePlayerMappingCopy(Player, World, PlayerInput, Source, Subsystem))
		{
			return Existing;
		}
		return Copy.Get();
	}

	bool RestorePlayerMappingPreferences(ULocalPlayer* Player, UWorld* World, UEnhancedPlayerInput* PlayerInput,
		const UInputMappingContext* Source, UInputMappingContext* Copy, UEnhancedInputLocalPlayerSubsystem* Subsystem)
	{
		// Only a fresh, unregistered copy is restored. An active shared copy keeps its current live keys.
		if (!IsLiveFeatureInputObject(Source) || !IsLiveFeatureInputObject(Copy)) { return false; }
		if (!Source->IsAsset()) { return true; }
		TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
		const APlayerController* Controller = IsLiveFeatureInputObject(Player) ? Player->GetPlayerController(nullptr) : nullptr;
		if (!IsLiveFeatureInputObject(Settings.Get()) || Settings->bApplyingAdditionalInputBinding
			|| !Settings->CanSaveAdditionalInputBindings() || !IsLiveFeatureInputObject(World) || World->bIsTearingDown
			|| !IsLiveFeatureInputObject(Player) || Player->GetWorld() != World
			|| !IsLiveFeatureInputObject(Controller) || Controller->IsActorBeingDestroyed()
			|| Controller->GetWorld() != World || !IsLiveFeatureInputObject(Subsystem) || !IsLiveFeatureInputObject(PlayerInput)
			|| Subsystem->GetPlayerInput() != PlayerInput || Controller->PlayerInput.Get() != PlayerInput
			|| !IsLiveFeatureInputObject(Source) || !IsLiveFeatureInputObject(Copy) || Copy == Source
			|| Copy->GetOuter() != Player || !Copy->HasAnyFlags(RF_Transient)) { return false; }
		if (UInputMappingContext* Active = FindActivePlayerMappingCopy(
			Player, World, PlayerInput, const_cast<UInputMappingContext*>(Source), Subsystem))
		{
			return Active == Copy;
		}
		const int32 PlayerIndex = Player->GetLocalPlayerIndex();
		if (PlayerIndex < 0 || Source->GetMappings().Num() != Copy->GetMappings().Num()) { return false; }
		const FSoftObjectPath ContextId(Source);
		TArray<FKey> RestoredKeys;
		for (int32 Index = 0; Index < Source->GetMappings().Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = Source->GetMappings()[Index];
			if (Authored.Action != Copy->GetMappings()[Index].Action) { return false; }
			RestoredKeys.Add(IsLiveFeatureInputObject(Authored.Action.Get()) && Authored.Action->IsAsset()
				? Settings->GetFeatureKeyBinding(PlayerIndex, ContextId, FSoftObjectPath(Authored.Action.Get()), Authored.Key)
				: Authored.Key);
		}
		for (int32 Index = 0; Index < RestoredKeys.Num(); ++Index)
		{
			for (int32 Other = Index + 1; Other < RestoredKeys.Num(); ++Other)
			{
				if (RestoredKeys[Index].IsValid() && RestoredKeys[Index] == RestoredKeys[Other]
					&& Source->GetMappings()[Index].Key != Source->GetMappings()[Other].Key)
				{
					// Keep the authored context usable; preserve the saved records for a later explicit change.
					UE_LOG(LogRPGFeatureInput, Warning, TEXT("RPG_INPUT RESTORE Context=%s Reason=SavedBindingCollision"),
						*Source->GetPathName());
					return true;
				}
			}
		}
		for (int32 Index = 0; Index < RestoredKeys.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = Source->GetMappings()[Index];
			if (RestoredKeys[Index] == Authored.Key) { continue; }
			for (const FEnhancedActionKeyMapping& Live : PlayerInput->GetEnhancedActionMappingsView())
			{
				if (Live.Key == RestoredKeys[Index] && Live.Action != Authored.Action)
				{
					UE_LOG(LogRPGFeatureInput, Warning, TEXT("RPG_INPUT RESTORE Context=%s Reason=ActiveBindingCollision"),
						*Source->GetPathName());
					return true;
				}
			}
		}
		for (int32 Index = 0; Index < RestoredKeys.Num(); ++Index) { Copy->GetMapping(Index).Key = RestoredKeys[Index]; }
		return true;
	}
}

const UInputMappingContext* UGameFeatureAction_RPGInputMapping::FindAppliedMappingContext(
	const ULocalPlayer* Player, const UInputMappingContext* SourceMapping) const
{
	if (!IsInGameThread() || !IsLiveFeatureInputObject(Player) || !IsLiveFeatureInputObject(SourceMapping)) { return nullptr; }
	UWorld* World = Player->GetWorld();
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const APlayerController* Controller = Player->GetPlayerController(nullptr);
	if (!IsLiveFeatureInputObject(World) || World->bIsTearingDown || !IsLiveFeatureInputObject(Instance)
		|| !Instance->GetLocalPlayers().Contains(Player) || !IsLiveFeatureInputObject(Controller)
		|| Controller->IsActorBeingDestroyed() || Controller->GetWorld() != World) { return nullptr; }
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	const UEnhancedPlayerInput* PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
	if (!IsLiveFeatureInputObject(Subsystem) || !IsLiveFeatureInputObject(PlayerInput)
		|| Controller->PlayerInput.Get() != PlayerInput) { return nullptr; }
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const TWeakObjectPtr<ULocalPlayer> WeakPlayer(const_cast<ULocalPlayer*>(Player));
	for (const auto& Context : ContextData)
	{
		const FWorldData* WorldData = Context.Value.Worlds.Find(WeakWorld);
		const FPlayerData* Data = WorldData ? WorldData->Players.Find(WeakPlayer) : nullptr;
		if (!Data || Data->Subsystem.Get() != Subsystem || Data->PlayerInput.Get() != PlayerInput) { continue; }
		for (const FMappingRegistration& Registration : Data->RegisteredMappings)
		{
			UInputMappingContext* Mapping = Registration.AppliedMapping.Get();
			int32 AppliedPriority = 0;
			if (Registration.SourceMapping.Get() == SourceMapping && IsLiveFeatureInputObject(Mapping)
				&& Subsystem->HasMappingContext(Mapping, AppliedPriority) && AppliedPriority == Registration.Priority)
			{
				return Mapping;
			}
		}
	}
	return nullptr;
}

void UGameFeatureAction_RPGInputMapping::GetInputOptionContexts(const ULocalPlayer* Player,
	TArray<FRPGAdditionalInputContext>& OutContexts) const
{
	OutContexts.Reset();
	if (!IsInGameThread() || !IsLiveFeatureInputObject(this) || !IsLiveFeatureInputObject(Player)) { return; }
	for (const auto& Context : ContextData)
	{
		const FWorldData* Data = Context.Value.Worlds.Find(TWeakObjectPtr<UWorld>(Player->GetWorld()));
		const FPlayerData* PlayerData = Data ? Data->Players.Find(
			TWeakObjectPtr<ULocalPlayer>(const_cast<ULocalPlayer*>(Player))) : nullptr;
		if (!PlayerData) { continue; }
		for (const FMappingRegistration& Registration : PlayerData->RegisteredMappings)
		{
			const UInputMappingContext* Source = Registration.SourceMapping.Get();
			const UInputMappingContext* Applied = Registration.AppliedMapping.Get();
			if (!Registration.bShowInInputOptions || !IsLiveFeatureInputObject(Applied)
				|| FindAppliedMappingContext(Player, Source) != Applied) { continue; }
			if (OutContexts.ContainsByPredicate([&](const FRPGAdditionalInputContext& Existing)
				{ return Existing.SourceContext.Get() == Source && Existing.AppliedContext.Get() == Applied; })) { continue; }
			FRPGAdditionalInputContext& View = OutContexts.AddDefaulted_GetRef();
			View.SourceContext = Source;
			View.AppliedContext = Applied;
			View.bCanRemap = FindEditableInputMapping(Player, Source, Applied) == Applied;
		}
	}
}

UInputMappingContext* UGameFeatureAction_RPGInputMapping::FindEditableInputMapping(
	const ULocalPlayer* Player, const UInputMappingContext* Source, const UInputMappingContext* Applied) const
{
	if (!IsInGameThread() || !IsLiveFeatureInputObject(this) || !IsLiveFeatureInputObject(Player)
		|| !IsLiveFeatureInputObject(Source) || !Source->IsAsset() || !IsLiveFeatureInputObject(Applied)
		|| Applied == Source || Applied->GetOuter() != Player || !Applied->HasAnyFlags(RF_Transient)
		|| FindAppliedMappingContext(Player, Source) != Applied) { return nullptr; }
	for (const auto& Context : ContextData)
	{
		const FWorldData* WorldData = Context.Value.Worlds.Find(TWeakObjectPtr<UWorld>(Player->GetWorld()));
		const FPlayerData* Data = WorldData ? WorldData->Players.Find(
			TWeakObjectPtr<ULocalPlayer>(const_cast<ULocalPlayer*>(Player))) : nullptr;
		if (!Data) { continue; }
		for (const FMappingRegistration& Registration : Data->RegisteredMappings)
		{
			if (Registration.bUsePlayerOwnedCopy && Registration.bShowInInputOptions
				&& Registration.SourceMapping.Get() == Source && Registration.AppliedMapping.Get() == Applied)
			{
				return Registration.AppliedMapping.Get();
			}
		}
	}
	return nullptr;
}

bool UGameFeatureAction_RPGInputMapping::TryApplyInputOptionBinding(ULocalPlayer* Player,
	const FRPGAdditionalInputBinding& ExpectedBinding, FKey NewKey,
	TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError)
{
	OutError = FText::GetEmpty();
	const FRPGAdditionalInputBinding Expected(ExpectedBinding);
	const auto Changed = [&]()
	{
		OutError = NSLOCTEXT("RPGInputOptions", "AdditionalBindingChanged",
			"입력 설정이 변경되었습니다. 목록을 새로 열고 다시 시도해 주세요.");
		return false;
	};
	if (!IsInGameThread() || !IsLiveFeatureInputObject(this) || !IsLiveFeatureInputObject(Player)
		|| !Expected.SourceContext.IsValid() || !Expected.AppliedContext.IsValid() || !Expected.Action.IsValid()
		|| !Expected.DefaultKey.IsValid() || Expected.DefaultKey.IsAnalog() || !Expected.CurrentKey.IsValid()
		|| !NewKey.IsValid() || NewKey.IsAnalog() || NewKey.IsGamepadKey() != Expected.DefaultKey.IsGamepadKey()
		|| (NewKey == EKeys::Escape && Expected.DefaultKey != EKeys::Escape)) { return Changed(); }

	TStrongObjectPtr<UGameFeatureAction_RPGInputMapping> KeepAlive(this);
	TStrongObjectPtr<ULocalPlayer> KeepPlayer(Player);
	TStrongObjectPtr<UWorld> World(Player->GetWorld());
	TStrongObjectPtr<APlayerController> Controller(Player->GetPlayerController(nullptr));
	TStrongObjectPtr<const UInputMappingContext> Source(Expected.SourceContext.Get());
	TStrongObjectPtr<const UInputAction> Action(Expected.Action.Get());
	TStrongObjectPtr<UInputMappingContext> Mapping(FindEditableInputMapping(Player, Source.Get(), Expected.AppliedContext.Get()));
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem(Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>());
	TStrongObjectPtr<UEnhancedPlayerInput> PlayerInput(Subsystem.IsValid() ? Subsystem->GetPlayerInput() : nullptr);
	const int32 PlayerIndex = Player->GetLocalPlayerIndex();
	const FSoftObjectPath ContextId(Source.Get());
	const FSoftObjectPath ActionId(Action.Get());
	TStrongObjectPtr<URPGGameUserSettings> Settings(URPGGameUserSettings::GetRPGGameUserSettings());
	if (!IsLiveFeatureInputObject(Settings.Get()) || Settings->bApplyingAdditionalInputBinding
		|| !Settings->CanSaveAdditionalInputBindings())
	{
		OutError = NSLOCTEXT("RPGInputOptions", "AdditionalBindingBusy", "키 설정을 변경할 수 없습니다. 잠시 후 다시 시도해 주세요.");
		return false;
	}
	TGuardValue<bool> ApplyingGuard(Settings->bApplyingAdditionalInputBinding, true);
	bool bBindingsChanged = false;
	ON_SCOPE_EXIT
	{
		if (bBindingsChanged) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(KeepPlayer.Get(), World.Get()); }
	};
	const auto HasSameOwner = [&]()
	{
		return IsLiveFeatureInputObject(this) && IsLiveFeatureInputObject(Player)
			&& IsLiveFeatureInputObject(World.Get()) && !World->bIsTearingDown && Player->GetWorld() == World.Get()
			&& IsLiveFeatureInputObject(Controller.Get()) && !Controller->IsActorBeingDestroyed()
			&& Controller->IsLocalController() && Player->GetPlayerController(nullptr) == Controller.Get()
			&& Controller->GetWorld() == World.Get() && Player->GetLocalPlayerIndex() == PlayerIndex && PlayerIndex >= 0
			&& IsLiveFeatureInputObject(Subsystem.Get()) && IsLiveFeatureInputObject(PlayerInput.Get())
			&& Subsystem->GetPlayerInput() == PlayerInput.Get() && Controller->PlayerInput.Get() == PlayerInput.Get()
			&& IsLiveFeatureInputObject(Source.Get()) && IsLiveFeatureInputObject(Action.Get()) && Action->IsAsset()
			&& ContextId.IsValid() && ActionId.IsValid()
			&& FSoftObjectPath(Source.Get()) == ContextId && FSoftObjectPath(Action.Get()) == ActionId
			&& IsLiveFeatureInputObject(Mapping.Get()) && FindEditableInputMapping(Player, Source.Get(), Mapping.Get()) == Mapping.Get()
			&& IsLiveFeatureInputObject(Settings.Get()) && URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get()
			&& Settings->CanSaveAdditionalInputBindings();
	};
	if (!HasSameOwner()) { return Changed(); }
	const TArray<FEnhancedActionKeyMapping> Defaults(Source->GetMappings());
	const TArray<FEnhancedActionKeyMapping> Previous(Mapping->GetMappings());
	if (Defaults.Num() != Previous.Num()) { return Changed(); }
	TArray<int32> TargetIndices;
	TArray<FKey> ExpectedKeys;
	for (int32 Index = 0; Index < Defaults.Num(); ++Index)
	{
		if (Defaults[Index].Action != Previous[Index].Action) { return Changed(); }
		ExpectedKeys.Add(Previous[Index].Key);
		if (Defaults[Index].Action == Action.Get() && Defaults[Index].Key == Expected.DefaultKey)
		{
			if (Previous[Index].Key != Expected.CurrentKey) { return Changed(); }
			TargetIndices.Add(Index);
		}
	}
	if (TargetIndices.IsEmpty()) { return Changed(); }
	const auto ValidateState = [&]()
	{
		if (!HasSameOwner() || Source->GetMappings().Num() != Defaults.Num() || Mapping->GetMappings().Num() != Previous.Num()) { return Changed(); }
		for (int32 Index = 0; Index < Defaults.Num(); ++Index)
		{
			const FEnhancedActionKeyMapping& Authored = Source->GetMappings()[Index];
			const FEnhancedActionKeyMapping& Live = Mapping->GetMappings()[Index];
			if (Authored.Action != Defaults[Index].Action || Authored.Key != Defaults[Index].Key
				|| Live.Action != Previous[Index].Action || Live.Key != ExpectedKeys[Index]) { return Changed(); }
		}
		return true;
	};
	if (!ValidateState() || !ValidatePolicy(OutError) || !ValidateState()) { return false; }
	if (Expected.CurrentKey == NewKey) { return true; }

	bBindingsChanged = true;
	for (int32 Index : TargetIndices)
	{
		Mapping->GetMapping(Index).Key = NewKey;
		ExpectedKeys[Index] = NewKey;
	}
	if (!Settings->SetFeatureKeyBinding(PlayerIndex, ContextId, ActionId, Expected.DefaultKey, NewKey)) { return Changed(); }
	const auto ValidateCommitted = [&]()
	{
		return ValidateState() && (Settings->MatchesFeatureKeyBinding(PlayerIndex, ContextId, ActionId, Expected.DefaultKey, NewKey)
			|| Changed());
	};
	if (!ValidateCommitted()) { return false; }
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Options.bIgnoreAllPressedKeysUntilRelease = true;
	Subsystem->RequestRebuildControlMappings(Options);
	if (!ValidateCommitted()) { return false; }
	Settings->SaveSettings();
	return ValidateCommitted();
}


struct UGameFeatureAction_RPGInputMapping::FDefaultRestorationPlan final : IRPGAdditionalInputRestorePlan
{
	struct FBindingState
	{
		TStrongObjectPtr<const UInputAction> Action;
		FSoftObjectPath ActionId;
		FKey DefaultKey;
	};
	struct FContextState
	{
		TStrongObjectPtr<const UInputMappingContext> Source;
		TStrongObjectPtr<UInputMappingContext> Applied;
		FSoftObjectPath ContextId;
		TArray<FEnhancedActionKeyMapping> Defaults;
		TArray<FEnhancedActionKeyMapping> Previous;
		TArray<FKey> FinalKeys;
		TArray<int32> TargetIndices;
		TArray<FBindingState> Bindings;
	};
	TStrongObjectPtr<UGameFeatureAction_RPGInputMapping> Owner;
	TStrongObjectPtr<ULocalPlayer> Player;
	TStrongObjectPtr<UWorld> World;
	TStrongObjectPtr<APlayerController> Controller;
	TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem;
	TStrongObjectPtr<UEnhancedPlayerInput> PlayerInput;
	TStrongObjectPtr<URPGGameUserSettings> Settings;
	int32 PlayerIndex = INDEX_NONE;
	TArray<FContextState> Contexts;
	bool bCommitted = false;
	bool bNotifyOnDestruction = false;

	virtual ~FDefaultRestorationPlan() override
	{
		// The coordinator has returned; Commit itself never broadcasts into partially staged state.
		if (bNotifyOnDestruction) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(Player.Get(), World.Get()); }
	}

	FDefaultRestorationPlan(UGameFeatureAction_RPGInputMapping* InOwner, ULocalPlayer* InPlayer)
		: Owner(InOwner), Player(InPlayer), World(InPlayer->GetWorld()), Controller(InPlayer->GetPlayerController(nullptr))
		, Subsystem(InPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		, PlayerInput(Subsystem.IsValid() ? Subsystem->GetPlayerInput() : nullptr)
		, Settings(URPGGameUserSettings::GetRPGGameUserSettings()), PlayerIndex(InPlayer->GetLocalPlayerIndex())
	{
	}

	static bool Changed(FText& OutError)
	{
		OutError = NSLOCTEXT("RPGInputOptions", "DefaultsChanged", "키 설정이 변경되었습니다. 복원을 다시 요청해 주세요.");
		return false;
	}

	bool HasSameOwner() const
	{
		return IsLiveFeatureInputObject(Owner.Get()) && IsLiveFeatureInputObject(Player.Get())
			&& IsLiveFeatureInputObject(World.Get()) && !World->bIsTearingDown && Player->GetWorld() == World.Get()
			&& IsLiveFeatureInputObject(Controller.Get()) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalController()
			&& Player->GetPlayerController(nullptr) == Controller.Get() && Controller->GetLocalPlayer() == Player.Get()
			&& Controller->GetWorld() == World.Get() && PlayerIndex >= 0 && Player->GetLocalPlayerIndex() == PlayerIndex
			&& IsLiveFeatureInputObject(Subsystem.Get()) && Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() == Subsystem.Get()
			&& IsLiveFeatureInputObject(PlayerInput.Get()) && Subsystem->GetPlayerInput() == PlayerInput.Get()
			&& Controller->PlayerInput.Get() == PlayerInput.Get() && IsLiveFeatureInputObject(Settings.Get())
			&& URPGGameUserSettings::GetRPGGameUserSettings() == Settings.Get() && Settings->CanSaveAdditionalInputBindings();
	}

	bool Initialize(const TArray<FRPGAdditionalInputBinding>& Requests, FText& OutError)
	{
		if (!HasSameOwner() || Settings->bApplyingAdditionalInputBinding || Requests.IsEmpty()) { return Changed(OutError); }
		for (const FRPGAdditionalInputBinding& Request : Requests)
		{
			const UInputMappingContext* Source = Request.SourceContext.Get();
			UInputMappingContext* Applied = Owner->FindEditableInputMapping(Player.Get(), Source, Request.AppliedContext.Get());
			const UInputAction* Action = Request.Action.Get();
			if (!Applied || !IsLiveFeatureInputObject(Action) || !Action->IsAsset() || !Request.DefaultKey.IsValid()
				|| Request.DefaultKey.IsAnalog() || !Request.CurrentKey.IsValid() || Request.CurrentKey == Request.DefaultKey
				|| Request.CurrentKey.IsGamepadKey() != Request.DefaultKey.IsGamepadKey()) { return Changed(OutError); }
			FContextState* Data = Contexts.FindByPredicate([Applied](const FContextState& State) { return State.Applied.Get() == Applied; });
			if (!Data)
			{
				Data = &Contexts.AddDefaulted_GetRef();
				Data->Source.Reset(Source);
				Data->Applied.Reset(Applied);
				Data->ContextId = FSoftObjectPath(Source);
				Data->Defaults = Source->GetMappings();
				Data->Previous = Applied->GetMappings();
				if (!Data->ContextId.IsValid() || Data->Defaults.Num() != Data->Previous.Num()) { return Changed(OutError); }
				for (int32 Index = 0; Index < Data->Defaults.Num(); ++Index)
				{
					if (Data->Defaults[Index].Action != Data->Previous[Index].Action) { return Changed(OutError); }
					Data->FinalKeys.Add(Data->Previous[Index].Key);
				}
			}
			if (Data->Source.Get() != Source || Data->Bindings.ContainsByPredicate([&](const FBindingState& Binding)
				{ return Binding.Action.Get() == Action && Binding.DefaultKey == Request.DefaultKey; })) { return Changed(OutError); }
			bool bFound = false;
			for (int32 Index = 0; Index < Data->Defaults.Num(); ++Index)
			{
				if (Data->Defaults[Index].Action != Action || Data->Defaults[Index].Key != Request.DefaultKey) { continue; }
				if (Data->Previous[Index].Key != Request.CurrentKey) { return Changed(OutError); }
				Data->FinalKeys[Index] = Request.DefaultKey;
				Data->TargetIndices.AddUnique(Index);
				bFound = true;
			}
			if (!bFound) { return Changed(OutError); }
			FBindingState& Binding = Data->Bindings.AddDefaulted_GetRef();
			Binding.Action.Reset(Action);
			Binding.ActionId = FSoftObjectPath(Action);
			Binding.DefaultKey = Request.DefaultKey;
			if (!Binding.ActionId.IsValid()) { return Changed(OutError); }
		}
		for (FContextState& Data : Contexts) { Data.TargetIndices.Sort(); }
		return Validate(OutError);
	}

	virtual void AppendViews(TArray<FRPGInputBindingRestoreContextView>& OutViews) const override
	{
		for (const FContextState& Data : Contexts)
		{
			FRPGInputBindingRestoreContextView& View = OutViews.AddDefaulted_GetRef();
			View.DefaultMappings = MakeArrayView(Data.Defaults);
			View.FinalKeys = MakeArrayView(Data.FinalKeys);
			View.RestoredMappingIndices = MakeArrayView(Data.TargetIndices);
			View.RuntimeContext = Data.Applied.Get();
			View.InputSubsystem = Subsystem.Get();
		}
	}

	virtual bool Validate(FText& OutError) const override
	{
		if (!IsInGameThread() || !HasSameOwner() || Contexts.IsEmpty()) { return Changed(OutError); }
		for (const FContextState& Data : Contexts)
		{
			if (!IsLiveFeatureInputObject(Data.Source.Get()) || !IsLiveFeatureInputObject(Data.Applied.Get())
				|| Owner->FindEditableInputMapping(Player.Get(), Data.Source.Get(), Data.Applied.Get()) != Data.Applied.Get()
				|| FSoftObjectPath(Data.Source.Get()) != Data.ContextId || Data.Source->GetMappings().Num() != Data.Defaults.Num()
				|| Data.Applied->GetMappings().Num() != Data.Previous.Num()) { return Changed(OutError); }
			for (int32 Index = 0; Index < Data.Defaults.Num(); ++Index)
			{
				const FEnhancedActionKeyMapping& Authored = Data.Source->GetMappings()[Index];
				const FEnhancedActionKeyMapping& Live = Data.Applied->GetMappings()[Index];
				const FKey ExpectedKey = bCommitted ? Data.FinalKeys[Index] : Data.Previous[Index].Key;
				if (Authored.Action != Data.Defaults[Index].Action || Authored.Key != Data.Defaults[Index].Key
					|| Live.Action != Data.Previous[Index].Action || Live.Key != ExpectedKey) { return Changed(OutError); }
			}
			for (const FBindingState& Binding : Data.Bindings)
			{
				if (!IsLiveFeatureInputObject(Binding.Action.Get()) || !Binding.Action->IsAsset()
					|| FSoftObjectPath(Binding.Action.Get()) != Binding.ActionId
					|| (bCommitted && !Settings->MatchesFeatureKeyBinding(PlayerIndex, Data.ContextId, Binding.ActionId,
						Binding.DefaultKey, Binding.DefaultKey))) { return Changed(OutError); }
			}
		}
		return true;
	}

	virtual bool Commit(FText& OutError) override
	{
		if (!Validate(OutError)) { return false; }
		if (bCommitted) { return true; }
		if (!Settings->bApplyingAdditionalInputBinding) { return Changed(OutError); }
		bNotifyOnDestruction = true;
		for (FContextState& Data : Contexts)
		{
			for (int32 Index : Data.TargetIndices) { Data.Applied->GetMapping(Index).Key = Data.FinalKeys[Index]; }
			for (const FBindingState& Binding : Data.Bindings)
			{
				if (!Settings->SetFeatureKeyBinding(PlayerIndex, Data.ContextId, Binding.ActionId,
					Binding.DefaultKey, Binding.DefaultKey)) { return Changed(OutError); }
			}
		}
		bCommitted = true;
		return Validate(OutError);
	}
};

TUniquePtr<IRPGAdditionalInputRestorePlan> UGameFeatureAction_RPGInputMapping::PrepareInputOptionRestoration(
	ULocalPlayer* Player, TConstArrayView<FRPGAdditionalInputBinding> ExpectedBindings, FText& OutError)
{
	OutError = FText::GetEmpty();
	if (!IsInGameThread() || !IsLiveFeatureInputObject(this) || !IsLiveFeatureInputObject(Player) || ExpectedBindings.IsEmpty())
	{
		FDefaultRestorationPlan::Changed(OutError);
		return nullptr;
	}
	TArray<FRPGAdditionalInputBinding> Requests;
	Requests.Append(ExpectedBindings.GetData(), ExpectedBindings.Num());
	TUniquePtr<FDefaultRestorationPlan> Plan = MakeUnique<FDefaultRestorationPlan>(this, Player);
	if (!Plan->Initialize(Requests, OutError)) { return nullptr; }
	return Plan;
}

void UGameFeatureAction_RPGInputMapping::NotifyRegisteredPlayersInputOptions() const
{
	TArray<TPair<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<UWorld>>> Players;
	for (const auto& Context : ContextData)
	{
		for (const auto& World : Context.Value.Worlds)
		{
			for (const auto& Player : World.Value.Players)
			{
				if (!Player.Value.RegisteredMappings.ContainsByPredicate(
					[](const FMappingRegistration& Mapping) { return Mapping.bShowInInputOptions; })) { continue; }
				const TPair<TWeakObjectPtr<ULocalPlayer>, TWeakObjectPtr<UWorld>> Entry(Player.Key, World.Key);
				Players.AddUnique(Entry);
			}
		}
	}
	// Event listeners may deactivate a feature. No native map entries are retained across broadcasts.
	for (const auto& Player : Players) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(Player.Key.Get(), Player.Value.Get()); }
}

void UGameFeatureAction_RPGInputMapping::BeginDestroy()
{
	if (bInputOptionsProviderRegistered)
	{
		bInputOptionsProviderRegistered = false;
		IModularFeatures::Get().UnregisterModularFeature(IRPGAdditionalInputProvider::GetFeatureName(), this);
	}
	NotifyRegisteredPlayersInputOptions();
	Super::BeginDestroy();
}

UGameFeatureAction_RPGInputMapping::FContextData* UGameFeatureAction_RPGInputMapping::FindActiveContext(
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Data = ContextData.Find(ChangeContext);
	return Data && Data->Generation == Generation ? Data : nullptr;
}

UGameFeatureAction_RPGInputMapping::FWorldData* UGameFeatureAction_RPGInputMapping::FindWorldData(
	TWeakObjectPtr<UWorld> World, const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	return Active ? Active->Worlds.Find(World) : nullptr;
}

void UGameFeatureAction_RPGInputMapping::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	TStrongObjectPtr<UGameFeatureAction_RPGInputMapping> KeepAlive(this);
	Super::OnGameFeatureActivating(Context);
	if (!GEngine || ContextData.Contains(Context)) { return; }
	const FGameFeatureStateChangeContext ChangeContext(Context);
	const uint64 Generation = ++NextGeneration;
	FContextData& Data = ContextData.Add(ChangeContext);
	Data.Generation = Generation;
	Data.GameInstanceStarted = FWorldDelegates::OnStartGameInstance.AddWeakLambda(this,
		[this, ChangeContext, Generation](UGameInstance* Instance)
		{
			if (IsValid(Instance) && FindActiveContext(ChangeContext, Generation))
			{
				if (const FWorldContext* WorldContext = Instance->GetWorldContext())
				{
					AddToWorld(*WorldContext, ChangeContext, Generation);
				}
			}
		});
	Data.WorldInitialized = FWorldDelegates::OnPostWorldInitialization.AddWeakLambda(this,
		[this, ChangeContext, Generation](UWorld* World, const UWorld::InitializationValues)
		{
			if (GEngine && FindActiveContext(ChangeContext, Generation))
			{
				if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
				{
					AddToWorld(*WorldContext, ChangeContext, Generation);
				}
			}
		});
	Data.WorldCleanup = FWorldDelegates::OnWorldCleanup.AddWeakLambda(this,
		[this, ChangeContext, Generation](UWorld* World, bool, bool)
		{
			RemoveWorld(World, ChangeContext, Generation);
		});
	TArray<TWeakObjectPtr<UWorld>> Worlds;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if (UWorld* World = WorldContext.World()) { Worlds.Add(World); }
	}
	for (const TWeakObjectPtr<UWorld>& WeakWorld : Worlds)
	{
		if (!GEngine || !FindActiveContext(ChangeContext, Generation)) { return; }
		UWorld* World = WeakWorld.Get();
		if (!World) { continue; }
		if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
		{
			AddToWorld(*WorldContext, ChangeContext, Generation);
		}
	}
	if (FindActiveContext(ChangeContext, Generation) && !bInputOptionsProviderRegistered)
	{
		// Registration broadcasts may deactivate this action; publish the flag before notifying listeners.
		bInputOptionsProviderRegistered = true;
		IModularFeatures::Get().RegisterModularFeature(IRPGAdditionalInputProvider::GetFeatureName(), this);
	}
}

void UGameFeatureAction_RPGInputMapping::AddToWorld(const FWorldContext& WorldContext,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	UWorld* World = WorldContext.World();
	if (!IsValid(World) || !World->IsGameWorld() || World->bIsTearingDown
		|| World->GetNetMode() == NM_DedicatedServer
		|| !ChangeContext.ShouldApplyToWorldContext(WorldContext))
	{
		return;
	}
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	if (!Active || Active->Worlds.Contains(World)) { return; }
	FWorldData& Data = Active->Worlds.Add(World);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	// PlayerInput can arrive later or change when its controller is replaced.
	World->GetTimerManager().SetTimer(Data.RefreshTimer,
		FTimerDelegate::CreateWeakLambda(this, [this, WeakWorld, ChangeContext, Generation]
		{
			RefreshPlayers(WeakWorld, ChangeContext, Generation);
		}), 0.25f, true);
	RefreshPlayers(WeakWorld, ChangeContext, Generation);
}

void UGameFeatureAction_RPGInputMapping::RefreshPlayers(TWeakObjectPtr<UWorld> WeakWorld,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	TStrongObjectPtr<UGameFeatureAction_RPGInputMapping> KeepAlive(this);
	TStrongObjectPtr<UWorld> KeepWorld(WeakWorld.Get());
	UWorld* World = KeepWorld.Get();
	FWorldData* Data = FindWorldData(WeakWorld, ChangeContext, Generation);
	UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	if (!Data || !IsValid(Instance) || World->bIsTearingDown || Data->bRefreshing) { return; }
	Data->bRefreshing = true;
	ON_SCOPE_EXIT
	{
		if (FWorldData* Current = FindWorldData(WeakWorld, ChangeContext, Generation))
		{
			Current->bRefreshing = false;
		}
	};

	const TArray<FRPGFeatureInputMapping> MappingSnapshot(InputMappings);
	TArray<TWeakObjectPtr<ULocalPlayer>> Players;
	for (ULocalPlayer* Player : Instance->GetLocalPlayers()) { Players.Add(Player); }
	TArray<TWeakObjectPtr<ULocalPlayer>> RegisteredPlayers;
	Data->Players.GetKeys(RegisteredPlayers);
	for (const TWeakObjectPtr<ULocalPlayer>& WeakPlayer : RegisteredPlayers)
	{
		Data = FindWorldData(WeakWorld, ChangeContext, Generation);
		if (!Data || !WeakWorld.IsValid() || World->bIsTearingDown || !IsValid(Instance)) { return; }
		ULocalPlayer* Player = WeakPlayer.Get();
		if (!Player || Player->GetWorld() != World || !Instance->GetLocalPlayers().Contains(Player))
		{
			FPlayerData Removed;
			if (Data->Players.RemoveAndCopyValue(WeakPlayer, Removed)) { ReleasePlayer(Removed); }
		}
	}
	for (const TWeakObjectPtr<ULocalPlayer>& WeakPlayer : Players)
	{
		Data = FindWorldData(WeakWorld, ChangeContext, Generation);
		if (!Data || !WeakWorld.IsValid() || World->bIsTearingDown || !IsValid(Instance)) { return; }
		TStrongObjectPtr<ULocalPlayer> KeepPlayer(WeakPlayer.Get());
		ULocalPlayer* Player = KeepPlayer.Get();
		if (!Player || Player->GetWorld() != World || !Instance->GetLocalPlayers().Contains(Player)) { continue; }
		UEnhancedInputLocalPlayerSubsystem* Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		UEnhancedPlayerInput* PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
		FPlayerData* Existing = Data->Players.Find(WeakPlayer);
		const bool bSameInput = Existing && Existing->PlayerInput == TWeakObjectPtr<UEnhancedPlayerInput>(PlayerInput)
			&& Existing->Subsystem == TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem>(Subsystem);
		if (bSameInput && !Existing->bRegistrationPending) { continue; }
		if (Existing && !bSameInput)
		{
			FPlayerData Removed;
			Data->Players.RemoveAndCopyValue(WeakPlayer, Removed);
			ReleasePlayer(Removed);
			Data = FindWorldData(WeakWorld, ChangeContext, Generation);
			if (!Data || !WeakWorld.IsValid() || World->bIsTearingDown || !IsValid(Instance)
				|| !IsValid(Player) || Player->GetWorld() != World
				|| !Instance->GetLocalPlayers().Contains(Player)) { return; }
			Subsystem = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			PlayerInput = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
		}
		if (!IsValid(Subsystem) || !IsValid(PlayerInput)) { continue; }

		const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> WeakSubsystem(Subsystem);
		const TWeakObjectPtr<UEnhancedPlayerInput> WeakInput(PlayerInput);
		FPlayerData& Added = Data->Players.FindOrAdd(WeakPlayer);
		const bool bRetryingMappings = Added.bRegistrationPending;
		Added.bRegistrationPending = false;
		Added.Subsystem = Subsystem;
		Added.PlayerInput = PlayerInput;
		Added.OwnerPlayer = Player;
		Added.OwnerWorld = World;
		const auto FindCurrentPlayer = [&]() -> FPlayerData*
		{
			FWorldData* LiveWorld = FindWorldData(WeakWorld, ChangeContext, Generation);
			FPlayerData* Current = LiveWorld ? LiveWorld->Players.Find(WeakPlayer) : nullptr;
			UEnhancedInputLocalPlayerSubsystem* LiveSubsystem = WeakSubsystem.Get();
			UEnhancedPlayerInput* LiveInput = WeakInput.Get();
			APlayerController* Controller = IsLiveFeatureInputObject(Player) ? Player->GetPlayerController(nullptr) : nullptr;
			const bool bCurrent = Current && Current->PlayerInput == WeakInput && Current->Subsystem == WeakSubsystem
				&& WeakWorld.IsValid() && IsLiveFeatureInputObject(World) && !World->bIsTearingDown && IsLiveFeatureInputObject(Player)
				&& Player->GetWorld() == World && IsLiveFeatureInputObject(Instance) && Instance->GetLocalPlayers().Contains(Player)
				&& IsLiveFeatureInputObject(LiveSubsystem) && IsLiveFeatureInputObject(LiveInput)
				&& LiveSubsystem->GetPlayerInput() == LiveInput && IsLiveFeatureInputObject(Controller)
				&& !Controller->IsActorBeingDestroyed() && Controller->GetWorld() == World && Controller->PlayerInput.Get() == LiveInput;
			if (!bCurrent && Current && Current->PlayerInput == WeakInput && Current->Subsystem == WeakSubsystem)
			{
				Current->bRegistrationPending = true;
			}
			return bCurrent ? Current : nullptr;
		};
		TSet<UInputMappingContext*> Seen;
		for (const FRPGFeatureInputMapping& Entry : MappingSnapshot)
		{
			// Every engine add/remove and object duplication may run callbacks. Reacquire native map entries.
			FPlayerData* Current = FindCurrentPlayer();
			if (!Current) { return; }
			Subsystem = WeakSubsystem.Get();
			PlayerInput = WeakInput.Get();
			TStrongObjectPtr<UInputMappingContext> SourceMapping(Entry.MappingContext.Get());
			if (!IsLiveFeatureInputObject(SourceMapping.Get()))
			{
				Current->bRegistrationPending |= !Entry.MappingContext.IsNull();
				continue;
			}
			if (Seen.Contains(SourceMapping.Get())) { continue; }
			Seen.Add(SourceMapping.Get());
			const bool bAlreadyRegistered = Current->RegisteredMappings.ContainsByPredicate(
				[&](const FMappingRegistration& Registration)
				{
					return Registration.SourceMapping.Get() == SourceMapping.Get()
						&& Registration.AppliedMapping.IsValid() && Subsystem->HasMappingContext(Registration.AppliedMapping.Get());
				});
			if (bAlreadyRegistered) { continue; }
			// A registration removed by an external callback no longer owns an engine count.
			Current->RegisteredMappings.RemoveAll([&](const FMappingRegistration& Registration)
			{
				return Registration.SourceMapping.Get() == SourceMapping.Get();
			});
			if (SourceMapping->GetRegistrationTrackingMode() != EMappingContextRegistrationTrackingMode::CountRegistrations)
			{
				if (!bRetryingMappings)
				{
					UE_LOG(LogRPGFeatureInput, Error,
						TEXT("RPG_INPUT SKIPPED Context=%s Reason=RequiresCountRegistrations"), *SourceMapping->GetPathName());
				}
				continue;
			}
			TStrongObjectPtr<UInputMappingContext> ActiveCopy(
				FindActivePlayerMappingCopy(Player, World, PlayerInput, SourceMapping.Get(), Subsystem));
			if ((Entry.bUsePlayerOwnedCopy && Subsystem->HasMappingContext(SourceMapping.Get()))
				|| (!Entry.bUsePlayerOwnedCopy && ActiveCopy.IsValid()))
			{
				Current->bRegistrationPending = true;
				if (!bRetryingMappings)
				{
					UE_LOG(LogRPGFeatureInput, Warning,
						TEXT("RPG_INPUT SKIPPED Context=%s Reason=MixedMappingOwnership"), *SourceMapping->GetPathName());
				}
				continue;
			}
			TStrongObjectPtr<UInputMappingContext> Mapping(Entry.bUsePlayerOwnedCopy
				? GetOrCreatePlayerMappingCopy(Player, World, PlayerInput, SourceMapping.Get(), Subsystem) : SourceMapping.Get());
			Current = FindCurrentPlayer();
			if (!Current) { return; }
			// A duplication callback can register the authored asset after the earlier mode check.
			if (Entry.bUsePlayerOwnedCopy && Subsystem->HasMappingContext(SourceMapping.Get()))
			{
				Current->bRegistrationPending = true;
				continue;
			}
			if (!IsLiveFeatureInputObject(Mapping.Get()))
			{
				Current->bRegistrationPending = true;
				continue;
			}
			if (Entry.bUsePlayerOwnedCopy && !Subsystem->HasMappingContext(Mapping.Get()))
			{
				if (!RestorePlayerMappingPreferences(Player, World, PlayerInput, SourceMapping.Get(), Mapping.Get(), Subsystem))
				{
					if (FPlayerData* Live = FindCurrentPlayer()) { Live->bRegistrationPending = true; }
					continue;
				}
				Current = FindCurrentPlayer();
				if (!Current) { return; }
				UInputMappingContext* LiveCopy = FindActivePlayerMappingCopy(Player, World, PlayerInput, SourceMapping.Get(), Subsystem);
				if (Subsystem->HasMappingContext(SourceMapping.Get()) || (LiveCopy && LiveCopy != Mapping.Get()))
				{
					Current->bRegistrationPending = true;
					continue;
				}
			}
			int32 ExistingPriority = 0;
			if (Subsystem->HasMappingContext(Mapping.Get(), ExistingPriority) && ExistingPriority != Entry.Priority)
			{
				Current->bRegistrationPending = true;
				if (!bRetryingMappings)
				{
					UE_LOG(LogRPGFeatureInput, Warning,
						TEXT("RPG_INPUT SKIPPED Context=%s Reason=PriorityConflict Existing=%d Requested=%d"),
						*Mapping->GetPathName(), ExistingPriority, Entry.Priority);
				}
				continue;
			}
			FMappingRegistration& Registration = Current->RegisteredMappings.AddDefaulted_GetRef();
			Registration.SourceMapping.Reset(SourceMapping.Get());
			Registration.AppliedMapping = Mapping.Get();
			Registration.Priority = Entry.Priority;
			Registration.bShowInInputOptions = Entry.bShowInInputOptions;
			Registration.bUsePlayerOwnedCopy = Entry.bUsePlayerOwnedCopy;
			TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> KeepSubsystem(Subsystem);
			TStrongObjectPtr<UEnhancedPlayerInput> KeepInput(PlayerInput);
			Subsystem->AddMappingContext(Mapping.Get(), Entry.Priority);
			Current = FindCurrentPlayer();
			if (!Current) { return; }
			if (!Subsystem->HasMappingContext(Mapping.Get()))
			{
				Current->RegisteredMappings.RemoveAll([&](const FMappingRegistration& Removed)
				{
					return Removed.AppliedMapping.Get() == Mapping.Get();
				});
				Current->bRegistrationPending = true;
				if (Entry.bShowInInputOptions) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(Player, World); }
				continue;
			}
			UE_LOG(LogRPGFeatureInput, Display,
				TEXT("RPG_INPUT APPLIED World=%s Player=%s Context=%s Source=%s PlayerCopy=%d Priority=%d"),
				*GetNameSafe(WeakWorld.Get()), *GetNameSafe(WeakPlayer.Get()), *Mapping->GetPathName(),
				*SourceMapping->GetPathName(), Entry.bUsePlayerOwnedCopy ? 1 : 0, Entry.Priority);
			if (Entry.bShowInInputOptions) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(Player, World); }
		}
	}
}

void UGameFeatureAction_RPGInputMapping::ReleasePlayer(FPlayerData& Data)
{
	const TWeakObjectPtr<ULocalPlayer> WeakPlayer = Data.OwnerPlayer;
	const TWeakObjectPtr<UWorld> WeakWorld = Data.OwnerWorld;
	const bool bHadVisibleMappings = Data.RegisteredMappings.ContainsByPredicate(
		[](const FMappingRegistration& Mapping) { return Mapping.bShowInInputOptions; });
	ON_SCOPE_EXIT
	{
		if (bHadVisibleMappings) { IRPGAdditionalInputProvider::NotifyInputOptionsChanged(WeakPlayer.Get(), WeakWorld.Get()); }
	};
	Data.OwnerPlayer.Reset();
	Data.OwnerWorld.Reset();
	const TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> WeakSubsystem = Data.Subsystem;
	const TWeakObjectPtr<UEnhancedPlayerInput> WeakInput = Data.PlayerInput;
	TArray<FMappingRegistration> Removed = MoveTemp(Data.RegisteredMappings);
	Data.Subsystem.Reset();
	Data.PlayerInput.Reset();
	Data.bRegistrationPending = false;
	for (const FMappingRegistration& Registration : Removed)
	{
		TStrongObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem(WeakSubsystem.Get());
		TStrongObjectPtr<UEnhancedPlayerInput> PlayerInput(WeakInput.Get());
		// Recheck for each mapping: the preceding removal can replace the controller.
		if (!Subsystem.IsValid() || !PlayerInput.IsValid()
			|| Subsystem->GetPlayerInput() != PlayerInput.Get()) { break; }
		TStrongObjectPtr<UInputMappingContext> Mapping(Registration.AppliedMapping.Get());
		if (!Mapping.IsValid()) { continue; }
		Subsystem->RemoveMappingContext(Mapping.Get());
		if (IsValid(Subsystem.Get()) && Subsystem->GetPlayerInput() == PlayerInput.Get())
		{
			UE_LOG(LogRPGFeatureInput, Display, TEXT("RPG_INPUT RELEASED Context=%s Remaining=%d"),
				*Mapping->GetPathName(), Subsystem->HasMappingContext(Mapping.Get()));
		}
	}
}

void UGameFeatureAction_RPGInputMapping::ReleaseWorld(UWorld* World, FWorldData& Data)
{
	if (World) { World->GetTimerManager().ClearTimer(Data.RefreshTimer); }
	TMap<TWeakObjectPtr<ULocalPlayer>, FPlayerData> Removed = MoveTemp(Data.Players);
	for (auto& Pair : Removed) { ReleasePlayer(Pair.Value); }
}

void UGameFeatureAction_RPGInputMapping::RemoveWorld(UWorld* World,
	const FGameFeatureStateChangeContext& ChangeContext, uint64 Generation)
{
	FContextData* Active = FindActiveContext(ChangeContext, Generation);
	FWorldData Removed;
	if (Active && Active->Worlds.RemoveAndCopyValue(World, Removed))
	{
		ReleaseWorld(World, Removed);
	}
}

void UGameFeatureAction_RPGInputMapping::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
	TStrongObjectPtr<UGameFeatureAction_RPGInputMapping> KeepAlive(this);
	FContextData Removed;
	if (ContextData.RemoveAndCopyValue(Context, Removed))
	{
		if (ContextData.IsEmpty() && bInputOptionsProviderRegistered)
		{
			bInputOptionsProviderRegistered = false;
			IModularFeatures::Get().UnregisterModularFeature(IRPGAdditionalInputProvider::GetFeatureName(), this);
		}
		FWorldDelegates::OnStartGameInstance.Remove(Removed.GameInstanceStarted);
		FWorldDelegates::OnPostWorldInitialization.Remove(Removed.WorldInitialized);
		FWorldDelegates::OnWorldCleanup.Remove(Removed.WorldCleanup);
		for (auto& Pair : Removed.Worlds) { ReleaseWorld(Pair.Key.Get(), Pair.Value); }
	}
	Super::OnGameFeatureDeactivating(Context);
}

#if WITH_EDITOR
EDataValidationResult UGameFeatureAction_RPGInputMapping::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(Super::IsDataValid(Context), EDataValidationResult::Valid);
	TSet<FSoftObjectPath> Seen;
	if (InputMappings.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("At least one additional input mapping is required.")));
		Result = EDataValidationResult::Invalid;
	}
	for (const FRPGFeatureInputMapping& Entry : InputMappings)
	{
		const FSoftObjectPath Path = Entry.MappingContext.ToSoftObjectPath();
		UInputMappingContext* Mapping = Entry.MappingContext.LoadSynchronous();
		if (!Mapping || Seen.Contains(Path))
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Missing or repeated input mapping: %s"), *Path.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		else if (Mapping->GetRegistrationTrackingMode() != EMappingContextRegistrationTrackingMode::CountRegistrations)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Input mapping must use CountRegistrations: %s"), *Path.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Path);
	}
	return Result;
}
#endif

#if WITH_EDITORONLY_DATA
void UGameFeatureAction_RPGInputMapping::AddAdditionalAssetBundleData(FAssetBundleData& AssetBundleData)
{
	Super::AddAdditionalAssetBundleData(AssetBundleData);
	for (const FRPGFeatureInputMapping& Entry : InputMappings)
	{
		if (!Entry.MappingContext.IsNull())
		{
			AssetBundleData.AddBundleAsset(TEXT("Client"), Entry.MappingContext.ToSoftObjectPath().GetAssetPath());
		}
	}
}
#endif
