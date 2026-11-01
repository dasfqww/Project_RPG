#include "Tests/RPGDungeonFlowE2EProbe.h"

#include "AI/RPGBossAIComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGInventoryProjectionComponent.h"
#include "Components/Button.h"
#include "Components/CapsuleComponent.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/FileManager.h"
#include "Manager/HttpWebManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"

namespace
{
	URPGDungeonResultActionsWidget* FindResult(UWorld* World, APlayerController* PC)
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Widgets, URPGDungeonResultActionsWidget::StaticClass(), false);
		for (auto* Widget : Widgets)
			if (Widget->GetOwningPlayer() == PC && Widget->IsInViewport())
				return Cast<URPGDungeonResultActionsWidget>(Widget);
		return nullptr;
	}
	bool SameId(const FString& A, const FString& B) { return A.Equals(B, ESearchCase::CaseSensitive); }
}

void URPGDungeonFlowE2EClient::HandleResultAbandonment(const FRPGDungeonAbandonmentReceipt& Receipt,
	bool bSuccess, const FString& ErrorCode)
{
	const UWorld* World = GetGameInstance()->GetWorld();
	const bool bRecoveryReply = bResultAbandonmentResponseLoss && (Stage == 43 || Stage == 48);
	if (!bResultAbandonment || !SameId(CharacterId, ResultAbandonCharacter) || (Stage != 60 && !bRecoveryReply)
		|| bResultAbandonmentReceipt || !bSawReturning || !World || World->GetNetMode() != NM_Standalone
		|| World->GetOutermost()->GetName() != TEXT("/Game/Maps/LobbyMap"))
	{ Fail(TEXT("ResultAbandonmentReceiptOutsideLobbyOrInvalid")); return; }
	if (!bSuccess && bResultAbandonmentResponseLoss)
	{
		const auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
		const auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
		if (!Flow || !Web || !Flow->HasUnresolvedDungeonAbandonment()
			|| !SameId(Flow->GetSelectedCharacterId(), CharacterId)
			|| !SameId(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| !SameId(Web->GetAuthenticatedSteamId(), SteamId))
		{ Fail(TEXT("ResultAbandonmentLostReplyForgotIdentity")); return; }
		if (Stage == 60 && Web->HasAccessToken() && SameId(ErrorCode, TEXT("dungeon_abandonment_unconfirmed")))
		{
			Stage = 41;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON LOST_REPLY SameProcess=1 ActualLobby=1 IdentityRetained=1 NoImplicitReceipt=1"));
			return;
		}
		// A same-credential 401 invalidates auth, not the uncertain commit. The
		// transport deliberately keeps the opt-out result unconfirmed; the proxy
		// and legacy lobby probe independently verify the real 401 and live login.
		if (Stage == 43 && bAbandonmentReauthenticate && !Web->HasAccessToken()
			&& SameId(ErrorCode, TEXT("dungeon_abandonment_unconfirmed"))) return;
		Fail(TEXT("ResultAbandonmentUnexpectedRecoveryFailure")); return;
	}
	if (!bSuccess || !ErrorCode.IsEmpty() || !Receipt.IsValidFor(SessionId, CharacterId))
	{ Fail(TEXT("ResultAbandonmentReceiptOutsideLobbyOrInvalid")); return; }
	bResultAbandonmentReceipt = true;
	UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON RECEIPT SameProcess=1 ActualLobby=1 ValidatedPlayerPost=1 RewardExcluded=1"));
}

bool URPGDungeonFlowE2EClient::TickResultAbandonmentProbe(UWorld* World, APlayerController* PC)
{
	auto* Flow = GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>();
	auto* Web = GetGameInstance()->GetSubsystem<UHttpWebManager>();
	if (!Flow || !Web) return Fail(TEXT("ResultAbandonmentContextMissing"));
	if (Stage == 60)
	{
		if (!bResultAbandonmentReceipt) return true;
		if (World->GetNetMode() != NM_Standalone || World->GetOutermost()->GetName() != TEXT("/Game/Maps/LobbyMap")
			|| Flow->IsRequestInFlight() || Flow->HasUnresolvedDungeonAbandonment() || Flow->HasQueuedDungeonAbandonment()
			|| Flow->HasActiveDungeonSession() || !Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty()
			|| !SameId(Flow->GetSelectedCharacterId(), CharacterId) || !Web->HasAccessToken()
			|| !SameId(Web->GetAuthenticatedSteamId(), SteamId) || !SameId(Flow->GetLastErrorCode(), TEXT("dungeon_abandoned"))
			|| Flow->GetFlowState() != ERPGDungeonClientFlowState::Idle
			|| World->URL.HasOption(TEXT("JoinTicket=")) || !World->URL.Host.IsEmpty())
			return Fail(TEXT("ResultAbandonmentReceiptDidNotClearOnlySession"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON LOBBY_COMPLETE Receipt=1 SessionCleared=1 AuthSelectionRetained=1 NoJoinTicket=1"));
		Stage = 61;
		return false;
	}
	auto* Controller = Cast<ARPGPlayerController>(PC);
	if (!Controller) return Fail(TEXT("ResultAbandonmentControllerMissing"));
	if (World->GetNetMode() != NM_Client) return Fail(TEXT("ResultAbandonmentLostDungeonBeforeConsent"));
	auto* Battle = World->GetGameState<ARPGBossBattleGameState>();
	auto* State = PC->GetPlayerState<ARPGPlayerState>();
	if (!Battle || !State || State->GetRewardDeliveryNotice().bApplied
		|| !SameId(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
		|| !SameId(Flow->GetCurrentDungeonSession().State, TEXT("InProgress")))
		return Fail(TEXT("ResultAbandonmentUnexpectedSessionOrReward"));
	const auto Snapshot = Battle->GetBattleSnapshot();
	if (ResultAbandonmentPhase == 0)
	{
		if (Snapshot.BattleState != EBossBattleState::Defeated) return true;
		auto* Result = FindResult(World, PC);
		if (!Result) return true;
		ResultDefeatRevision = Snapshot.Revision;
		ResultOriginalPawn = PC->GetPawn();
		ResultOriginalBoss = Snapshot.Boss;
		if (!SameId(CharacterId, ResultAbandonCharacter))
		{
			ResultAbandonmentPhase = 1;
			UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON REMAINING_DEFEATED LiveResult=1 MembershipRetained=1"));
			return true;
		}
		auto* Open = Cast<UButton>(Result->GetWidgetFromName(TEXT("AbandonDungeonFromResultButton")));
		auto* Cancel = Cast<UButton>(Result->GetWidgetFromName(TEXT("CancelResultAbandonmentButton")));
		auto* Confirm = Cast<UButton>(Result->GetWidgetFromName(TEXT("ConfirmResultAbandonmentButton")));
		if (!Open || !Cancel || !Confirm || !Open->GetIsEnabled()) return true;
		if (!ClickFlowButton(Open)) return false;
		if (!Confirm->GetIsEnabled() || !Cancel->GetIsEnabled()) return Fail(TEXT("ResultAbandonmentConfirmationUnavailable"));
		if (!VerifyFlowConfirmationNavigation(Open, Confirm, Cancel)) return false;
		if (!ClickFlowButton(Cancel)) return false;
		if (Flow->HasUnresolvedDungeonAbandonment() || Flow->IsRequestInFlight()
			|| Flow->HasQueuedDungeonAbandonment() || !Open->GetIsEnabled()) return Fail(TEXT("ResultAbandonmentCancelMutatedSession"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON CANCEL LiveConfirmation=1 NoPost=1 NoTravel=1 MembershipRetained=1"));
		if (!ClickFlowButton(Open)) return false;
		if (!Confirm->GetIsEnabled()) return Fail(TEXT("ResultAbandonmentReopenFailed"));
		Stage = 60; // before the production callback/travel; the fixture never calls the POST itself
		if (!ClickFlowButton(Confirm)) return false;
		if (!Flow->HasQueuedDungeonAbandonment() || !Flow->HasUnresolvedDungeonAbandonment()
			|| !Flow->IsRequestInFlight() || Flow->GetFlowState() != ERPGDungeonClientFlowState::ReturningToLobby
			|| !SameId(Flow->GetCurrentDungeonSession().DungeonSessionId, SessionId)
			|| Controller->TryRequestBossBattleRestart()) return Fail(TEXT("ResultAbandonmentConsentNotPinnedBeforeTravel"));
		Confirm->OnClicked.Broadcast(); // duplicate dispatcher must not create a request while connected
		Cancel->OnClicked.Broadcast();
		// IsRequestInFlight also covers travel. Queued consent (not yet consumed
		// by BeginAbandonment) plus ReturningToLobby distinguishes it from a POST.
		if (!Flow->HasQueuedDungeonAbandonment() || !Flow->IsRequestInFlight()
			|| Flow->GetFlowState() != ERPGDungeonClientFlowState::ReturningToLobby)
			return Fail(TEXT("ResultAbandonmentDuplicateBypassedQueue"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON QUEUED LiveResultConfirmation=1 IdentityPinned=1 NoConnectedPost=1 RetryBlocked=1 DuplicateGuard=1"));
		return true;
	}
	if (ResultAbandonmentPhase == 1)
	{
		if (!IFileManager::Get().FileExists(*ResultRetryGateFile)) return true;
		if (Snapshot.BattleState != EBossBattleState::Defeated || Snapshot.Revision != ResultDefeatRevision)
			return Fail(TEXT("ResultRemainingBattleChangedBeforeRetry"));
		auto* Result = FindResult(World, PC);
		auto* Retry = Result ? Cast<UButton>(Result->GetWidgetFromName(TEXT("RetryBattleButton"))) : nullptr;
		if (!Retry || !Retry->GetIsEnabled()) return true;
		ResultAbandonmentPhase = 2;
		if (!ClickFlowButton(Retry)) return false;
		if (!Controller->IsBossBattleRestartPending() || Controller->CanRequestBossBattleRestart()
			|| Controller->TryRequestBossBattleRestart()) return Fail(TEXT("ResultRemainingRetryNotLocked"));
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON RETRY_CLICK LiveResult=1 ServerRPC=1 DuplicateGuard=1"));
		return true;
	}
	if (ResultAbandonmentPhase == 2)
	{
		if (Snapshot.BattleState != EBossBattleState::InProgress || Snapshot.Revision <= ResultDefeatRevision) return true;
		auto* Player = Cast<ARPGPlayer>(PC->GetPawn());
		const auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		if (!Player || Player != ResultOriginalPawn.Get() || !Snapshot.Boss || Snapshot.Boss == ResultOriginalBoss.Get()
			|| !ASC || ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)
			|| ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) <= 0.f
			|| !FMath::IsNearlyEqual(ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()),
				ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()))
			|| Player->GetCapsuleComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision
			|| !Player->InputComponent || Controller->IsBossBattleRestartPending()) return true;
		ResultAbandonmentPhase = 3;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON RETRY_RECOVERED SamePawn=1 FullHealth=1 DeathCleared=1 CapsuleInputReady=1 NewBoss=1 PendingCleared=1"));
	}
	return true;
}

bool URPGDungeonFlowE2EServer::ObserveResultAbandonmentRetry()
{
#if !UE_BUILD_SHIPPING
	if (ResultAbandonmentPhase == 0 && !IFileManager::Get().FileExists(*GatePath)) return false;
	auto* Battle = GetWorld()->GetGameState<ARPGBossBattleGameState>();
	if (!Battle) { Fail(TEXT("ResultAbandonmentServerBattleMissing")); return false; }
	const auto Snapshot = Battle->GetBattleSnapshot();
	if (ResultAbandonmentPhase == 0)
	{
		TArray<ARPGPlayer*> Players;
		bool bTargetPresent = false;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* PC = It->Get();
			auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
			auto* Player = PC ? Cast<ARPGPlayer>(PC->GetPawn()) : nullptr;
			auto* Projection = PC ? PC->FindComponentByClass<URPGInventoryProjectionComponent>() : nullptr;
			auto* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
			if (!State || !State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
				|| !ASC || !Projection || Projection->GetLoadState() != ERPGInventoryProjectionLoadState::Ready
				|| ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) <= 0.f)
			{ Fail(TEXT("ResultAbandonmentWipePartyNotReady")); return false; }
			Players.Add(Player);
			if (SameId(State->GetBackendCharacterId(), AbandonCharacter)) bTargetPresent = true;
			else ResultSurvivorPawn = Player;
		}
		if (Players.Num() != 2 || !bTargetPresent || !ResultSurvivorPawn.IsValid()
			|| Snapshot.BattleState != EBossBattleState::InProgress || !Snapshot.Boss)
		{ Fail(TEXT("ResultAbandonmentWipePartyMismatch")); return false; }
		InitialBoss = Snapshot.Boss;
		ResultInitialRevision = Snapshot.Revision;
		ResultAbandonmentPhase = 1;
		for (auto* Player : Players)
		{
			auto* ASC = Player->GetAbilitySystemComponent();
			UGameplayEffect* Damage = NewObject<UGameplayEffect>(this);
			Damage->DurationPolicy = EGameplayEffectDurationType::Instant;
			auto& Modifier = Damage->Modifiers.AddDefaulted_GetRef();
			Modifier.Attribute = URPGAttributeSet::GetTakeDamageAttribute();
			Modifier.ModifierOp = EGameplayModOp::Override;
			Modifier.ModifierMagnitude = FScalableFloat(ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) + 1.f);
			ASC->ApplyGameplayEffectToSelf(Damage, 1.f, ASC->MakeEffectContext());
			if (!ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
			{ Fail(TEXT("ResultAbandonmentWipeDidNotTriggerDeath")); return false; }
		}
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON SERVER_WIPE Party=2 ServerGASFixture=1 NoDirectBattleStateWrite=1"));
		return false;
	}
	if (Snapshot.BattleState != EBossBattleState::InProgress || Snapshot.Revision <= ResultInitialRevision) return false;
	if (ResultAbandonmentPhase == 1)
	{
		int32 Count = 0;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			auto* PC = It->Get();
			auto* State = PC ? PC->GetPlayerState<ARPGPlayerState>() : nullptr;
			if (!State || !State->HasAuthenticatedCharacter() || State->IsAuthenticatedIdentityRevoked()
				|| SameId(State->GetBackendCharacterId(), AbandonCharacter) || PC->GetPawn() != ResultSurvivorPawn.Get())
			{ Fail(TEXT("ResultAbandonmentDepartedMemberParticipatedInRetry")); return false; }
			++Count;
		}
		if (Count != 1 || !Snapshot.Boss || Snapshot.Boss == InitialBoss.Get())
		{ Fail(TEXT("ResultAbandonmentRetryPartyOrBossMismatch")); return false; }
		ResultAbandonmentPhase = 2;
		UE_LOG(LogTemp, Display, TEXT("DUNGEON_RESULT_ABANDON SERVER_RETRY Remaining=1 DepartedAbsent=1 SamePawn=1 NewBoss=1"));
	}
	return IFileManager::Get().FileExists(*ResultSettleGateFile);
#else
	return false;
#endif
}
