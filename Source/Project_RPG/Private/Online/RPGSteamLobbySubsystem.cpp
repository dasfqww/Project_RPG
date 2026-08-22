#include "Online/RPGSteamLobbySubsystem.h"

#include "Online/RPGDungeonSessionSubsystem.h"

#include "Online/OnlineSessionNames.h"
#include "Interfaces/OnlineExternalUIInterface.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"

namespace
{
	const FName PartyLobbySessionName(TEXT("RPGPartyLobby"));
	const FName LobbyTypeKey(TEXT("RPG_LOBBY_TYPE"));
	const FName DungeonSessionIdKey(TEXT("RPG_DUNGEON_SESSION_ID"));
	const FName DungeonIdKey(TEXT("RPG_DUNGEON_ID"));
	const FName DifficultyKey(TEXT("RPG_DIFFICULTY"));
	const FName DungeonStateKey(TEXT("RPG_DUNGEON_STATE"));
	const FName ServerIdKey(TEXT("RPG_SERVER_ID"));
	const FName ServerAddressKey(TEXT("RPG_SERVER_ADDRESS"));
	const FName BuildIdKey(TEXT("RPG_BUILD_ID"));
	const FString DungeonLobbyType(TEXT("PVE_DUNGEON"));

	constexpr int32 MinPartySize = 1;
	constexpr int32 MaxPartySize = 4;
	constexpr int32 MinSearchResults = 1;
	constexpr int32 MaxSearchResults = 100;

	bool IsWaitingState(const FString& State)
	{
		return State.Equals(TEXT("Waiting"), ESearchCase::CaseSensitive);
	}

	bool IsConnectableState(const FString& State)
	{
		return State.Equals(TEXT("Loading"), ESearchCase::CaseSensitive)
			|| State.Equals(
				TEXT("InProgress"),
				ESearchCase::CaseSensitive);
	}

	FString BuildIdFromSettings(const FOnlineSessionSettings& Settings)
	{
		FString BuildId;
		if (!Settings.Get(BuildIdKey, BuildId))
		{
			BuildId = FString::FromInt(Settings.BuildUniqueId);
		}
		return BuildId;
	}

	FString JoinResultErrorCode(
		const EOnJoinSessionCompleteResult::Type Result)
	{
		switch (Result)
		{
		case EOnJoinSessionCompleteResult::SessionIsFull:
			return TEXT("join_lobby_session_full");
		case EOnJoinSessionCompleteResult::SessionDoesNotExist:
			return TEXT("join_lobby_not_found");
		case EOnJoinSessionCompleteResult::CouldNotRetrieveAddress:
			return TEXT("join_lobby_address_unavailable");
		case EOnJoinSessionCompleteResult::AlreadyInSession:
			return TEXT("join_lobby_already_joined");
		case EOnJoinSessionCompleteResult::Success:
			return FString();
		case EOnJoinSessionCompleteResult::UnknownError:
		default:
			return TEXT("join_lobby_unknown_error");
		}
	}
}

bool URPGSteamLobbySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer)
		&& !IsRunningDedicatedServer();
}

void URPGSteamLobbySubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<URPGDungeonSessionSubsystem>();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		DungeonFlow =
			GameInstance->GetSubsystem<URPGDungeonSessionSubsystem>();
	}

	if (DungeonFlow)
	{
		DungeonFlow->OnFlowStateChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDungeonFlowStateChanged);
		DungeonFlow->OnFlowStateChanged.AddDynamic(
			this,
			&ThisClass::HandleDungeonFlowStateChanged);
	}
	else
	{
		SetLobbyState(
			ERPGSteamLobbyState::Error,
			TEXT("dungeon_flow_unavailable"));
	}

	// The online subsystem may finish loading after this subsystem. Public
	// operations call EnsureSessionInterface again, so this is best effort.
	EnsureSessionInterface();
}

