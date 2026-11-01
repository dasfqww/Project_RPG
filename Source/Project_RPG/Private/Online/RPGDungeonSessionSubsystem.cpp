#include "Online/RPGDungeonSessionSubsystem.h"

#include "GameFramework/PlayerController.h"
#include "Controller/RPGPlayerController.h"
#include "GameState/RPGBossBattleGameState.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace
{
	bool IsActiveDungeonState(const FString& State)
	{
		return State.Equals(TEXT("Waiting"), ESearchCase::CaseSensitive)
			|| State.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)
			|| State.Equals(
				TEXT("InProgress"),
				ESearchCase::CaseSensitive)
			|| State.Equals(TEXT("SettlementPending"), ESearchCase::CaseSensitive);
	}

	bool IsConnectableDungeonState(const FString& State)
	{
		return State.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)
			|| State.Equals(
				TEXT("InProgress"),
				ESearchCase::CaseSensitive);
	}
}

bool URPGDungeonSessionSubsystem::ShouldCreateSubsystem(
	UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer)
		&& !IsRunningDedicatedServer();
}

void URPGDungeonSessionSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	bAbandonmentTestingEnabled = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonAbandonmentTesting"))
		&& FParse::Param(FCommandLine::Get(), TEXT("nosteam"));
	bResultAbandonmentTestingEnabled = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonResultAbandonmentTesting"));
	bGameplayAbandonmentTestingEnabled = FParse::Param(FCommandLine::Get(), TEXT("RPGDungeonGameplayAbandonmentTesting"));
#endif
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandleLobbyMapLoaded);
	if (GEngine)
	{
		GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleLobbyTravelFailure);
		GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleDungeonNetworkFailure);
	}
	Collection.InitializeDependency<UHttpWebManager>();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		WebManager = GameInstance->GetSubsystem<UHttpWebManager>();
	}

	if (!WebManager)
	{
		SetFlowState(
			ERPGDungeonClientFlowState::Error,
			TEXT("http_manager_unavailable"));
		return;
	}

	WebManager->OnDungeonSessionUpdated.RemoveDynamic(
		this,
		&ThisClass::HandleDungeonSessionUpdated);
	WebManager->OnDungeonSessionUpdated.AddDynamic(
		this,
		&ThisClass::HandleDungeonSessionUpdated);
	WebManager->OnJoinTicketResult.RemoveAll(this);
	WebManager->OnJoinTicketResult.AddUObject(
		this,
		&ThisClass::HandleJoinTicketIssued);
	WebManager->OnDungeonAbandonmentCompleted.AddUniqueDynamic(this, &ThisClass::HandleDungeonAbandonmentCompleted);
}

void URPGDungeonSessionSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	CancelLocalRecovery();
	if (GEngine)
	{
		GEngine->OnTravelFailure().RemoveAll(this);
		GEngine->OnNetworkFailure().RemoveAll(this);
	}
	if (WebManager)
	{
		WebManager->OnDungeonSessionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleDungeonSessionUpdated);
		WebManager->OnJoinTicketResult.RemoveAll(this);
		WebManager->OnDungeonAbandonmentCompleted.RemoveDynamic(this, &ThisClass::HandleDungeonAbandonmentCompleted);
	}

	PendingOperation = EPendingOperation::None;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();
	PendingReturnSessionId.Reset();
	ClearAbandonmentIdentity();
	ClearRejectedDungeonAdmission();
	WebManager = nullptr;
	OnFlowStateChanged.Clear();
	OnDungeonSessionChanged.Clear();
	OnDungeonTravelStarted.Clear();
	OnLobbyTravelStarted.Clear();
	Super::Deinitialize();
}

bool URPGDungeonSessionSubsystem::SelectCharacter(
	const FString& CharacterId)
{
	if (RejectUnresolvedAbandonment()) return false;
	const FString NormalizedCharacterId = CharacterId.TrimStartAndEnd();
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (NormalizedCharacterId.IsEmpty()
		|| (HasActiveDungeonSession()
			&& !SelectedCharacterId.Equals(
				NormalizedCharacterId,
				ESearchCase::CaseSensitive)))
	{
		const TCHAR* ErrorCode = NormalizedCharacterId.IsEmpty()
				? TEXT("invalid_character_id")
				: TEXT("character_change_not_allowed");
		FailOperation(ErrorCode);
		return false;
	}

	if (HasUnresolvedDeparture() && PendingDepartureCharacter != NormalizedCharacterId)
	{
		RejectRequest(TEXT("departure_recovery_character_locked")); return false;
	}
	SelectedCharacterId = NormalizedCharacterId;
	if (!RestoreDepartureCommand()) return false;
	LastErrorCode.Reset();
	if (FlowState == ERPGDungeonClientFlowState::Error)
	{
		SetFlowState(
			HasActiveDungeonSession()
				? ERPGDungeonClientFlowState::InSession
				: ERPGDungeonClientFlowState::Idle);
	}
	return true;
}

bool URPGDungeonSessionSubsystem::CreateDungeonSession(
	const FString& DungeonId,
	const FString& Difficulty)
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (!ValidateSelectedCharacter()
		|| DungeonId.TrimStartAndEnd().IsEmpty()
		|| Difficulty.TrimStartAndEnd().IsEmpty())
	{
		if (LastErrorCode.IsEmpty())
		{
			FailOperation(TEXT("invalid_dungeon_selection"));
		}
		return false;
	}

	if (!RestoreDepartureCommand()) return false;
	if (HasUnresolvedDeparture()) { RejectRequest(TEXT("departure_result_unconfirmed")); return false; }
	if (HasActiveDungeonSession())
	{
		FailOperation(TEXT("active_dungeon_session_exists"));
		return false;
	}

	if (!BeginOperation(
		EPendingOperation::Create,
		ERPGDungeonClientFlowState::CreatingSession))
	{
		return false;
	}

	WebManager->CreateDungeonSession(
		SelectedCharacterId,
		DungeonId,
		Difficulty);
	return true;
}

