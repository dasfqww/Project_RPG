#include "Tests/RPGDungeonEjectionNetworkProbe.h"

#include "Component/RPGPlayerMovementComponent.h"
#include "DataAsset/StartUpData/DataAsset_StartUpDataBase.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameState/RPGGameStateBase.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Manager/HttpWebManager.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGDungeonEjectionNetworkProbe)

DEFINE_LOG_CATEGORY_STATIC(LogRPGDungeonEjectionNetwork, Log, All);

namespace
{
	bool IsProbeEnabled()
	{
#if !UE_BUILD_SHIPPING
		const bool bLocal = FParse::Param(FCommandLine::Get(), TEXT("RPGNetTestMode"));
		const bool bBackend = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"));
		return FParse::Param(FCommandLine::Get(), TEXT("nosteam"))
			&& ((bBackend && !bLocal) || (!bBackend && bLocal
				&& FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonEjectionNetE2E"))));
#else
		return false;
#endif
	}
	const FString ProbeSession = TEXT("20304050-6070-8090-a0b0-c0d0e0f00102");
	// Enable only the read-only client network-failure observer, never the fixture GameMode/admission path.
	bool IsClientObservationEnabled()
	{
#if !UE_BUILD_SHIPPING
		return IsProbeEnabled() || (FParse::Param(FCommandLine::Get(), TEXT("nosteam")) &&
			FParse::Param(FCommandLine::Get(), TEXT("RPGInventoryRecoveryRevocationE2E")));
#else
		return false;
#endif
	}
	const FString ProbeServer = TEXT("local-network-ejection-fixture");
	bool VerifySimulation(UWorld* World, const TCHAR* Role, int32 PlayerId)
	{
#if !UE_BUILD_SHIPPING && DO_ENABLE_NET_TEST
		const UNetDriver* Driver = World ? World->GetNetDriver() : nullptr;
		int32 Min = 0, Max = 0, Loss = 0;
		FParse::Value(FCommandLine::Get(), TEXT("PktLagMin="), Min);
		FParse::Value(FCommandLine::Get(), TEXT("PktLagMax="), Max);
		FParse::Value(FCommandLine::Get(), TEXT("PktLoss="), Loss);
		if (!Driver || !Driver->IsUsingIrisReplication()) return false;
		const auto& Settings = Driver->PacketSimulationSettings;
		if (Settings.PktLagMin != Min || Settings.PktLagMax != Max || Settings.PktLoss != Loss
			|| Settings.PktLag || Settings.PktOrder || Settings.PktDup
			|| Settings.PktIncomingLagMin || Settings.PktIncomingLagMax || Settings.PktIncomingLoss) return false;
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET SIMULATION Role=%s PlayerId=%d Iris=1 LagMin=%d LagMax=%d Loss=%d"),
			Role, PlayerId, Min, Max, Loss);
		return true;
#else
		return false;
#endif
	}
}

void ARPGDungeonEjectionNetworkPlayer::BeginPlay()
{
#if !UE_BUILD_SHIPPING
	if (IsProbeEnabled())
	{
		// Minimal startup satisfies the real player/ASC initialization contract.
		ProbeStartup = NewObject<UDataAsset_StartUpDataBase>(this);
		CharacterStartUpData = ProbeStartup.Get();
	}
#endif
	Super::BeginPlay();
}

void ARPGDungeonEjectionNetworkPlayer::ProcessEvent(UFunction* Function, void* Parms)
{
#if !UE_BUILD_SHIPPING
	if (IsProbeEnabled() && HasAuthority() && Function
		&& Function->GetFName() == GET_FUNCTION_NAME_CHECKED(ACharacter, ServerMovePacked)
		&& Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer))
	{
		// Observation only. The production ARPGPlayer dispatch decides whether to parse.
		if (IsDungeonGameplayRevoked(this)) ++RevokedMoves;
		else ++NormalMoves;
	}
#endif
	Super::ProcessEvent(Function, Parms);
}

void ARPGDungeonEjectionNetworkController::SetupInputComponent()
{
	// Native movement injection, not authored Enhanced Input coverage.
	APlayerController::SetupInputComponent();
}