void URPGSteamLobbySubsystem::Deinitialize()
{
	if (DungeonFlow)
	{
		DungeonFlow->OnFlowStateChanged.RemoveDynamic(
			this,
			&ThisClass::HandleDungeonFlowStateChanged);
	}

	ClearSessionDelegates();
	SessionInterface.Reset();
	SessionSearch.Reset();
	PendingInviteResult.Reset();
	PendingInviteLobby = FRPGSteamLobbyInfo();
	VisibleSearchResultIndices.Reset();
	LastSearchResults.Reset();
	ClearCurrentLobby();
	DungeonFlow = nullptr;
	PendingOperation = EPendingOperation::None;
	OnLobbyStateChanged.Clear();
	OnLobbySearchCompleted.Clear();
	OnLobbyChanged.Clear();
	OnPartyInviteAccepted.Clear();
	Super::Deinitialize();
}

bool URPGSteamLobbySubsystem::CreatePartyLobby(const int32 MaxPlayers)
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}
	if (!DungeonFlow || DungeonFlow->IsRequestInFlight())
	{
		FailOperation(TEXT("dungeon_flow_busy"));
		return false;
	}
	if (MaxPlayers < MinPartySize || MaxPlayers > MaxPartySize)
	{
		FailOperation(TEXT("invalid_party_size"));
		return false;
	}
	if (SessionInterface->GetNamedSession(PartyLobbySessionName))
	{
		FailOperation(TEXT("party_lobby_already_exists"));
		return false;
	}

	const FRPGDungeonSession DungeonSession =
		DungeonFlow->GetCurrentDungeonSession();
	if (!DungeonFlow->HasActiveDungeonSession()
		|| DungeonSession.DungeonSessionId.IsEmpty())
	{
		FailOperation(TEXT("backend_dungeon_session_required"));
		return false;
	}

	FOnlineSessionSettings Settings;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.NumPrivateConnections = 0;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bIsLANMatch = false;
	Settings.bIsDedicated = false;
	Settings.bUsesStats = false;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bAntiCheatProtected = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bUseLobbiesVoiceChatIfAvailable = false;

	const EOnlineDataAdvertisementType::Type Advertisement =
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing;
	Settings.Set(LobbyTypeKey, DungeonLobbyType, Advertisement);
	Settings.Set(
		DungeonSessionIdKey,
		DungeonSession.DungeonSessionId,
		Advertisement);
	Settings.Set(DungeonIdKey, DungeonSession.DungeonId, Advertisement);
	Settings.Set(DifficultyKey, DungeonSession.Difficulty, Advertisement);
	Settings.Set(DungeonStateKey, DungeonSession.State, Advertisement);
	Settings.Set(
		BuildIdKey,
		FString::FromInt(Settings.BuildUniqueId),
		Advertisement);
	if (!DungeonSession.ServerId.IsEmpty())
	{
		Settings.Set(ServerIdKey, DungeonSession.ServerId, Advertisement);
	}

	if (!BeginOperation(
		EPendingOperation::Create,
		ERPGSteamLobbyState::CreatingLobby))
	{
		return false;
	}

	if (!SessionInterface->CreateSession(
		0,
		PartyLobbySessionName,
		Settings))
	{
		FailOperation(TEXT("create_lobby_request_rejected"));
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::FindPartyLobbies(const int32 MaxResults)
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}
	if (MaxResults < MinSearchResults || MaxResults > MaxSearchResults)
	{
		FailOperation(TEXT("invalid_search_limit"));
		return false;
	}

	SessionSearch = MakeShared<FOnlineSessionSearch>();
	SessionSearch->MaxSearchResults = MaxResults;
	SessionSearch->bIsLanQuery = false;
	SessionSearch->QuerySettings.Set(
		SEARCH_LOBBIES,
		true,
		EOnlineComparisonOp::Equals);
	VisibleSearchResultIndices.Reset();
	LastSearchResults.Reset();

	if (!BeginOperation(
		EPendingOperation::Find,
		ERPGSteamLobbyState::Searching))
	{
		SessionSearch.Reset();
		return false;
	}

	if (!SessionInterface->FindSessions(0, SessionSearch.ToSharedRef()))
	{
		SessionSearch.Reset();
		FailOperation(TEXT("find_lobbies_request_rejected"));
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::ShowPartyInviteOverlay()
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}
	if (!SessionInterface->GetNamedSession(PartyLobbySessionName))
	{
		FailOperation(TEXT("party_lobby_required"));
		return false;
	}

	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
	const IOnlineExternalUIPtr ExternalUI = OnlineSubsystem
		? OnlineSubsystem->GetExternalUIInterface()
		: nullptr;
	if (!ExternalUI.IsValid()
		|| !ExternalUI->ShowInviteUI(0, PartyLobbySessionName))
	{
		FailOperation(TEXT("invite_overlay_unavailable"));
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::JoinPartyLobbyByIndex(
	const int32 ResultIndex)
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!SessionSearch.IsValid()
		|| !VisibleSearchResultIndices.IsValidIndex(ResultIndex))
	{
		FailOperation(TEXT("invalid_lobby_result"));
		return false;
	}

	const int32 NativeIndex = VisibleSearchResultIndices[ResultIndex];
	if (!SessionSearch->SearchResults.IsValidIndex(NativeIndex))
	{
		FailOperation(TEXT("stale_lobby_result"));
		return false;
	}

	return StartJoinPartyLobby(
		SessionSearch->SearchResults[NativeIndex],
		0,
		ResultIndex);
}

