#pragma once

#include "CoreMinimal.h"
#include "RPGDungeonSessionTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSessionMember
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString CharacterId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString JoinedAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString LeaseExpiresAt;

	/** Optional public character name from the authenticated session response; never identity/authority. */
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString CharacterName;
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGDungeonSession
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonSessionId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString DungeonId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString Difficulty;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString State;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerId;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ServerAddress;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString LeaderCharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") int64 LobbyRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString DepartureRequestId;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") int64 DepartureRequestRevision = 0;
	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session") FString DepartureRequestedAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	FString ExpiresAt;

	UPROPERTY(BlueprintReadOnly, Category = "HTTP|Dungeon Session")
	TArray<FRPGDungeonSessionMember> Members;
};
