#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InputCoreTypes.h"

class APlayerController;
class UDataAsset_InputConfig;
class UInputMappingContext;
class UQuickSlotComponent;
class URPGSkillWindowComponent;
struct FRPGKeyMapping;
struct FRPGInputBindingRestoreView;
struct FRPGInputBindingRestoreExtension;

/** Live input ownership, deliberately excluding inventory, interaction and encounter presentation. */
class IRPGInputBindingHost
{
public:
	virtual ~IRPGInputBindingHost() = default;
	virtual APlayerController& GetInputBindingController() const = 0;
	virtual UDataAsset_InputConfig* GetInputBindingConfig() const = 0;
	virtual const UDataAsset_InputConfig* GetSourceInputConfig() const = 0;
	virtual UInputMappingContext* GetRebuiltInputContext() const = 0;
	virtual UQuickSlotComponent* GetInputQuickSlots() const = 0;
	virtual URPGSkillWindowComponent* GetInputSkillWindow() const = 0;
};

/** Owns key-edit transactions and their reentrancy guard for one input host.
 * Host accessors are live so every callback boundary can revalidate ownership and mappings.
 * The controller retains authored assets and engine input binding/lifecycle wiring.
 */
class PROJECT_RPG_API FRPGInputBindingTransactions
{
	friend class FRPGInputBindingTransactionsTest;
public:
	explicit FRPGInputBindingTransactions(IRPGInputBindingHost& InHost) : Host(InHost) {}
	FRPGInputBindingTransactions(const FRPGInputBindingTransactions&) = delete;
	FRPGInputBindingTransactions& operator=(const FRPGInputBindingTransactions&) = delete;
	void UpdateInputMappings();
	void ApplyKeyMapping(FGameplayTag InTag, FKey NewKey);
	bool TryApplyKeyBinding(FGameplayTag InTag, FKey DefaultKey, FKey NewKey, FText& OutError);
	bool TryApplyKeyBindingWithPolicy(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
		TFunctionRef<bool(FText&)> ValidatePolicy, FText& OutError);
	bool TryRestoreDefaultKeyBindings(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy, FText& OutError);
	bool TryRestoreDefaultKeyBindingsWithAdditional(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
		const FRPGInputBindingRestoreExtension& Additional, FText& OutError);
	FKey GetCurrentKeyForBinding(FGameplayTag InTag, FKey DefaultKey) const;
	FKey GetCurrentKeyForTag(FGameplayTag InTag) const;
private:
	IRPGInputBindingHost& Host;
	bool bApplyingKeyBinding = false;
	bool RestoreDefaultKeyBindingsInternal(const TArray<FRPGKeyMapping>& ExpectedBindings,
		TFunctionRef<bool(const FRPGInputBindingRestoreView&, FText&)> ValidatePolicy,
		const FRPGInputBindingRestoreExtension* Additional, FText& OutError);
	bool ApplyKeyBindingInternal(FGameplayTag InTag, FKey DefaultKey, FKey NewKey,
		FText& OutError, bool bSaveSettings, TFunctionRef<bool(FText&)>* ValidatePolicy = nullptr);
};