bool URPGSteamLobbySubsystem::AcceptPendingPartyInvite()
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!PendingInviteResult.IsValid())
	{
		FailOperation(TEXT("pending_party_invite_required"));
		return false;
	}

	const TSharedPtr<FOnlineSessionSearchResult> InviteResult =
		PendingInviteResult;
	const int32 LocalUserNum = PendingInviteLocalUserNum;
	if (!StartJoinPartyLobby(*InviteResult, LocalUserNum, INDEX_NONE))
	{
		return false;
	}

	PendingInviteResult.Reset();
	PendingInviteLobby = FRPGSteamLobbyInfo();
	PendingInviteLocalUserNum = 0;
	return true;
}

bool URPGSteamLobbySubsystem::PublishDungeonServerAddress()
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}
	if (!DungeonFlow || DungeonFlow->IsRequestInFlight())
	{
		FailOperation(TEXT("dungeon_flow_busy"));
		return false;
	}

	const FRPGDungeonSession DungeonSession =
		DungeonFlow->GetCurrentDungeonSession();
	const FString NormalizedAddress =
		DungeonSession.ServerAddress.TrimStartAndEnd();
	FNamedOnlineSession* NamedSession =
		SessionInterface->GetNamedSession(PartyLobbySessionName);
	if (!NamedSession
		|| !NamedSession->bHosting
		|| NormalizedAddress.IsEmpty()
		|| DungeonSession.DungeonSessionId.IsEmpty()
		|| DungeonSession.ServerId.IsEmpty()
		|| !IsConnectableState(DungeonSession.State))
	{
		FailOperation(TEXT("dungeon_server_not_publishable"));
		return false;
	}

	FString LobbyDungeonSessionId;
	NamedSession->SessionSettings.Get(
		DungeonSessionIdKey,
		LobbyDungeonSessionId);
	if (!LobbyDungeonSessionId.Equals(
		DungeonSession.DungeonSessionId,
		ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("lobby_dungeon_session_mismatch"));
		return false;
	}

	const EOnlineDataAdvertisementType::Type Advertisement =
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing;
	FOnlineSessionSettings UpdatedSettings =
		NamedSession->SessionSettings;
	UpdatedSettings.Set(
		DungeonStateKey,
		DungeonSession.State,
		Advertisement);
	UpdatedSettings.Set(
		ServerIdKey,
		DungeonSession.ServerId,
		Advertisement);
	UpdatedSettings.Set(
		ServerAddressKey,
		NormalizedAddress,
		Advertisement);

	if (!BeginOperation(
		EPendingOperation::Update,
		ERPGSteamLobbyState::UpdatingLobby))
	{
		return false;
	}
	if (!SessionInterface->UpdateSession(
		PartyLobbySessionName,
		UpdatedSettings,
		true))
	{
		FailOperation(TEXT("update_lobby_request_rejected"));
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::ConnectToDungeonServer(
	APlayerController* PlayerController)
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface() || !DungeonFlow)
	{
		FailOperation(TEXT("online_flow_unavailable"));
		return false;
	}

	RefreshCurrentLobbyFromNamedSession();
	const FRPGDungeonSession DungeonSession =
		DungeonFlow->GetCurrentDungeonSession();
	const FString TrustedServerAddress =
		DungeonSession.ServerAddress.TrimStartAndEnd();
	if (TrustedServerAddress.IsEmpty()
		|| CurrentLobby.DungeonSessionId.IsEmpty()
		|| !CurrentLobby.DungeonSessionId.Equals(
			DungeonSession.DungeonSessionId,
			ESearchCase::CaseSensitive)
		|| !CurrentLobby.ServerId.Equals(
			DungeonSession.ServerId,
			ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("backend_server_not_ready"));
		return false;
	}
	if (!CurrentLobby.ServerAddress.IsEmpty()
		&& !CurrentLobby.ServerAddress.Equals(
			TrustedServerAddress,
			ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("lobby_server_metadata_mismatch"));
		return false;
	}

	if (!DungeonFlow->ConnectToDungeon(
		PlayerController,
		TrustedServerAddress))
	{
		FailOperation(
			DungeonFlow->GetLastErrorCode().IsEmpty()
				? TEXT("connect_dungeon_request_rejected")
				: DungeonFlow->GetLastErrorCode());
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::LeavePartyLobby()
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}

	if (DungeonFlow && DungeonFlow->HasActiveDungeonSession())
	{
		const FRPGDungeonSession DungeonSession =
			DungeonFlow->GetCurrentDungeonSession();
		if (!IsWaitingState(DungeonSession.State))
		{
			FailOperation(TEXT("active_dungeon_leave_not_allowed"));
			return false;
		}
		if (DungeonFlow->IsRequestInFlight())
		{
			FailOperation(TEXT("dungeon_flow_busy"));
			return false;
		}

		PendingOperation = EPendingOperation::LeaveBackend;
		SetLobbyState(ERPGSteamLobbyState::LeavingBackendSession);
		if (!DungeonFlow->LeaveDungeonSession())
		{
			FailOperation(
				DungeonFlow->GetLastErrorCode().IsEmpty()
					? TEXT("leave_backend_session_rejected")
					: DungeonFlow->GetLastErrorCode());
			return false;
		}
		return true;
	}

	return BeginDestroyLobby();
}