bool URPGDungeonSessionSubsystem::JoinDungeonSession(
	const FString& DungeonSessionId)
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	const FString NormalizedSessionId =
		DungeonSessionId.TrimStartAndEnd();
	if (!ValidateSelectedCharacter() || NormalizedSessionId.IsEmpty())
	{
		if (LastErrorCode.IsEmpty())
		{
			FailOperation(TEXT("invalid_dungeon_session_id"));
		}
		return false;
	}

	if (!RestoreDepartureCommand()) return false;
	if (HasUnresolvedDeparture()) { RejectRequest(TEXT("departure_result_unconfirmed")); return false; }
	if (HasActiveDungeonSession())
	{
		FailOperation(TEXT("active_dungeon_session_exists"));
		return false;
	}

	if (!BeginOperation(
		EPendingOperation::Join,
		ERPGDungeonClientFlowState::JoiningSession))
	{
		return false;
	}

	WebManager->JoinDungeonSession(
		NormalizedSessionId,
		SelectedCharacterId);
	return true;
}

bool URPGDungeonSessionSubsystem::ResumeDungeonSession()
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (!ValidateSelectedCharacter())
	{
		return false;
	}
	if (!RestoreDepartureCommand()) return false;

	if (HasActiveDungeonSession())
	{
		// A rejected instance may no longer authorize GET-by-session. Recheck the
		// selected character's own membership before offering a new dungeon.
		if (!GetDungeonAdmissionBlockReason().IsEmpty()) return RefreshDungeonSession();
		FailOperation(TEXT("active_dungeon_session_exists"));
		return false;
	}

	if (!BeginOperation(
		EPendingOperation::Resume,
		ERPGDungeonClientFlowState::ResumingSession))
	{
		return false;
	}

	WebManager->LoadActiveDungeonSession(SelectedCharacterId);
	return true;
}

bool URPGDungeonSessionSubsystem::RefreshDungeonSession()
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (!WebManager
		|| CurrentDungeonSession.DungeonSessionId.IsEmpty())
	{
		FailOperation(TEXT("no_dungeon_session"));
		return false;
	}

	const bool bRecheckMembership = !GetDungeonAdmissionBlockReason().IsEmpty();
	if (!BeginOperation(
		bRecheckMembership ? EPendingOperation::RecheckMembership : EPendingOperation::Refresh,
		ERPGDungeonClientFlowState::RefreshingSession))
	{
		return false;
	}

	if (bRecheckMembership) WebManager->LoadActiveDungeonSession(SelectedCharacterId);
	else WebManager->LoadDungeonSession(CurrentDungeonSession.DungeonSessionId);
	return true;
}

bool URPGDungeonSessionSubsystem::LeaveDungeonSession()
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (!ValidateSelectedCharacter()
		|| CurrentDungeonSession.DungeonSessionId.IsEmpty())
	{
		if (LastErrorCode.IsEmpty())
		{
			FailOperation(TEXT("no_dungeon_session"));
		}
		return false;
	}

	if (!BeginOperation(
		EPendingOperation::Leave,
		ERPGDungeonClientFlowState::LeavingSession))
	{
		return false;
	}

	WebManager->LeaveDungeonSession(
		CurrentDungeonSession.DungeonSessionId,
		SelectedCharacterId);
	return true;
}

bool URPGDungeonSessionSubsystem::CanSetDeparture(bool bCancel) const
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone || World->bIsTearingDown
		|| UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) != LobbyMapPackage
		|| !WebManager || !WebManager->HasAccessToken() || IsRequestInFlight() || HasUnresolvedDungeonAbandonment()
		|| CurrentDungeonSession.State != TEXT("Waiting") || !CurrentSessionContainsSelectedCharacter()
		|| CurrentDungeonSession.LeaderCharacterId != SelectedCharacterId || CurrentDungeonSession.LobbyRevision < 1) return false;
	if (!PendingDepartureRequest.IsEmpty())
		return PendingDepartureSession == CurrentDungeonSession.DungeonSessionId && PendingDepartureCharacter == SelectedCharacterId
			&& IsDepartureScopeCurrent() && bPendingDepartureCancel == bCancel;
	return !bDepartureRecoveryBlocked && (bCancel ? !CurrentDungeonSession.DepartureRequestedAt.IsEmpty() : CurrentDungeonSession.DepartureRequestedAt.IsEmpty());
}

bool URPGDungeonSessionSubsystem::SetDeparture(bool bCancel)
{
	if (!RestoreDepartureCommand()) return false;
	if (!CanSetDeparture(bCancel)) { RejectRequest(TEXT("dungeon_departure_not_available")); return false; }
	if (PendingDepartureRequest.IsEmpty())
	{
		PendingDepartureSession = CurrentDungeonSession.DungeonSessionId;
		PendingDepartureCharacter = SelectedCharacterId;
		PendingDepartureSteam = WebManager->GetAuthenticatedSteamId();
		PendingDepartureBackend = WebManager->GetApiUrl();
		PendingDepartureRequest = bCancel ? CurrentDungeonSession.DepartureRequestId : FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		PendingDepartureRevision = bCancel ? CurrentDungeonSession.DepartureRequestRevision : CurrentDungeonSession.LobbyRevision;
		bPendingDepartureCancel = bCancel;
	}
	if (!PersistDepartureCommand()) return false;
	if (!BeginOperation(EPendingOperation::Departure, ERPGDungeonClientFlowState::RefreshingSession)) return false;
	WebManager->SetDungeonDeparture(PendingDepartureSession, PendingDepartureCharacter, PendingDepartureRequest, PendingDepartureRevision, bCancel);
	return true;
}

bool URPGDungeonSessionSubsystem::RetryDeparture()
{
	if (!HasUnresolvedDeparture()) { RejectRequest(TEXT("no_pending_departure")); return false; }
	return SetDeparture(bPendingDepartureCancel);
}

bool URPGDungeonSessionSubsystem::IsAbandonmentTestingEnabled() const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return bAbandonmentTestingEnabled;
#endif
}

