// Opt-in local network fixture. Uses real consent/flow/HTTP; never seeds identity or receipts.
#include "Tests/RPGDungeonFlowE2EProbe.h"
#if !UE_BUILD_SHIPPING
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/FileManager.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	URPGDungeonResultActionsWidget* FindCombatConsent(UWorld* World, APlayerController* PC)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, URPGDungeonResultActionsWidget::StaticClass(), false);
		URPGDungeonResultActionsWidget* Found = nullptr;
		for (auto* Widget : Widgets)
		{
			if (Widget->GetOwningPlayer() != PC || !Widget->IsInViewport()) continue;
			if (Found) return nullptr;
			Found = Cast<URPGDungeonResultActionsWidget>(Widget);
		}
		return Found;
	}
}
#endif

void URPGDungeonFlowE2EClient::HandleGameplayAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt,
	bool bSuccess, const FString& ErrorCode)
{
#if !UE_BUILD_SHIPPING
	const auto* World = GetGameInstance()->GetWorld();
	const bool bRecoveryReply = bGameplayAbandonmentResponseLoss && (Stage == 43 || Stage == 48);
	if (!bGameplayAbandonment || (Stage != 60 && !bRecoveryReply) || GameplayConsentPhase != 3 || bGameplayAbandonmentReceipt || !bSawReturning
		|| !World || World->GetNetMode() != NM_Standalone || World->GetOutermost()->GetName() != TEXT("/Game/Maps/LobbyMap"))
	{ Fail(TEXT("GameplayAbandonmentReceiptBeforeLobbyOrInvalid")); return; }
	if (!bSuccess && bGameplayAbandonmentResponseLoss)
	{
		const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
		const auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		if (!Flow || !Web || !Flow->HasUnresolvedDungeonAbandonment()
			|| Flow->GetSelectedCharacterId() != CharacterId || Flow->GetCurrentDungeonSession().DungeonSessionId != SessionId
			|| Web->GetAuthenticatedSteamId() != SteamId || ErrorCode != TEXT("dungeon_abandonment_unconfirmed"))
		{ Fail(TEXT("GameplayAbandonmentLostReplyForgotIdentity")); return; }
		if (Stage == 60 && Web->HasAccessToken())
		{
			Stage = 41;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON LOST_REPLY Session=%s Character=%s SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1"), *SessionId, *CharacterId);
			return;
		}
		if (Stage == 43 && bAbandonmentReauthenticate && !Web->HasAccessToken()) return;
		Fail(TEXT("GameplayAbandonmentUnexpectedRecoveryFailure")); return;
	}
	if (!bSuccess || !ErrorCode.IsEmpty() || !Receipt.IsValidFor(SessionId, CharacterId))
	{ Fail(TEXT("GameplayAbandonmentReceiptBeforeLobbyOrInvalid")); return; }
	bGameplayAbandonmentReceipt = true;
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON RECEIPT Session=%s Character=%s PlayerId=%d AbandonedAt=%s State=%s RewardExcluded=1 ActualLobby=1 SameProcess=1"),
		*SessionId, *CharacterId, GameplayPlayerId, *Receipt.AbandonedAt, *Receipt.SessionStateAtAbandonment);
#endif
}

