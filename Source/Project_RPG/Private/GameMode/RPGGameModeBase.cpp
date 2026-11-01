// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/RPGGameModeBase.h"
#include "UObject/Package.h"
#include "Item/RPGItemBase.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Component/RPGItemCommandComponent.h"
#include "Controller/RPGPlayerController.h"
#include "DataTable/DropItemData.h"
#include "Economy/RPGDungeonRewardDefinition.h"
#include "Online/RPGDungeonServerDefinition.h"
#include "Economy/RPGDungeonRewardSnapshot.h"
#include "Engine/World.h"
#include "Engine/NetConnection.h"
#include "Engine/ChildConnection.h"
#include "Engine/NetDriver.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UI/RPGContentClearPanel.h"
#include "GameInstance/RPGGameInstance.h"
#include "GameState/RPGGameStateBase.h"
#include "Manager/HttpWebManager.h"
#include "Player/RPGPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "RPGDebugHelper.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	bool IsLocalNetworkTestModeEnabled()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(
			FCommandLine::Get(),
			TEXT("RPGNetTestMode"));
#endif
	}

	bool IsItemE2EServerProxyEnabled()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		return FParse::Param(
			FCommandLine::Get(),
			TEXT("RPGItemE2EServer"));
#endif
	}

	bool IsItemE2EIdentityBypassEnabled()
	{
#if UE_BUILD_SHIPPING
		return false;
#else
		// A Development server may never disable identity verification on its
		// own. This exists solely for the installed-engine E2E proxy, where the
		// headless client deliberately runs without a Steam net driver.
		return IsItemE2EServerProxyEnabled()
			&& FParse::Param(
				FCommandLine::Get(),
				TEXT("RPGItemE2EAllowUnverifiedIdentity"));
#endif
	}

	bool IsGameModeServerRuntime()
	{
#if UE_BUILD_SHIPPING
		return IsRunningDedicatedServer();
#else
		return IsRunningDedicatedServer()
			|| IsItemE2EServerProxyEnabled();
#endif
	}

	bool IsRewardIdentifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len() > 64)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			const bool bAsciiAlphaNumeric =
				(Character >= TEXT('A') && Character <= TEXT('Z'))
				|| (Character >= TEXT('a') && Character <= TEXT('z'))
				|| (Character >= TEXT('0') && Character <= TEXT('9'));
			if (!bAsciiAlphaNumeric
				&& Character != TEXT('.')
				&& Character != TEXT('_')
				&& Character != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}

	bool IsBoundedBackendText(const FString& Value, const int32 MaximumLength)
	{
		if (Value.IsEmpty() || Value.Len() > MaximumLength)
		{
			return false;
		}
		for (const TCHAR Character : Value)
		{
			if (Character < TEXT(' ') || Character == TEXT('\x7f'))
			{
				return false;
			}
		}
		return true;
	}
}

void ARPGGameModeBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PollDungeonPreLoginConnections();
}

bool ARPGGameModeBase::IsDungeonPreLoginScopeCurrent(const FPreLoginConnectionState& State) const
{
	const UWorld* World = GetWorld();
	const UNetDriver* Driver = State.Driver.Get();
	return IsInGameThread() && !bDungeonConnectionReportingStopped && !IsEngineExitRequested()
		&& HasAuthority() && IsValid(World) && !World->bIsTearingDown && World->GetAuthGameMode() == this
		&& IsValid(Driver) && World->GetNetDriver() == Driver && Driver->GetWorld() == World
		&& !Driver->ServerConnection;
}

bool ARPGGameModeBase::IsDungeonPreLoginConnectionCurrent(const FPreLoginConnectionState& State) const
{
	UNetConnection* Connection = State.Connection.Get();
	if (State.LostAtUtc.GetTicks() > 0 || State.bLossDurablyQueued
		|| !IsDungeonPreLoginScopeCurrent(State) || !IsValid(Connection)
		|| Connection->GetDriver() != State.Driver.Get() || Cast<UChildConnection>(Connection)
		|| !Connection->Children.IsEmpty() || !State.Driver->ClientConnections.Contains(Connection)
		|| Connection->GetConnectionState() != USOCK_Open || Connection->PlayerId != State.PlayerId
		|| (Connection->ClientLoginState != EClientLoginState::LoggingIn
			&& Connection->ClientLoginState != EClientLoginState::Welcomed)) return false;
	int32 OptionsAt = INDEX_NONE;
	return Connection->RequestURL.FindChar(TEXT('?'), OptionsAt)
		&& UGameplayStatics::ParseOption(Connection->RequestURL.Mid(OptionsAt), TEXT("JoinTicket"))
			.Equals(State.Ticket, ESearchCase::CaseSensitive);
}

UNetConnection* ARPGGameModeBase::FindDungeonPreLoginConnection(
	const FString& Ticket, const FUniqueNetIdRepl& PlayerId) const
{
	UNetDriver* Driver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr;
	if (!IsValid(Driver) || Ticket.IsEmpty()) return nullptr;
	FPreLoginConnectionState Candidate;
	Candidate.Ticket = Ticket; Candidate.Driver = Driver; Candidate.PlayerId = PlayerId;
	UNetConnection* Found = nullptr;
	for (UNetConnection* Connection : Driver->ClientConnections)
	{
		Candidate.Connection = Connection;
		if (!IsDungeonPreLoginConnectionCurrent(Candidate)
			|| Connection->ClientLoginState != EClientLoginState::LoggingIn
			|| Connection->PlayerController || Connection->OwningActor) continue;
		if (Found) return nullptr; // Ambiguous/replayed tickets never select a replacement socket.
		Found = Connection;
	}
	return Found;
}

bool ARPGGameModeBase::ObserveDungeonPreLoginLoss(FPreLoginConnectionState& State) const
{
	if (!IsDungeonPreLoginScopeCurrent(State)) return false;
	if (State.LostAtUtc.GetTicks() > 0) return true;
	const UNetConnection* Connection = State.Connection.Get();
	if (Connection && Connection->GetDriver() && Connection->GetDriver() != State.Driver.Get()) return false;
	// List absence, changed identity, or a replaced driver is not disconnection evidence.
	if (Connection && Connection->GetConnectionState() != USOCK_Closed
		&& Connection->ClientLoginState != EClientLoginState::CleanedUp) return false;
	State.LostAtUtc = FDateTime::UtcNow();
	return true;
}

bool ARPGGameModeBase::QueueLostDungeonPreLoginAdmission(const FValidatedAdmission& Admission)
{
	const auto& State = Admission.PreLoginConnection;
	if (!State || !ObserveDungeonPreLoginLoss(*State)) return false;
	if (State->bLossDurablyQueued) return true;
	const double Now = FPlatformTime::Seconds();
	if (Now < State->NextReportAttemptAt) return false;
	State->bLossDurablyQueued = CompleteLostDungeonAdmission(Admission, State->LostAtUtc, false, 0);
	if (!State->bLossDurablyQueued) State->NextReportAttemptAt = Now + 30.0;
	return State->bLossDurablyQueued;
}

void ARPGGameModeBase::PollDungeonPreLoginConnections()
{
	if (PendingAdmissions.IsEmpty() && ValidatedAdmissions.IsEmpty()) return;
	UHttpWebManager* Web = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	TArray<FString> CompletedRecoveries;
	for (auto& Entry : PendingAdmissions)
	{
		const auto& Pending = Entry.Value;
		if (Pending.PreLoginConnection && ObserveDungeonPreLoginLoss(*Pending.PreLoginConnection) && Web)
			Web->ObserveDungeonConsumeConnectionLoss(Entry.Key, Pending.PlatformId, Pending.PreLoginConnection->LostAtUtc);
		if (Pending.bConsumeRecoveryExpired && Web && !Web->HasExpiredDungeonConsumeRecovery(Entry.Key))
			CompletedRecoveries.Add(Entry.Key);
	}
	for (const FString& Ticket : CompletedRecoveries) PendingAdmissions.Remove(Ticket);
	TArray<FString> Tickets;
	ValidatedAdmissions.GenerateKeyArray(Tickets);
	for (const FString& Ticket : Tickets)
	{
		const auto* Found = ValidatedAdmissions.Find(Ticket);
		if (!Found || !Found->PreLoginConnection) continue;
		const FValidatedAdmission Admission = *Found;
		const bool bRevoked = !GetDungeonAdmissionRevocationReason(Admission.DungeonSessionId,
			Admission.CharacterId, Admission.SteamId).IsEmpty();
		if (!bRevoked && !QueueLostDungeonPreLoginAdmission(Admission)) continue;
		const auto* Current = ValidatedAdmissions.Find(Ticket);
		if (Current && Current->ConnectionGeneration == Admission.ConnectionGeneration
			&& Current->PreLoginConnection == Admission.PreLoginConnection) ValidatedAdmissions.Remove(Ticket);
	}
}

void ARPGGameModeBase::RetainExpiredDungeonPreLogin(const FString& Ticket)
{
	FPendingAdmission* Pending = PendingAdmissions.Find(Ticket);
	if (!Pending || Pending->bConsumeRecoveryExpired) return;
	const FPendingAdmission Snapshot = *Pending;
	Pending->bConsumeRecoveryExpired = true;
	Pending->Completion.Unbind(); // Never retain an engine callback after terminal failure.
	if (Snapshot.PreLoginConnection && IsDungeonPreLoginConnectionCurrent(*Snapshot.PreLoginConnection))
		Snapshot.Completion.ExecuteIfBound(TEXT("dungeon_admission_recovery_expired"));
}

