#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "InputCoreTypes.h"
#include "Containers/ArrayView.h"

class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class UWorld;
struct FRPGInputBindingRestoreContextView;

/** Invalidation only: listeners only schedule work here. Queries and mutations must be deferred. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnRPGAdditionalInputOptionsChanged, ULocalPlayer*, UWorld*);

/** One active registration offered by its feature owner. Queries never create or edit mappings. */
struct FRPGAdditionalInputContext
{
	TWeakObjectPtr<const UInputMappingContext> SourceContext;
	TWeakObjectPtr<const UInputMappingContext> AppliedContext;
	/** Only an owned transient copy with a persistent authored source may be edited. */
	bool bCanRemap = false;
};

/** Expected identities and live key of one displayed feature binding. */
struct FRPGAdditionalInputBinding : FRPGAdditionalInputContext
{
	TWeakObjectPtr<const UInputAction> Action;
	FKey DefaultKey;
	FKey CurrentKey;
};

/** One request's native snapshots. Preparation/validation never changes mappings or settings.
 * Commit stages feature keys/preferences; the controller owns the single rebuild/save.
 */
class PROJECT_RPG_API IRPGAdditionalInputRestorePlan
{
public:
	IRPGAdditionalInputRestorePlan();
	virtual ~IRPGAdditionalInputRestorePlan();
	virtual void AppendViews(TArray<FRPGInputBindingRestoreContextView>& OutViews) const = 0;
	virtual bool Validate(FText& OutError) const = 0;
	virtual bool Commit(FText& OutError) = 0;
};

/** Native discovery contract shared by feature actions and input options.
 * Register once while at least one activation is alive; unregister before the owner is destroyed.
 * All calls run on the game thread. Queries must not load assets, change registrations, or broadcast callbacks.
 * Implementations expose only registrations belonging to the supplied player's current world and PlayerInput.
 * Discovery uses existing modular features and does not activate or load a GameFeature.
 */
class PROJECT_RPG_API IRPGAdditionalInputProvider : public IModularFeature
{
public:
	// Out-of-line exports are needed by the GameFeature DLL consumers.
	IRPGAdditionalInputProvider();
	static FName GetFeatureName();
	/** Game-thread event, scoped to one player/world. It does not own either object or replay old changes. */
	static FOnRPGAdditionalInputOptionsChanged& OnInputOptionsChanged();
	/** Also invalidates the common input options list; the event above remains feature-only. */
	static void NotifyInputOptionsChanged(ULocalPlayer* Player, UWorld* World);

	virtual const UObject* GetInputOptionsOwner() const = 0;
	virtual void GetInputOptionContexts(const ULocalPlayer* Player,
		TArray<FRPGAdditionalInputContext>& OutContexts) const = 0;
	/** Synchronous change owned by the feature. Policy is evaluated before commit.
	 * Requests preserve mapping metadata, rebuild the current input, and save once after owner/state checks.
	 */
	virtual bool TryApplyInputOptionBinding(ULocalPlayer* Player, const FRPGAdditionalInputBinding& Expected,
		FKey NewKey, TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError) = 0;
	virtual TUniquePtr<IRPGAdditionalInputRestorePlan> PrepareInputOptionRestoration(ULocalPlayer* Player,
		TConstArrayView<FRPGAdditionalInputBinding> ExpectedBindings, FText& OutError) = 0;
};