bool URPGDungeonSessionSubsystem::CanAbandonDungeonSession() const
{
	if (!IsAbandonmentTestingEnabled() || IsRequestInFlight() || !WebManager || !WebManager->HasAccessToken()
		|| SelectedCharacterId.IsEmpty() || CurrentDungeonSession.DungeonSessionId.IsEmpty()
		|| !FRPGBackendPlayerAuthState::IsSteamId(WebManager->GetAuthenticatedSteamId())) return false;
	if (HasUnresolvedDungeonAbandonment())
		return AbandonmentSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)
			&& AbandonmentCharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive)
			&& AbandonmentSteamId == WebManager->GetAuthenticatedSteamId();
	return IsConnectableDungeonState(CurrentDungeonSession.State) && CurrentSessionContainsSelectedCharacter();
}

bool URPGDungeonSessionSubsystem::IsResultAbandonmentTestingEnabled() const
{
	return IsAbandonmentTestingEnabled() && bResultAbandonmentTestingEnabled;
}

bool URPGDungeonSessionSubsystem::CanQueueDungeonAbandonmentFromResult(APlayerController* PlayerController) const
{
	return CanQueueDungeonAbandonmentFromBattle(PlayerController, true);
}

bool URPGDungeonSessionSubsystem::IsGameplayAbandonmentTestingEnabled() const
{
	return IsAbandonmentTestingEnabled() && bGameplayAbandonmentTestingEnabled;
}

bool URPGDungeonSessionSubsystem::CanQueueDungeonAbandonmentFromGameplay(APlayerController* PlayerController) const
{
	return CanQueueDungeonAbandonmentFromBattle(PlayerController, false);
}

bool URPGDungeonSessionSubsystem::CanQueueDungeonAbandonmentFromBattle(APlayerController* PlayerController, const bool bFromResult) const
{
	if (!(bFromResult ? IsResultAbandonmentTestingEnabled() : IsGameplayAbandonmentTestingEnabled()) || !IsValidLocalController(PlayerController)
		|| PlayerController->GetWorld()->GetNetMode() != NM_Client || HasUnresolvedDungeonAbandonment()
		|| !CanAbandonDungeonSession() || !IsLobbyMapAvailable()
		// The admission cache may still say Loading while the authoritative battle
		// is already running. Consent is not permission to commit an opt-out; the
		// lobby POST must still pass the backend's current-state/settlement lock.
		|| !(bFromResult ? CurrentDungeonSession.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
			: IsConnectableDungeonState(CurrentDungeonSession.State))
		|| IsDungeonGameplayRevoked(PlayerController) || IsDungeonGameplayRevoked(PlayerController->GetPawn())) return false;
	const auto* Controller = Cast<ARPGPlayerController>(PlayerController);
	const auto* State = PlayerController->GetPlayerState<ARPGPlayerState>();
	const auto* Battle = PlayerController->GetWorld()->GetGameState<ARPGBossBattleGameState>();
	const auto* Authentication = GetGameInstance()->GetSubsystem<URPGSteamAuthSubsystem>();
	if (!Controller || Controller->IsBossBattleRestartPending() || !State || !State->HasAuthenticatedCharacter()
		|| !Battle || Battle->GetBattleSnapshot().BattleState != (bFromResult ? EBossBattleState::Defeated : EBossBattleState::InProgress)
		|| (Authentication && Authentication->IsLoginRequestInFlight())
		|| !State->GetAuthenticatedSteamId().Equals(WebManager->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive)
		|| !State->GetBackendCharacterId().Equals(SelectedCharacterId, ESearchCase::CaseSensitive)
		|| !State->GetBackendDungeonSessionId().Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)) return false;
	const auto Notice = State->GetRewardDeliveryNotice();
	return !(Notice.bApplied && Notice.DungeonSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive));
}

bool URPGDungeonSessionSubsystem::QueueDungeonAbandonmentFromResult(APlayerController* PlayerController,
	const int32 ExpectedBattleRevision, const FString& ExpectedSessionId,
	const FString& ExpectedCharacterId, const FString& ExpectedSteamId)
{
	return QueueDungeonAbandonmentFromBattle(PlayerController, ExpectedBattleRevision, ExpectedSessionId, ExpectedCharacterId, ExpectedSteamId, true);
}

bool URPGDungeonSessionSubsystem::QueueDungeonAbandonmentFromGameplay(APlayerController* PlayerController,
	const int32 ExpectedBattleRevision, const FString& ExpectedSessionId,
	const FString& ExpectedCharacterId, const FString& ExpectedSteamId)
{
	return QueueDungeonAbandonmentFromBattle(PlayerController, ExpectedBattleRevision, ExpectedSessionId, ExpectedCharacterId, ExpectedSteamId, false);
}

bool URPGDungeonSessionSubsystem::QueueDungeonAbandonmentFromBattle(APlayerController* PlayerController,
	const int32 ExpectedBattleRevision, const FString& ExpectedSessionId,
	const FString& ExpectedCharacterId, const FString& ExpectedSteamId, const bool bFromResult)
{
	FGuid SessionGuid, CharacterGuid;
	if (!CanQueueDungeonAbandonmentFromBattle(PlayerController, bFromResult) || ExpectedBattleRevision < 0
		|| PlayerController->GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot().Revision != ExpectedBattleRevision
		|| !ExpectedSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)
		|| !ExpectedCharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive)
		|| !ExpectedSteamId.Equals(WebManager->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive)
		|| !FGuid::ParseExact(ExpectedSessionId, EGuidFormats::DigitsWithHyphens, SessionGuid) || !SessionGuid.IsValid()
		|| !FGuid::ParseExact(ExpectedCharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid())
	{
		RejectRequest(bFromResult ? TEXT("dungeon_abandonment_result_context_changed") : TEXT("dungeon_abandonment_gameplay_context_changed"));
		return false;
	}
	// Pin consent before travel. This is NOT an HTTP acknowledgement or reward exclusion.
	AbandonmentSessionId = ExpectedSessionId;
	AbandonmentCharacterId = ExpectedCharacterId;
	AbandonmentSteamId = ExpectedSteamId;
	bAbandonmentQueuedForLobby = true;
	if (QueueLocalRecovery(PlayerController->GetWorld(), TEXT("dungeon_abandonment_queued_for_lobby"), PlayerController)) return true;
	ClearAbandonmentIdentity(); // No POST has been submitted on this failure path.
	return false;
}