bool URPGSteamLobbySubsystem::IsInPartyLobby() const
{
	return SessionInterface.IsValid()
		&& SessionInterface->GetNamedSession(PartyLobbySessionName) != nullptr;
}

bool URPGSteamLobbySubsystem::IsLobbyRequestInFlight() const
{
	return PendingOperation != EPendingOperation::None;
}

bool URPGSteamLobbySubsystem::EnsureSessionInterface()
{
	if (SessionInterface.IsValid())
	{
		return true;
	}

	IOnlineSubsystem* OnlineSubsystem = Online::GetSubsystem(GetWorld());
	if (!OnlineSubsystem)
	{
		return false;
	}

	SessionInterface = OnlineSubsystem->GetSessionInterface();
	if (!SessionInterface.IsValid())
	{
		return false;
	}

	RegisterSessionDelegates();
	return true;
}

void URPGSteamLobbySubsystem::RegisterSessionDelegates()
{
	if (!SessionInterface.IsValid() || bDelegatesRegistered)
	{
		return;
	}

	CreateSessionDelegateHandle =
		SessionInterface->AddOnCreateSessionCompleteDelegate_Handle(
			FOnCreateSessionCompleteDelegate::CreateUObject(
				this,
				&ThisClass::HandleCreateSessionComplete));
	FindSessionsDelegateHandle =
		SessionInterface->AddOnFindSessionsCompleteDelegate_Handle(
			FOnFindSessionsCompleteDelegate::CreateUObject(
				this,
				&ThisClass::HandleFindSessionsComplete));
	JoinSessionDelegateHandle =
		SessionInterface->AddOnJoinSessionCompleteDelegate_Handle(
			FOnJoinSessionCompleteDelegate::CreateUObject(
				this,
				&ThisClass::HandleJoinSessionComplete));
	UpdateSessionDelegateHandle =
		SessionInterface->AddOnUpdateSessionCompleteDelegate_Handle(
			FOnUpdateSessionCompleteDelegate::CreateUObject(
				this,
				&ThisClass::HandleUpdateSessionComplete));
	DestroySessionDelegateHandle =
		SessionInterface->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(
				this,
				&ThisClass::HandleDestroySessionComplete));
	InviteAcceptedDelegateHandle =
		SessionInterface->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(
				this,
				&ThisClass::HandleSessionUserInviteAccepted));
	bDelegatesRegistered = true;
}