ARPGGameModeBase::ARPGGameModeBase()
{
	GameStateClass = ARPGGameStateBase::StaticClass();
	PlayerStateClass = ARPGPlayerState::StaticClass();
	GameNetDriverReplicationSystem = EReplicationSystem::Iris;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

#if WITH_EDITOR
EDataValidationResult ARPGGameModeBase::IsDataValid(
	FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(
		Super::IsDataValid(Context),
		EDataValidationResult::Valid);
	if (!DungeonServerDefinitions.IsEmpty())
	{
		FString Error;
		if (!URPGDungeonServerDefinition::ValidateCatalog(DungeonServerDefinitions, Error))
		{ Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
		return Result;
	}
	if (!bGiveReward)
	{
		return Result;
	}

	const ERPGGameDifficulty SupportedDifficulties[] =
	{
		ERPGGameDifficulty::Easy,
		ERPGGameDifficulty::Normal,
		ERPGGameDifficulty::Hard,
		ERPGGameDifficulty::Hell
	};
	for (const ERPGGameDifficulty Difficulty : SupportedDifficulties)
	{
		const TObjectPtr<URPGDungeonRewardDefinition>* RewardDefinition =
			DungeonClearRewardsByDifficulty.Find(Difficulty);
		if (!RewardDefinition || !IsValid(RewardDefinition->Get()))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("RPGGameModeBase", "MissingDungeonReward",
					"Reward-enabled GameMode is missing a dungeon reward for difficulty {0}."),
				FText::AsNumber(static_cast<int32>(Difficulty))));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		FString RewardVersion;
		TArray<FRPGCurrencyChange> CurrencyChanges;
		TArray<FRPGDungeonItemReward> ItemRewards;
		FString Error;
		if (!RewardDefinition->Get()->BuildSettlement(
			RewardVersion,
			CurrencyChanges,
			ItemRewards,
			Error))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("RPGGameModeBase", "InvalidDungeonReward",
					"Dungeon reward for difficulty {0} is invalid: {1}"),
				FText::AsNumber(static_cast<int32>(Difficulty)),
				FText::FromString(Error)));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}
#endif

void ARPGGameModeBase::BeginPlay()
{
	Super::BeginPlay();

	if (IsLocalNetworkTestModeEnabled())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RPG_NETTEST LOCAL_ADMISSION_BYPASS_ENABLED Development builds only."));
	}
	if (IsItemE2EIdentityBypassEnabled())
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("RPG_ITEM_E2E IDENTITY_MATCH_BYPASS_ENABLED Development builds only."));
	}

	if (IsGameModeServerRuntime())
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			if (UHttpWebManager* WebManager =
				GameInstance->GetSubsystem<UHttpWebManager>())
			{
				if (bRequireBackendJoinTicket)
				{
					WebManager->OnJoinTicketConsumed.RemoveDynamic(
						this,
						&ThisClass::HandleJoinTicketConsumed);
					WebManager->OnJoinTicketConsumed.AddDynamic(
						this,
						&ThisClass::HandleJoinTicketConsumed);
				}

				if (WebManager->IsConfiguredForDungeonServer())
				{
					WebManager->OnDungeonSessionUpdated.RemoveDynamic(
						this,
						&ThisClass::HandleDungeonSessionUpdated);
					WebManager->OnDungeonSessionUpdated.AddDynamic(
						this,
						&ThisClass::HandleDungeonSessionUpdated);
					WebManager->OnDungeonRewardSettlementRequested.RemoveDynamic(
						this,
						&ThisClass::HandleDungeonRewardSettlementRequested);
					WebManager->OnDungeonRewardSettlementRequested.AddDynamic(
						this,
						&ThisClass::HandleDungeonRewardSettlementRequested);
					bDungeonStartConfirmed = false;
					bDungeonFinishRequested = false;
					bDungeonFinishReported = false;
					bRequestedDungeonCleared = false;
					bRewardSettlementRequested = false;
					bRewardSettlementRequestInFlight = false;
					DungeonMemberCharacterIds.Reset();
					PendingRewardVersion.Reset();
					PendingCurrencyChanges.Reset();
					PendingItemRewards.Reset();
					DungeonMembershipPresentation = {};
					if (DungeonMembershipMonitor.Configure(WebManager->GetConfiguredDungeonSessionId(), WebManager->GetGameServerId()))
					{
						const float PollInterval = FMath::IsFinite(DungeonMembershipPollIntervalSeconds)
							? FMath::Clamp(DungeonMembershipPollIntervalSeconds, 1.f, 30.f) : 5.f;
						GetWorldTimerManager().SetTimer(DungeonMembershipTimer, this,
							&ThisClass::PollDungeonMembership, PollInterval, true);
						PollDungeonMembership();
					}
					WebManager->StartConfiguredDungeonSession();
					GetWorldTimerManager().SetTimer(
						DungeonHeartbeatTimer,
						this,
						&ThisClass::SendDungeonSessionHeartbeat,
						FMath::Max(5.f, DungeonHeartbeatIntervalSeconds),
						true);
				}
			}
		}
	}

	//마을이라거나 보상이 없는 맵은 제외한다.
	
	if (bGiveReward)
	{
		//InitializeRewardItems();
		if (URPGGameInstance* GI = GetGameInstance<URPGGameInstance>())
		{
			SetGameDifficulty(GI->GetPendingGameDifficulty());
		}
	}
}

void ARPGGameModeBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bDungeonStartConfirmed = false;
	ConfirmedDungeonDefinition = nullptr; ConfirmedDungeonRewardDefinition = nullptr;
	ConfirmedDungeonContentId.Reset(); ConfirmedDungeonRewardVersion.Reset();
	ConfirmedDungeonCurrencyChanges.Reset(); ConfirmedDungeonItemRewards.Reset();
	OnBackendDungeonDifficultyConfirmed.Clear();
	bDungeonConnectionReportingStopped = true;
	GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
	GetWorldTimerManager().ClearTimer(DungeonRewardRefreshTimer);
	GetWorldTimerManager().ClearTimer(DungeonMembershipTimer);
	DungeonMembershipMonitor.Reset();
	bRewardRefreshActive = false;
	RewardRefreshOutstanding.Reset();
	RewardRefreshInFlight.Reset();

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
			GameInstance->GetSubsystem<UHttpWebManager>())
		{
			WebManager->OnJoinTicketConsumed.RemoveDynamic(
				this,
				&ThisClass::HandleJoinTicketConsumed);
			WebManager->OnDungeonSessionUpdated.RemoveDynamic(
				this,
				&ThisClass::HandleDungeonSessionUpdated);
			WebManager->OnDungeonRewardSettlementRequested.RemoveDynamic(
				this,
				&ThisClass::HandleDungeonRewardSettlementRequested);
		}
	}

	for (TPair<FString, FPendingAdmission>& Pair : PendingAdmissions)
	{
		// Driver teardown owns socket closure; never invoke an old engine callback on a cleaned socket.
		Pair.Value.Completion.Unbind();
	}
	PendingAdmissions.Reset();
	ValidatedAdmissions.Reset();
	AdmissionsAwaitingBackend.Reset();
	ActivatedAdmissions.Reset();
	DungeonMemberCharacterIds.Reset();
	PendingRewardVersion.Reset();
	PendingCurrencyChanges.Reset();
	PendingItemRewards.Reset();
	bRewardSettlementRequestInFlight = false;
	Super::EndPlay(EndPlayReason);
}

void ARPGGameModeBase::PreLoginAsync(
	const FString& Options,
	const FString& Address,
	const FUniqueNetIdRepl& UniqueId,
	const FOnPreLoginCompleteDelegate& OnComplete)
{
	if (!IsGameModeServerRuntime()
		|| !bRequireBackendJoinTicket
		|| IsLocalNetworkTestModeEnabled())
	{
		Super::PreLoginAsync(
			Options,
			Address,
			UniqueId,
			OnComplete);
		return;
	}

	FString ErrorMessage;
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		OnComplete.ExecuteIfBound(ErrorMessage);
		return;
	}

	const FString JoinTicket =
		UGameplayStatics::ParseOption(Options, TEXT("JoinTicket"));
	if (JoinTicket.IsEmpty())
	{
		OnComplete.ExecuteIfBound(TEXT("missing_join_ticket"));
		return;
	}

	if (PendingAdmissions.Contains(JoinTicket)
		|| ValidatedAdmissions.Contains(JoinTicket))
	{
		OnComplete.ExecuteIfBound(TEXT("duplicate_join_ticket"));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager)
	{
		OnComplete.ExecuteIfBound(TEXT("backend_unavailable"));
		return;
	}
	if (!WebManager->CanDispatchConfiguredDungeonConnectionMutation())
	{
		// No ticket consumption and no permanent membership/reward revocation.
		OnComplete.ExecuteIfBound(TEXT("dungeon_connection_reports_pending"));
		return;
	}

	// Do not burn a one-use ticket before this server has applied its backend difficulty.
	if (WebManager->IsConfiguredForDungeonServer() && !bDungeonStartConfirmed)
	{
		OnComplete.ExecuteIfBound(TEXT("dungeon_configuration_not_ready"));
		return;
	}

	UNetConnection* Connection = FindDungeonPreLoginConnection(JoinTicket, UniqueId);
	if (!Connection)
	{
		OnComplete.ExecuteIfBound(TEXT("dungeon_prelogin_connection_unavailable"));
		return;
	}
	FPendingAdmission& Pending = PendingAdmissions.Add(JoinTicket);
	Pending.Completion = OnComplete;
	Pending.PreLoginConnection = MakeShared<FPreLoginConnectionState>();
	Pending.PreLoginConnection->Ticket = JoinTicket;
	Pending.PreLoginConnection->Connection = Connection;
	Pending.PreLoginConnection->Driver = Connection->GetDriver();
	Pending.PreLoginConnection->PlayerId = Connection->PlayerId;
	Pending.PlatformId = UniqueId.IsValid()
		? UniqueId.ToString()
		: FString();
	WebManager->ConsumeJoinTicket(JoinTicket, Pending.PlatformId);
}