bool URPGDungeonSessionSubsystem::BeginAbandonment(APlayerController* PlayerController,
	const FString& ExpectedSessionId, const FString& ExpectedCharacterId, const FString& ExpectedSteamId)
{
	// HTTP mutation remains restricted to a disconnected local lobby, including
	// consent captured at defeat. Never tear down a listen host's whole world.
	if (!IsAbandonmentTestingEnabled()) { RejectRequest(TEXT("dungeon_abandonment_disabled")); return false; }
	if (IsRequestInFlight()) { RejectRequest(TEXT("request_in_flight")); return false; }
	if (!IsValidLocalController(PlayerController)
		|| PlayerController->GetWorld()->GetNetMode() != NM_Standalone
		|| !PlayerController->GetWorld()->GetOutermost()->GetName().Equals(LobbyMapPackage, ESearchCase::CaseSensitive))
	{
		RejectRequest(TEXT("dungeon_abandonment_requires_lobby"));
		return false;
	}
	FGuid SessionGuid, CharacterGuid;
	if (!CanAbandonDungeonSession() || !ExpectedSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)
		|| !ExpectedCharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive) || ExpectedSteamId != WebManager->GetAuthenticatedSteamId()
		|| !FGuid::ParseExact(ExpectedSessionId, EGuidFormats::DigitsWithHyphens, SessionGuid) || !SessionGuid.IsValid()
		|| !FGuid::ParseExact(ExpectedCharacterId, EGuidFormats::DigitsWithHyphens, CharacterGuid) || !CharacterGuid.IsValid())
	{
		RejectRequest(TEXT("dungeon_abandonment_identity_or_state_mismatch"));
		return false;
	}
	AbandonmentSessionId = ExpectedSessionId;
	AbandonmentCharacterId = ExpectedCharacterId;
	AbandonmentSteamId = ExpectedSteamId;
	bAbandonmentQueuedForLobby = false;
	WebManager->InvalidatePlayerDungeonRequests();
	return BeginOperation(EPendingOperation::Abandon, ERPGDungeonClientFlowState::AbandoningSession);
}

bool URPGDungeonSessionSubsystem::AbandonDungeonSession(APlayerController* PlayerController,
	const FString& ExpectedSessionId, const FString& ExpectedCharacterId, const FString& ExpectedSteamId)
{
	if (!BeginAbandonment(PlayerController, ExpectedSessionId, ExpectedCharacterId, ExpectedSteamId)) return false;
	WebManager->AbandonDungeonSession(ExpectedSessionId, ExpectedCharacterId, ExpectedSteamId);
	return true;
}

void URPGDungeonSessionSubsystem::HandleDungeonAbandonmentCompleted(const FRPGDungeonAbandonmentReceipt& Receipt,
	bool bSuccess, const FString& ErrorCode)
{
	if (PendingOperation != EPendingOperation::Abandon || !HasUnresolvedDungeonAbandonment()
		|| !Receipt.DungeonSessionId.Equals(AbandonmentSessionId, ESearchCase::CaseSensitive)
		|| !Receipt.CharacterId.Equals(AbandonmentCharacterId, ESearchCase::CaseSensitive)) return;
	if (!WebManager || AbandonmentSteamId != WebManager->GetAuthenticatedSteamId()
		|| !AbandonmentSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)
		|| !AbandonmentCharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("dungeon_abandonment_identity_or_state_mismatch"));
		return;
	}
	if (!bSuccess || !Receipt.IsValidFor(AbandonmentSessionId, AbandonmentCharacterId))
	{
		const bool bDefinitive = !bSuccess && ErrorCode.Equals(TEXT("dungeon_abandonment_not_available"), ESearchCase::CaseSensitive);
		if (bDefinitive) ClearAbandonmentIdentity();
		FailOperation(bDefinitive ? ErrorCode : TEXT("dungeon_abandonment_unconfirmed"));
		return;
	}
	// The receipt acknowledges only our opt-out. Do NOT apply its historical state
	// to the remaining party, and do not clear selection/authentication.
	PendingOperation = EPendingOperation::None;
	CurrentDungeonSession = FRPGDungeonSession();
	ClearAbandonmentIdentity();
	SetFlowState(ERPGDungeonClientFlowState::Idle, TEXT("dungeon_abandoned"));
	BroadcastSessionChanged();
}

void URPGDungeonSessionSubsystem::ClearAbandonmentIdentity()
{
	AbandonmentSessionId.Reset(); AbandonmentCharacterId.Reset(); AbandonmentSteamId.Reset();
	bAbandonmentQueuedForLobby = false;
}

bool URPGDungeonSessionSubsystem::RejectUnresolvedAbandonment()
{
	if (!HasUnresolvedDungeonAbandonment()) return false;
	RejectRequest(TEXT("dungeon_abandonment_unconfirmed"));
	return true;
}

bool URPGDungeonSessionSubsystem::ConnectToDungeon(
	APlayerController* PlayerController,
	const FString& ServerAddress)
{
	if (HasUnresolvedDeparture() || bDepartureRecoveryBlocked) { RejectRequest(TEXT("departure_result_unconfirmed")); return false; }
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	const FString AdmissionBlockReason = GetDungeonAdmissionBlockReason();
	if (!AdmissionBlockReason.IsEmpty())
	{
		FailOperation(AdmissionBlockReason);
		return false;
	}

	const FString NormalizedAddress = ServerAddress.TrimStartAndEnd();
	if (!ValidateSelectedCharacter()
		|| !IsValid(PlayerController)
		|| NormalizedAddress.IsEmpty()
		|| CurrentDungeonSession.DungeonSessionId.IsEmpty()
		|| CurrentDungeonSession.ServerId.IsEmpty()
		|| !IsConnectableDungeonState(CurrentDungeonSession.State)
		|| !CurrentSessionContainsSelectedCharacter())
	{
		if (LastErrorCode.IsEmpty())
		{
			FailOperation(TEXT("dungeon_not_ready_to_connect"));
		}
		return false;
	}

	// A BP caller may assert the destination, never replace the authenticated
	// assignment with an arbitrary endpoint or a URL carrying travel options.
	const FString AssignedAddress = CurrentDungeonSession.ServerAddress.TrimStartAndEnd();
	if (AssignedAddress.IsEmpty() || !NormalizedAddress.Equals(AssignedAddress, ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("dungeon_server_address_mismatch"));
		return false;
	}

	if (!BeginOperation(
		EPendingOperation::Connect,
		ERPGDungeonClientFlowState::RequestingJoinTicket))
	{
		return false;
	}

	PendingPlayerController = PlayerController;
	PendingServerAddress = NormalizedAddress;
	WebManager->RequestJoinTicket(
		SelectedCharacterId,
		CurrentDungeonSession.DungeonSessionId);
	return true;
}