void URPGSteamLobbySubsystem::ClearSessionDelegates()
{
	if (!SessionInterface.IsValid() || !bDelegatesRegistered)
	{
		return;
	}

	SessionInterface->ClearOnCreateSessionCompleteDelegate_Handle(
		CreateSessionDelegateHandle);
	SessionInterface->ClearOnFindSessionsCompleteDelegate_Handle(
		FindSessionsDelegateHandle);
	SessionInterface->ClearOnJoinSessionCompleteDelegate_Handle(
		JoinSessionDelegateHandle);
	SessionInterface->ClearOnUpdateSessionCompleteDelegate_Handle(
		UpdateSessionDelegateHandle);
	SessionInterface->ClearOnDestroySessionCompleteDelegate_Handle(
		DestroySessionDelegateHandle);
	SessionInterface->ClearOnSessionUserInviteAcceptedDelegate_Handle(
		InviteAcceptedDelegateHandle);
	bDelegatesRegistered = false;
}

bool URPGSteamLobbySubsystem::BeginOperation(
	const EPendingOperation Operation,
	const ERPGSteamLobbyState RequestState)
{
	if (IsLobbyRequestInFlight())
	{
		RejectRequest(TEXT("lobby_request_in_flight"));
		return false;
	}

	PendingOperation = Operation;
	SetLobbyState(RequestState);
	return true;
}

bool URPGSteamLobbySubsystem::StartJoinPartyLobby(
	const FOnlineSessionSearchResult& SearchResult,
	const int32 LocalUserNum,
	const int32 ResultIndex)
{
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}
	if (!DungeonFlow || DungeonFlow->IsRequestInFlight())
	{
		FailOperation(TEXT("dungeon_flow_busy"));
		return false;
	}
	if (DungeonFlow->HasActiveDungeonSession())
	{
		FailOperation(TEXT("active_dungeon_session_exists"));
		return false;
	}
	if (SessionInterface->GetNamedSession(PartyLobbySessionName))
	{
		FailOperation(TEXT("party_lobby_already_exists"));
		return false;
	}

	const FRPGSteamLobbyInfo Lobby = BuildLobbyInfo(
		SearchResult.Session,
		ResultIndex,
		SearchResult.PingInMs);
	FString LobbyType;
	SearchResult.Session.SessionSettings.Get(LobbyTypeKey, LobbyType);
	const FString LocalBuildId =
		FString::FromInt(FOnlineSessionSettings().BuildUniqueId);
	if (!SearchResult.IsValid()
		|| !Lobby.bJoinable
		|| !LobbyType.Equals(
			DungeonLobbyType,
			ESearchCase::CaseSensitive)
		|| Lobby.DungeonSessionId.IsEmpty()
		|| !Lobby.BuildId.Equals(
			LocalBuildId,
			ESearchCase::CaseSensitive))
	{
		FailOperation(TEXT("lobby_not_joinable"));
		return false;
	}

	PendingDungeonSessionId = Lobby.DungeonSessionId;
	if (!BeginOperation(
		EPendingOperation::JoinSteam,
		ERPGSteamLobbyState::JoiningLobby))
	{
		PendingDungeonSessionId.Reset();
		return false;
	}

	if (!SessionInterface->JoinSession(
		LocalUserNum,
		PartyLobbySessionName,
		SearchResult))
	{
		PendingDungeonSessionId.Reset();
		FailOperation(TEXT("join_lobby_request_rejected"));
		return false;
	}
	return true;
}