FString ARPGGameModeBase::InitNewPlayer(
	APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId,
	const FString& Options,
	const FString& Portal)
{
	FString ErrorMessage = Super::InitNewPlayer(
		NewPlayerController,
		UniqueId,
		Options,
		Portal);
	if (!ErrorMessage.IsEmpty()
		// Installed-engine E2E uses a listen-server proxy.  The engine creates
		// this one local controller while loading the map, before any external
		// client can provide a validated ticket.  It is never a remote client;
		// all remote controllers still follow the validation path below.
		|| (IsItemE2EServerProxyEnabled()
			&& NewPlayerController
			&& NewPlayerController->IsLocalController())
		|| !IsGameModeServerRuntime()
		|| !bRequireBackendJoinTicket
		|| IsLocalNetworkTestModeEnabled())
	{
		return ErrorMessage;
	}

	const FString JoinTicket =
		UGameplayStatics::ParseOption(Options, TEXT("JoinTicket"));
	FValidatedAdmission Admission;
	const FValidatedAdmission* Validated = ValidatedAdmissions.Find(JoinTicket);
	if (!Validated)
	{
		return TEXT("join_ticket_not_validated");
	}
	Admission = *Validated;
	if (!Admission.PreLoginConnection || !IsDungeonPreLoginConnectionCurrent(*Admission.PreLoginConnection))
	{
		PollDungeonPreLoginConnections();
		return TEXT("dungeon_prelogin_connection_unavailable");
	}
	const FString RevocationReason = GetDungeonAdmissionRevocationReason(
		Admission.DungeonSessionId, Admission.CharacterId, Admission.SteamId);
	if (!RevocationReason.IsEmpty())
	{
		return RevocationReason;
	}

	ARPGPlayerState* PlayerState =
		NewPlayerController
			? NewPlayerController->GetPlayerState<ARPGPlayerState>()
			: nullptr;
	if (!PlayerState)
	{
		return TEXT("rpg_player_state_unavailable");
	}

	const TWeakObjectPtr<APlayerController> Key(NewPlayerController);
	if (AdmissionsAwaitingBackend.Contains(Key)) return TEXT("dungeon_admission_already_pending");
	FAdmissionAwaitingBackend& Awaiting = AdmissionsAwaitingBackend.Add(Key);
	Awaiting.Admission = MoveTemp(Admission);
	Awaiting.PlayerState = PlayerState;
	// Login attaches the UNetConnection only after InitNewPlayer returns. Do not
	// grant identity or spawn a pawn before the backend confirms this generation.
	return FString();
}

void ARPGGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	if ((IsItemE2EServerProxyEnabled() && NewPlayer && NewPlayer->IsLocalController())
		|| !IsGameModeServerRuntime() || !bRequireBackendJoinTicket
		|| IsLocalNetworkTestModeEnabled())
	{
		Super::PostLogin(NewPlayer);
		return;
	}

	const TWeakObjectPtr<APlayerController> Key(NewPlayer);
	FAdmissionAwaitingBackend* Awaiting = AdmissionsAwaitingBackend.Find(Key);
	UNetConnection* Connection = NewPlayer ? Cast<UNetConnection>(NewPlayer->Player) : nullptr;
	UHttpWebManager* Web = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	if (!Awaiting || !IsValid(Connection) || Cast<UChildConnection>(Connection)
		|| !Awaiting->Admission.PreLoginConnection
		|| !IsDungeonPreLoginConnectionCurrent(*Awaiting->Admission.PreLoginConnection)
		|| Awaiting->Admission.PreLoginConnection->Connection.Get() != Connection
		|| !Web || !Web->IsConfiguredForDungeonServer()
		|| Awaiting->Admission.DungeonSessionId != Web->GetConfiguredDungeonSessionId())
	{
		AdmissionsAwaitingBackend.Remove(Key);
		if (IsValid(Connection) && NewPlayer && NewPlayer->Player == Connection
			&& Connection->GetConnectionState() == USOCK_Open)
			Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended,
				TEXT("dungeon_admission_unavailable")));
		return;
	}

	Awaiting->Connection = Connection;
	const FString RevocationReason = GetDungeonAdmissionRevocationReason(
		Awaiting->Admission.DungeonSessionId, Awaiting->Admission.CharacterId, Awaiting->Admission.SteamId);
	if (!RevocationReason.IsEmpty())
	{
		// InitNewPlayer and PostLogin are distinct boundaries. A receipt learned
		// between them must not start confirmation or wait for another HTTP reply.
		AdmissionsAwaitingBackend.Remove(Key);
		Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, RevocationReason));
		return;
	}
	const TSharedRef<FAdmissionRequestState> RequestState =
		MakeShared<FAdmissionRequestState>();
	Awaiting->RequestState = RequestState;
	const FValidatedAdmission Expected = Awaiting->Admission;
	const TWeakObjectPtr<ARPGGameModeBase> WeakMode(this);
	const TWeakObjectPtr<APlayerController> WeakController(NewPlayer);
	const TWeakObjectPtr<UNetConnection> WeakConnection(Connection);
	const bool bConfirmationStarted = Web->ConfirmConfiguredDungeonMemberAdmission(Expected.DungeonSessionId,
		Expected.CharacterId, Expected.SteamId, Expected.ConnectionGeneration,
		[WeakMode, WeakController, WeakConnection, RequestState, Expected](bool bConfirmed, int32 HttpStatus)
		{
			if (ARPGGameModeBase* Mode = WeakMode.Get())
			{
				if (RequestState->bConnectionLostBeforeResponse)
				{
					if (!RequestState->bConnectionLossDurablyQueued)
						RequestState->bConnectionLossDurablyQueued = Mode->CompleteLostDungeonAdmission(
							Expected, RequestState->ConnectionLostAtUtc, bConfirmed, HttpStatus);
				}
				else
					Mode->CompleteDungeonAdmission(WeakController.Get(), WeakConnection.Get(),
						Expected, bConfirmed, HttpStatus);
			}
		});
	if (!bConfirmationStarted)
	{
		// PostLogin is still on the Login stack; World has not yet assigned
		// Connection->PlayerController. Do not use the async completion guard here.
		AdmissionsAwaitingBackend.Remove(Key);
		if (IsValid(Connection) && NewPlayer->Player == Connection
			&& Connection->GetConnectionState() == USOCK_Open)
			Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended,
				TEXT("dungeon_admission_request_failed")));
	}
	else if (Expected.PreLoginConnection)
	{
		const FString& Ticket = Expected.PreLoginConnection->Ticket;
		const auto* Current = ValidatedAdmissions.Find(Ticket);
		if (Current && Current->PreLoginConnection == Expected.PreLoginConnection
			&& Current->ConnectionGeneration == Expected.ConnectionGeneration) ValidatedAdmissions.Remove(Ticket);
	}
}

void ARPGGameModeBase::Logout(AController* Exiting)
{
	if (APlayerController* Controller = Cast<APlayerController>(Exiting))
	{
		const TWeakObjectPtr<APlayerController> Key(Controller);
		FValidatedAdmission LostAdmission;
		FValidatedAdmission PendingLostAdmission;
		TSharedPtr<FAdmissionRequestState> LostRequestState;
		const bool bUnexpectedLoss = GetUnexpectedDungeonConnectionCleanup(Controller, LostAdmission);
		const FDateTime ObservedAtUtc = FDateTime::UtcNow();
		if (FAdmissionAwaitingBackend* Awaiting = AdmissionsAwaitingBackend.Find(Key))
		{
			// OnNetCleanup clears Controller->Player before Destroyed/Logout. The
			// controller's scoped cleanup identity still identifies that exact socket.
			if (Awaiting->RequestState.IsValid()
				&& !Awaiting->RequestState->bConnectionLostBeforeResponse
				&& IsOwnedDungeonConnectionCleanup(Controller, Awaiting->Connection.Get(), Awaiting->PlayerState.Get()))
			{
				Awaiting->RequestState->ConnectionLostAtUtc = ObservedAtUtc;
				Awaiting->RequestState->bConnectionLostBeforeResponse = true;
				LostRequestState = Awaiting->RequestState;
				PendingLostAdmission = Awaiting->Admission;
			}
		}
		AdmissionsAwaitingBackend.Remove(Key);
		ActivatedAdmissions.Remove(Key);
		// The ticket was already consumed. Persist this exact generation now,
		// even if its admission-confirmation HTTP response has not returned.
		if (LostRequestState)
			LostRequestState->bConnectionLossDurablyQueued = CompleteLostDungeonAdmission(
				PendingLostAdmission, ObservedAtUtc, false, 0);
		// Remove local activation before dispatch; duplicate/reentrant Logout cannot
		// report twice. Backend generation fencing also rejects an old socket's HTTP.
		if (bUnexpectedLoss) CompleteLostDungeonAdmission(LostAdmission, ObservedAtUtc, true, 200);
	}
	Super::Logout(Exiting);
}

bool ARPGGameModeBase::IsOwnedDungeonConnectionCleanup(APlayerController* Controller,
	UNetConnection* Connection, ARPGPlayerState* State) const
{
	const UWorld* World = GetWorld();
	UNetDriver* Driver = IsValid(Connection) ? Connection->GetDriver() : nullptr;
	const ARPGPlayerController* RPGController = Cast<ARPGPlayerController>(Controller);
	return IsInGameThread() && !bDungeonConnectionReportingStopped && !IsEngineExitRequested()
		&& HasAuthority() && IsValid(World) && !World->bIsTearingDown
		&& World->GetAuthGameMode() == this && Controller && Controller->HasAuthority()
		&& Controller->GetWorld() == World && IsValid(Connection)
		&& RPGController && Connection == RPGController->GetConnectionBeingCleanedUp()
		&& IsValid(Driver) && Driver == World->GetNetDriver() && Driver->GetWorld() == World
		&& !Driver->ServerConnection && !Driver->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
		&& Connection->OwningActor == Controller
		// APlayerController::OnNetCleanup already nulled these two pointers.
		// Never accept a different controller/socket, even while cleanup is active.
		&& (!Connection->PlayerController || Connection->PlayerController == Controller)
		&& (!Controller->Player || Controller->Player == Connection)
		&& (!Controller->GetNetConnection() || Controller->GetNetConnection() == Connection)
		&& !Cast<UChildConnection>(Connection) && Connection->Children.IsEmpty() && !Connection->IsReplay()
		&& IsValid(State) && State->HasAuthority() && State->GetWorld() == World
		&& Controller->GetPlayerState<ARPGPlayerState>() == State && State->GetOwner() == Controller;
}

bool ARPGGameModeBase::GetUnexpectedDungeonConnectionCleanup(APlayerController* Controller,
	FValidatedAdmission& OutAdmission) const
{
	const FActivatedAdmission* Active = ActivatedAdmissions.Find(TWeakObjectPtr<APlayerController>(Controller));
	if (!Active || Active->bPlannedLobbyDisconnect
		|| !IsOwnedDungeonConnectionCleanup(Controller, Active->Connection.Get(), Active->PlayerState.Get())) return false;
	const ARPGPlayerState* State = Active->PlayerState.Get();
	const FValidatedAdmission& Expected = Active->Admission;
	if (!State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
		|| Expected.ConnectionGeneration <= 0
		|| State->GetBackendDungeonSessionId() != Expected.DungeonSessionId
		|| State->GetBackendCharacterId() != Expected.CharacterId
		|| State->GetAuthenticatedSteamId() != Expected.SteamId
		|| State->GetBackendConnectionGeneration() != Expected.ConnectionGeneration
		|| !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId).IsEmpty()) return false;
	OutAdmission = Expected;
	return true;
}

