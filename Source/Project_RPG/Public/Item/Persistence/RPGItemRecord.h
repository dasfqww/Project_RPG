#pragma once

#include "CoreMinimal.h"
#include "Item/RPGItemRuntimeTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "RPGItemRecord.generated.h"

UENUM(BlueprintType)
enum class ERPGItemOwnerType : uint8
{
	None,
	Character,
	Account,
	System,
	World
};

UENUM(BlueprintType)
enum class ERPGItemContainerType : uint8
{
	None,
	Inventory,
	Equipment,
	CharacterStorage,
	AccountStorage,
	Mail,
	Trade,
	Auction,
	World,
	Terminal
};

UENUM(BlueprintType)
enum class ERPGItemBindState : uint8
{
	Unbound,
	BindOnEquip,
	CharacterBound,
	AccountBound
};

UENUM(BlueprintType)
enum class ERPGItemLifecycleState : uint8
{
	Active,
	Consumed,
	Destroyed,
	Expired
};

/** Stable external owner identity. It deliberately does not reference an Actor. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemOwnerRef
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ERPGItemOwnerType Type = ERPGItemOwnerType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString OwnerId;

	bool IsValid() const
	{
		return Type != ERPGItemOwnerType::None && !OwnerId.IsEmpty();
	}

	friend bool operator==(
		const FRPGItemOwnerRef& Left,
		const FRPGItemOwnerRef& Right)
	{
		return Left.Type == Right.Type && Left.OwnerId == Right.OwnerId;
	}

	friend bool operator!=(
		const FRPGItemOwnerRef& Left,
		const FRPGItemOwnerRef& Right)
	{
		return !(Left == Right);
	}
};

FORCEINLINE uint32 GetTypeHash(const FRPGItemOwnerRef& Owner)
{
	return HashCombine(
		GetTypeHash(static_cast<uint8>(Owner.Type)),
		GetTypeHash(Owner.OwnerId));
}

/**
 * Logical location used by persistence and transactions.
 * ContainerId is a stable DB/application identifier, never an Actor pointer.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemLocation
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ERPGItemContainerType ContainerType = ERPGItemContainerType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString ContainerId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 SlotIndex = INDEX_NONE;

	bool IsActiveLocation() const
	{
		return ContainerType != ERPGItemContainerType::None &&
			ContainerType != ERPGItemContainerType::Terminal &&
			!ContainerId.IsEmpty() &&
			SlotIndex >= 0;
	}

	bool IsTerminalLocation() const
	{
		return ContainerType == ERPGItemContainerType::Terminal &&
			ContainerId.IsEmpty() &&
			SlotIndex == INDEX_NONE;
	}

	static FRPGItemLocation MakeTerminal()
	{
		FRPGItemLocation Result;
		Result.ContainerType = ERPGItemContainerType::Terminal;
		return Result;
	}

	friend bool operator==(
		const FRPGItemLocation& Left,
		const FRPGItemLocation& Right)
	{
		return Left.ContainerType == Right.ContainerType &&
			Left.ContainerId == Right.ContainerId &&
			Left.SlotIndex == Right.SlotIndex;
	}

	friend bool operator!=(
		const FRPGItemLocation& Left,
		const FRPGItemLocation& Right)
	{
		return !(Left == Right);
	}
};

FORCEINLINE uint32 GetTypeHash(const FRPGItemLocation& Location)
{
	uint32 Hash = GetTypeHash(static_cast<uint8>(Location.ContainerType));
	Hash = HashCombine(Hash, GetTypeHash(Location.ContainerId));
	return HashCombine(Hash, GetTypeHash(Location.SlotIndex));
}

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemDurability
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Current = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	int32 Maximum = 0;

	bool IsTracked() const { return Maximum > 0; }
	bool IsValid() const
	{
		return Maximum >= 0 && Current >= 0 && Current <= Maximum;
	}
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemRecordMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ERPGItemBindState BindState = ERPGItemBindState::Unbound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FRPGItemDurability Durability;

	/** Zero ticks means this item does not expire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FDateTime ExpiresAtUtc;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag CreationSource;

	/** Locked records cannot be moved, merged, consumed, traded, or equipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	bool bLocked = false;
};

/**
 * Authoritative persistence representation of one item.
 *
 * This struct contains no UObject pointers and is safe to serialize through a
 * backend adapter. URPGItemInstance is a session projection of this record, not
 * the source of truth.
 */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGItemRecord
{
	GENERATED_BODY()

public:
	static bool TryCreate(
		const FPrimaryAssetId& InDefinitionId,
		int32 InDefinitionVersion,
		const FRPGItemOwnerRef& InOwner,
		const FRPGItemLocation& InLocation,
		const FRPGItemInstanceState& InState,
		const FRPGItemRecordMetadata& InMetadata,
		FRPGItemRecord& OutRecord);

	static bool TryRestore(
		const FPrimaryAssetId& InDefinitionId,
		int32 InDefinitionVersion,
		const FRPGItemOwnerRef& InOwner,
		const FRPGItemLocation& InLocation,
		const FRPGItemInstanceState& InState,
		int64 InRevision,
		ERPGItemLifecycleState InLifecycleState,
		const FRPGItemRecordMetadata& InMetadata,
		FRPGItemRecord& OutRecord);

	bool IsStructurallyValid() const;
	bool IsActive() const
	{
		return LifecycleState == ERPGItemLifecycleState::Active;
	}
	bool HasExpiration() const { return Metadata.ExpiresAtUtc.GetTicks() > 0; }
	bool IsExpiredAt(const FDateTime& UtcNow) const
	{
		return HasExpiration() && UtcNow >= Metadata.ExpiresAtUtc;
	}
	bool CanBeMutatedBy(
		const FRPGItemOwnerRef& Actor,
		const FDateTime& UtcNow) const
	{
		return IsActive() &&
			!Metadata.bLocked &&
			Owner == Actor &&
			!IsExpiredAt(UtcNow);
	}

	bool TryMoveTo(
		const FRPGItemLocation& NewLocation,
		FRPGItemRecord& OutRecord) const;
	bool TryEquipTo(
		const FRPGItemLocation& EquipmentLocation,
		FRPGItemRecord& OutRecord) const;
	bool TrySetQuantity(
		int32 NewQuantity,
		FRPGItemRecord& OutRecord) const;
	bool TryMarkConsumed(FRPGItemRecord& OutRecord) const;
	FRPGItemRecord CopyWithRevision(int64 NewRevision) const;

	const FGuid& GetItemId() const { return State.GetInstanceId(); }
	const FPrimaryAssetId& GetDefinitionId() const { return DefinitionId; }
	int32 GetDefinitionVersion() const { return DefinitionVersion; }
	const FRPGItemOwnerRef& GetOwner() const { return Owner; }
	const FRPGItemLocation& GetLocation() const { return Location; }
	const FRPGItemInstanceState& GetState() const { return State; }
	int32 GetQuantity() const { return State.GetQuantity(); }
	int64 GetRevision() const { return Revision; }
	ERPGItemLifecycleState GetLifecycleState() const { return LifecycleState; }
	const FRPGItemRecordMetadata& GetMetadata() const { return Metadata; }

private:
	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FPrimaryAssetId DefinitionId;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	int32 DefinitionVersion = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FRPGItemOwnerRef Owner;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FRPGItemLocation Location;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FRPGItemInstanceState State;

	/** Zero is reserved for a record that has not been inserted yet. */
	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	ERPGItemLifecycleState LifecycleState = ERPGItemLifecycleState::Active;

	UPROPERTY(BlueprintReadOnly, SaveGame, meta = (AllowPrivateAccess = "true"))
	FRPGItemRecordMetadata Metadata;
};
