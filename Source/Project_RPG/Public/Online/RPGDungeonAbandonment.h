#pragma once

#include "CoreMinimal.h"
#include "RPGDungeonAbandonment.generated.h"

/** Immutable acknowledgement of THIS character's opt-out, not remaining party state. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonAbandonmentReceipt
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString DungeonSessionId;
	UPROPERTY(BlueprintReadOnly) FString CharacterId;
	UPROPERTY(BlueprintReadOnly) FString AbandonedAt;
	UPROPERTY(BlueprintReadOnly) FString SessionStateAtAbandonment;
	UPROPERTY(BlueprintReadOnly) bool bRewardExcluded = false;

	bool IsValidFor(const FString& SessionId, const FString& SelectedCharacterId) const;
};

class PROJECT_RPG_API FRPGDungeonAbandonmentCodec
{
public:
	static bool Decode(const FString& Body, int32 HttpStatus, const FString& SessionId,
		const FString& CharacterId, FRPGDungeonAbandonmentReceipt& OutReceipt);
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDungeonAbandonmentCompleted,
	const FRPGDungeonAbandonmentReceipt&, Receipt, bool, bSuccess, const FString&, ErrorCode);