bool ARPGGameModeBase::RequestPlannedDungeonLobbyDisconnect(APlayerController* Controller)
{
	UWorld* World = GetWorld();
	if (!IsInGameThread() || bDungeonConnectionReportingStopped || IsEngineExitRequested()
		|| !HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this || !IsValid(Controller) || !Controller->HasAuthority()
		|| Controller->GetWorld() != World) return false;
	FActivatedAdmission* Active = ActivatedAdmissions.Find(TWeakObjectPtr<APlayerController>(Controller));
	if (!Active || Active->bPlannedLobbyDisconnect) return false;
	ARPGPlayerState* State = Active->PlayerState.Get();
	UNetConnection* Connection = Active->Connection.Get();
	UNetDriver* Driver = IsValid(Connection) ? Connection->GetDriver() : nullptr;
	const FValidatedAdmission& Expected = Active->Admission;
	if (!IsValid(State) || Controller->GetPlayerState<ARPGPlayerState>() != State
		|| State->GetOwner() != Controller || !State->HasAuthority() || State->GetWorld() != World
		|| !State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
		|| State->GetBackendDungeonSessionId() != Expected.DungeonSessionId
		|| State->GetBackendCharacterId() != Expected.CharacterId
		|| State->GetAuthenticatedSteamId() != Expected.SteamId
		|| Expected.ConnectionGeneration <= 0
		|| State->GetBackendConnectionGeneration() != Expected.ConnectionGeneration
		|| !IsValid(Connection) || Controller->Player != Connection || Controller->GetNetConnection() != Connection
		|| Connection->PlayerController != Controller || Connection->OwningActor != Controller
		|| Connection->GetConnectionState() != USOCK_Open || Cast<UChildConnection>(Connection)
		|| !Connection->Children.IsEmpty() || Connection->IsReplay()
		|| !IsValid(Driver) || Driver != World->GetNetDriver() || Driver->GetWorld() != World
		|| Driver->ServerConnection || !Driver->ClientConnections.Contains(Connection)
		|| !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId).IsEmpty()) return false;
	// This is not abandonment and does not revoke persistent membership. A client
	// cannot merely mark itself planned and remain connected: Close is synchronous.
	Active->bPlannedLobbyDisconnect = true;
	Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, TEXT("dungeon_planned_lobby_disconnect")));
	return Connection->GetConnectionState() == USOCK_Closed;
}

bool ARPGGameModeBase::CompleteLostDungeonAdmission(
	const FValidatedAdmission& Expected, FDateTime ObservedAtUtc, bool bConfirmed, int32 HttpStatus)
{
	if (!IsInGameThread()) return false;
	UWorld* World = GetWorld();
	if (bDungeonConnectionReportingStopped || IsEngineExitRequested()
		|| !HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this
		|| !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId,
			Expected.CharacterId, Expected.SteamId).IsEmpty()
		|| (!bConfirmed && HttpStatus >= 400 && HttpStatus < 500)) return false;
	UHttpWebManager* Web = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	if (Web && Web->ReportConfiguredDungeonMemberDisconnection(
			Expected.DungeonSessionId, Expected.CharacterId,
			Expected.SteamId, Expected.ConnectionGeneration, ObservedAtUtc)) return true;
	UE_LOG(LogTemp, Warning, TEXT("Lost admission connection report could not start."));
	return false;
}

void ARPGGameModeBase::RestartPlayer(AController* NewPlayer)
{
	APlayerController* Controller = Cast<APlayerController>(NewPlayer);
	if (Controller && IsGameModeServerRuntime() && bRequireBackendJoinTicket
		&& !IsLocalNetworkTestModeEnabled()
		&& !(IsItemE2EServerProxyEnabled() && Controller->IsLocalController()))
	{
		const ARPGPlayerState* State = Controller->GetPlayerState<ARPGPlayerState>();
		const UHttpWebManager* Web = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
		if (!ActivatedAdmissions.Contains(TWeakObjectPtr<APlayerController>(Controller))
			|| AdmissionsAwaitingBackend.Contains(TWeakObjectPtr<APlayerController>(Controller))
			|| !State || !State->HasAuthenticatedCharacter()
			|| State->GetBackendConnectionGeneration() <= 0
			|| !Web || !Web->IsConfiguredForDungeonServer()
			|| State->GetBackendDungeonSessionId() != Web->GetConfiguredDungeonSessionId()
			|| !GetDungeonAdmissionRevocationReason(State->GetBackendDungeonSessionId(),
				State->GetBackendCharacterId(), State->GetAuthenticatedSteamId()).IsEmpty()) return;
	}
	Super::RestartPlayer(NewPlayer);
}

void ARPGGameModeBase::CompleteDungeonAdmission(APlayerController* Controller,
	UNetConnection* ExpectedConnection, const FValidatedAdmission& Expected,
	bool bConfirmed, int32 HttpStatus)
{
	if (!IsInGameThread()) return;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this || !IsValid(Controller)) return;
	const TWeakObjectPtr<APlayerController> Key(Controller);
	FAdmissionAwaitingBackend* Awaiting = AdmissionsAwaitingBackend.Find(Key);
	if (!Awaiting || Awaiting->Connection.Get() != ExpectedConnection
		|| Awaiting->Admission.DungeonSessionId != Expected.DungeonSessionId
		|| Awaiting->Admission.CharacterId != Expected.CharacterId
		|| Awaiting->Admission.SteamId != Expected.SteamId
		|| Awaiting->Admission.ConnectionGeneration != Expected.ConnectionGeneration) return;
	ARPGPlayerState* State = Awaiting->PlayerState.Get();
	AdmissionsAwaitingBackend.Remove(Key);

	UNetDriver* Driver = IsValid(ExpectedConnection) ? ExpectedConnection->GetDriver() : nullptr;
	auto CloseOwnConnection = [&]()
	{
		if (IsValid(World) && !World->bIsTearingDown && World->GetAuthGameMode() == this
			&& IsValid(Controller) && IsValid(ExpectedConnection) && IsValid(Driver)
			&& Driver == World->GetNetDriver() && Controller->Player == ExpectedConnection
			&& Controller->GetNetConnection() == ExpectedConnection
			&& ExpectedConnection->PlayerController == Controller
			&& ExpectedConnection->GetConnectionState() == USOCK_Open
			&& !Cast<UChildConnection>(ExpectedConnection)
			&& ExpectedConnection->Children.IsEmpty())
		{
			// A confirmed admission may have committed even when its HTTP reply
			// was lost. Report only this owned socket/generation before failing closed.
			// A definitive 4xx response cannot have confirmed admission.
			if (GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId,
					Expected.CharacterId, Expected.SteamId).IsEmpty()
				&& (bConfirmed || HttpStatus < 400 || HttpStatus >= 500))
			{
				UHttpWebManager* Web = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
				if (!Web || !Web->ReportConfiguredDungeonMemberDisconnection(
						Expected.DungeonSessionId, Expected.CharacterId,
						Expected.SteamId, Expected.ConnectionGeneration, FDateTime::UtcNow()))
					UE_LOG(LogTemp, Warning,
						TEXT("Dungeon admission loss report could not start."));
			}
			ExpectedConnection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended,
				TEXT("dungeon_admission_unconfirmed")));
		}
	};
	if (!IsValid(ExpectedConnection) || !IsValid(Driver) || Driver != World->GetNetDriver()
		|| Controller->GetWorld() != World || Controller->Player != ExpectedConnection
		|| Controller->GetNetConnection() != ExpectedConnection
		|| ExpectedConnection->PlayerController != Controller
		|| ExpectedConnection->GetConnectionState() != USOCK_Open
		|| Cast<UChildConnection>(ExpectedConnection) || !ExpectedConnection->Children.IsEmpty()
		|| !IsValid(State) || State != Controller->GetPlayerState<ARPGPlayerState>()
		|| State->GetOwner() != Controller || !State->HasAuthority())
	{
		CloseOwnConnection();
		return;
	}

	if (!bConfirmed || !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId,
			Expected.CharacterId, Expected.SteamId).IsEmpty()
		|| State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
		|| Controller->GetPawn())
	{
		UE_LOG(LogTemp, Warning, TEXT("Dungeon admission confirmation rejected (HTTP %d)."), HttpStatus);
		CloseOwnConnection();
		return;
	}

	State->SetAuthenticatedIdentity(Expected.CharacterId, Expected.SteamId,
		Expected.DungeonSessionId, Expected.ConnectionGeneration);
	// Identity observers may revoke or disconnect synchronously. Do not publish
	// PostLogin/spawn against an actor or socket that changed during callbacks.
	if (!IsValid(Controller) || !IsValid(State) || Controller->Player != ExpectedConnection
		|| ExpectedConnection->GetConnectionState() != USOCK_Open
		|| Controller->GetPlayerState<ARPGPlayerState>() != State
		|| !State->HasAuthenticatedCharacter()
		|| State->GetBackendDungeonSessionId() != Expected.DungeonSessionId
		|| State->GetBackendCharacterId() != Expected.CharacterId
		|| State->GetAuthenticatedSteamId() != Expected.SteamId
		|| State->GetBackendConnectionGeneration() != Expected.ConnectionGeneration
		|| !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId).IsEmpty())
	{
		CloseOwnConnection();
		return;
	}
	if (URPGInventoryProjectionComponent* Projection =
		Controller->FindComponentByClass<URPGInventoryProjectionComponent>())
		Projection->LoadAuthenticatedCharacterItems();
	if (!IsValid(Controller) || !IsValid(State) || Controller->Player != ExpectedConnection
		|| ExpectedConnection->GetConnectionState() != USOCK_Open
		|| Controller->GetPlayerState<ARPGPlayerState>() != State
		|| !State->HasAuthenticatedCharacter()
		|| State->GetBackendDungeonSessionId() != Expected.DungeonSessionId
		|| State->GetBackendCharacterId() != Expected.CharacterId
		|| State->GetAuthenticatedSteamId() != Expected.SteamId
		|| State->GetBackendConnectionGeneration() != Expected.ConnectionGeneration
		|| !GetDungeonAdmissionRevocationReason(Expected.DungeonSessionId, Expected.CharacterId, Expected.SteamId).IsEmpty())
	{
		CloseOwnConnection();
		return;
	}
	FActivatedAdmission& Active = ActivatedAdmissions.Add(Key);
	Active.Admission = Expected;
	Active.PlayerState = State;
	Active.Connection = ExpectedConnection;
	Super::PostLogin(Controller);
}

