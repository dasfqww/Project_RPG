#include "Online/RPGOnlineLobbySubsystem.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Online/RPGSteamLobbySubsystem.h"
#include "Trading/RPGTradeSubsystem.h"
#include "Auction/RPGAuctionSubsystem.h"
#include "Riding/RPGRidingCollectionSubsystem.h"
#include "Shop/RPGShopSubsystem.h"
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
	if (GEngine) GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleFrontendTravelFailure);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickLobby), .25f);
}

void URPGOnlineLobbySubsystem::Deinitialize()
{
	bShuttingDown = true;
	OnLogoutCompleted.Clear();
	if (GEngine) GEngine->OnTravelFailure().RemoveAll(this);
	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	if (LobbyWidget) LobbyWidget->RemoveFromParent();
	LobbyWidget = nullptr;
	Super::Deinitialize();
}

bool URPGOnlineLobbySubsystem::IsLobbyController(const APlayerController* Controller) const
{
	return IsLocalControllerInMap(Controller, LobbyMapPackage) || IsCharacterSelectionController(Controller);
}

bool URPGOnlineLobbySubsystem::IsCharacterSelectionController(const APlayerController* Controller) const
{
	return IsLocalControllerInMap(Controller, CharacterSelectionMapPackage);
}

bool URPGOnlineLobbySubsystem::IsSeparateGameplayLobbyController(const APlayerController* Controller) const
{
	return !CharacterSelectionMapPackage.IsEmpty() && LobbyMapPackage.StartsWith(TEXT("/Game/"))
		&& IsLocalControllerInMap(Controller, LobbyMapPackage);
}

bool URPGOnlineLobbySubsystem::IsLocalControllerInMap(const APlayerController* Controller, const FString& Package) const
{
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	// PIE duplicates the map package. Normalize only that world type; ordinary
	// game maps still require the exact configured package, including its path.
	const FString MapPackage = World && World->WorldType == EWorldType::PIE
		? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())
		: (World ? World->GetOutermost()->GetName() : FString());
	return !bShuttingDown && Controller && Controller->IsLocalController() && World && World->IsGameWorld()
		&& World->GetGameInstance() == GetGameInstance() && World->GetNetMode() == NM_Standalone
		&& MapPackage.Equals(Package, ESearchCase::CaseSensitive);
}

bool URPGOnlineLobbySubsystem::IsFrontendTravelPending() const
{
	const UWorld* World = GetGameInstance()->GetWorld();
	return World && PendingFrontendTravel.Get() == World;
}

bool URPGOnlineLobbySubsystem::HasFrontendTravelFailed() const
{
	const UWorld* World = GetGameInstance()->GetWorld();
	return World && FailedFrontendTravel.Get() == World;
}

bool URPGOnlineLobbySubsystem::OpenCharacterSelection(APlayerController* Controller)
{
	return IsSeparateGameplayLobbyController(Controller) && OpenFrontendMap(Controller, CharacterSelectionMapPackage);
}

bool URPGOnlineLobbySubsystem::EnterGameplayLobby(APlayerController* Controller)
{
	const auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	return IsCharacterSelectionController(Controller) && Web && Web->HasAccessToken() && Flow
		&& !Flow->GetSelectedCharacterId().IsEmpty() && !Flow->IsRequestInFlight()
		&& OpenFrontendMap(Controller, LobbyMapPackage);
}

bool URPGOnlineLobbySubsystem::OpenFrontendMap(APlayerController* Controller, const FString& Package)
{
	if (IsFrontendTravelPending()) return false;
	if (!FPackageName::IsValidLongPackageName(Package) || !FPackageName::DoesPackageExist(Package))
	{
		FailedFrontendTravel = Controller->GetWorld();
		return false;
	}
	FailedFrontendTravel.Reset();
	PendingFrontendTravel = Controller->GetWorld();
	UGameplayStatics::OpenLevel(Controller, FName(*Package), true);
	return true;
}

