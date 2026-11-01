#pragma once

#include "CoreMinimal.h"
#include "Item/Backend/RPGItemBackendTypes.h"
#include "Item/Projection/RPGInventoryProjectionTypes.h"
#include "RPGEquipmentMutationTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGEquipmentMutationOperation : uint8
{
	Upgrade, Transfer, Craft, Dismantle,
	Count UMETA(Hidden)
};

UENUM(BlueprintType)
enum class ERPGEquipmentMutationState : uint8
{
	Idle, AwaitingServer, RecoveryRequired, AuthenticationRequired,
	Committed, Rejected, StorageBlocked, Unavailable
};

/** An expectation, never a replacement item or a client-supplied price. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGEquipmentItemExpectation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Equipment") FGuid ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Equipment") int64 ExpectedRevision = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Equipment") int32 ExpectedLevel = -1;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGEquipmentCostExpectation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Equipment") FGuid ItemId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RPG|Equipment") int64 ExpectedRevision = 0;
};

/** UI state only. Receipt records are deliberately not applied as current inventory. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGEquipmentMutationView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") FGuid CharacterId;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") FGuid RequestId;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") ERPGEquipmentMutationOperation Operation = ERPGEquipmentMutationOperation::Upgrade;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") ERPGEquipmentMutationState State = ERPGEquipmentMutationState::Idle;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") int32 HttpStatus = 0;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") int32 AffectedQuantity = 0;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") bool bNeedsInventoryRefresh = false;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") ERPGInventoryProjectionLoadState ItemsLoadState = ERPGInventoryProjectionLoadState::Uninitialized;
	/** Read-only fresh owner query, never historical receipt records. */
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") TArray<FRPGInventoryProjectionEntry> Items;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") TArray<FRPGInventoryProjectionEntry> MailItems;
	UPROPERTY(BlueprintReadOnly, Category="RPG|Equipment") FText Message;
};

/** Native immutable-intent value. Every retry uses the same ID and encoded body. */
struct PROJECT_RPG_API FRPGEquipmentMutationCommand
{
	FGuid RequestId;
	FGuid CharacterId;
	ERPGEquipmentMutationOperation Operation = ERPGEquipmentMutationOperation::Count;
	FRPGEquipmentItemExpectation Equipment;
	FRPGEquipmentItemExpectation Target;
	TArray<FRPGEquipmentCostExpectation> Costs;
	FString TargetDefinitionName;
};

/** No credentials in persistent state, request bodies, Blueprint results or logs. */
struct PROJECT_RPG_API FRPGEquipmentMutationScope
{
	FString Key;
	FGuid CharacterId;
	static bool Create(const FString& Endpoint, const FString& SteamId, const FGuid& CharacterId,
		FRPGEquipmentMutationScope& OutScope);
	bool operator==(const FRPGEquipmentMutationScope& Other) const
	{ return CharacterId == Other.CharacterId && Key.Equals(Other.Key, ESearchCase::CaseSensitive); }
};

struct PROJECT_RPG_API FRPGEquipmentMutationHttpResult
{
	bool bCurrentPlayerContext = false;
	FRPGItemBackendHttpResponse Response;
};
