#pragma once

#include "CoreMinimal.h"
#include "Item/Persistence/RPGItemRecord.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "RPGInventoryProjectionTypes.generated.h"

class URPGInventoryProjectionComponent;

UENUM(BlueprintType)
enum class ERPGInventoryProjectionLoadState : uint8
{
	Uninitialized,
	Loading,
	Ready,
	Failed
};

/**
 * Client-safe read model for one inventory record.
 *
 * Owner IDs, container IDs, generation seeds, lifecycle data, and persistence
 * metadata are deliberately not replicated.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGInventoryProjectionEntry
	: public FFastArraySerializerItem
{
	GENERATED_BODY()

public:
	const FGuid& GetItemId() const { return ItemId; }
	const FPrimaryAssetId& GetDefinitionId() const { return DefinitionId; }
	int32 GetDefinitionVersion() const { return DefinitionVersion; }
	int32 GetSlotIndex() const { return SlotIndex; }
	int32 GetQuantity() const { return Quantity; }
	int64 GetRevision() const { return Revision; }
	ERPGItemBindState GetBindState() const { return BindState; }
	const FRPGItemDurability& GetDurability() const { return Durability; }
	const FDateTime& GetExpiresAtUtc() const { return ExpiresAtUtc; }
	bool IsLocked() const { return bLocked; }
	const FGameplayTagContainer& GetInstanceTags() const
	{
		return InstanceTags;
	}
	const TArray<FRPGItemStatValue>& GetRolledStats() const
	{
		return RolledStats;
	}

	bool HasSamePayload(const FRPGInventoryProjectionEntry& Other) const;
	void CopyPayloadFrom(const FRPGInventoryProjectionEntry& Other);

private:
	friend class FRPGInventoryProjectionMapper;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	FGuid ItemId;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	FPrimaryAssetId DefinitionId;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	int32 DefinitionVersion = 0;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	int32 Quantity = 0;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	FRPGItemDurability Durability;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	FDateTime ExpiresAtUtc;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	bool bLocked = false;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	FGameplayTagContainer InstanceTags;

	UPROPERTY(BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TArray<FRPGItemStatValue> RolledStats;
};

/** Delta-replicated collection reconciled by stable item identity. */
USTRUCT()
struct PROJECT_RPG_API FRPGInventoryProjectionList
	: public FFastArraySerializer
{
	GENERATED_BODY()

public:
	FRPGInventoryProjectionList() = default;
	explicit FRPGInventoryProjectionList(
		URPGInventoryProjectionComponent* InOwnerComponent)
		: OwnerComponent(InOwnerComponent)
	{
	}

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParams)
	{
		return FastArrayDeltaSerialize<
			FRPGInventoryProjectionEntry,
			FRPGInventoryProjectionList>(Entries, DeltaParams, *this);
	}

	void PostReplicatedReceive(
		const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

	/** Returns false only when the desired snapshot itself is invalid. */
	bool Reconcile(
		const TArray<FRPGInventoryProjectionEntry>& DesiredEntries,
		bool& bOutChanged);

	const TArray<FRPGInventoryProjectionEntry>& GetEntries() const
	{
		return Entries;
	}

	const FRPGInventoryProjectionEntry* Find(const FGuid& ItemId) const;

private:
	friend class URPGInventoryProjectionComponent;

	UPROPERTY()
	TArray<FRPGInventoryProjectionEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<URPGInventoryProjectionComponent> OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FRPGInventoryProjectionList>
	: public TStructOpsTypeTraitsBase2<FRPGInventoryProjectionList>
{
	enum { WithNetDeltaSerializer = true };
};
