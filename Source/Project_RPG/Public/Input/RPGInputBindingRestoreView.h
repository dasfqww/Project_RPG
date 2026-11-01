#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "EnhancedActionKeyMapping.h"
#include "InputCoreTypes.h"

class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;

/**
 * Borrowed input-restoration plan for one synchronous policy callback.
 * Arrays refer to request-owned snapshots; they must not be retained.
 * Contexts/subsystem are provided for read-only queries, without ownership transfer.
 * The controller coordinates validation; each feature plan retains its own snapshots.
 */
struct FRPGInputBindingRestoreContextView
{
	TConstArrayView<FEnhancedActionKeyMapping> DefaultMappings;
	TConstArrayView<FKey> FinalKeys;
	TConstArrayView<int32> RestoredMappingIndices;
	const UInputMappingContext* RuntimeContext = nullptr;
	/** A derived context rebuilt with the base context, evaluated by the same final key plan. */
	const UInputMappingContext* RebuiltContext = nullptr;
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = nullptr;
};

/** Complete synchronous policy view: primary input plus the feature contexts participating in this request. */
struct FRPGInputBindingRestoreView : FRPGInputBindingRestoreContextView
{
	TConstArrayView<FRPGInputBindingRestoreContextView> AdditionalContexts;
};

/** Borrowed extension for one controller-owned commit/rebuild/save.
 * Callbacks and arrays must remain alive until the controller returns.
 * Commit only stages keys/settings; it must not rebuild input, save, or emit callbacks.
 * Validate checks both the prepared and committed state of every participating feature.
 */
struct FRPGInputBindingRestoreExtension
{
	TConstArrayView<FRPGInputBindingRestoreContextView> Contexts;
	TFunctionRef<bool(FText&)> Validate;
	TFunctionRef<bool(FText&)> Commit;
};
