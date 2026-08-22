#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "Item/Projection/RPGInventoryProjectionStore.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "RPGInventoryProjectionComponent.generated.h"

struct FRPGItemBackendCommitResult;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPGInventoryProjectionChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGInventoryProjectionLoadStateChanged,
	ERPGInventoryProjectionLoadState,
	LoadState);
DECLARE_MULTICAST_DELEGATE(FRPGAuthoritativeItemRecordsChanged);

/**
 * Owner-only replicated read model of authoritative backend inventory records.
 * This component never accepts client-authored item state.
 */
UCLASS(ClassGroup = (RPG), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGInventoryProjectionComponent final
	: public UActorComponent
{
	GENERATED_BODY()

public:
	URPGInventoryProjectionComponent();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Starts the Dedicated Server load after backend admission succeeds. */
	bool LoadAuthenticatedCharacterItems();

	/** Server-only replacement from an authoritative backend result. */
	bool ApplyAuthoritativeRecords(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& Records,
		FString* OutError = nullptr);

	/** Applies only the records returned by a successful Item V2 Commit. */
	bool ApplyAuthoritativeMutationRecords(
		const FRPGItemOwnerRef& ExpectedOwner,
		const TArray<FRPGItemRecord>& MutationRecords,
		FString* OutError = nullptr);

	/** Validates and applies a successful Item V2 backend commit receipt. */
	bool ApplyAuthoritativeCommitResult(
		const FRPGItemOwnerRef& ExpectedOwner,
		const FRPGItemBackendCommitResult& CommitResult,
		FString* OutError = nullptr);

	/** Server-only read boundary used by command planners. */
	const IRPGItemRecordSource* GetAuthoritativeRecordSource() const
	{
		return AuthoritativeStore.IsInitialized()
			? &AuthoritativeStore
			: nullptr;
	}

	UFUNCTION(BlueprintPure, Category = "RPG|Inventory Projection")
	TArray<FRPGInventoryProjectionEntry> GetProjectedItems() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Inventory Projection")
	bool FindProjectedItem(
		const FGuid& ItemId,
		FRPGInventoryProjectionEntry& OutEntry) const;

	UFUNCTION(BlueprintPure, Category = "RPG|Inventory Projection")
	ERPGInventoryProjectionLoadState GetLoadState() const
	{
		return LoadState;
	}

	UPROPERTY(BlueprintAssignable, Category = "RPG|Inventory Projection")
	FRPGInventoryProjectionChanged OnProjectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "RPG|Inventory Projection")
	FRPGInventoryProjectionLoadStateChanged OnLoadStateChanged;

	/** Server-only notification used by equipment and command application. */
	FRPGAuthoritativeItemRecordsChanged OnAuthoritativeRecordsChanged;

private:
	friend struct FRPGInventoryProjectionList;

	void HandleProjectionReplicated();
	bool ReconcileProjection(
		const TArray<FRPGInventoryProjectionEntry>& DesiredEntries,
		FString* OutError);
	void SetLoadState(ERPGInventoryProjectionLoadState NewState);

	UFUNCTION()
	void OnRep_LoadState();

	UPROPERTY(Replicated)
	FRPGInventoryProjectionList Projection;

	UPROPERTY(ReplicatedUsing = OnRep_LoadState)
	ERPGInventoryProjectionLoadState LoadState =
		ERPGInventoryProjectionLoadState::Uninitialized;

	uint32 ActiveLoadGeneration = 0;
	FRPGInventoryProjectionStore AuthoritativeStore;
};
