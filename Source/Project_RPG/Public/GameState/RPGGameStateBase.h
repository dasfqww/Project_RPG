// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "RPGGameStateBase.generated.h"

/**
 * Small, server-authored snapshot used by clients and automated smoke tests to
 * prove that the game net driver is replicating through Iris.
 */
USTRUCT(BlueprintType)
struct FRPGNetworkSessionSnapshot
{
	GENERATED_BODY()

	/** Number of remote net connections currently admitted by the server. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Network")
	int32 ConnectedPlayerCount = 0;

	/** Expected count supplied by -RPGExpectedClients for a network smoke test. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Network")
	int32 ExpectedPlayerCount = 0;

	/** True only when the authoritative GameNetDriver reports Iris at runtime. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Network")
	bool bServerUsesIris = false;

	/** Increments whenever the authoritative snapshot changes. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Network")
	int32 Revision = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FRPGNetworkSessionSnapshotChanged,
	const FRPGNetworkSessionSnapshot&,
	Snapshot);

/**
 * Replicated session state shared by every RPG game mode.
 *
 * The class intentionally uses ordinary replicated UPROPERTY state: when the
 * GameNetDriver is running Iris this travels through the Iris replication
 * bridge, making it a useful end-to-end probe rather than a config-only check.
 */
UCLASS()
class PROJECT_RPG_API ARPGGameStateBase : public AGameStateBase
{
	GENERATED_BODY()

public:
	ARPGGameStateBase();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "RPG|Network")
	const FRPGNetworkSessionSnapshot& GetNetworkSessionSnapshot() const
	{
		return NetworkSessionSnapshot;
	}

	/** Fired on clients whenever the server-authored snapshot is received. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Network")
	FRPGNetworkSessionSnapshotChanged OnNetworkSessionSnapshotChanged;

private:
	void RefreshAuthoritativeNetworkSnapshot();
	bool IsLocalGameNetDriverUsingIris() const;
	void LogClientSnapshot() const;

	UFUNCTION()
	void OnRep_NetworkSessionSnapshot();

	UPROPERTY(
		ReplicatedUsing = OnRep_NetworkSessionSnapshot,
		VisibleInstanceOnly,
		Category = "RPG|Network")
	FRPGNetworkSessionSnapshot NetworkSessionSnapshot;

	FTimerHandle NetworkSnapshotRefreshTimer;
	bool bServerReadyMarkerLogged = false;
};