void ARPGDungeonEjectionNetworkController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (!IsProbeEnabled() || !IsLocalController() || GetNetMode() != NM_Client) return;
	const auto* SnapshotState = GetWorld()->GetGameState<ARPGGameStateBase>();
	const auto* State = GetPlayerState<ARPGPlayerState>();
	auto* ProbePawn = Cast<ARPGDungeonEjectionNetworkPlayer>(GetPawn());
	if (!SnapshotState || !State || !ProbePawn) return;
	const auto& Snapshot = SnapshotState->GetNetworkSessionSnapshot();
	if (!bReadySent && Snapshot.bServerUsesIris && Snapshot.ExpectedPlayerCount >= 2
		&& Snapshot.ConnectedPlayerCount == Snapshot.ExpectedPlayerCount)
	{
		if (!VerifySimulation(GetWorld(), TEXT("Client"), State->GetPlayerId()))
		{
			UE_LOG(LogRPGDungeonEjectionNetwork, Error, TEXT("RPG_EJECTION_NET FAILED ClientSimulation"));
			return;
		}
		bReadySent = true;
		GetGameInstance()->GetSubsystem<URPGDungeonEjectionNetworkClient>()->PlayerId = State->GetPlayerId();
		ServerConfirmProbeReady();
		UE_LOG(LogRPGDungeonEjectionNetwork, Display, TEXT("RPG_EJECTION_NET CLIENT_READY PlayerId=%d"), State->GetPlayerId());
	}
	if (bReadySent)
	{
		// Small loops avoid running into test-map bounds while continuing CMC RPC traffic.
		const double Angle = GetWorld()->GetTimeSeconds() * 1.5 + State->GetPlayerId();
		ProbePawn->AddMovementInput(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f));
	}
	if (bCompleteRequested && !bCompleteLogged && Snapshot.ConnectedPlayerCount == ExpectedConnectionsAfter)
	{
		bCompleteLogged = true;
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET CLIENT_COMPLETE PlayerId=%d Revoked=%d Connections=%d Iris=1"),
			State->GetPlayerId(), bExpectedRevoked ? 1 : 0, Snapshot.ConnectedPlayerCount);
	}
#endif
}

void ARPGDungeonEjectionNetworkController::ServerConfirmProbeReady_Implementation()
{
#if !UE_BUILD_SHIPPING
	if (IsProbeEnabled() && GetNetMode() == NM_DedicatedServer) bServerReady = true;
#endif
}

void ARPGDungeonEjectionNetworkController::ClientConfirmProbeComplete_Implementation(bool bRevoked, int32 ExpectedConnections)
{
#if !UE_BUILD_SHIPPING
	if (!IsProbeEnabled() || GetNetMode() != NM_Client) return;
	bCompleteRequested = true;
	bExpectedRevoked = bRevoked;
	ExpectedConnectionsAfter = ExpectedConnections;
#endif
}

bool URPGDungeonEjectionNetworkClient::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && IsClientObservationEnabled() && !IsRunningDedicatedServer();
}

void URPGDungeonEjectionNetworkClient::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEngine) GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
}

void URPGDungeonEjectionNetworkClient::Deinitialize()
{
	if (GEngine) GEngine->OnNetworkFailure().RemoveAll(this);
	Super::Deinitialize();
}

void URPGDungeonEjectionNetworkClient::HandleNetworkFailure(UWorld* World, UNetDriver* Driver,
	ENetworkFailure::Type Failure, const FString&)
{
#if !UE_BUILD_SHIPPING
	if (!IsClientObservationEnabled() || !World || World->GetGameInstance() != GetGameInstance() || PlayerId == INDEX_NONE) return;
	const bool bClosed = Failure == ENetworkFailure::ConnectionLost || Failure == ENetworkFailure::FailureReceived;
	UE_LOG(LogRPGDungeonEjectionNetwork, Display,
		TEXT("RPG_EJECTION_NET CLIENT_DISCONNECTED PlayerId=%d EngineFailure=%d ExpectedCloseType=%d"),
		PlayerId, static_cast<int32>(Failure), bClosed ? 1 : 0);
#endif
}