bool URPGDungeonSessionSubsystem::IsValidLocalController(const APlayerController* PlayerController) const
{
	return IsValid(PlayerController) && PlayerController->IsLocalController()
		&& PlayerController->GetWorld()
		&& (PlayerController->GetWorld()->GetNetMode() == NM_Client
			|| PlayerController->GetWorld()->GetNetMode() == NM_Standalone)
		&& PlayerController->GetWorld()->GetGameInstance() == GetGameInstance();
}

bool URPGDungeonSessionSubsystem::ReturnToLobby(APlayerController* PlayerController)
{
	if (RejectUnresolvedAbandonment()) return false;
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}
	if (!IsValidLocalController(PlayerController) || SelectedCharacterId.IsEmpty()
		|| CurrentDungeonSession.DungeonSessionId.IsEmpty() || !CurrentSessionContainsSelectedCharacter())
	{
		FailOperation(TEXT("dungeon_return_not_available"));
		return false;
	}
	// Reject URL options, remote destinations, short names and missing cooked packages.
	if (!IsLobbyMapAvailable())
	{
		FailOperation(TEXT("lobby_map_unavailable"));
		return false;
	}
	if (!BeginOperation(EPendingOperation::Return, ERPGDungeonClientFlowState::RefreshingSession))
	{
		return false;
	}
	PendingPlayerController = PlayerController;
	PendingReturnSessionId = CurrentDungeonSession.DungeonSessionId;
	WebManager->LoadDungeonSession(PendingReturnSessionId);
	return true;
}

bool URPGDungeonSessionSubsystem::IsLobbyMapAvailable() const
{
	return LobbyMapPackage.StartsWith(TEXT("/Game/"))
		&& FPackageName::IsValidLongPackageName(LobbyMapPackage)
		&& FPackageName::DoesPackageExist(LobbyMapPackage);
}

bool URPGDungeonSessionSubsystem::DisconnectToLobby(APlayerController* PlayerController)
{
	if (!IsValidLocalController(PlayerController))
	{
		RejectRequest(TEXT("dungeon_disconnect_not_available"));
		return false;
	}
	return QueueLocalRecovery(PlayerController->GetWorld(), TEXT("dungeon_connection_closed"), PlayerController);
}

bool URPGDungeonSessionSubsystem::QueueLocalRecovery(UWorld* World, const FString& ReasonCode, APlayerController* PlannedController)
{
	if (FlowState == ERPGDungeonClientFlowState::ReturningToLobby) return false;
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance()
		|| (World->GetNetMode() != NM_Client && World->GetNetMode() != NM_Standalone)
		|| CurrentDungeonSession.DungeonSessionId.IsEmpty() || SelectedCharacterId.IsEmpty()
		|| !CurrentSessionContainsSelectedCharacter())
	{
		RejectRequest(TEXT("dungeon_disconnect_not_available"));
		return false;
	}
	if (UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()).Equals(LobbyMapPackage, ESearchCase::CaseSensitive))
	{
		// Admission can fail before leaving the lobby. Release its Connecting
		// guard without reloading the map or changing authoritative membership.
		if (FlowState == ERPGDungeonClientFlowState::Connecting)
		{
			if (WebManager) WebManager->InvalidatePlayerDungeonRequests();
			FailOperation(ReasonCode);
			return true;
		}
		RejectRequest(TEXT("dungeon_disconnect_not_available"));
		return false;
	}
	if (!IsLobbyMapAvailable())
	{
		RejectRequest(TEXT("lobby_map_unavailable"));
		return false;
	}
	// Do not reset membership or mark the session finished. A submitted backend
	// mutation may still complete; the lobby must re-read authoritative state.
	if (WebManager) WebManager->InvalidatePlayerDungeonRequests();
	PendingOperation = EPendingOperation::None;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();
	PendingReturnSessionId.Reset();
	LocalRecoveryWorld = World;
	LocalRecoveryReason = ReasonCode;
	SetFlowState(ERPGDungeonClientFlowState::ReturningToLobby);
	// Send the intent on the current owned actor channel before local travel can
	// destroy it. Never send this RPC for automatic network-failure recovery.
	UNetDriver* Driver = World->GetNetDriver();
	ARPGPlayerController* Controller = Cast<ARPGPlayerController>(PlannedController);
	if (World->GetNetMode() == NM_Client && IsValid(Driver)
		&& IsValid(Driver->ServerConnection) && Driver->ServerConnection->GetConnectionState() == USOCK_Open
		&& IsValid(Controller) && Controller->IsLocalController() && Controller->GetWorld() == World
		&& Driver->ServerConnection->PlayerController == Controller)
	{
		PlannedDisconnectConnection = Driver->ServerConnection;
		PlannedDisconnectDeadline = FPlatformTime::Seconds() + 2.0;
		Controller->ServerRequestDungeonLobbyDisconnect();
	}
	// Engine failure handlers also schedule ?closed/default-map travel. Defer to
	// the next core tick so their callback ordering cannot overwrite this route.
	LocalRecoveryTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickLocalRecovery));
	return true;
}