void ARPGGameModeBase::HandleJoinTicketConsumed(
	const FString& JoinTicket,
	const FString& DungeonSessionId,
	const FString& CharacterId,
	const FString& SteamId,
	int64 ConnectionGeneration,
	bool bSuccess)
{
	const UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance ? GameInstance->GetSubsystem<UHttpWebManager>() : nullptr;
	if (!bSuccess && WebManager && WebManager->HasExpiredDungeonConsumeRecovery(JoinTicket))
	{
		RetainExpiredDungeonPreLogin(JoinTicket);
		PollDungeonPreLoginConnections();
		return;
	}
	FPendingAdmission Pending;
	if (!PendingAdmissions.RemoveAndCopyValue(JoinTicket, Pending))
	{
		return;
	}
	auto CompleteCurrentConnection = [&](const FString& Error)
	{
		if (Pending.PreLoginConnection && IsDungeonPreLoginConnectionCurrent(*Pending.PreLoginConnection))
			Pending.Completion.ExecuteIfBound(Error);
	};

	if (!bSuccess || !Pending.PreLoginConnection
		|| DungeonSessionId.IsEmpty()
		|| CharacterId.IsEmpty()
		|| SteamId.IsEmpty()
		|| ConnectionGeneration <= 0)
	{
		CompleteCurrentConnection(TEXT("invalid_join_ticket"));
		return;
	}

	if (!WebManager
		|| !DungeonSessionId.Equals(
			WebManager->GetConfiguredDungeonSessionId(),
			ESearchCase::CaseSensitive))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Rejected a join ticket assigned to another dungeon session."));
		CompleteCurrentConnection(
			TEXT("dungeon_session_mismatch"));
		return;
	}

	if (bRequireSteamIdentityMatch
		&& !IsItemE2EIdentityBypassEnabled()
		&& (Pending.PlatformId.IsEmpty()
			|| !Pending.PlatformId.Equals(
				SteamId,
				ESearchCase::CaseSensitive)))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Rejected a join ticket because the connection identity did not match."));
		CompleteCurrentConnection(TEXT("steam_identity_mismatch"));
		return;
	}

	const FString RevocationReason = GetDungeonAdmissionRevocationReason(DungeonSessionId, CharacterId, SteamId);
	if (!RevocationReason.IsEmpty())
	{
		CompleteCurrentConnection(RevocationReason);
		return;
	}

	FValidatedAdmission& Admission = ValidatedAdmissions.Add(JoinTicket);
	Admission.DungeonSessionId = DungeonSessionId;
	Admission.CharacterId = CharacterId;
	Admission.SteamId = SteamId;
	Admission.ConnectionGeneration = ConnectionGeneration;
	Admission.PreLoginConnection = Pending.PreLoginConnection;
	PollDungeonPreLoginConnections();
	CompleteCurrentConnection(FString());
}

bool ARPGGameModeBase::IsDungeonAdmissionAbandoned(const FString& SessionId,
	const FString& CharacterId, const FString& SteamId) const
{
	return DungeonMembershipMonitor.GetSessionId() == SessionId
		&& DungeonMembershipMonitor.IsExplicitlyAbandoned(CharacterId, SteamId);
}

FString ARPGGameModeBase::GetDungeonAdmissionRevocationReason(const FString& SessionId,
	const FString& CharacterId, const FString& SteamId) const
{
	// Keep the evidence and close reasons separate. Missing members, failed reads
	// and a locally elapsed clock never authorize either form of revocation.
	if (IsDungeonAdmissionAbandoned(SessionId, CharacterId, SteamId))
		return TEXT("dungeon_member_abandoned");
	if (DungeonMembershipMonitor.GetSessionId() == SessionId
		&& DungeonMembershipMonitor.IsDisconnectedExpelled(CharacterId, SteamId))
		return TEXT("dungeon_member_disconnected_expired");
	return FString();
}

bool ARPGGameModeBase::RequestDungeonMemberEjection(APlayerController* Controller)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this || !IsValid(Controller)) return false;
	ARPGPlayerState* State = Controller->GetPlayerState<ARPGPlayerState>();
	UNetConnection* Connection = Cast<UNetConnection>(Controller->Player);
	const TWeakObjectPtr<APawn> ExpectedPawn(Controller->GetPawn());
	const bool bHadPawn = Controller->GetPawn() != nullptr;
	// Closing a primary connection also removes its child players. This game does
	// not support split-screen opt-out; refuse it instead of ejecting another member.
	auto IsSameOwnedConnection = [&]()
	{
		UNetDriver* Driver = IsValid(Connection) ? Connection->GetDriver() : nullptr;
		APawn* Pawn = ExpectedPawn.Get();
		return IsValid(Controller) && IsValid(State) && Controller->HasAuthority() && State->HasAuthority()
			&& Controller->GetWorld() == World && State->GetWorld() == World
			&& Controller->GetPlayerState<ARPGPlayerState>() == State && State->GetOwner() == Controller
			&& (!bHadPawn || IsValid(Pawn)) && Controller->GetPawn() == Pawn
			&& (!Pawn || (Pawn->GetWorld() == World && Pawn->HasAuthority() && Pawn->GetController() == Controller
				&& Pawn->GetPlayerState<ARPGPlayerState>() == State))
			&& IsValid(Connection) && Controller->Player == Connection && Controller->GetNetConnection() == Connection
			&& Connection->PlayerController == Controller && Connection->OwningActor == Controller
			&& !Cast<UChildConnection>(Connection) && Connection->Children.IsEmpty() && !Connection->IsReplay()
			&& Connection->GetConnectionState() == USOCK_Open
			&& IsValid(Driver) && Driver == World->GetNetDriver() && Driver->GetWorld() == World
			&& !Driver->ServerConnection && Driver->ClientConnections.Contains(Connection);
	};
	if (!IsSameOwnedConnection() || !State->HasAuthenticatedCharacter()) return false;
	const FString SessionId = State->GetBackendDungeonSessionId();
	const FString CharacterId = State->GetBackendCharacterId();
	const FString SteamId = State->GetAuthenticatedSteamId();
	const FString RevocationReason = GetDungeonAdmissionRevocationReason(SessionId, CharacterId, SteamId);
	if (RevocationReason.IsEmpty()
		|| !State->RevokeAuthenticatedIdentity(SessionId, CharacterId, SteamId)) return false;
	// Identity notifications can re-enter gameplay. Never close a replacement
	// connection, Pawn or PlayerState after a synchronous ownership change.
	if (!IsValid(World) || World->bIsTearingDown || World->GetAuthGameMode() != this || !IsSameOwnedConnection()) return false;
	Connection->Close(UE::Net::FNetCloseResult(ENetCloseResult::Extended, RevocationReason));
	// Do not KickPlayer/Destroy/UnPossess here: Iris may still have queued moves.
	// Net-driver cleanup removes the connection before normal OnNetCleanup/Logout.
	return Connection->GetConnectionState() == USOCK_Closed;
}

int32 ARPGGameModeBase::ApplyCachedDungeonRevocations()
{
	if (!IsInGameThread()) return 0;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this) return 0;
	const FString SessionId = DungeonMembershipMonitor.GetSessionId();
	const FString ServerId = DungeonMembershipMonitor.GetServerId();
	if (SessionId.IsEmpty() || ServerId.IsEmpty()) return 0;
	// Identity callbacks can remove controllers or end/reconfigure this scope.
	// Snapshot weak candidates, then recheck the live scope before every request.
	TArray<TWeakObjectPtr<APlayerController>> Candidates;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		Candidates.Add(It->Get());
	int32 ClosedConnections = 0;
	for (const TWeakObjectPtr<APlayerController>& Candidate : Candidates)
	{
		if (!IsValid(this) || !IsValid(World) || World->bIsTearingDown
			|| GetWorld() != World || World->GetAuthGameMode() != this
			|| DungeonMembershipMonitor.GetSessionId() != SessionId
			|| DungeonMembershipMonitor.GetServerId() != ServerId) break;
		// The existing boundary checks the current member/actor/connection and
		// exact immutable receipt, revokes authority, then defers cleanup to UE.
		if (RequestPendingDungeonAdmissionEjection(Candidate.Get())
			|| RequestDungeonMemberEjection(Candidate.Get())) ++ClosedConnections;
	}
	return ClosedConnections;
}

bool ARPGGameModeBase::CompleteDungeonMembershipRead(uint64 ReadSerial,
	const FRPGDungeonMembershipSnapshotResult& Result, double RequestStartedAtSeconds)
{
	if (!IsInGameThread()) return false;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !IsValid(World) || World->bIsTearingDown
		|| World->GetAuthGameMode() != this
		|| !DungeonMembershipMonitor.FinishRead(ReadSerial, Result)) return false;
	DungeonMembershipPresentation.Snapshot = Result.Snapshot;
	DungeonMembershipPresentation.Revision = ReadSerial;
	DungeonMembershipPresentation.ReceivedAtSeconds = FPlatformTime::Seconds();
	DungeonMembershipPresentation.bHasRequestClock = FMath::IsFinite(RequestStartedAtSeconds)
		&& RequestStartedAtSeconds > 0.0 && RequestStartedAtSeconds <= DungeonMembershipPresentation.ReceivedAtSeconds;
	DungeonMembershipPresentation.RequestStartedAtSeconds = DungeonMembershipPresentation.bHasRequestClock ? RequestStartedAtSeconds : 0.0;
	ApplyCachedDungeonRevocations();
	return true;
}