void URPGOnlineLobbySubsystem::HandleFrontendTravelFailure(UWorld* World, ETravelFailure::Type, const FString&)
{
	if (!World || PendingFrontendTravel.Get() != World) return;
	PendingFrontendTravel.Reset();
	FailedFrontendTravel = World;
}

bool URPGOnlineLobbySubsystem::IsLogoutAllowed(const bool bLocalLobby, const bool bHasCredential,
	const bool bActiveDungeon, const bool bUnresolvedAbandonment, const bool bParty, const bool bBusy)
{
	return bLocalLobby && bHasCredential && !bActiveDungeon && !bUnresolvedAbandonment && !bParty && !bBusy;
}

bool URPGOnlineLobbySubsystem::CanLogoutFromBackend(const APlayerController* Controller) const
{
    auto* Shop = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<URPGShopSubsystem>() : nullptr;
    if (Shop && Shop->BlocksTransitions()) return false;
    auto* Riding = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<URPGRidingCollectionSubsystem>() : nullptr;
    if (Riding && Riding->BlocksTransitions()) return false;
	auto* Trades = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<URPGTradeSubsystem>() : nullptr;
	if (Trades && Trades->BlocksTransitions()) return false;
	auto* Auctions = Controller && Controller->GetLocalPlayer() ? Controller->GetLocalPlayer()->GetSubsystem<URPGAuctionSubsystem>() : nullptr;
	if (Auctions && Auctions->BlocksTransitions()) return false;
	UGameInstance* GI = GetGameInstance();
	const auto* Web = GI ? GI->GetSubsystem<UHttpWebManager>() : nullptr;
	const auto* Auth = GI ? GI->GetSubsystem<URPGSteamAuthSubsystem>() : nullptr;
	const auto* Flow = GI ? GI->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	const auto* Steam = GI ? GI->GetSubsystem<URPGSteamLobbySubsystem>() : nullptr;
	if (!Web || !Auth || !Flow || !Steam || IsFrontendTravelPending() || Flow->HasUnresolvedDeparture() || Flow->HasDepartureRecoveryError()) return false;
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
		auto* Auctions = Player ? Player->GetSubsystem<URPGAuctionSubsystem>() : nullptr;
        auto* Riding = Player ? Player->GetSubsystem<URPGRidingCollectionSubsystem>() : nullptr;
        auto* Shop = Player ? Player->GetSubsystem<URPGShopSubsystem>() : nullptr;
		return CurrentWeb && CurrentFlow && Auth && Steam
            && (!Riding || !Riding->BlocksTransitions())
            && (!Shop || !Shop->BlocksTransitions())
			&& (!Trades || !Trades->BlocksTransitions())
			&& (!Auctions || !Auctions->BlocksTransitions())
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
	if (LobbyWidget && (LobbyWidget->GetWorld() != World || LobbyWidget->GetOwningPlayer() != PC || !LobbyWidget->IsInViewport()))
	{
		LobbyWidget->RemoveFromParent();
		LobbyWidget = nullptr;
	}
	const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	if (IsFrontendTravelPending()) return true;
	if (IsSeparateGameplayLobbyController(PC) && Flow && Flow->GetSelectedCharacterId().IsEmpty()
		&& !Flow->IsRequestInFlight() && !HasFrontendTravelFailed() && OpenCharacterSelection(PC)) return true;
	if (!LobbyWidget)
	{
		LobbyWidget = CreateWidget<URPGOnlineLobbyWidget>(PC, URPGOnlineLobbyWidget::StaticClass());
		if (LobbyWidget)
		{
			LobbyWidget->AddToViewport(120);
			if (IsCharacterSelectionController(PC))
			{
				FInputModeUIOnly Input;
				Input.SetWidgetToFocus(LobbyWidget->TakeWidget());
				PC->SetInputMode(Input);
			}
			else
			{
				FInputModeGameAndUI Input;
				Input.SetWidgetToFocus(LobbyWidget->TakeWidget());
				Input.SetHideCursorDuringCapture(false);
				PC->SetInputMode(Input);
			}
			PC->bShowMouseCursor = true;
		}
	}
	return true;
}