bool URPGDungeonSessionSubsystem::TickLocalRecovery(float)
{
	UWorld* World = LocalRecoveryWorld.Get();
	if (FlowState != ERPGDungeonClientFlowState::ReturningToLobby)
	{
		CancelLocalRecovery();
		return false;
	}
	if (PlannedDisconnectDeadline > 0.0)
	{
		UNetDriver* Driver = IsValid(World) ? World->GetNetDriver() : nullptr;
		if (!IsValid(World) || World->GetGameInstance() != GetGameInstance()
			|| (IsValid(Driver) && Driver->ServerConnection
				&& Driver->ServerConnection != PlannedDisconnectConnection.Get()))
		{
			// A delayed local return must not tear down a replacement connection.
			CancelLocalRecovery();
			FailOperation(TEXT("lobby_recovery_connection_changed"));
			return false;
		}
	}
	if (UNetConnection* Connection = PlannedDisconnectConnection.Get();
		IsValid(World) && Connection && Connection->GetConnectionState() == USOCK_Open
		&& FPlatformTime::Seconds() < PlannedDisconnectDeadline)
	{
		// Bounded wait, not a permanent client-side exemption. An unreachable
		// server still permits local emergency recovery after the deadline.
		UNetDriver* Driver = World->GetNetDriver();
		if (IsValid(Driver) && Driver == Connection->GetDriver() && Driver->ServerConnection == Connection)
			return true;
	}
	LocalRecoveryTicker.Reset();
	LocalRecoveryWorld.Reset();
	PlannedDisconnectConnection.Reset();
	PlannedDisconnectDeadline = 0.0;
	if (!World && GetGameInstance()) World = GetGameInstance()->GetWorld();
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance()
		|| (World->GetNetMode() != NM_Client && World->GetNetMode() != NM_Standalone)
		|| !IsLobbyMapAvailable())
	{
		FailOperation(TEXT("lobby_recovery_unavailable"));
		return false;
	}
	UGameplayStatics::OpenLevel(World, FName(*LobbyMapPackage), true);
	OnLobbyTravelStarted.Broadcast(LobbyMapPackage);
	return false;
}

void URPGDungeonSessionSubsystem::CancelLocalRecovery()
{
	FTSTicker::GetCoreTicker().RemoveTicker(LocalRecoveryTicker);
	LocalRecoveryTicker.Reset();
	LocalRecoveryWorld.Reset();
	LocalRecoveryReason.Reset();
	PlannedDisconnectConnection.Reset();
	PlannedDisconnectDeadline = 0.0;
}

void URPGDungeonSessionSubsystem::HandleDungeonNetworkFailure(UWorld* World, UNetDriver* NetDriver,
	ENetworkFailure::Type FailureType, const FString& Error)
{
	(void)FailureType;
	(void)Error; // Raw engine errors can contain addresses/tickets. Never forward them.
	if (!NetDriver || NetDriver->GetNetMode() != NM_Client) return;
	bool bPendingDriver = false;
	if (NetDriver->NetDriverName == NAME_PendingNetDriver && FlowState == ERPGDungeonClientFlowState::Connecting && GEngine)
	{
		// UE broadcasts PendingConnectionFailure with a null World, before
		// cancelling PendingNetGame. Resolve only the exact active driver in
		// this GameInstance; a name match alone can select another PIE client.
		const FWorldContext* Context = GEngine->GetWorldContextFromPendingNetGameNetDriver(NetDriver);
		if (!Context || Context->OwningGameInstance != GetGameInstance()
			|| (World && World != Context->World())) return;
		World = Context->World();
		bPendingDriver = true;
	}
	if (!World || World->GetGameInstance() != GetGameInstance()) return;
	const bool bGameDriver = NetDriver->NetDriverName == NAME_GameNetDriver && NetDriver->GetWorld() == World;
	if ((bGameDriver || bPendingDriver) && !QueueLocalRecovery(World, TEXT("dungeon_connection_lost"))
		&& FlowState == ERPGDungeonClientFlowState::Connecting)
	{
		FailOperation(TEXT("dungeon_connection_lost"));
	}
}

void URPGDungeonSessionSubsystem::CompleteLobbyReturn(const FRPGDungeonSession& DungeonSession)
{
	APlayerController* PlayerController = PendingPlayerController.Get();
	PendingPlayerController.Reset();
	PendingReturnSessionId.Reset();
	CurrentDungeonSession = DungeonSession;
	if (!DungeonSession.State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive)
		&& !DungeonSession.State.Equals(TEXT("Failed"), ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("dungeon_not_finished"));
		BroadcastSessionChanged();
		return;
	}
	if (!IsValidLocalController(PlayerController))
	{
		FailOperation(TEXT("return_controller_unavailable"));
		return;
	}
	// Same planned socket-close handshake as the emergency button. Backend
	// terminal-state validation above remains mandatory for this normal return.
	if (QueueLocalRecovery(PlayerController->GetWorld(), FString(), PlayerController))
		BroadcastSessionChanged();
}

void URPGDungeonSessionSubsystem::HandleLobbyMapLoaded(UWorld* LoadedWorld)
{
	if (FlowState == ERPGDungeonClientFlowState::ReturningToLobby && LoadedWorld
		&& LoadedWorld->GetGameInstance() == GetGameInstance()
		&& (LoadedWorld->WorldType == EWorldType::PIE
			? UWorld::RemovePIEPrefix(LoadedWorld->GetOutermost()->GetName())
			: LoadedWorld->GetOutermost()->GetName()) == LobbyMapPackage)
	{
		const FString Reason = LocalRecoveryReason;
		CancelLocalRecovery();
		SetFlowState(ERPGDungeonClientFlowState::Idle, Reason);
		if (bAbandonmentQueuedForLobby)
		{
			// Copy before re-entrant flow events. Expiry/context failure retains the
			// pinned consent for the existing lobby re-login/manual confirmation UX.
			const FString SessionId = AbandonmentSessionId;
			const FString CharacterId = AbandonmentCharacterId;
			const FString SteamId = AbandonmentSteamId;
			AbandonDungeonSession(UGameplayStatics::GetPlayerController(LoadedWorld, 0), SessionId, CharacterId, SteamId);
		}
	}
}

void URPGDungeonSessionSubsystem::HandleLobbyTravelFailure(UWorld* World,
	ETravelFailure::Type FailureType, const FString& Error)
{
	(void)FailureType;
	(void)Error; // Do not forward URLs/tickets from engine errors to UI or logs.
	if (FlowState == ERPGDungeonClientFlowState::ReturningToLobby && World
		&& World->GetGameInstance() == GetGameInstance())
	{
		// A second diagnostic for the failed dungeon connection is not a failure
		// of the lobby travel, which has not been scheduled yet.
		if (LocalRecoveryTicker.IsValid()) return;
		FailOperation(TEXT("lobby_travel_failed"));
	}
	else if (FlowState == ERPGDungeonClientFlowState::Connecting && World
		&& World->GetGameInstance() == GetGameInstance())
	{
		if (!QueueLocalRecovery(World, TEXT("dungeon_connection_failed")))
			FailOperation(TEXT("dungeon_connection_failed"));
	}
}

