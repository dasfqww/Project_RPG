// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameState/RPGGameStateBase.h"

#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRPGNetwork, Log, All);

namespace
{
	constexpr float NetworkSnapshotRefreshIntervalSeconds = 0.25f;

	int32 GetExpectedNetworkTestClients()
	{
		int32 ExpectedClients = 0;
		FParse::Value(
			FCommandLine::Get(),
			TEXT("RPGExpectedClients="),
			ExpectedClients);
		return FMath::Max(0, ExpectedClients);
	}

	bool IsIrisRequiredForNetworkTest()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("RPGRequireIris"));
	}
}

ARPGGameStateBase::ARPGGameStateBase()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
}

void ARPGGameStateBase::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	NetworkSessionSnapshot.ExpectedPlayerCount =
		GetExpectedNetworkTestClients();
	RefreshAuthoritativeNetworkSnapshot();

	if (GetNetMode() != NM_Standalone)
	{
		GetWorldTimerManager().SetTimer(
			NetworkSnapshotRefreshTimer,
			this,
			&ThisClass::RefreshAuthoritativeNetworkSnapshot,
			NetworkSnapshotRefreshIntervalSeconds,
			true);
	}
}

void ARPGGameStateBase::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(NetworkSnapshotRefreshTimer);
	Super::EndPlay(EndPlayReason);
}

void ARPGGameStateBase::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARPGGameStateBase, NetworkSessionSnapshot);
}

void ARPGGameStateBase::RefreshAuthoritativeNetworkSnapshot()
{
	if (!HasAuthority())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UNetDriver* NetDriver = World ? World->GetNetDriver() : nullptr;
	const int32 ConnectedPlayers = NetDriver
		? NetDriver->ClientConnections.Num()
		: 0;
	const bool bUsesIris = NetDriver && NetDriver->IsUsingIrisReplication();
	const bool bSnapshotChanged =
		NetworkSessionSnapshot.ConnectedPlayerCount != ConnectedPlayers
		|| NetworkSessionSnapshot.bServerUsesIris != bUsesIris;

	if (bSnapshotChanged)
	{
		NetworkSessionSnapshot.ConnectedPlayerCount = ConnectedPlayers;
		NetworkSessionSnapshot.bServerUsesIris = bUsesIris;
		++NetworkSessionSnapshot.Revision;
		ForceNetUpdate();
		OnNetworkSessionSnapshotChanged.Broadcast(NetworkSessionSnapshot);

		UE_LOG(
			LogRPGNetwork,
			Display,
			TEXT("RPG_NETTEST SERVER_STATE Iris=%d Connected=%d Expected=%d Revision=%d"),
			NetworkSessionSnapshot.bServerUsesIris ? 1 : 0,
			NetworkSessionSnapshot.ConnectedPlayerCount,
			NetworkSessionSnapshot.ExpectedPlayerCount,
			NetworkSessionSnapshot.Revision);
	}

	if (IsIrisRequiredForNetworkTest()
		&& !NetworkSessionSnapshot.bServerUsesIris)
	{
		UE_LOG(
			LogRPGNetwork,
			Error,
			TEXT("RPG_NETTEST IRIS_REQUIRED_BUT_INACTIVE"));
	}

	const bool bReachedExpectedPlayers =
		NetworkSessionSnapshot.ExpectedPlayerCount > 0
		&& NetworkSessionSnapshot.ConnectedPlayerCount >=
			NetworkSessionSnapshot.ExpectedPlayerCount;
	if (!bServerReadyMarkerLogged
		&& bReachedExpectedPlayers
		&& NetworkSessionSnapshot.bServerUsesIris)
	{
		bServerReadyMarkerLogged = true;
		UE_LOG(
			LogRPGNetwork,
			Display,
			TEXT("RPG_NETTEST SERVER_READY Iris=1 Connected=%d Expected=%d Revision=%d"),
			NetworkSessionSnapshot.ConnectedPlayerCount,
			NetworkSessionSnapshot.ExpectedPlayerCount,
			NetworkSessionSnapshot.Revision);
	}
}

bool ARPGGameStateBase::IsLocalGameNetDriverUsingIris() const
{
	const UWorld* World = GetWorld();
	const UNetDriver* NetDriver = World ? World->GetNetDriver() : nullptr;
	return NetDriver && NetDriver->IsUsingIrisReplication();
}

void ARPGGameStateBase::OnRep_NetworkSessionSnapshot()
{
	OnNetworkSessionSnapshotChanged.Broadcast(NetworkSessionSnapshot);
	LogClientSnapshot();
}

void ARPGGameStateBase::LogClientSnapshot() const
{
	const bool bClientUsesIris = IsLocalGameNetDriverUsingIris();
	UE_LOG(
		LogRPGNetwork,
		Display,
		TEXT("RPG_NETTEST CLIENT_STATE ServerIris=%d ClientIris=%d Connected=%d Expected=%d Revision=%d"),
		NetworkSessionSnapshot.bServerUsesIris ? 1 : 0,
		bClientUsesIris ? 1 : 0,
		NetworkSessionSnapshot.ConnectedPlayerCount,
		NetworkSessionSnapshot.ExpectedPlayerCount,
		NetworkSessionSnapshot.Revision);

	const bool bReady =
		NetworkSessionSnapshot.ExpectedPlayerCount > 0
		&& NetworkSessionSnapshot.ConnectedPlayerCount >=
			NetworkSessionSnapshot.ExpectedPlayerCount
		&& NetworkSessionSnapshot.bServerUsesIris
		&& bClientUsesIris;
	if (bReady)
	{
		UE_LOG(
			LogRPGNetwork,
			Display,
			TEXT("RPG_NETTEST CLIENT_READY ServerIris=1 ClientIris=1 Connected=%d Expected=%d Revision=%d"),
			NetworkSessionSnapshot.ConnectedPlayerCount,
			NetworkSessionSnapshot.ExpectedPlayerCount,
			NetworkSessionSnapshot.Revision);
	}
}