void ARPGGameModeBase::PollDungeonMembership()
{
	if (!IsGameModeServerRuntime() || !GetWorld() || GetWorld()->bIsTearingDown) return;
	UGameInstance* Instance = GetGameInstance();
	UHttpWebManager* Web = Instance ? Instance->GetSubsystem<UHttpWebManager>() : nullptr;
	if (!Web || !Web->IsConfiguredForDungeonServer()
		|| DungeonMembershipMonitor.GetSessionId() != Web->GetConfiguredDungeonSessionId()
		|| DungeonMembershipMonitor.GetServerId() != Web->GetGameServerId()) return;
	const FString ExpectedSessionId = DungeonMembershipMonitor.GetSessionId();
	const FString ExpectedServerId = DungeonMembershipMonitor.GetServerId();
	// Previously validated immutable evidence survives failed/empty later reads.
	// This also handles a connection that finished admission after the last sweep.
	ApplyCachedDungeonRevocations();
	if (!IsValid(this) || !GetWorld() || GetWorld()->bIsTearingDown
		|| GetWorld()->GetAuthGameMode() != this
		|| DungeonMembershipMonitor.GetSessionId() != ExpectedSessionId
		|| DungeonMembershipMonitor.GetServerId() != ExpectedServerId) return;
	uint64 ReadSerial = 0;
	if (!DungeonMembershipMonitor.BeginRead(ReadSerial)) return;
	const TWeakObjectPtr<ARPGGameModeBase> WeakMode(this);
	const TWeakObjectPtr<UWorld> ExpectedWorld(GetWorld());
	const TSharedRef<bool> bReadStarted = MakeShared<bool>(false);
	auto ReadSnapshot = [WeakMode, ExpectedWorld, ExpectedSessionId, ExpectedServerId,
		ReadSerial, bReadStarted]()
	{
		if (*bReadStarted) return;
		*bReadStarted = true;
		ARPGGameModeBase* Mode = WeakMode.Get();
		UWorld* World = ExpectedWorld.Get();
		if (!IsValid(Mode) || !IsValid(World) || World->bIsTearingDown
			|| Mode->GetWorld() != World || World->GetAuthGameMode() != Mode
			|| Mode->DungeonMembershipMonitor.GetSessionId() != ExpectedSessionId
			|| Mode->DungeonMembershipMonitor.GetServerId() != ExpectedServerId) return;
		UGameInstance* CurrentInstance = Mode->GetGameInstance();
		UHttpWebManager* CurrentWeb = CurrentInstance ? CurrentInstance->GetSubsystem<UHttpWebManager>() : nullptr;
		if (!CurrentWeb || !CurrentWeb->IsConfiguredForDungeonServer()
			|| CurrentWeb->GetConfiguredDungeonSessionId() != ExpectedSessionId
			|| CurrentWeb->GetGameServerId() != ExpectedServerId)
		{
			Mode->DungeonMembershipMonitor.FinishRead(ReadSerial, FRPGDungeonMembershipSnapshotResult());
			return;
		}
		const double RequestStartedAtSeconds = FPlatformTime::Seconds();
		const bool bSubmitted = CurrentWeb->LoadConfiguredDungeonMembershipSnapshot(
			[WeakMode, ExpectedWorld, ReadSerial, RequestStartedAtSeconds](FRPGDungeonMembershipSnapshotResult Result)
			{
				ARPGGameModeBase* CompletionMode = WeakMode.Get();
				UWorld* CompletionWorld = ExpectedWorld.Get();
				if (!IsValid(CompletionMode) || !IsValid(CompletionWorld) || CompletionWorld->bIsTearingDown
					|| CompletionMode->GetWorld() != CompletionWorld) return;
				CompletionMode->CompleteDungeonMembershipRead(ReadSerial, Result, RequestStartedAtSeconds);
			});
		if (!bSubmitted)
			Mode->DungeonMembershipMonitor.FinishRead(ReadSerial, FRPGDungeonMembershipSnapshotResult());
	};
	// The POST changes backend membership under its session lock; only the
	// subsequent validated GET may add immutable ejection evidence. A failed
	// POST still permits reading existing receipts (including Closed grace).
	if (!Web->ProcessConfiguredDungeonDisconnectionExpirations(
		[ReadSnapshot]() mutable
		{
			ReadSnapshot();
		})) ReadSnapshot();
}

void ARPGGameModeBase::SendDungeonSessionHeartbeat()
{
	if (bDungeonFinishReported)
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UHttpWebManager* WebManager =
				GameInstance->GetSubsystem<UHttpWebManager>())
		{
			if (!bDungeonStartConfirmed)
			{
				WebManager->StartConfiguredDungeonSession();
			}
			else if (bRewardSettlementRequested)
			{
				WebManager->HeartbeatConfiguredDungeonSession();
				SendPendingDungeonRewardSettlement();
			}
			else if (bDungeonFinishRequested)
			{
				WebManager->FinishConfiguredDungeonSession(
					bRequestedDungeonCleared);
			}
			else
			{
				WebManager->HeartbeatConfiguredDungeonSession();
			}
		}
	}
}

void ARPGGameModeBase::SendPendingDungeonRewardSettlement()
{
	if (!bRewardSettlementRequested
		|| bRewardSettlementRequestInFlight
		|| !bDungeonStartConfirmed
		|| DungeonMemberCharacterIds.IsEmpty())
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager || !WebManager->IsConfiguredForDungeonServer()
		|| !WebManager->CanDispatchConfiguredDungeonConnectionMutation())
	{
		return;
	}

	bRewardSettlementRequestInFlight = true;
	WebManager->SettleConfiguredDungeonRewards(
		PendingRewardVersion,
		PendingCurrencyChanges,
		PendingItemRewards);
}

void ARPGGameModeBase::FailDungeonForInvalidReward(const FString& Reason)
{
	UE_LOG(LogTemp, Error,
		TEXT("Dungeon clear reward is invalid; failing the session: %s"),
		*Reason);

	bRewardSettlementRequested = false;
	bRewardSettlementRequestInFlight = false;
	PendingRewardVersion.Reset();
	PendingCurrencyChanges.Reset();
	PendingItemRewards.Reset();
	ReportDungeonFinished(false);
}

void ARPGGameModeBase::ReportDungeonFinished(bool bCleared)
{
	if (bCleared)
	{
		const TArray<FRPGCurrencyChange> NoCurrencyChanges;
		const TArray<FRPGDungeonItemReward> NoItemRewards;
		ReportDungeonClearedWithRewards(
			TEXT("no_reward"),
			NoCurrencyChanges,
			NoItemRewards);
		return;
	}

	if (!IsRunningDedicatedServer()
		|| bDungeonFinishRequested
		|| bDungeonFinishReported
		|| bRewardSettlementRequested)
	{
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager || !WebManager->IsConfiguredForDungeonServer())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Cannot report dungeon completion: backend assignment is missing."));
		return;
	}

	bDungeonFinishRequested = true;
	bRequestedDungeonCleared = false;
	if (bDungeonStartConfirmed)
	{
		WebManager->FinishConfiguredDungeonSession(false);
	}
}

void ARPGGameModeBase::ReportDungeonClearedWithCurrencyReward(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges)
{
	const TArray<FRPGDungeonItemReward> NoItemRewards;
	ReportDungeonClearedWithRewards(
		RewardVersion,
		CurrencyChanges,
		NoItemRewards);
}

void ARPGGameModeBase::ReportConfiguredDungeonClear()
{
	if (!IsRunningDedicatedServer())
	{
		return;
	}
	const auto* GI = GetGameInstance();
	const auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
	if (Web && Web->IsConfiguredForDungeonServer() && !bDungeonStartConfirmed)
	{
		// Never snapshot an offline/default reward definition while startup is unresolved.
		return;
	}

	if (!bGiveReward)
	{
		ReportDungeonFinished(true);
		return;
	}
	if (ConfirmedDungeonDefinition)
	{
		// This copy was validated before admission. Late edits cannot retune the reward.
		ReportDungeonClearedWithRewards(ConfirmedDungeonRewardVersion,
			ConfirmedDungeonCurrencyChanges, ConfirmedDungeonItemRewards);
		return;
	}

	const TObjectPtr<URPGDungeonRewardDefinition>* RewardDefinition =
		DungeonClearRewardsByDifficulty.Find(CurrentGameDifficulty);
	if (!RewardDefinition || !IsValid(RewardDefinition->Get()))
	{
		FailDungeonForInvalidReward(FString::Printf(
			TEXT("No reward definition is configured for difficulty %d."),
			static_cast<int32>(CurrentGameDifficulty)));
		return;
	}

	FString RewardVersion;
	TArray<FRPGCurrencyChange> CurrencyChanges;
	TArray<FRPGDungeonItemReward> ItemRewards;
	FString ValidationError;
	if (!RewardDefinition->Get()->BuildSettlement(
		RewardVersion,
		CurrencyChanges,
		ItemRewards,
		ValidationError))
	{
		FailDungeonForInvalidReward(FString::Printf(
			TEXT("Reward definition %s failed validation: %s"),
			*RewardDefinition->Get()->GetPathName(),
			*ValidationError));
		return;
	}

	ReportDungeonClearedWithRewards(
		RewardVersion,
		CurrencyChanges,
		ItemRewards);
}