void URPGDungeonSessionSubsystem::ResetDungeonFlow(
	const bool bKeepSelectedCharacter)
{
	if (RejectUnresolvedAbandonment()) return;
	if (HasUnresolvedDeparture() || bDepartureRecoveryBlocked) { RejectRequest(TEXT("departure_result_unconfirmed")); return; }
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return;
	}

	CurrentDungeonSession = FRPGDungeonSession();
	PendingServerAddress.Reset();
	PendingReturnSessionId.Reset();
	PendingPlayerController.Reset();
	LastErrorCode.Reset();
	ClearRejectedDungeonAdmission();
	if (!bKeepSelectedCharacter)
	{
		SelectedCharacterId.Reset();
	}
	BroadcastSessionChanged();
	SetFlowState(ERPGDungeonClientFlowState::Idle);
}

bool URPGDungeonSessionSubsystem::IsRequestInFlight() const
{
	return (WebManager && WebManager->IsPlayerLogoutInFlight()) || PendingOperation != EPendingOperation::None
		|| FlowState == ERPGDungeonClientFlowState::ReturningToLobby;
}

bool URPGDungeonSessionSubsystem::HasActiveDungeonSession() const
{
	return !CurrentDungeonSession.DungeonSessionId.IsEmpty()
		&& IsActiveDungeonState(CurrentDungeonSession.State);
}

FString URPGDungeonSessionSubsystem::GetDungeonAdmissionBlockReason() const
{
	return !RejectedAdmissionReason.IsEmpty()
		&& RejectedAdmissionSessionId.Equals(CurrentDungeonSession.DungeonSessionId, ESearchCase::CaseSensitive)
		&& RejectedAdmissionCharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive)
		? RejectedAdmissionReason : FString();
}

void URPGDungeonSessionSubsystem::ClearRejectedDungeonAdmission()
{
	RejectedAdmissionSessionId.Reset();
	RejectedAdmissionCharacterId.Reset();
	RejectedAdmissionReason.Reset();
}

void URPGDungeonSessionSubsystem::HandleDungeonSessionUpdated(
	const FRPGDungeonSession& DungeonSession,
	const bool bSuccess)
{
	const EPendingOperation CompletedOperation = PendingOperation;
	if (CompletedOperation == EPendingOperation::None
		|| CompletedOperation == EPendingOperation::Connect || CompletedOperation == EPendingOperation::Abandon)
	{
		return;
	}
	if (CompletedOperation == EPendingOperation::Return && bSuccess
		&& !DungeonSession.DungeonSessionId.Equals(PendingReturnSessionId, ESearchCase::CaseSensitive))
	{
		// An unrelated session callback cannot authorize this pending return.
		return;
	}

	PendingOperation = EPendingOperation::None;
	if (!bSuccess)
	{
		switch (CompletedOperation)
		{
		case EPendingOperation::Create:
			FailOperation(TEXT("create_dungeon_session_failed"));
			break;
		case EPendingOperation::Join:
			FailOperation(TEXT("join_dungeon_session_failed"));
			break;
		case EPendingOperation::Refresh:
		case EPendingOperation::RecheckMembership:
			FailOperation(TEXT("refresh_dungeon_session_failed"));
			break;
		case EPendingOperation::Leave:
			FailOperation(TEXT("leave_dungeon_session_failed"));
			break;
		case EPendingOperation::Resume:
			FailOperation(TEXT("resume_dungeon_session_failed"));
			break;
		case EPendingOperation::Return:
			FailOperation(TEXT("refresh_dungeon_session_failed"));
			break;
		default:
			FailOperation(TEXT("dungeon_session_request_failed"));
			break;
		}
		return;
	}

	if (CompletedOperation == EPendingOperation::Leave)
	{
		if (!ResolveDepartureCommand()) return;
		CurrentDungeonSession = FRPGDungeonSession();
		BroadcastSessionChanged();
		SetFlowState(ERPGDungeonClientFlowState::Idle);
		return;
	}

	if ((CompletedOperation == EPendingOperation::Resume || CompletedOperation == EPendingOperation::RecheckMembership)
		&& DungeonSession.DungeonSessionId.IsEmpty())
	{
		if (!ResolveDepartureCommand()) return; // authenticated no-active-session recovery
		ClearRejectedDungeonAdmission();
		CurrentDungeonSession = FRPGDungeonSession();
		BroadcastSessionChanged();
		SetFlowState(ERPGDungeonClientFlowState::Idle);
		return;
	}

	if ((CompletedOperation == EPendingOperation::Return || CompletedOperation == EPendingOperation::RecheckMembership)
		&& !DungeonSession.Members.ContainsByPredicate([this](const FRPGDungeonSessionMember& Member)
		{
			return Member.CharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive);
		}))
	{
		FailOperation(TEXT("dungeon_membership_mismatch"));
		return;
	}
	CurrentDungeonSession = DungeonSession;
	if (!CurrentSessionContainsSelectedCharacter())
	{
		CurrentDungeonSession = FRPGDungeonSession();
		BroadcastSessionChanged();
		FailOperation(TEXT("dungeon_membership_mismatch"));
		return;
	}
	// Only an accepted owner/member snapshot may resolve uncertainty. A claim
	// makes cancellation impossible; it must not block this character's next lobby.
	if (CompletedOperation == EPendingOperation::Departure
		|| (!PendingDepartureRequest.IsEmpty()
			&& (PendingDepartureSession != DungeonSession.DungeonSessionId || DungeonSession.State != TEXT("Waiting")
				|| (bPendingDepartureCancel
					? DungeonSession.DepartureRequestedAt.IsEmpty() || DungeonSession.DepartureRequestId != PendingDepartureRequest
						|| DungeonSession.DepartureRequestRevision != PendingDepartureRevision
					: DungeonSession.LobbyRevision > PendingDepartureRevision))))
	{
		if (!ResolveDepartureCommand()) return;
	}

	if (CompletedOperation == EPendingOperation::Return)
	{
		CompleteLobbyReturn(DungeonSession);
		return;
	}
	// A successful owner-scoped read may resolve a transient nonmember conflict
	// or select a different active instance. Mere membership in the same expired
	// instance does not restore reconnect eligibility while its cleanup is pending.
	if (GetDungeonAdmissionBlockReason().IsEmpty()
		|| (CompletedOperation == EPendingOperation::RecheckMembership
			&& RejectedAdmissionReason != TEXT("dungeon_reconnect_grace_expired")))
		ClearRejectedDungeonAdmission();
	CompleteSessionOperation(DungeonSession);
}

