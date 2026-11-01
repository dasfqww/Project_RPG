#pragma once

#include "Components/ActorComponent.h"
#include "Online/RPGDungeonPartyCombatTypes.h"
#include "Online/RPGDungeonMembershipSnapshot.h"
#include "RPGDungeonPartyCombatComponent.generated.h"

class ARPGPlayerState;

/** Owner-only, server-authored read model. No RPCs or client-authored health. */
UCLASS(ClassGroup = (RPG))
class PROJECT_RPG_API URPGDungeonPartyCombatComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGDungeonPartyCombatComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category = "RPG|Dungeon|Party")
	FRPGDungeonPartyCombatSnapshot GetPartyCombatSnapshot() const;
private:
	friend struct FRPGDungeonPartyCombatTestAccess;
	void RefreshServerSnapshot();
	struct FCachedMember
	{
		FRPGDungeonMembershipRecord Record;
		TWeakObjectPtr<ARPGPlayerState> LastObservedPlayer;
		FString PublicName;
		int32 PlayerNumber = INDEX_NONE;
		int32 Slot = INDEX_NONE;
		int64 HighestObservedConnectionGeneration = 0;
		FRPGDungeonPartyPresenceHistory PresenceHistory;
		bool bListed = false;
	};
	// Server-only memory. Not a replicated property, admission grant or expiry timer.
	TMap<FString, FCachedMember> MemberCache;
	TArray<FRPGDungeonPartyNotice> PendingNotices;
	FString CachedSessionId;
	FString CachedRecipientId;
	int32 NextMemberSlot = 0;
	UPROPERTY(Replicated) FRPGDungeonPartyCombatSnapshot Snapshot;
};