bool URPGSteamLobbySubsystem::BeginDestroyLobby(
	const FString& InErrorAfterDestroy)
{
	if (!EnsureSessionInterface())
	{
		FailOperation(TEXT("online_session_unavailable"));
		return false;
	}

	ErrorAfterDestroy = InErrorAfterDestroy;
	if (!SessionInterface->GetNamedSession(PartyLobbySessionName))
	{
		PendingOperation = EPendingOperation::None;
		ClearCurrentLobby();
		if (!ErrorAfterDestroy.IsEmpty())
		{
			const FString ErrorCode = ErrorAfterDestroy;
			ErrorAfterDestroy.Reset();
			FailOperation(ErrorCode);
		}
		else
		{
			SetLobbyState(ERPGSteamLobbyState::Idle);
		}
		return true;
	}

	PendingOperation = EPendingOperation::Destroy;
	SetLobbyState(ERPGSteamLobbyState::LeavingLobby);
	if (!SessionInterface->DestroySession(PartyLobbySessionName))
	{
		const FString ErrorCode = ErrorAfterDestroy.IsEmpty()
			? TEXT("leave_lobby_request_rejected")
			: ErrorAfterDestroy;
		ErrorAfterDestroy.Reset();
		FailOperation(ErrorCode);
		return false;
	}
	return true;
}

void URPGSteamLobbySubsystem::FailOperation(const FString& ErrorCode)
{
	PendingOperation = EPendingOperation::None;
	PendingDungeonSessionId.Reset();
	ErrorAfterDestroy.Reset();
	SetLobbyState(
		ERPGSteamLobbyState::Error,
		ErrorCode.IsEmpty()
			? TEXT("unknown_lobby_error")
			: ErrorCode);
}

void URPGSteamLobbySubsystem::RejectRequest(const FString& ErrorCode)
{
	LastErrorCode = ErrorCode.IsEmpty()
		? TEXT("lobby_request_rejected")
		: ErrorCode;
	OnLobbyStateChanged.Broadcast(LobbyState, LastErrorCode);
}

void URPGSteamLobbySubsystem::SetLobbyState(
	const ERPGSteamLobbyState NewState,
	const FString& ErrorCode)
{
	LobbyState = NewState;
	LastErrorCode = ErrorCode;
	OnLobbyStateChanged.Broadcast(LobbyState, LastErrorCode);
}

void URPGSteamLobbySubsystem::ClearCurrentLobby()
{
	CurrentLobby = FRPGSteamLobbyInfo();
	OnLobbyChanged.Broadcast(CurrentLobby);
}

void URPGSteamLobbySubsystem::RefreshCurrentLobbyFromNamedSession()
{
	if (!SessionInterface.IsValid())
	{
		return;
	}

	if (const FNamedOnlineSession* NamedSession =
		SessionInterface->GetNamedSession(PartyLobbySessionName))
	{
		CurrentLobby = BuildLobbyInfo(*NamedSession, INDEX_NONE, 0);
		OnLobbyChanged.Broadcast(CurrentLobby);
	}
}

FRPGSteamLobbyInfo URPGSteamLobbySubsystem::BuildLobbyInfo(
	const FOnlineSession& Session,
	const int32 ResultIndex,
	const int32 PingInMs) const
{
	FRPGSteamLobbyInfo Lobby;
	Lobby.ResultIndex = ResultIndex;
	Lobby.LobbyId = Session.GetSessionIdStr();
	Lobby.OwnerName = Session.OwningUserName;
	Lobby.PingInMs = PingInMs;
	Lobby.MaxPlayers = Session.SessionSettings.NumPublicConnections;
	Lobby.CurrentPlayers = FMath::Max(
		0,
		Lobby.MaxPlayers - Session.NumOpenPublicConnections);
	Lobby.bJoinable = Session.NumOpenPublicConnections > 0
		&& Session.SessionSettings.bAllowJoinInProgress;

	Session.SessionSettings.Get(
		DungeonSessionIdKey,
		Lobby.DungeonSessionId);
	Session.SessionSettings.Get(DungeonIdKey, Lobby.DungeonId);
	Session.SessionSettings.Get(DifficultyKey, Lobby.Difficulty);
	Session.SessionSettings.Get(DungeonStateKey, Lobby.DungeonState);
	Session.SessionSettings.Get(ServerIdKey, Lobby.ServerId);
	Session.SessionSettings.Get(ServerAddressKey, Lobby.ServerAddress);
	Lobby.BuildId = BuildIdFromSettings(Session.SessionSettings);
	return Lobby;
}