ARPGDungeonEjectionNetworkGameMode::ARPGDungeonEjectionNetworkGameMode()
{
	// Merely selecting this reflected fixture in Shipping cannot bypass admission.
	if (IsProbeEnabled())
	{
		DefaultPawnClass = ARPGDungeonEjectionNetworkPlayer::StaticClass();
		PlayerControllerClass = ARPGDungeonEjectionNetworkController::StaticClass();
		GameStateClass = ARPGGameStateBase::StaticClass();
	}
}

APawn* ARPGDungeonEjectionNetworkGameMode::SpawnDefaultPawnAtTransform_Implementation(
	AController* Controller, const FTransform& Transform)
{
	FTransform ProbeTransform = Transform;
#if !UE_BUILD_SHIPPING
	if (IsProbeEnabled() && GetNetMode() == NM_DedicatedServer)
	{
		// The shared smoke-test map has no rated starts for four native Pawns.
		// Keep normal spawn collision/CMC gravity, but never stack peers at origin.
		ProbeTransform.AddToTranslation(FVector(600.f * SpawnSlot++, 0.f, 300.f));
	}
#endif
	return Super::SpawnDefaultPawnAtTransform_Implementation(Controller, ProbeTransform);
}

void ARPGDungeonEjectionNetworkGameMode::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	bEnabled = IsProbeEnabled() && GetNetMode() == NM_DedicatedServer;
	if (!bEnabled) return;
	FParse::Value(FCommandLine::Get(), TEXT("RPGExpectedClients="), ExpectedPlayers);
	bQuarantine = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonQuarantineNetE2E"));
	bBackendMembership = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonMembershipBackendE2E"));
	int32 Timeout = 90;
	FParse::Value(FCommandLine::Get(), TEXT("RPGMembershipE2ETimeout="), Timeout);
	Deadline = FPlatformTime::Seconds() + FMath::Clamp(Timeout, 60, 600);
	if (ExpectedPlayers < 2 || ExpectedPlayers > 4) { Fail(TEXT("Configuration")); return; }
	if (bBackendMembership)
	{
		const auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		FParse::Value(FCommandLine::Get(), TEXT("RPGAbandonCharacter="), AbandonCharacter);
		FGuid Target;
		if (bQuarantine || !bRequireBackendJoinTicket || !Web || !Web->IsConfiguredForDungeonServer()
			|| !FGuid::ParseExact(AbandonCharacter, EGuidFormats::DigitsWithHyphens, Target) || !Target.IsValid())
		{ Fail(TEXT("BackendConfiguration")); return; }
		BackendSession = Web->GetConfiguredDungeonSessionId(); BackendServer = Web->GetGameServerId();
		if (DungeonMembershipMonitor.GetSessionId() != BackendSession || DungeonMembershipMonitor.GetServerId() != BackendServer)
			Fail(TEXT("ProductionMonitorNotConfigured"));
	}
	else if (!DungeonMembershipMonitor.Configure(ProbeSession, ProbeServer)) Fail(TEXT("Configuration"));
#endif
}

void ARPGDungeonEjectionNetworkGameMode::Fail(const TCHAR* Reason)
{
	UE_LOG(LogRPGDungeonEjectionNetwork, Error, TEXT("RPG_EJECTION_NET FAILED %s"), Reason);
	bEnabled = false;
}

void ARPGDungeonEjectionNetworkGameMode::HandleBackendTargetRevoked(const FString&)
{
#if !UE_BUILD_SHIPPING
	if (!bEnabled || !bBackendMembership || !bAwaitingBackendReceipt || Stage != 1 || Peers.IsEmpty()) return;
	const FPeer& Target = Peers[0];
	if (!IsInGameThread() || !Target.State.IsValid() || !Target.State->IsAuthenticatedIdentityRevoked()
		|| !Target.Pawn.IsValid() || !Target.Controller.IsValid() || !Target.Connection.IsValid()
		|| Target.Controller->GetPawn() != Target.Pawn.Get() || Target.Connection->GetConnectionState() != USOCK_Open
		|| LogoutCount != 0 || !IsDungeonAdmissionAbandoned(BackendSession, Target.CharacterId, Target.SteamId))
	{ Fail(TEXT("BackendRevocationOrdering")); return; }
	// Observe the real notification before Close. Never inject/cache a receipt,
	// invoke revocation/ejection, or mutate the connection in this backend mode.
	UE_LOG(LogRPGDungeonEjectionNetwork, Display,
		TEXT("RPG_EJECTION_NET HTTP_REVOCATION PlayerId=%d Session=%s Server=%s Character=%s ReceiptCache=1 GameThread=1 ConnectionOpen=1 ActorsRetained=1 Logouts=0"),
		Target.PlayerId, *BackendSession, *BackendServer, *Target.CharacterId);
	UE_LOG(LogRPGDungeonEjectionNetwork, Display,
		TEXT("RPG_EJECTION_NET ACTION Scenario=Ejection PlayerId=%d Revoked=1 ActorsRetained=1 Logouts=0"), Target.PlayerId);
	for (FPeer& Peer : Peers)
	{
		if (!Peer.Pawn.IsValid()) { Fail(TEXT("PeerMissingDuringNotification")); return; }
		Peer.MoveBaseline = Peer.Pawn->NormalMoves; Peer.Distance = 0.0; Peer.LastLocation = Peer.Pawn->GetActorLocation();
	}
	PhaseStarted = FPlatformTime::Seconds(); Stage = 2;
#endif
}

void ARPGDungeonEjectionNetworkGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (!bEnabled || Stage == 3) return;
	if (FPlatformTime::Seconds() > Deadline) { Fail(TEXT("Timeout")); return; }
	UNetDriver* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->IsUsingIrisReplication()) { Fail(TEXT("IrisInactive")); return; }
	if (Stage == 0)
	{
		TArray<ARPGDungeonEjectionNetworkController*> Ready;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* Controller = Cast<ARPGDungeonEjectionNetworkController>(It->Get());
			auto* Pawn = Controller ? Cast<ARPGDungeonEjectionNetworkPlayer>(Controller->GetPawn()) : nullptr;
			if (Controller && Pawn && Controller->bServerReady && Pawn->NormalMoves >= 20) Ready.Add(Controller);
		}
		if (Ready.Num() != ExpectedPlayers || Driver->ClientConnections.Num() != ExpectedPlayers) return;
		Ready.Sort([](const auto& A, const auto& B) { return A.PlayerState->GetPlayerId() < B.PlayerState->GetPlayerId(); });
		if (bBackendMembership)
		{
			const int32 TargetIndex = Ready.IndexOfByPredicate([this](const auto* Controller)
				{ const auto* State = Controller->template GetPlayerState<ARPGPlayerState>(); return State && State->GetBackendCharacterId() == AbandonCharacter; });
			if (TargetIndex == INDEX_NONE) { Fail(TEXT("AbandonTargetNotAdmitted")); return; }
			Ready.Swap(0, TargetIndex);
		}
		for (auto* Controller : Ready)
		{
			auto* State = Controller->GetPlayerState<ARPGPlayerState>();
			if (!State || !VerifySimulation(GetWorld(), TEXT("Server"), State->GetPlayerId())) { Fail(TEXT("ServerSimulation")); return; }
			if (!bBackendMembership)
				State->SetAuthenticatedIdentity(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens),
					FString::Printf(TEXT("76561198%09d"), State->GetPlayerId()), ProbeSession);
			if (!State->HasAuthenticatedCharacter()) { Fail(TEXT("FixtureIdentity")); return; }
			if (bBackendMembership && State->GetBackendDungeonSessionId() != BackendSession) { Fail(TEXT("AdmissionScope")); return; }
			FPeer& Peer = Peers.AddDefaulted_GetRef();
			Peer.Controller = Controller; Peer.State = State; Peer.Pawn = CastChecked<ARPGDungeonEjectionNetworkPlayer>(Controller->GetPawn());
			Peer.Connection = Controller->GetNetConnection(); Peer.PlayerId = State->GetPlayerId();
			Peer.CharacterId = State->GetBackendCharacterId(); Peer.SteamId = State->GetAuthenticatedSteamId();
			Peer.MoveBaseline = Peer.Pawn->NormalMoves; Peer.LastLocation = Peer.Pawn->GetActorLocation();
			AddTestParticipant(Controller);
			if (bBackendMembership)
				UE_LOG(LogRPGDungeonEjectionNetwork, Display,
					TEXT("RPG_EJECTION_NET BACKEND_PEER PlayerId=%d Character=%s Steam=%s Session=%s Authenticated=1 TicketIdentity=1"),
					Peer.PlayerId, *Peer.CharacterId, *Peer.SteamId, *BackendSession);
		}
		if (bBackendMembership)
			Peers[0].State->OnCharacterIdentityChanged.AddUniqueDynamic(this, &ThisClass::HandleBackendTargetRevoked);
		PhaseStarted = FPlatformTime::Seconds(); Stage = 1;
		return;
	}
	for (int32 Index = 0; Index < Peers.Num(); ++Index)
	{
		FPeer& Peer = Peers[Index];
		if (Stage == 2 && Index == 0) continue;
		if (!Peer.Controller.IsValid() || !Peer.State.IsValid() || !Peer.Pawn.IsValid() || !Peer.Connection.IsValid()
			|| Peer.Controller->GetPawn() != Peer.Pawn.Get() || Peer.Controller->GetPlayerState<ARPGPlayerState>() != Peer.State.Get()
			|| Peer.Controller->GetNetConnection() != Peer.Connection.Get() || Peer.Connection->GetConnectionState() != USOCK_Open
			|| !Driver->ClientConnections.Contains(Peer.Connection.Get()) || !Peer.State->HasAuthenticatedCharacter()
			|| IsDungeonGameplayRevoked(Peer.Pawn.Get())) { Fail(TEXT("LivePeerChanged")); return; }
		Peer.Distance += FVector::Distance(Peer.LastLocation, Peer.Pawn->GetActorLocation());
		Peer.LastLocation = Peer.Pawn->GetActorLocation();
	}
	if (FPlatformTime::Seconds() - PhaseStarted < 5.0) return;
	FPeer& Target = Peers[0];
	if (Stage == 1)
	{
		if (bBackendMembership && bAwaitingBackendReceipt) return;
		for (const FPeer& Peer : Peers)
			if (Peer.Distance < 200.0 || Peer.Pawn->NormalMoves - Peer.MoveBaseline < 20) return;
		for (const FPeer& Peer : Peers)
		{
			UE_LOG(LogRPGDungeonEjectionNetwork, Display,
				TEXT("RPG_EJECTION_NET BASELINE PlayerId=%d Moves=%d Distance=%.1f"),
				Peer.PlayerId, Peer.Pawn->NormalMoves - Peer.MoveBaseline, Peer.Distance);
		}
		if (bBackendMembership)
		{
			bAwaitingBackendReceipt = true;
			UE_LOG(LogRPGDungeonEjectionNetwork, Display,
				TEXT("RPG_EJECTION_NET BACKEND_READY Session=%s Server=%s Character=%s Players=%d NoIdentitySeed=1 NoReceiptSeed=1"),
				*BackendSession, *BackendServer, *Target.CharacterId, ExpectedPlayers);
			return;
		}
		FRPGDungeonMembershipSnapshotResult Result;
		Result.bSuccess = true; Result.HttpStatus = 200;
		Result.Snapshot.DungeonSessionId = ProbeSession; Result.Snapshot.ServerId = ProbeServer; Result.Snapshot.State = TEXT("InProgress");
		auto& Entry = Result.Snapshot.Abandonments.AddDefaulted_GetRef();
		Entry.SteamId = Target.State->GetAuthenticatedSteamId(); Entry.Receipt.DungeonSessionId = ProbeSession;
		Entry.Receipt.CharacterId = Target.State->GetBackendCharacterId(); Entry.Receipt.AbandonedAt = TEXT("2026-10-11T00:00:00Z");
		Entry.Receipt.SessionStateAtAbandonment = TEXT("InProgress"); Entry.Receipt.bRewardExcluded = true;
		uint64 Serial = 0;
		if (!DungeonMembershipMonitor.BeginRead(Serial) || !DungeonMembershipMonitor.FinishRead(Serial, Result)) { Fail(TEXT("FixtureReceipt")); return; }
		FrozenLocation = Target.Pawn->GetActorLocation();
		const bool bActed = bQuarantine
			? (IsDungeonAdmissionAbandoned(ProbeSession, Entry.Receipt.CharacterId, Entry.SteamId)
				&& Target.State->RevokeAuthenticatedIdentity(ProbeSession, Entry.Receipt.CharacterId, Entry.SteamId))
			: RequestDungeonMemberEjection(Target.Controller.Get());
		if (!bActed || !Target.State.IsValid() || !Target.State->IsAuthenticatedIdentityRevoked()
			|| !Target.Controller.IsValid() || !Target.Pawn.IsValid() || LogoutCount != 0
			|| Target.Connection->GetConnectionState() != (bQuarantine ? USOCK_Open : USOCK_Closed)) { Fail(TEXT("ActionOrdering")); return; }
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET ACTION Scenario=%s PlayerId=%d Revoked=1 ActorsRetained=1 Logouts=0"),
			bQuarantine ? TEXT("Quarantine") : TEXT("Ejection"), Target.PlayerId);
		for (FPeer& Peer : Peers) { Peer.MoveBaseline = Peer.Pawn->NormalMoves; Peer.Distance = 0.0; Peer.LastLocation = Peer.Pawn->GetActorLocation(); }
		PhaseStarted = FPlatformTime::Seconds(); Stage = 2;
		return;
	}
	const int32 Remaining = bQuarantine ? ExpectedPlayers : ExpectedPlayers - 1;
	if (Driver->ClientConnections.Num() != Remaining || GetTestParticipantCount() != Remaining || GetTestRetryStateCount() != Remaining
		|| LogoutCount != (bQuarantine ? 0 : 1)) { Fail(TEXT("ConnectionLogoutMembership")); return; }
	if (bQuarantine)
	{
		if (!Target.Pawn.IsValid() || !Target.State.IsValid() || !Target.Controller.IsValid() || !Target.Connection.IsValid()
			|| Target.Controller->GetPawn() != Target.Pawn.Get() || Target.Controller->GetNetConnection() != Target.Connection.Get()
			|| Target.Connection->GetConnectionState() != USOCK_Open || !Target.State->IsAuthenticatedIdentityRevoked()
			|| !IsDungeonGameplayRevoked(Target.Pawn.Get()) || Target.Pawn->RevokedMoves < 20
			|| Target.Pawn->NormalMoves != Target.MoveBaseline || Target.Pawn->GetCharacterMovement()->MovementMode != MOVE_None
			|| !Target.Pawn->GetActorLocation().Equals(FrozenLocation, 0.01f)) { Fail(TEXT("QueuedMoveQuarantine")); return; }
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET QUARANTINE PlayerId=%d PackedRpcs=%d NormalAfter=0 Frozen=1 ConnectionOpen=1"),
			Target.PlayerId, Target.Pawn->RevokedMoves);
		Target.Controller->ClientConfirmProbeComplete(true, Remaining);
	}
	else
	{
		if (Target.Pawn.IsValid() || Target.Controller.IsValid() || (Target.Connection.IsValid() && Driver->ClientConnections.Contains(Target.Connection.Get())))
		{ Fail(TEXT("EngineCleanup")); return; }
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET CLEANUP PlayerId=%d Logouts=1 PawnRemoved=1 ControllerRemoved=1 Remaining=%d RetryStates=%d"),
			Target.PlayerId, Remaining, GetTestRetryStateCount());
	}
	for (int32 Index = 1; Index < Peers.Num(); ++Index)
	{
		FPeer& Peer = Peers[Index];
		const int32 Moves = Peer.Pawn->NormalMoves - Peer.MoveBaseline;
		if (Moves < 20 || Peer.Distance < 200.0) { Fail(TEXT("PeerMovementStopped")); return; }
		UE_LOG(LogRPGDungeonEjectionNetwork, Display,
			TEXT("RPG_EJECTION_NET PEER_PRESERVED PlayerId=%d Moves=%d Distance=%.1f Authenticated=1 SameConnection=1"),
			Peer.PlayerId, Moves, Peer.Distance);
		Peer.Controller->ClientConfirmProbeComplete(false, Remaining);
	}
	UE_LOG(LogRPGDungeonEjectionNetwork, Display, TEXT("RPG_EJECTION_NET SERVER_COMPLETE Scenario=%s Players=%d Target=%d Remaining=%d Iris=1 SyntheticReceipt=%d"),
		bQuarantine ? TEXT("Quarantine") : TEXT("Ejection"), ExpectedPlayers, Target.PlayerId, Remaining, bBackendMembership ? 0 : 1);
	Stage = 3;
#endif
}
