#include "Online/RPGDungeonSessionSubsystem.h"

#include "GameFramework/PlayerController.h"

namespace
{
	bool IsActiveDungeonState(const FString& State)
	{
		return State.Equals(TEXT("Waiting"), ESearchCase::CaseSensitive)
			|| State.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)
			|| State.Equals(
				TEXT("InProgress"),
				ESearchCase::CaseSensitive);
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
	WebManager->OnJoinTicketIssued.RemoveDynamic(
		this,
		&ThisClass::HandleJoinTicketIssued);
	WebManager->OnJoinTicketIssued.AddDynamic(
		this,
		&ThisClass::HandleJoinTicketIssued);
}

void URPGDungeonSessionSubsystem::Deinitialize()
{
	if (WebManager)
	{
		WebManager->OnDungeonSessionUpdated.RemoveDynamic(
			this,
			&ThisClass::HandleDungeonSessionUpdated);
		WebManager->OnJoinTicketIssued.RemoveDynamic(
			this,
			&ThisClass::HandleJoinTicketIssued);
	}

	PendingOperation = EPendingOperation::None;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();
	WebManager = nullptr;
	OnFlowStateChanged.Clear();
	OnDungeonSessionChanged.Clear();
	OnDungeonTravelStarted.Clear();
	Super::Deinitialize();
}

bool URPGDungeonSessionSubsystem::SelectCharacter(
	const FString& CharacterId)
{
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

	SelectedCharacterId = NormalizedCharacterId;
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
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return false;
	}

	if (!ValidateSelectedCharacter())
	{
		return false;
	}

	if (HasActiveDungeonSession())
	{
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

	if (!BeginOperation(
		EPendingOperation::Refresh,
		ERPGDungeonClientFlowState::RefreshingSession))
	{
		return false;
	}

	WebManager->LoadDungeonSession(
		CurrentDungeonSession.DungeonSessionId);
	return true;
}

bool URPGDungeonSessionSubsystem::LeaveDungeonSession()
{
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

bool URPGDungeonSessionSubsystem::ConnectToDungeon(
	APlayerController* PlayerController,
	const FString& ServerAddress)
{
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
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

void URPGDungeonSessionSubsystem::ResetDungeonFlow(
	const bool bKeepSelectedCharacter)
{
	if (IsRequestInFlight())
	{
		RejectRequest(TEXT("request_in_flight"));
		return;
	}

	CurrentDungeonSession = FRPGDungeonSession();
	PendingServerAddress.Reset();
	PendingPlayerController.Reset();
	LastErrorCode.Reset();
	if (!bKeepSelectedCharacter)
	{
		SelectedCharacterId.Reset();
	}
	BroadcastSessionChanged();
	SetFlowState(ERPGDungeonClientFlowState::Idle);
}

bool URPGDungeonSessionSubsystem::IsRequestInFlight() const
{
	return PendingOperation != EPendingOperation::None;
}

bool URPGDungeonSessionSubsystem::HasActiveDungeonSession() const
{
	return !CurrentDungeonSession.DungeonSessionId.IsEmpty()
		&& IsActiveDungeonState(CurrentDungeonSession.State);
}

void URPGDungeonSessionSubsystem::HandleDungeonSessionUpdated(
	const FRPGDungeonSession& DungeonSession,
	const bool bSuccess)
{
	const EPendingOperation CompletedOperation = PendingOperation;
	if (CompletedOperation == EPendingOperation::None
		|| CompletedOperation == EPendingOperation::Connect)
	{
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
			FailOperation(TEXT("refresh_dungeon_session_failed"));
			break;
		case EPendingOperation::Leave:
			FailOperation(TEXT("leave_dungeon_session_failed"));
			break;
		case EPendingOperation::Resume:
			FailOperation(TEXT("resume_dungeon_session_failed"));
			break;
		default:
			FailOperation(TEXT("dungeon_session_request_failed"));
			break;
		}
		return;
	}

	if (CompletedOperation == EPendingOperation::Leave)
	{
		CurrentDungeonSession = FRPGDungeonSession();
		BroadcastSessionChanged();
		SetFlowState(ERPGDungeonClientFlowState::Idle);
		return;
	}

	if (CompletedOperation == EPendingOperation::Resume
		&& DungeonSession.DungeonSessionId.IsEmpty())
	{
		CurrentDungeonSession = FRPGDungeonSession();
		BroadcastSessionChanged();
		SetFlowState(ERPGDungeonClientFlowState::Idle);
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

	CompleteSessionOperation(DungeonSession);
}

void URPGDungeonSessionSubsystem::HandleJoinTicketIssued(
	const FString& DungeonSessionId,
	const FString& CharacterId,
	const FString& JoinTicket,
	const FString& ExpiresAt,
	const bool bSuccess)
{
	if (PendingOperation != EPendingOperation::Connect)
	{
		return;
	}

	PendingOperation = EPendingOperation::None;
	const bool bIdentityMatches = DungeonSessionId.Equals(
			CurrentDungeonSession.DungeonSessionId,
			ESearchCase::CaseSensitive)
		&& CharacterId.Equals(
			SelectedCharacterId,
			ESearchCase::CaseSensitive);
	APlayerController* PlayerController = PendingPlayerController.Get();
	const FString ServerAddress = PendingServerAddress;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();

	if (!bSuccess
		|| !bIdentityMatches
		|| JoinTicket.IsEmpty()
		|| ExpiresAt.IsEmpty()
		|| !UHttpWebManager::ConnectWithJoinTicket(
			PlayerController,
			ServerAddress,
			JoinTicket))
	{
		FailOperation(TEXT("join_ticket_or_travel_failed"));
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
	SetFlowState(
		IsActiveDungeonState(DungeonSession.State)
			? ERPGDungeonClientFlowState::InSession
			: ERPGDungeonClientFlowState::Idle);
}

void URPGDungeonSessionSubsystem::FailOperation(
	const FString& ErrorCode)
{
	PendingOperation = EPendingOperation::None;
	PendingPlayerController.Reset();
	PendingServerAddress.Reset();
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