void URPGSteamLobbySubsystem::HandleCreateSessionComplete(
	const FName SessionName,
	const bool bSuccess)
{
	if (SessionName != PartyLobbySessionName
		|| PendingOperation != EPendingOperation::Create)
	{
		return;
	}

	PendingOperation = EPendingOperation::None;
	if (!bSuccess)
	{
		FailOperation(TEXT("create_lobby_failed"));
		return;
	}

	RefreshCurrentLobbyFromNamedSession();
	SetLobbyState(ERPGSteamLobbyState::InLobby);
}

void URPGSteamLobbySubsystem::HandleFindSessionsComplete(
	const bool bSuccess)
{
	if (PendingOperation != EPendingOperation::Find)
	{
		return;
	}

	PendingOperation = EPendingOperation::None;
	VisibleSearchResultIndices.Reset();
	LastSearchResults.Reset();
	if (bSuccess && SessionSearch.IsValid())
	{
		const FString LocalBuildId =
			FString::FromInt(FOnlineSessionSettings().BuildUniqueId);
		for (int32 NativeIndex = 0;
			NativeIndex < SessionSearch->SearchResults.Num();
			++NativeIndex)
		{
			const FOnlineSessionSearchResult& SearchResult =
				SessionSearch->SearchResults[NativeIndex];
			FString LobbyType;
			SearchResult.Session.SessionSettings.Get(
				LobbyTypeKey,
				LobbyType);
			const FRPGSteamLobbyInfo Lobby = BuildLobbyInfo(
				SearchResult.Session,
				LastSearchResults.Num(),
				SearchResult.PingInMs);
			if (!SearchResult.IsValid()
				|| !LobbyType.Equals(
					DungeonLobbyType,
					ESearchCase::CaseSensitive)
				|| Lobby.DungeonSessionId.IsEmpty()
				|| !Lobby.BuildId.Equals(
					LocalBuildId,
					ESearchCase::CaseSensitive))
			{
				continue;
			}

			VisibleSearchResultIndices.Add(NativeIndex);
			LastSearchResults.Add(Lobby);
		}
	}

	const bool bHasLobby =
		SessionInterface.IsValid()
		&& SessionInterface->GetNamedSession(PartyLobbySessionName);
	SetLobbyState(
		bHasLobby
			? ERPGSteamLobbyState::InLobby
			: ERPGSteamLobbyState::Idle);
	OnLobbySearchCompleted.Broadcast(LastSearchResults, bSuccess);
}

void URPGSteamLobbySubsystem::HandleJoinSessionComplete(
	const FName SessionName,
	const EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != PartyLobbySessionName
		|| PendingOperation != EPendingOperation::JoinSteam)
	{
		return;
	}

	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		FailOperation(JoinResultErrorCode(Result));
		return;
	}

	RefreshCurrentLobbyFromNamedSession();
	if (PendingDungeonSessionId.IsEmpty()
		|| !CurrentLobby.DungeonSessionId.Equals(
			PendingDungeonSessionId,
			ESearchCase::CaseSensitive))
	{
		BeginDestroyLobby(TEXT("joined_lobby_metadata_mismatch"));
		return;
	}

	PendingOperation = EPendingOperation::JoinBackend;
	SetLobbyState(ERPGSteamLobbyState::JoiningBackendSession);
	const bool bBackendJoinStarted = DungeonFlow
		&& DungeonFlow->JoinDungeonSession(PendingDungeonSessionId);
	if (!bBackendJoinStarted
		&& PendingOperation == EPendingOperation::JoinBackend)
	{
		const FString ErrorCode =
			DungeonFlow && !DungeonFlow->GetLastErrorCode().IsEmpty()
				? DungeonFlow->GetLastErrorCode()
				: TEXT("join_backend_session_rejected");
		BeginDestroyLobby(ErrorCode);
	}
}

void URPGSteamLobbySubsystem::HandleUpdateSessionComplete(
	const FName SessionName,
	const bool bSuccess)
{
	if (SessionName != PartyLobbySessionName
		|| PendingOperation != EPendingOperation::Update)
	{
		return;
	}

	PendingOperation = EPendingOperation::None;
	if (!bSuccess)
	{
		FailOperation(TEXT("update_lobby_failed"));
		return;
	}

	RefreshCurrentLobbyFromNamedSession();
	SetLobbyState(ERPGSteamLobbyState::InLobby);
}