bool URPGDungeonFlowE2EClient::TickGameplayAbandonmentProbe(UWorld* World, APlayerController* PC)
{
#if !UE_BUILD_SHIPPING
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	if (!Flow || !Web) return Fail(TEXT("GameplayAbandonmentMissingFlow"));
	if (Stage == 60)
	{
		if (!bGameplayAbandonmentReceipt) return true;
		if (!bSawReturning || World->GetNetMode() != NM_Standalone || World->GetOutermost()->GetName() != TEXT("/Game/Maps/LobbyMap")
			|| Flow->IsRequestInFlight() || Flow->HasQueuedDungeonAbandonment() || Flow->HasUnresolvedDungeonAbandonment()
			|| Flow->HasActiveDungeonSession() || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()
			|| Flow->GetSelectedCharacterId() != CharacterId || !Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId
			|| Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle || Flow->GetLastErrorCode() != TEXT("dungeon_abandoned")
			|| World->URL.HasOption(TEXT("JoinTicket=")) || !World->URL.Host.IsEmpty())
			return Fail(TEXT("GameplayAbandonmentLobbyReceiptDidNotClearOnlySession"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON LOBBY_COMPLETE Session=%s Character=%s Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1 SameProcess=1"), *SessionId, *CharacterId);
		Stage = 61;
		return false;
	}
	auto* Controller = Cast<ARPGPlayerController>(PC);
	auto* State = PC->GetPlayerState<ARPGPlayerState>();
	auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
	if (Stage != 3 || !Controller || !State || World->GetNetMode() != NM_Client
		|| !State->HasAuthenticatedCharacter() || State->GetBackendCharacterId() != CharacterId
		|| State->GetBackendDungeonSessionId() != SessionId || State->GetAuthenticatedSteamId() != SteamId
		|| State->GetRewardDeliveryNotice().bApplied
		|| !Web->HasAccessToken() || Web->GetAuthenticatedSteamId() != SteamId
		|| Flow->GetSelectedCharacterId() != CharacterId || Flow->GetCurrentDungeonSession().DungeonSessionId != SessionId
		|| Flow->HasUnresolvedDungeonAbandonment()) return Fail(TEXT("GameplayAbandonmentCombatScopeChanged"));
	// Identity/Pawn readiness may precede the first replicated battle snapshot.
	// Connection generations deliberately remain server-only; never copy one into the client fixture.
	if (!Battle || Battle->GetBattleSnapshot().BattleState == EBossBattleState::Waiting)
		return GameplayConsentPhase == 0 ? true : Fail(TEXT("GameplayAbandonmentBattleLost"));
	if (Battle->GetBattleSnapshot().BattleState != EBossBattleState::InProgress) return Fail(TEXT("GameplayAbandonmentBattleEnded"));
	if (GameplayConsentPhase == 0)
	{
		if (!IFileManager::Get().FileExists(*GameplayConsentGateFile)) return true;
		GameplayOriginalController = PC; GameplayOriginalPawn = PC->GetPawn(); GameplayOriginalState = State;
		GameplayOriginalConnection = PC->GetNetConnection(); GameplayPlayerId = State->GetPlayerId();
		bGameplayPreviousMoveIgnored = PC->IsMoveInputIgnored(); bGameplayPreviousLookIgnored = PC->IsLookInputIgnored();
		bGameplayPreviousCursor = PC->bShowMouseCursor;
		if (!GameplayOriginalPawn.IsValid() || !GameplayOriginalConnection.IsValid()
			|| !Controller->ShowDungeonAbandonmentConfirmation()) return Fail(TEXT("GameplayAbandonmentPublicEntryRejected"));
		GameplayConsentPhase = 1;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON OPEN Session=%s Character=%s PlayerId=%d PublicControllerEntry=1 LiveWidget=1 InProgress=1"), *SessionId, *CharacterId, GameplayPlayerId);
		return true;
	}
	if (GameplayOriginalController.Get() != PC || GameplayOriginalPawn.Get() != PC->GetPawn()
		|| GameplayOriginalState.Get() != State || GameplayOriginalConnection.Get() != PC->GetNetConnection()
		|| GameplayPlayerId != State->GetPlayerId()) return Fail(TEXT("GameplayAbandonmentCancelReplacedPeer"));
	if (GameplayConsentPhase == 1)
	{
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Widget(FindCombatConsent(World, PC));
		auto* Cancel = Widget.IsValid() ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelResultAbandonmentButton"))) : nullptr;
		auto* Confirm = Widget.IsValid() ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmResultAbandonmentButton"))) : nullptr;
		if (!Cancel || !Confirm || !Confirm->GetIsEnabled() || !PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored()
			|| !ClickFlowButton(Cancel)) return Fail(TEXT("GameplayAbandonmentCancelUnavailable"));
		if (Widget->IsInViewport() || PC->IsMoveInputIgnored() != bGameplayPreviousMoveIgnored
			|| PC->IsLookInputIgnored() != bGameplayPreviousLookIgnored || PC->bShowMouseCursor != bGameplayPreviousCursor
			|| !Flow->HasActiveDungeonSession() || Flow->HasUnresolvedDungeonAbandonment() || Flow->IsRequestInFlight())
			return Fail(TEXT("GameplayAbandonmentCancelChangedStateOrLocks"));
		GameplayConsentPhase = 2;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON CANCELLED Session=%s Character=%s PlayerId=%d SameControllerPawnStateConnection=1 InputRestored=1 NoConsent=1"), *SessionId, *CharacterId, GameplayPlayerId);
		return true;
	}
	if (GameplayConsentPhase == 2)
	{
		if (!IFileManager::Get().FileExists(*GameplayConfirmGateFile)) return true;
		if (!Controller->ShowDungeonAbandonmentConfirmation()) return Fail(TEXT("GameplayAbandonmentReopenRejected"));
		TStrongObjectPtr<URPGDungeonResultActionsWidget> Widget(FindCombatConsent(World, PC));
		auto* Confirm = Widget.IsValid() ? Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmResultAbandonmentButton"))) : nullptr;
		if (!Confirm || !Confirm->GetIsEnabled()) return Fail(TEXT("GameplayAbandonmentConfirmationUnavailable"));
		Stage = 60; GameplayConsentPhase = 3; // before synchronous flow/travel notifications
		if (!ClickFlowButton(Confirm) || !Flow->HasQueuedDungeonAbandonment() || !Flow->HasUnresolvedDungeonAbandonment()
			|| Flow->GetFlowState() != ERPGDungeonClientFlowState::ReturningToLobby || !Flow->HasActiveDungeonSession())
			return Fail(TEXT("GameplayAbandonmentConsentDidNotQueueLobby"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_GAMEPLAY_ABANDON CONFIRMED Session=%s Character=%s PlayerId=%d LiveButton=1 ConsentPinned=1 PlannedReturn=1 NoReceiptYet=1"), *SessionId, *CharacterId, GameplayPlayerId);
	}
	return true;
#else
	return false;
#endif
}