void URPGDungeonSessionSubsystem::HandleJoinTicketIssued(
	const FString& DungeonSessionId,
	const FString& CharacterId,
	const FString& JoinTicket,
	const FString& ExpiresAt,
	const bool bSuccess, const FString& ErrorCode)
{
	if (PendingOperation != EPendingOperation::Connect)
	{
		return;
	}

	const bool bIdentityMatches = DungeonSessionId.Equals(
			CurrentDungeonSession.DungeonSessionId,
			ESearchCase::CaseSensitive)
		&& CharacterId.Equals(
			SelectedCharacterId,
			ESearchCase::CaseSensitive);
	if (!bIdentityMatches) return;
	PendingOperation = EPendingOperation::None;
	APlayerController* PlayerController = PendingPlayerController.Get();
	const FString ServerAddress = PendingServerAddress;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();

	// A successful ticket only authorizes its session/member. It cannot revive a
	// completed/removed membership or redirect travel after a newer session read.
	if (bSuccess && (ServerAddress.IsEmpty()
		|| !ServerAddress.Equals(CurrentDungeonSession.ServerAddress.TrimStartAndEnd(), ESearchCase::CaseSensitive)
		|| CurrentDungeonSession.ServerId.IsEmpty()
		|| !IsConnectableDungeonState(CurrentDungeonSession.State)
		|| !CurrentSessionContainsSelectedCharacter()))
	{
		FailOperation(TEXT("dungeon_server_assignment_changed"));
		return;
	}

	if (!bSuccess
		|| !bIdentityMatches
		|| JoinTicket.IsEmpty()
		|| ExpiresAt.IsEmpty()
		|| !UHttpWebManager::ConnectWithJoinTicket(
			PlayerController,
			ServerAddress,
			JoinTicket))
	{
		const bool bKnownDenial = !bSuccess
			&& (ErrorCode.Equals(TEXT("dungeon_reconnect_grace_expired"), ESearchCase::CaseSensitive)
				|| ErrorCode.Equals(TEXT("dungeon_member_not_active"), ESearchCase::CaseSensitive));
		if (bKnownDenial)
		{
			RejectedAdmissionSessionId = DungeonSessionId;
			RejectedAdmissionCharacterId = CharacterId;
			RejectedAdmissionReason = ErrorCode;
		}
		FailOperation(bKnownDenial ? ErrorCode : TEXT("join_ticket_or_travel_failed"));
		return;
	}

	SetFlowState(ERPGDungeonClientFlowState::Connecting);
	OnDungeonTravelStarted.Broadcast(ServerAddress);
}

bool URPGDungeonSessionSubsystem::BeginOperation(
	const EPendingOperation Operation,
	const ERPGDungeonClientFlowState RequestState)
{
	if (!WebManager)
	{
		FailOperation(TEXT("http_manager_unavailable"));
		return false;
	}
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	PendingOperation = Operation;
	SetFlowState(RequestState);
	return true;
}

bool URPGDungeonSessionSubsystem::ValidateSelectedCharacter()
{
	if (!SelectedCharacterId.IsEmpty())
	{
		return true;
	}

	FailOperation(TEXT("character_not_selected"));
	return false;
}

bool URPGDungeonSessionSubsystem::
	CurrentSessionContainsSelectedCharacter() const
{
	return CurrentDungeonSession.Members.ContainsByPredicate(
		[this](const FRPGDungeonSessionMember& Member)
		{
			return Member.CharacterId.Equals(
				SelectedCharacterId,
				ESearchCase::CaseSensitive);
		});
}

void URPGDungeonSessionSubsystem::CompleteSessionOperation(
	const FRPGDungeonSession& DungeonSession)
{
	BroadcastSessionChanged();
	const FString AdmissionBlockReason = GetDungeonAdmissionBlockReason();
	if (!AdmissionBlockReason.IsEmpty())
	{
		SetFlowState(ERPGDungeonClientFlowState::Error, AdmissionBlockReason);
		return;
	}
	SetFlowState(
		IsActiveDungeonState(DungeonSession.State)
			? ERPGDungeonClientFlowState::InSession
			: ERPGDungeonClientFlowState::Idle);
}

void URPGDungeonSessionSubsystem::FailOperation(
	const FString& ErrorCode)
{
	CancelLocalRecovery();
	PendingOperation = EPendingOperation::None;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();
	PendingReturnSessionId.Reset();
	SetFlowState(
		ERPGDungeonClientFlowState::Error,
		ErrorCode.IsEmpty()
			? TEXT("unknown_dungeon_flow_error")
			: ErrorCode);
}

void URPGDungeonSessionSubsystem::RejectRequest(
	const FString& ErrorCode)
{
	LastErrorCode = ErrorCode.IsEmpty()
		? TEXT("request_rejected")
		: ErrorCode;
	OnFlowStateChanged.Broadcast(FlowState, LastErrorCode);
}

void URPGDungeonSessionSubsystem::SetFlowState(
	const ERPGDungeonClientFlowState NewState,
	const FString& ErrorCode)
{
	FlowState = NewState;
	LastErrorCode = ErrorCode;
	OnFlowStateChanged.Broadcast(FlowState, LastErrorCode);
}

void URPGDungeonSessionSubsystem::BroadcastSessionChanged()
{
	OnDungeonSessionChanged.Broadcast(CurrentDungeonSession);
}