void ARPGGameModeBase::ReportDungeonClearedWithRewards(
	const FString& RewardVersion,
	const TArray<FRPGCurrencyChange>& CurrencyChanges,
	const TArray<FRPGDungeonItemReward>& ItemRewards)
{
	if (!IsRunningDedicatedServer()
		|| bDungeonFinishRequested
		|| bDungeonFinishReported
		|| bRewardSettlementRequested)
	{
		return;
	}

	if (!IsRewardIdentifier(RewardVersion.TrimStartAndEnd())
		|| CurrencyChanges.Num() > 16
		|| ItemRewards.Num() > 16
		|| !FRPGDungeonItemReward::HasValidPersonalLootPool(ItemRewards))
	{
		FailDungeonForInvalidReward(
			TEXT("Reward version, reward entry count, or personal loot pool is invalid."));
		return;
	}
	if (bDungeonStartConfirmed && DungeonMemberCharacterIds.IsEmpty())
	{
		FailDungeonForInvalidReward(
			TEXT("The confirmed dungeon session contains no reward members."));
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	const UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	TSet<FString> CurrencyCodes;
	for (const FRPGCurrencyChange& Change : CurrencyChanges)
	{
		const FString CurrencyCode = Change.CurrencyCode.ToString();
		if (!IsRewardIdentifier(CurrencyCode)
			|| Change.Delta <= 0
			|| CurrencyCodes.Contains(CurrencyCode))
		{
			FailDungeonForInvalidReward(
				TEXT("Currency changes require unique identifiers and positive deltas."));
			return;
		}
		CurrencyCodes.Add(CurrencyCode);
	}

	int64 TotalItemQuantity = 0;
	for (const FRPGDungeonItemReward& Reward : ItemRewards)
	{
		const FString DefinitionType = Reward.DefinitionType.ToString();
		const FString DefinitionName = Reward.DefinitionName.ToString();
		if (!IsBoundedBackendText(DefinitionType, 64)
			|| !IsBoundedBackendText(DefinitionName, 128)
			|| Reward.DefinitionVersion < 1
			|| Reward.Quantity < 1
			|| Reward.MaxStackSize < 0
			|| (Reward.MaxStackSize > 0 && Reward.Quantity > Reward.MaxStackSize)
			|| !Reward.HasValidEquipmentProgression()
			|| !Reward.Durability.IsValid()
			|| Reward.InstanceTags.Num() > 64
			|| Reward.StatValues.Num() > 128
			|| !StaticEnum<ERPGItemBindState>()->IsValidEnumValue(
				static_cast<int64>(Reward.BindState)))
		{
			FailDungeonForInvalidReward(
				TEXT("An item definition, quantity, bind state, durability, or metadata count is invalid."));
			return;
		}

		TArray<FGameplayTag> InstanceTags;
		Reward.InstanceTags.GetGameplayTagArray(InstanceTags);
		for (const FGameplayTag& InstanceTag : InstanceTags)
		{
			if (!IsBoundedBackendText(InstanceTag.ToString(), 128))
			{
				FailDungeonForInvalidReward(
					TEXT("An item instance tag exceeds the backend text limit."));
				return;
			}
		}

		TotalItemQuantity += Reward.Quantity;
		if (TotalItemQuantity > MAX_int32)
		{
			FailDungeonForInvalidReward(
				TEXT("The total item reward quantity is too large."));
			return;
		}

		TSet<FGameplayTag> StatTags;
		for (const FRPGDungeonItemRewardStat& Stat : Reward.StatValues)
		{
			if (!Stat.StatTag.IsValid()
				|| !IsBoundedBackendText(Stat.StatTag.ToString(), 128)
				|| !FMath::IsFinite(Stat.Value)
				|| StatTags.Contains(Stat.StatTag))
			{
				FailDungeonForInvalidReward(
					TEXT("Item stat tags must be bounded, unique, and have finite values."));
				return;
			}
			StatTags.Add(Stat.StatTag);
		}
	}

	if (!WebManager
		|| !WebManager->IsConfiguredForDungeonServer())
	{
		FailDungeonForInvalidReward(
			TEXT("The backend dungeon assignment is invalid."));
		return;
	}

	bRewardSettlementRequested = true;
	bRewardSettlementRequestInFlight = false;
	PendingRewardVersion = RewardVersion.TrimStartAndEnd();
	PendingCurrencyChanges = CurrencyChanges;
	PendingItemRewards = ItemRewards;
	SendPendingDungeonRewardSettlement();
}

void ARPGGameModeBase::HandleDungeonRewardSettlementRequested(
	const FString& State,
	int32 HttpStatus,
	bool bSuccess)
{
	if (!bRewardSettlementRequested)
	{
		return;
	}
	bRewardSettlementRequestInFlight = false;

	if (!bSuccess)
	{
		const bool bPermanentFailure = HttpStatus >= 400
			&& HttpStatus < 500
			&& HttpStatus != 408
			&& HttpStatus != 425
			&& HttpStatus != 429;
		UE_LOG(LogTemp, Warning,
			TEXT("Dungeon reward settlement was not accepted (HTTP %d, "
				"permanent=%s)."),
			HttpStatus,
			bPermanentFailure ? TEXT("true") : TEXT("false"));
		if (bPermanentFailure)
		{
			bRewardSettlementRequested = false;
			PendingRewardVersion.Reset();
			PendingCurrencyChanges.Reset();
			PendingItemRewards.Reset();
			if (HttpStatus == 400 || HttpStatus == 409)
			{
				ReportDungeonFinished(false);
			}
			else
			{
				bDungeonFinishReported = true;
				GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
			}
		}
		return;
	}

	bRewardSettlementRequested = false;
	bDungeonFinishRequested = true;
	bRequestedDungeonCleared = true;
	bDungeonFinishReported = true;
	RefreshingRewardVersion = PendingRewardVersion;
	RefreshingItemQuantity = 0;
	for (const FRPGDungeonItemReward& Item : PendingItemRewards)
	{
		// Old backends may omit receipt counts only for all-guaranteed bundles.
		if (Item.PersonalLootWeight > 0) { RefreshingItemQuantity = INDEX_NONE; break; }
		RefreshingItemQuantity += Item.Quantity;
	}
	RewardRefreshOutstanding = DungeonMemberCharacterIds;
	RewardRefreshInFlight.Reset();
	RewardRefreshDeadline = FPlatformTime::Seconds() + 120.0;
	bRewardRefreshActive = true;
	GetWorldTimerManager().SetTimer(DungeonRewardRefreshTimer, this,
		&ThisClass::PollCompletedDungeonRewards, 1.0f, true);
	PendingRewardVersion.Reset();
	PendingCurrencyChanges.Reset();
	PendingItemRewards.Reset();
	GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
	UE_LOG(LogTemp, Log,
		TEXT("Dungeon reward settlement accepted by the backend (state %s)."),
		*State);
}

void ARPGGameModeBase::PollCompletedDungeonRewards()
{
	if (!bRewardRefreshActive) return;
	if (RewardRefreshOutstanding.IsEmpty() || FPlatformTime::Seconds() >= RewardRefreshDeadline)
	{
		if (!RewardRefreshOutstanding.IsEmpty()) UE_LOG(LogTemp, Warning, TEXT("Reward projection refresh timed out for %d members; persisted rewards remain recoverable."), RewardRefreshOutstanding.Num());
		bRewardRefreshActive = false;
		GetWorldTimerManager().ClearTimer(DungeonRewardRefreshTimer);
		return;
	}
	UHttpWebManager* Web = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	if (!Web) return;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		ARPGPlayerState* Player = Controller ? Controller->GetPlayerState<ARPGPlayerState>() : nullptr;
		URPGInventoryProjectionComponent* Projection = Controller ? Controller->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
		const URPGItemCommandComponent* Commands = Controller ? Controller->FindComponentByClass<URPGItemCommandComponent>() : nullptr;
		if (!Player || !Projection || !Player->HasAuthenticatedCharacter()
			|| Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready
			|| (Commands && Commands->HasInFlightCommands())) continue;
		const FString CharacterId = Player->GetBackendCharacterId();
		if (!RewardRefreshOutstanding.Contains(CharacterId) || RewardRefreshInFlight.Contains(CharacterId)) continue;
		RewardRefreshInFlight.Add(CharacterId);
		const uint64 Generation = Projection->GetAuthoritativeSnapshotGeneration();
		const TWeakObjectPtr<ThisClass> WeakThis(this);
		const TWeakObjectPtr<APlayerController> WeakController(Controller);
		const bool bStarted = Web->LoadConfiguredDungeonRewardSnapshot(CharacterId,
			[WeakThis, WeakController, CharacterId, Generation](FRPGDungeonRewardSnapshotResult Result)
			{
				ThisClass* Self = WeakThis.Get();
				if (!Self || !Self->bRewardRefreshActive || !Self->RewardRefreshOutstanding.Contains(CharacterId)) return;
				Self->RewardRefreshInFlight.Remove(CharacterId);
				APlayerController* Current = WeakController.Get();
				ARPGPlayerState* PlayerState = Current ? Current->GetPlayerState<ARPGPlayerState>() : nullptr;
				URPGInventoryProjectionComponent* CurrentProjection = Current ? Current->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
				const URPGItemCommandComponent* CurrentCommands = Current ? Current->FindComponentByClass<URPGItemCommandComponent>() : nullptr;
				if (Result.Status != ERPGDungeonRewardSnapshotStatus::Ready || !PlayerState || !CurrentProjection
					|| PlayerState->GetBackendCharacterId() != CharacterId || Result.CharacterId != CharacterId
					|| CurrentProjection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready
					|| (CurrentCommands && CurrentCommands->HasInFlightCommands())
					|| !PlayerState->CanApplyCompletedRewardWallet(Result.Wallet, Result.DungeonSessionId)) return;
				FGuid Session;
				if (!FGuid::Parse(Result.DungeonSessionId, Session)) return;
				const FString MailContainer = TEXT("DungeonReward.") + Session.ToString(EGuidFormats::Digits).ToLower();
				int64 MailQuantity = 0;
				for (const FRPGItemRecord& Item : Result.Items)
					if (Item.GetLocation().ContainerType == ERPGItemContainerType::Mail && Item.GetLocation().ContainerId == MailContainer) MailQuantity += Item.GetQuantity();
				if (Result.DeliveredItemQuantity.IsSet() && Result.DeliveryRewardVersion != Self->RefreshingRewardVersion) return;
				const int32 GrantedQuantity = Result.DeliveredItemQuantity.Get(Self->RefreshingItemQuantity);
				if (GrantedQuantity < 0 || MailQuantity > GrantedQuantity) return;
				FRPGItemOwnerRef Owner;
				Owner.Type = ERPGItemOwnerType::Character;
				Owner.OwnerId = CharacterId;
				FString Error;
				if (!CurrentProjection->ApplyCompletedRewardSnapshot(Owner, Result.Items, Generation, &Error)) return;
				FRPGDungeonRewardDeliveryNotice Notice;
				Notice.DungeonSessionId = Result.DungeonSessionId;
				Notice.RewardVersion = Self->RefreshingRewardVersion;
				Notice.MailQuantity = static_cast<int32>(MailQuantity);
				Notice.InventoryQuantity = GrantedQuantity - Notice.MailQuantity;
				Notice.bApplied = true;
				if (!PlayerState->ApplyCompletedRewardWallet(Result.Wallet, Notice)) return;
				Self->RewardRefreshOutstanding.Remove(CharacterId);
				UE_LOG(LogTemp, Log, TEXT("RPG_REWARD_REFRESH_APPLIED Character=%s Inventory=%d Mail=%d"), *CharacterId, Notice.InventoryQuantity, Notice.MailQuantity);
			}, RefreshingRewardVersion);
		if (!bStarted) RewardRefreshInFlight.Remove(CharacterId);
	}
}

