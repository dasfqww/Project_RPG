#pragma once

#include "Character/RPGPlayer.h"
#include "Controller/RPGPlayerController.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tests/RPGDungeonEjectionTestFixture.h"
#include "RPGDungeonEjectionNetworkProbe.generated.h"

class ARPGPlayerState;

/** Native movement fixture. No authored input, boss, backend or Steam identity. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonEjectionNetworkPlayer final : public ARPGPlayer
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void ProcessEvent(UFunction* Function, void* Parms) override;
	int32 NormalMoves = 0;
	int32 RevokedMoves = 0;
private:
	UPROPERTY()
	TObjectPtr<UDataAsset_StartUpDataBase> ProbeStartup;
};

UCLASS(Transient, NotBlueprintable)
class ARPGDungeonEjectionNetworkController final : public ARPGPlayerController
{
	GENERATED_BODY()
public:
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaSeconds) override;
	bool bServerReady = false;
	UFUNCTION(Client, Reliable)
	void ClientConfirmProbeComplete(bool bRevoked, int32 ExpectedConnections);
private:
	UFUNCTION(Server, Reliable)
	void ServerConfirmProbeReady();
	bool bReadySent = false;
	bool bCompleteRequested = false;
	bool bCompleteLogged = false;
	bool bExpectedRevoked = false;
	int32 ExpectedConnectionsAfter = 0;
};

/** Records actual engine network failure after the owning Controller is destroyed. */
UCLASS(Transient, NotBlueprintable)
class URPGDungeonEjectionNetworkClient final : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	int32 PlayerId = INDEX_NONE;
private:
	void HandleNetworkFailure(UWorld* World, UNetDriver* Driver,
		ENetworkFailure::Type Failure, const FString& Error);
};

/** Explicit development-only scenarios: real ejection OR live queued-RPC quarantine. */
UCLASS(Transient, NotBlueprintable)
class ARPGDungeonEjectionNetworkGameMode final : public ARPGDungeonEjectionTestGameMode
{
	GENERATED_BODY()
public:
	ARPGDungeonEjectionNetworkGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* Controller, const FTransform& Transform) override;
private:
	struct FPeer
	{
		TWeakObjectPtr<ARPGDungeonEjectionNetworkController> Controller;
		TWeakObjectPtr<ARPGDungeonEjectionNetworkPlayer> Pawn;
		TWeakObjectPtr<ARPGPlayerState> State;
		TWeakObjectPtr<UNetConnection> Connection;
		int32 PlayerId = INDEX_NONE;
		FString CharacterId;
		FString SteamId;
		int32 MoveBaseline = 0;
		FVector LastLocation = FVector::ZeroVector;
		double Distance = 0.0;
	};
	void Fail(const TCHAR* Reason);
	UFUNCTION()
	void HandleBackendTargetRevoked(const FString& CharacterId);
	TArray<FPeer> Peers;
	FVector FrozenLocation = FVector::ZeroVector;
	double Deadline = 0.0;
	double PhaseStarted = 0.0;
	int32 ExpectedPlayers = 0;
	int32 Stage = 0;
	int32 SpawnSlot = 0;
	bool bEnabled = false;
	bool bQuarantine = false;
	bool bBackendMembership = false;
	bool bAwaitingBackendReceipt = false;
	FString BackendSession;
	FString BackendServer;
	FString AbandonCharacter;
};
