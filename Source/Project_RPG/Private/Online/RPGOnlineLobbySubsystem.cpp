#include "Online/RPGOnlineLobbySubsystem.h"
#include "UObject/Package.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Online/RPGSteamLobbySubsystem.h"
#include "Trading/RPGTradeSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"

namespace
{
	bool HasUnresolvedLogoutSession(const FRPGDungeonSession& Session)
	{
		if (Session.DungeonSessionId.IsEmpty()) return !Session.State.IsEmpty();
		// Unknown nonempty states must not become an account-switch escape hatch.
		return !Session.State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive)
			&& !Session.State.Equals(TEXT("Failed"), ESearchCase::CaseSensitive)
			&& !Session.State.Equals(TEXT("Closed"), ESearchCase::CaseSensitive);
	}
}

bool URPGOnlineLobbySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && !IsRunningDedicatedServer();
}

void URPGOnlineLobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bShuttingDown = false;
	Collection.InitializeDependency<UHttpWebManager>();
	Collection.InitializeDependency<URPGSteamAuthSubsystem>();
	Collection.InitializeDependency<URPGDungeonSessionSubsystem>();
	Collection.InitializeDependency<URPGSteamLobbySubsystem>();
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickLobby), .25f);
}

void URPGOnlineLobbySubsystem::Deinitialize()
{
	bShuttingDown = true;
	OnLogoutCompleted.Clear();
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	if (LobbyWidget) LobbyWidget->RemoveFromParent();
	LobbyWidget = nullptr;
	Super::Deinitialize();
}

bool URPGOnlineLobbySubsystem::IsLobbyController(const APlayerController* Controller) const
{
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	// PIE duplicates the map package. Normalize only that world type; ordinary
	// game maps still require the exact configured package, including its path.
	const FString MapPackage = World && World->WorldType == EWorldType::PIE
		? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())
		: (World ? World->GetOutermost()->GetName() : FString());
	return !bShuttingDown && Controller && Controller->IsLocalController() && World && World->IsGameWorld()
		&& World->GetGameInstance() == GetGameInstance() && World->GetNetMode() == NM_Standalone
		&& MapPackage.Equals(LobbyMapPackage, ESearchCase::CaseSensitive);
}

bool URPGOnlineLobbySubsystem::IsLogoutAllowed(const bool bLocalLobby, const bool bHasCredential,
	const bool bActiveDungeon, const bool bUnresolvedAbandonment, const bool bParty, const bool bBusy)
{
	return bLocalLobby && bHasCredential && !bActiveDungeon && !bUnresolvedAbandonment && !bParty && !bBusy;
}

bool URPGOnlineLobbySubsystem::CanLogoutFromBackend(const APlayerController* Controller) const
{
	auto* Trades = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<URPGTradeSubsystem>() : nullptr;
	if (Trades && Trades->BlocksTransitions()) return false;
	UGameInstance* GI = GetGameInstance();
	const auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
	const auto* Auth = GI ? GI->GetSubsystem<URPGSteamAuthSubsystem>() : nullptr;
	const auto* Flow = GI ? GI->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	const auto* Steam = GI ? GI->GetSubsystem<URPGSteamLobbySubsystem>() : nullptr;
	if (!Web || !Auth || !Flow || !Steam || Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()) return false;
	return IsLogoutAllowed(IsLobbyController(Controller), Web->HasPlayerLogoutCredential(),
		HasUnresolvedLogoutSession(Flow->GetCurrentDungeonSession()), Flow->HasUnresolvedDungeonAbandonment(), Steam->IsInPartyLobby(),
		Auth->IsLoginRequestInFlight() || Flow->IsRequestInFlight() || Steam->IsLobbyRequestInFlight()
		|| Flow->GetFlowState() == ERPGDungeonClientFlowState::Connecting
		|| Flow->GetFlowState() == ERPGDungeonClientFlowState::ReturningToLobby
		|| (LobbyWidget && LobbyWidget->IsFrontendRequestInFlight()));
}

bool URPGOnlineLobbySubsystem::LogoutFromBackend(APlayerController* Controller)
{
	if (!CanLogoutFromBackend(Controller)) return false;
	auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	const FString SteamId = Web->GetAuthenticatedSteamId();
	const FString CharacterId = Flow->GetSelectedCharacterId();
	const FString SessionId = Flow->GetCurrentDungeonSession().DungeonSessionId;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	const TWeakObjectPtr<APlayerController> ExpectedController(Controller);
	return Web->RequestPlayerLogout([WeakThis, ExpectedController, SteamId, CharacterId, SessionId]()
	{
		const auto* Lobby = WeakThis.Get();
		if (!Lobby || !Lobby->IsLobbyController(ExpectedController.Get())) return false;
		UGameInstance* GI = Lobby->GetGameInstance();
		const auto* CurrentWeb = GI->GetSubsystem<UHttpWebManager>();
		const auto* CurrentFlow = GI->GetSubsystem<URPGDungeonSessionSubsystem>();
		const auto* Auth = GI->GetSubsystem<URPGSteamAuthSubsystem>();
		const auto* Steam = GI->GetSubsystem<URPGSteamLobbySubsystem>();
		auto* Player = ExpectedController.IsValid() ? ExpectedController->GetLocalPlayer() : nullptr;
		auto* Trades = Player ? Player->GetSubsystem<URPGTradeSubsystem>() : nullptr;
		return CurrentWeb && CurrentFlow && Auth && Steam
			&& (!Trades || !Trades->BlocksTransitions())
			&& CurrentWeb->GetAuthenticatedSteamId() == SteamId
			&& CurrentFlow->GetSelectedCharacterId() == CharacterId
			&& CurrentFlow->GetCurrentDungeonSession().DungeonSessionId == SessionId
			&& !HasUnresolvedLogoutSession(CurrentFlow->GetCurrentDungeonSession()) && !CurrentFlow->HasUnresolvedDungeonAbandonment()
			&& !CurrentFlow->HasUnresolvedDeparture() && !CurrentFlow->HasDepartureRecoveryError()
			&& !Auth->IsLoginRequestInFlight() && !Steam->IsInPartyLobby() && !Steam->IsLobbyRequestInFlight()
			&& CurrentFlow->GetFlowState() != ERPGDungeonClientFlowState::Connecting
			&& CurrentFlow->GetFlowState() != ERPGDungeonClientFlowState::ReturningToLobby;
	}, [WeakThis](const bool bSuccess)
	{
		if (auto* Lobby = WeakThis.Get(); Lobby && !Lobby->bShuttingDown)
		{
			if (bSuccess)
				Lobby->GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>()->ResetDungeonFlow(false);
			Lobby->OnLogoutCompleted.Broadcast(bSuccess);
		}
	});
}

bool URPGOnlineLobbySubsystem::TickLobby(float)
{
	UWorld* World = GetGameInstance()->GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!bAutoShowOnlineLobby || !IsLobbyController(PC))
	{
		if (LobbyWidget) LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
		return true;
	}
	if (LobbyWidget && LobbyWidget->GetOwningPlayer() != PC)
	{
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}
	if (!LobbyWidget)
	{
		LobbyWidget = CreateWidget<URPGOnlineLobbyWidget>(PC, URPGOnlineLobbyWidget::StaticClass());
		if (LobbyWidget)
		{
			LobbyWidget->AddToViewport(120);
			FInputModeGameAndUI Input;
			Input.SetWidgetToFocus(LobbyWidget->TakeWidget());
			Input.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Input);
			PC->bShowMouseCursor = true;
		}
	}
	return true;
}