void ARPGGameModeBase::HandleDungeonSessionUpdated(
	const FRPGDungeonSession& DungeonSession,
	bool bSuccess)
{
	if (!bSuccess)
	{
		return;
	}

	const UGameInstance* GameInstance = GetGameInstance();
	UHttpWebManager* WebManager = GameInstance
		? GameInstance->GetSubsystem<UHttpWebManager>()
		: nullptr;
	if (!WebManager
		|| !HasAuthority()
		|| !WebManager->IsConfiguredForDungeonServer()
		|| !DungeonSession.DungeonSessionId.Equals(
			WebManager->GetConfiguredDungeonSessionId(),
			ESearchCase::CaseSensitive))
	{
		return;
	}

	if (DungeonSession.State.Equals(
		TEXT("InProgress"),
		ESearchCase::CaseSensitive))
	{
		if (!ApplyBackendDungeonConfiguration(DungeonSession, WebManager->GetConfiguredDungeonSessionId(),
			WebManager->GetGameServerId())) return;

		if (!bDungeonFinishReported)
		{
			if (bDungeonFinishRequested)
			{
				WebManager->FinishConfiguredDungeonSession(false);
			}
			else if (bRewardSettlementRequested)
			{
				if (DungeonMemberCharacterIds.IsEmpty())
				{
					FailDungeonForInvalidReward(
						TEXT("The confirmed dungeon session contains no reward members."));
				}
				else
				{
					SendPendingDungeonRewardSettlement();
				}
			}
		}
	}

	const TCHAR* ExpectedOutcome = bRequestedDungeonCleared
		? TEXT("Cleared")
		: TEXT("Failed");
	if (bDungeonFinishRequested
		&& DungeonSession.State.Equals(
			ExpectedOutcome,
			ESearchCase::CaseSensitive))
	{
		bDungeonFinishReported = true;
		GetWorldTimerManager().ClearTimer(DungeonHeartbeatTimer);
		UE_LOG(LogTemp, Log,
			TEXT("Dungeon session completion confirmed: %s."),
			ExpectedOutcome);
	}
}

void ARPGGameModeBase::SetGameDifficulty(ERPGGameDifficulty InGameDifficulty)
{
	const auto* GI = GetGameInstance();
	const auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
	if (bDungeonStartConfirmed || (Web && Web->IsConfiguredForDungeonServer()))
	{
		UE_LOG(LogTemp, Warning, TEXT("Online dungeon difficulty is owned by the backend assignment; local override ignored."));
		return;
	}
	ApplyGameDifficulty(InGameDifficulty);
}

bool ARPGGameModeBase::ApplyBackendDungeonConfiguration(const FRPGDungeonSession& Session,
	const FString& ExpectedSessionId, const FString& ExpectedServerId)
{
	UWorld* World = GetWorld();
	ERPGGameDifficulty BackendDifficulty = CurrentGameDifficulty;
	if (!HasAuthority() || bDungeonConnectionReportingStopped || !IsValid(World) || World->bIsTearingDown || World->GetAuthGameMode() != this
		|| ExpectedSessionId.IsEmpty() || ExpectedServerId.IsEmpty()
		|| !Session.DungeonSessionId.Equals(ExpectedSessionId, ESearchCase::CaseSensitive)
		|| !Session.ServerId.Equals(ExpectedServerId, ESearchCase::CaseSensitive)
		|| !Session.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
		|| !TryParseBackendDungeonDifficulty(Session.Difficulty, BackendDifficulty)
		|| (bDungeonStartConfirmed && BackendDifficulty != CurrentGameDifficulty)) return false;
	FRPGDungeonServerSelection Content;
	if (!DungeonServerDefinitions.IsEmpty())
	{
		FString Error;
		if (!URPGDungeonServerDefinition::BuildAssignedSelection(DungeonServerDefinitions, Session.DungeonId,
			World->GetOutermost()->GetName(), BackendDifficulty, Content, Error)) return false;
	}
	if (bDungeonStartConfirmed && (Content.Definition != ConfirmedDungeonDefinition.Get()
		|| Content.RewardDefinition != ConfirmedDungeonRewardDefinition.Get()
		|| (Content.Definition && (!Session.DungeonId.Equals(ConfirmedDungeonContentId, ESearchCase::CaseSensitive)
			|| !Content.RewardVersion.Equals(ConfirmedDungeonRewardVersion, ESearchCase::CaseSensitive))))) return false;
	const bool bFirstConfirmation = !bDungeonStartConfirmed;
	if (bFirstConfirmation)
	{
		ConfirmedDungeonDefinition = Content.Definition; ConfirmedDungeonRewardDefinition = Content.RewardDefinition;
		if (Content.Definition)
		{
			ConfirmedDungeonContentId = Session.DungeonId;
			ConfirmedDungeonRewardVersion = MoveTemp(Content.RewardVersion);
			ConfirmedDungeonCurrencyChanges = MoveTemp(Content.CurrencyChanges);
			ConfirmedDungeonItemRewards = MoveTemp(Content.ItemRewards);
		}
		ApplyGameDifficulty(BackendDifficulty);
		if (auto* Instance = GetGameInstance<URPGGameInstance>()) Instance->SetPendingGameDifficulty(BackendDifficulty);
	}
	bDungeonStartConfirmed = true;
	DungeonMemberCharacterIds.Reset();
	for (const auto& Member : Session.Members)
		if (!Member.CharacterId.IsEmpty()) DungeonMemberCharacterIds.Add(Member.CharacterId);
	if (bFirstConfirmation) OnBackendDungeonDifficultyConfirmed.Broadcast(BackendDifficulty);
	// Server BP may travel/end the world while initializing its encounter.
	const bool bConfigurationReady = IsValid(World) && !World->bIsTearingDown
		&& World->GetAuthGameMode() == this && bDungeonStartConfirmed;
	if (bConfigurationReady && bFirstConfirmation && ConfirmedDungeonDefinition && World->GetNetMode() == NM_DedicatedServer)
	{
		// Non-secret operational evidence, emitted only after the actual world
		// accepted the authenticated ID/map/difficulty and pinned its rewards.
		UE_LOG(LogTemp, Log, TEXT("RPG_DUNGEON_SERVER_READY Session=%s Server=%s Dungeon=%s Map=%s Difficulty=%s"),
			*Session.DungeonSessionId, *Session.ServerId, *ConfirmedDungeonContentId,
			*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), *Session.Difficulty);
	}
	return bConfigurationReady;
}

bool ARPGGameModeBase::TryParseBackendDungeonDifficulty(const FString& Value, ERPGGameDifficulty& OutDifficulty)
{
	for (const auto Difficulty : {ERPGGameDifficulty::Easy, ERPGGameDifficulty::Normal,
		ERPGGameDifficulty::Hard, ERPGGameDifficulty::Hell})
	{
		if (Value.Equals(StaticEnum<ERPGGameDifficulty>()->GetNameStringByValue(static_cast<int64>(Difficulty)),
			ESearchCase::CaseSensitive))
		{
			OutDifficulty = Difficulty;
			return true;
		}
	}
	return false;
}

void ARPGGameModeBase::ApplyGameDifficulty(ERPGGameDifficulty InGameDifficulty)
{
	switch (InGameDifficulty)
	{
	case ERPGGameDifficulty::Easy:
		CurrentGameDifficulty = ERPGGameDifficulty::Easy;
		InitializeRewardItems("EasyReward");
		break;
	case ERPGGameDifficulty::Normal:
		CurrentGameDifficulty = ERPGGameDifficulty::Normal;
		InitializeRewardItems("NormalReward");
		break;
	case ERPGGameDifficulty::Hard:
		CurrentGameDifficulty = ERPGGameDifficulty::Hard;
		InitializeRewardItems("HardReward");
		break;
	case ERPGGameDifficulty::Hell:
		CurrentGameDifficulty = ERPGGameDifficulty::Hell;
		InitializeRewardItems("HellReward");
		break;
	default:
		break;
	}
	
}

void ARPGGameModeBase::GiveContentReward(ARPGPlayer* Player)
{
	// Existing content Blueprints call this once per player. The first server
	// call queues the party-wide backend settlement; subsequent calls are
	// ignored by the GameMode settlement state guards.
	ReportConfiguredDungeonClear();

	//if (!RewardDataTable || !Player) return;

	//URPGInventoryComponent* PlayerInventory = Player->GetRPGInventory();

	//if (!PlayerInventory) return;

	////TODO:인벤토리에 아이템 지급

	//if (ClearPanelClass)
	//{
	//	URPGContentClearPanel* ClearPanel = CreateWidget<URPGContentClearPanel>(GetWorld(), ClearPanelClass);

	//	if (ClearPanel)
	//	{
	//		ClearPanel->AddToViewport();
	//		ClearPanel->SetRenderTranslation(FVector2D(CoordX, 0.f));
	//	}
	//}

	//for (URPGItemBase* RewardItem : RewardItems)
	//{
	//	if (RewardItem)
	//	{
	//		const FItemAddResult AddResult = PlayerInventory->HandleAddItem(RewardItem);

	//		switch (AddResult.OperationResult)
	//		{
	//		case EItemAddResult::IAR_NoItemAdded:
	//			break;
	//		case EItemAddResult::IAR_PartialAmountItemAdded:

	//			break;
	//		case EItemAddResult::IAR_AllItemAdded:
	//			Destroy();
	//			break;
	//		}

	//		UE_LOG(LogTemp, Warning, TEXT("%s"), *AddResult.ResultMessage.ToString());
	//	}

	//	else
	//	{
	//		Debug::Print("Player Inventory component is null..");
	//	}
	//}
}

void ARPGGameModeBase::InitializeRewardItems(FName InName)
{	
	//FName RowName("SurvivalReward");

	//FString ContextString = TEXT("Reward Lookup");

	/*const FItemDropTable* RewardData = RewardDataTable->FindRow<FItemDropTable>(InName, InName.ToString());

	if (!RewardData)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 데이터를 찾을 수 없습니다: %s"), *InName.ToString());
		return;
	}

	TArray<FName> ItemRowNames;
	TArray<int32> ItemQuantities;

	const TArray<FRewardItem>& Rewards = RewardData->RewardItemList;

	for (const FRewardItem& Reward : Rewards)
	{
		UE_LOG(LogTemp, Warning, TEXT("보상 아이템: %s, 수량: %d, 확률: %f"),
			*Reward.ItemRowName.ToString(), Reward.DropQuantity, Reward.DropChance);
		ItemRowNames.Add(Reward.ItemRowName);
		ItemQuantities.Add(Reward.DropQuantity);
	}
	
	for (int32 i = 0; i < ItemRowNames.Num(); i++)
	{
		const FRPGItemData* ItemData = ItemDataTable->FindRow<FRPGItemData>(ItemRowNames[i], ItemRowNames[i].ToString());
	
		if (ItemData)
		{
			URPGItemBase* RewardItem = NewObject<URPGItemBase>();

			

			RewardItems.Add(RewardItem);
		}
	}*/
}