void URPGSteamLobbySubsystem::HandleDestroySessionComplete(
	const FName SessionName,
	const bool bSuccess)
{
	if (SessionName != PartyLobbySessionName
		|| PendingOperation != EPendingOperation::Destroy)
	{
		return;
	}

	PendingOperation = EPendingOperation::None;
	PendingDungeonSessionId.Reset();
	const FString DeferredError = ErrorAfterDestroy;
	ErrorAfterDestroy.Reset();
	if (!bSuccess)
	{
		RefreshCurrentLobbyFromNamedSession();
		FailOperation(
			DeferredError.IsEmpty()
				? TEXT("leave_lobby_failed")
				: TEXT("lobby_cleanup_failed"));
		return;
	}

	ClearCurrentLobby();
	if (!DeferredError.IsEmpty())
	{
		FailOperation(DeferredError);
		return;
	}

	SetLobbyState(ERPGSteamLobbyState::Idle);
}

void URPGSteamLobbySubsystem::HandleSessionUserInviteAccepted(
	const bool bSuccess,
	const int32 ControllerId,
	FUniqueNetIdPtr UserId,
	const FOnlineSessionSearchResult& InviteResult)
{
	if (!bSuccess || !UserId.IsValid() || !InviteResult.IsValid())
	{
		RejectRequest(TEXT("party_invite_invalid"));
		return;
	}

	const FRPGSteamLobbyInfo Lobby = BuildLobbyInfo(
		InviteResult.Session,
		INDEX_NONE,
		InviteResult.PingInMs);
	FString LobbyType;
	InviteResult.Session.SessionSettings.Get(LobbyTypeKey, LobbyType);
	const FString LocalBuildId =
		FString::FromInt(FOnlineSessionSettings().BuildUniqueId);
	if (!LobbyType.Equals(
			DungeonLobbyType,
			ESearchCase::CaseSensitive)
		|| Lobby.DungeonSessionId.IsEmpty()
		|| !Lobby.BuildId.Equals(
			LocalBuildId,
			ESearchCase::CaseSensitive))
	{
		RejectRequest(TEXT("party_invite_incompatible"));
		return;
	}

	PendingInviteResult =
		MakeShared<FOnlineSessionSearchResult>(InviteResult);
	PendingInviteLobby = Lobby;
	PendingInviteLocalUserNum = FMath::Max(0, ControllerId);
	OnPartyInviteAccepted.Broadcast(PendingInviteLobby);
}

void URPGSteamLobbySubsystem::HandleDungeonFlowStateChanged(
	const ERPGDungeonClientFlowState NewState,
	const FString& ErrorCode)
{
	if (PendingOperation == EPendingOperation::JoinBackend)
	{
		if (NewState == ERPGDungeonClientFlowState::InSession)
		{
			const FRPGDungeonSession DungeonSession =
				DungeonFlow->GetCurrentDungeonSession();
			if (!DungeonSession.DungeonSessionId.Equals(
				PendingDungeonSessionId,
				ESearchCase::CaseSensitive))
			{
				BeginDestroyLobby(
					TEXT("joined_backend_session_mismatch"));
				return;
			}

			PendingOperation = EPendingOperation::None;
			PendingDungeonSessionId.Reset();
			RefreshCurrentLobbyFromNamedSession();
			SetLobbyState(ERPGSteamLobbyState::InLobby);
		}
		else if (NewState == ERPGDungeonClientFlowState::Error)
		{
			BeginDestroyLobby(
				ErrorCode.IsEmpty()
					? TEXT("join_backend_session_failed")
					: ErrorCode);
		}
		return;
	}

	if (PendingOperation == EPendingOperation::LeaveBackend)
	{
		if (NewState == ERPGDungeonClientFlowState::Idle)
		{
			BeginDestroyLobby();
		}
		else if (NewState == ERPGDungeonClientFlowState::Error)
		{
			FailOperation(
				ErrorCode.IsEmpty()
					? TEXT("leave_backend_session_failed")
					: ErrorCode);
		}
	}
}
