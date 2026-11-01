#pragma once

#include "GameMode/RPGBossBattleGameMode.h"
#include "Engine/GameInstance.h"
#include "IpConnection.h"
#include "RPGDungeonEjectionTestFixture.generated.h"

/** Native connection lifecycle fixture, not a real socket or Steam session. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonEjectionTestGameMode : public ARPGBossBattleGameMode
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void Logout(AController* Exiting) override;
	void AddTestParticipant(APlayerController* Controller);
	int32 GetTestParticipantCount() const { return Participants.Num(); }
	int32 GetTestRetryStateCount() const { return ParticipantRetryStates.Num(); }
	int32 LogoutCount = 0;
	TWeakObjectPtr<UNetConnection> ObservedCleanupConnection;
	UPROPERTY()
	TObjectPtr<APlayerController> ReplaceConnectionController;
	UPROPERTY()
	TObjectPtr<UNetConnection> ReplacementConnection;
	UFUNCTION()
	void ReplaceConnectionOnIdentityChanged(const FString& CharacterId);
	UFUNCTION()
	void EndMembershipScopeOnIdentityChanged(const FString& CharacterId);
};

/** Uses real engine Close/CleanUp; no initialized socket, packet dispatch or Iris system. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonEjectionTestConnection : public UIpConnection
{
	GENERATED_BODY()
public:
	void InitializeNativeConnection(UNetDriver& InDriver);
	virtual FString LowLevelDescribe() override { return TEXT("Native ejection fixture"); }
	virtual void LowLevelSend(void*, int32, FOutPacketTraits&) override { ++SendCalls; }
	int32 SendCalls = 0;
};

/** Observes production transport completions without sending HTTP or changing gameplay. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonMutationTestObserver : public UObject
{
	GENERATED_BODY()
public:
	int32 ConsumeReplies = 0;
	int64 LastConsumeGeneration = 0;
	int32 SettlementReplies = 0;
	bool bLastConsumeSucceeded = false;
	bool bLastSettlementSucceeded = false;
	FString LastSettlementState;
	TWeakObjectPtr<UHttpWebManager> InspectConsumeGuard;
	bool bMutationAllowedDuringConsumeReply = true;
	UFUNCTION()
	void ConsumeCompleted(const FString& Ticket, const FString& Session, const FString& Character,
		const FString& Steam, int64 Generation, bool bSuccess);
	UFUNCTION()
	void SettlementCompleted(const FString& State, int32 HttpStatus, bool bSuccess);
};

/** Isolated world binding without initializing subsystems, networking or HTTP. */
UCLASS(Transient)
class URPGDungeonMutationTestGameInstance : public UGameInstance
{
	GENERATED_BODY()
public:
	void SetTestWorldContext(FWorldContext* Context);
};
