#pragma once

#include "Component/RPGItemCommandComponent.h"
#include "CoreMinimal.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "Type/RPGEnumTypes.h"
#include "UI/MVVM/RPGViewModelBase.h"
#include "RPGInventoryProjectionViewModel.generated.h"

class APlayerController;
class URPGInventoryProjectionComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGInventoryProjectionCommandCompleted,
	FRPGItemCommandClientResult,
	Result);

/**
 * UI-only adapter for Item V2's owner projection and command facade.
 *
 * This ViewModel deliberately exposes projected entries rather than legacy
 * URPGItemBase objects. Commands carry only an item identity and the current
 * projected revision; the server still owns all authorization and mutation.
 */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGInventoryProjectionViewModel final
	: public URPGViewModelBase
{
	GENERATED_BODY()

public:
	virtual void BeginDestroy() override;

	/** Bind this ViewModel to the local player controller's Item V2 components. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	void InitializeFromPlayerController(APlayerController* PlayerController);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestConsume(FGuid ItemId, FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestEquip(
		FGuid ItemId,
		EEquipmentSlotType SlotType,
		FGuid& OutRequestId);

	UFUNCTION(BlueprintCallable, Category = "RPG|Item|ViewModel")
	bool RequestUnequip(
		FGuid ItemId,
		int32 InventorySlotIndex,
		FGuid& OutRequestId);

	/** Projection state for loading and empty-state UI. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	ERPGInventoryProjectionLoadState LoadState =
		ERPGInventoryProjectionLoadState::Uninitialized;

	/** Active owner records in the Inventory container, ordered by slot. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	TArray<FRPGInventoryProjectionEntry> InventoryItems;

	/** Active owner records in the Equipment container, ordered by slot. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	TArray<FRPGInventoryProjectionEntry> EquipmentItems;

	/** The most recent owner-only command outcome for UI feedback. */
	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	FRPGItemCommandClientResult LastCommandResult;

	UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "RPG|Item|ViewModel")
	bool bHasLastCommandResult = false;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Item|ViewModel")
	FRPGInventoryProjectionCommandCompleted OnCommandCompleted;

private:
	UFUNCTION()
	void HandleProjectionChanged();

	UFUNCTION()
	void HandleLoadStateChanged(
		ERPGInventoryProjectionLoadState NewLoadState);

	UFUNCTION()
	void HandleCommandCompleted(FRPGItemCommandClientResult Result);

	bool TryGetEntry(
		const FGuid& ItemId,
		ERPGItemContainerType ExpectedContainer,
		FRPGInventoryProjectionEntry& OutEntry) const;
	void RefreshProjection();
	void UnbindComponents();

	TWeakObjectPtr<URPGInventoryProjectionComponent> ProjectionComponent;
	TWeakObjectPtr<URPGItemCommandComponent> CommandComponent;
};
