#pragma once

#include "CoreMinimal.h"
#include "Manager/HttpWebManager.h"
#include "RPGOnlineLobbyPartyView.generated.h"

/** Read-only dungeon membership presentation; never a Steam lobby, readiness or admission authority. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGOnlineLobbyPartyMemberView
{
	GENERATED_BODY()

	/** Stable row identity; the default UI does not print the full ID. */
	UPROPERTY(BlueprintReadOnly, Category = "Online|Lobby|Party") FString CharacterId;
	UPROPERTY(BlueprintReadOnly, Category = "Online|Lobby|Party") FText DisplayName;
	UPROPERTY(BlueprintReadOnly, Category = "Online|Lobby|Party") bool bHasKnownName = false;
	UPROPERTY(BlueprintReadOnly, Category = "Online|Lobby|Party") bool bIsLocalCharacter = false;
	UPROPERTY(BlueprintReadOnly, Category = "Online|Lobby|Party") bool bIsLeader = false;
};

/** Shared native/custom-UI formatting of already loaded membership/owned snapshots; no authorization decision. */
class PROJECT_RPG_API FRPGOnlineLobbyPartyPresentation
{
public:
	static TArray<FRPGOnlineLobbyPartyMemberView> BuildMembers(const FRPGDungeonSession& Session,
		const FString& SelectedCharacterId, const TArray<FRPGBackendCharacter>& OwnedCharacters);
	static FText Summary(const TArray<FRPGOnlineLobbyPartyMemberView>& Members, bool bCachedSnapshot);
	static FText RosterText(const TArray<FRPGOnlineLobbyPartyMemberView>& Members);
};
