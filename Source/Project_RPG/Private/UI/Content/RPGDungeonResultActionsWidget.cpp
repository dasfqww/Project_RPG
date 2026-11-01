#include "UI/Content/RPGDungeonResultActionsWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Player/RPGPlayerState.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/RPGConfirmationInput.h"
#include "UI/RPGMenuFocus.h"
#include "UI/Lobby/RPGOnlineLobbyNavigation.h"
#include "TimerManager.h"

FRPGDungeonResultActionsPresentation URPGDungeonResultActionsWidget::BuildPresentation(
	const FRPGDungeonSession& Session, const FString& SelectedCharacterId,
	const FString& AuthenticatedCharacterId, const FString& AuthenticatedSessionId,
	const FRPGDungeonRewardDeliveryNotice& Notice, const ERPGDungeonClientFlowState FlowState,
	const bool bRequestInFlight, const FString& ErrorCode, const bool bPollingExpired,
	const bool bBackendAuthenticated, const bool bAuthenticationRequest, const bool bAccountMatches,
	const bool bDefeatedResult, const bool bRetryRequest)
{
	FRPGDungeonResultActionsPresentation View;
	View.bSessionMatches = bAccountMatches && !SelectedCharacterId.IsEmpty() && !Session.DungeonSessionId.IsEmpty()
		&& SelectedCharacterId.Equals(AuthenticatedCharacterId, ESearchCase::CaseSensitive)
		&& Session.DungeonSessionId.Equals(AuthenticatedSessionId, ESearchCase::CaseSensitive)
		&& Session.Members.ContainsByPredicate([&](const FRPGDungeonSessionMember& Member)
			{ return Member.CharacterId.Equals(SelectedCharacterId, ESearchCase::CaseSensitive); });
	if (!View.bSessionMatches)
	{
		View.Status = NSLOCTEXT("RPGDungeonResult", "Unavailable", "Dungeon session is not ready.");
		return View;
	}
	const bool bReturning = FlowState == ERPGDungeonClientFlowState::ReturningToLobby;
	const bool bCleared = Session.State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive);
	const bool bFailed = Session.State.Equals(TEXT("Failed"), ESearchCase::CaseSensitive);
	const bool bTerminal = bCleared || bFailed;
	const bool bBusy = bRequestInFlight || bAuthenticationRequest || bReturning || bRetryRequest;
	const bool bRetryableDefeat = bDefeatedResult && Session.State.Equals(TEXT("InProgress"), ESearchCase::CaseSensitive)
		&& !(Notice.bApplied && Notice.DungeonSessionId.Equals(Session.DungeonSessionId, ESearchCase::CaseSensitive));
	View.bCanRetry = bRetryableDefeat && bBackendAuthenticated && !bBusy;
	View.bCanReturn = bBackendAuthenticated && bTerminal && !bBusy;
	View.bCanRefresh = bBackendAuthenticated && !bBusy;
	View.bCanAuthenticate = !bBackendAuthenticated && !bBusy;
	View.bCanDisconnect = !bBusy && ((bDefeatedResult && !bTerminal) || !bBackendAuthenticated || !ErrorCode.IsEmpty());
	if (Notice.bApplied && Notice.DungeonSessionId.Equals(Session.DungeonSessionId, ESearchCase::CaseSensitive)
		&& Notice.InventoryQuantity >= 0 && Notice.MailQuantity >= 0)
	{
		View.RewardSummary = FText::Format(NSLOCTEXT("RPGDungeonResult", "Rewards",
			"Rewards delivered: Inventory {0}, Mail {1}"),
			FText::AsNumber(Notice.InventoryQuantity), FText::AsNumber(Notice.MailQuantity));
	}
	if (bReturning)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Returning", "Returning to the lobby...");
	else if (bAuthenticationRequest)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Authenticating", "Signing in again with the same account...");
	else if (bRequestInFlight)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Checking", "Checking dungeon status...");
	else if (bRetryRequest)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Retrying", "Requesting a party retry from the server...");
	else if (!bBackendAuthenticated)
		View.Status = FText::Format(NSLOCTEXT("RPGDungeonResult", "AuthenticationRequired",
			"Authentication expired. Sign in again with the same Steam account, then recheck dungeon completion. {0}"), FText::FromString(ErrorCode));
	else if (!ErrorCode.IsEmpty())
		View.Status = FText::Format(NSLOCTEXT("RPGDungeonResult", "FailedRequest",
			"Unable to complete the request. Refresh or try again. ({0})"), FText::FromString(ErrorCode));
	else if (bCleared)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Saved", "Rewards saved. You can return to the lobby.");
	else if (bFailed)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Ended", "Dungeon ended. You can return to the lobby.");
	else if (bDefeatedResult && !bTerminal)
		View.Status = NSLOCTEXT("RPGDungeonResult", "Defeated", "Battle lost. Retry with the party, or disconnect to the lobby while retaining membership. Disconnecting is not abandonment.");
	else if (bPollingExpired)
		View.Status = NSLOCTEXT("RPGDungeonResult", "PendingTimeout", "Rewards are still pending. Refresh to check again.");
	else
		View.Status = NSLOCTEXT("RPGDungeonResult", "Pending", "Waiting for dungeon completion and reward settlement...");
	return View;
}

void URPGDungeonResultActionsWidget::InitializeResult(const bool bVictory)
{
	bVictoryResult = bVictory;
	bGameplayAbandonment = false;
}

void URPGDungeonResultActionsWidget::InitializeGameplayAbandonment()
{
	bVictoryResult = false;
	bGameplayAbandonment = true;
}

bool URPGDungeonResultActionsWidget::OpenGameplayAbandonmentConfirmation()
{
	if (!bGameplayAbandonment) return false;
	HandleAbandonClicked();
	return bAbandonmentConfirmationOpen;
}

TSharedRef<SWidget> URPGDungeonResultActionsWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Canvas;
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(680.f);
		UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Size);
		CanvasSlot->SetAnchors(FAnchors(.5f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(.5f, 1.f));
		CanvasSlot->SetPosition(FVector2D(0.f, -24.f));
		CanvasSlot->SetAutoSize(true);
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>();
		Border->SetBrushColor(FLinearColor(0.04f, 0.055f, 0.075f, 0.98f));
		Border->SetPadding(FMargin(16.f));
		Size->SetContent(Border);
		UVerticalBox* Rows = WidgetTree->ConstructWidget<UVerticalBox>();
		Border->SetContent(Rows);
		StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("DungeonStatus"));
		StatusText->SetAutoWrapText(true);
		Rows->AddChildToVerticalBox(StatusText);
		RewardText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("RewardSummary"));
		RewardText->SetAutoWrapText(true);
		Rows->AddChildToVerticalBox(RewardText);
		UHorizontalBox* Actions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Rows->AddChildToVerticalBox(Actions);
		auto AddAction = [this](UHorizontalBox* Row, const FName Name, const FText& Text)
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>();
			Label->SetText(Text);
			Button->SetContent(Label);
			Row->AddChildToHorizontalBox(Button)->SetPadding(FMargin(8.f));
			return Button;
		};
		RetryButton = AddAction(Actions, TEXT("RetryBattleButton"), NSLOCTEXT("RPGDungeonResult", "Retry", "Retry battle (party)"));
		ReturnButton = AddAction(Actions, TEXT("ReturnToLobbyButton"), NSLOCTEXT("RPGDungeonResult", "Return", "Return to lobby"));
		RefreshButton = AddAction(Actions, TEXT("RefreshDungeonButton"), NSLOCTEXT("RPGDungeonResult", "Refresh", "Refresh"));
		UHorizontalBox* AbandonActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Rows->AddChildToVerticalBox(AbandonActions);
		AbandonButton = AddAction(AbandonActions, TEXT("AbandonDungeonFromResultButton"),
			NSLOCTEXT("RPGDungeonResult", "Abandon", "로비 이동 후 본인 던전 포기 (개발 테스트)"));
		AbandonmentConfirmation = WidgetTree->ConstructWidget<UVerticalBox>();
		Rows->AddChildToVerticalBox(AbandonmentConfirmation);
		auto* Warning = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultAbandonmentWarning"));
		Warning->SetAutoWrapText(true);
		Warning->SetText(NSLOCTEXT("RPGDungeonResult", "AbandonWarning",
			"이 캐릭터만 포기합니다. 연결을 종료하고 로비에 도착한 뒤 같은 계정으로 포기를 요청합니다. 서버 영수증 전에는 보상 제외가 확정되지 않으며, 파티가 정산을 시작하면 거절될 수 있습니다. 응답 유실 시 같은 요청으로 재확인합니다. 남은 파티는 계속 진행합니다."));
		AbandonmentConfirmation->AddChildToVerticalBox(Warning);
		UHorizontalBox* ConfirmationActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		AbandonmentConfirmation->AddChildToVerticalBox(ConfirmationActions);
		ConfirmAbandonButton = AddAction(ConfirmationActions, TEXT("ConfirmResultAbandonmentButton"),
			NSLOCTEXT("RPGDungeonResult", "ConfirmAbandon", "보상 제외에 동의하고 이동 / 포기 요청"));
		CancelAbandonButton = AddAction(ConfirmationActions, TEXT("CancelResultAbandonmentButton"),
			NSLOCTEXT("RPGDungeonResult", "CancelAbandon", "취소"));
		UHorizontalBox* RecoveryActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Rows->AddChildToVerticalBox(RecoveryActions);
		DisconnectButton = AddAction(RecoveryActions, TEXT("DisconnectToLobbyButton"),
			NSLOCTEXT("RPGDungeonResult", "Disconnect", "Disconnect to lobby (session retained; rewards not yet confirmed)"));
		UHorizontalBox* LoginActions = WidgetTree->ConstructWidget<UHorizontalBox>();
		Rows->AddChildToVerticalBox(LoginActions);
		LoginSteamButton = AddAction(LoginActions, TEXT("ResultLoginSteamButton"), NSLOCTEXT("RPGDungeonResult", "LoginSteam", "Sign in again with Steam"));
		if (URPGOnlineLobbyWidget::IsDevelopmentUIEnabled())
			LoginDevelopmentButton = AddAction(LoginActions, TEXT("ResultLoginDevelopmentButton"), NSLOCTEXT("RPGDungeonResult", "LoginDevelopment", "Development re-login (not Steam)"));
	}
	return Super::RebuildWidget();
}

void URPGDungeonResultActionsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RPGConfirmationInput::ConfigureNavigation(ConfirmAbandonButton, CancelAbandonButton);
	DungeonFlow = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGDungeonSessionSubsystem>() : nullptr;
	WebManager = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
	Authentication = GetGameInstance() ? GetGameInstance()->GetSubsystem<URPGSteamAuthSubsystem>() : nullptr;
	RetryController = Cast<ARPGPlayerController>(GetOwningPlayer());
	if (RetryController.IsValid())
		RetryController->OnBossBattleRetryRequestChanged.AddUniqueDynamic(this, &ThisClass::HandleRetryRequestChanged);
	if (Authentication) Authentication->OnLoginCompleted.AddUniqueDynamic(this, &ThisClass::HandleLoginCompleted);
	if (DungeonFlow)
	{
		DungeonFlow->OnFlowStateChanged.AddUniqueDynamic(this, &ThisClass::HandleFlowChanged);
		DungeonFlow->OnDungeonSessionChanged.AddUniqueDynamic(this, &ThisClass::HandleSessionChanged);
	}
	if (ReturnButton) ReturnButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReturnClicked);
	if (RetryButton) RetryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRetryClicked);
	if (RefreshButton) RefreshButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRefreshClicked);
	if (DisconnectButton) DisconnectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDisconnectClicked);
	if (AbandonButton) AbandonButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAbandonClicked);
	if (ConfirmAbandonButton) ConfirmAbandonButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmAbandonClicked);
	if (CancelAbandonButton) CancelAbandonButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCancelAbandonClicked);
	if (LoginSteamButton) LoginSteamButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSteamLoginClicked);
	if (LoginDevelopmentButton) LoginDevelopmentButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDevelopmentLoginClicked);
	PollUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + 120.0 : 0.0;
	bDefeatStatusChecked = false;
	RefreshPresentation();
	if (GetWorld()) GetWorld()->GetTimerManager().SetTimer(PollTimer, this, &ThisClass::PollSession, 1.f, true);
}

void URPGDungeonResultActionsWidget::NativeDestruct()
{
	if (bGameplayAbandonment)
		if (auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer())) Controller->CloseDungeonAbandonmentConfirmation(this);
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(PollTimer);
	if (DungeonFlow)
	{
		DungeonFlow->OnFlowStateChanged.RemoveDynamic(this, &ThisClass::HandleFlowChanged);
		DungeonFlow->OnDungeonSessionChanged.RemoveDynamic(this, &ThisClass::HandleSessionChanged);
	}
	if (RewardPlayerState.IsValid())
		RewardPlayerState->OnDungeonRewardDelivered.RemoveDynamic(this, &ThisClass::HandleRewardDelivered);
	if (ReturnButton) ReturnButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleReturnClicked);
	if (RetryButton) RetryButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRetryClicked);
	if (RetryController.IsValid())
		RetryController->OnBossBattleRetryRequestChanged.RemoveDynamic(this, &ThisClass::HandleRetryRequestChanged);
	RetryController.Reset();
	if (RefreshButton) RefreshButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleRefreshClicked);
	if (DisconnectButton) DisconnectButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleDisconnectClicked);
	if (AbandonButton) AbandonButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleAbandonClicked);
	if (ConfirmAbandonButton) ConfirmAbandonButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConfirmAbandonClicked);
	if (CancelAbandonButton) CancelAbandonButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCancelAbandonClicked);
	CloseAbandonmentConfirmation();
	if (LoginSteamButton) LoginSteamButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSteamLoginClicked);
	if (LoginDevelopmentButton) LoginDevelopmentButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleDevelopmentLoginClicked);
	if (Authentication) Authentication->OnLoginCompleted.RemoveDynamic(this, &ThisClass::HandleLoginCompleted);
	bAuthenticating = false;
	LoginSteamId.Reset(); LoginCharacterId.Reset(); LoginSessionId.Reset(); AuthenticationError.Reset();
	RewardPlayerState.Reset();
	DungeonFlow = nullptr;
	WebManager = nullptr;
	Authentication = nullptr;
	Super::NativeDestruct();
}

void URPGDungeonResultActionsWidget::BindPlayerState()
{
	ARPGPlayerState* State = GetOwningPlayer() ? GetOwningPlayer()->GetPlayerState<ARPGPlayerState>() : nullptr;
	if (RewardPlayerState.Get() == State) return;
	if (RewardPlayerState.IsValid())
		RewardPlayerState->OnDungeonRewardDelivered.RemoveDynamic(this, &ThisClass::HandleRewardDelivered);
	RewardPlayerState = State;
	if (State) State->OnDungeonRewardDelivered.AddUniqueDynamic(this, &ThisClass::HandleRewardDelivered);
}

FRPGDungeonResultActionsPresentation URPGDungeonResultActionsWidget::CurrentPresentation() const
{
	if (bGameplayAbandonment)
	{
		FRPGDungeonResultActionsPresentation View;
		View.Status = bAbandonmentConfirmationOpen
			? NSLOCTEXT("RPGDungeonResult", "GameplayAbandonConfirm", "아직 포기 요청을 보내지 않았습니다. 이 화면은 전투를 일시 정지하지 않습니다. 취소하면 계속 플레이합니다.")
			: NSLOCTEXT("RPGDungeonResult", "GameplayAbandonIdle", "던전 포기 확인 (개발 전용). 로비에 도착한 뒤 서버에 포기를 요청하며, 응답을 받지 못하면 같은 요청으로 결과를 재확인합니다.");
		return View; // No result/reward/retry/authentication actions during combat.
	}
	const ARPGPlayerState* State = RewardPlayerState.Get();
	const bool bAuthenticated = State && State->HasAuthenticatedCharacter();
	const bool bAccountMatches = bAuthenticated && WebManager && Authentication && GetWorld()
		&& GetWorld()->GetNetMode() == NM_Client && GetOwningPlayer() && GetOwningPlayer()->IsLocalController()
		&& !WebManager->GetAuthenticatedSteamId().IsEmpty()
		&& WebManager->GetAuthenticatedSteamId().Equals(State->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive);
	const auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer());
	const auto* Battle = GetWorld() ? GetWorld()->GetGameState<ARPGBossBattleGameState>() : nullptr;
	const bool bDefeated = !bVictoryResult && Battle && Battle->GetBattleSnapshot().BattleState == EBossBattleState::Defeated;
	const bool bRetryPending = Controller && Controller->IsBossBattleRestartPending();
	auto View = BuildPresentation(DungeonFlow ? DungeonFlow->GetCurrentDungeonSession() : FRPGDungeonSession(),
		DungeonFlow ? DungeonFlow->GetSelectedCharacterId() : FString(),
		bAuthenticated ? State->GetBackendCharacterId() : FString(), bAuthenticated ? State->GetBackendDungeonSessionId() : FString(),
		State ? State->GetRewardDeliveryNotice() : FRPGDungeonRewardDeliveryNotice(),
		DungeonFlow ? DungeonFlow->GetFlowState() : ERPGDungeonClientFlowState::Idle,
		DungeonFlow && DungeonFlow->IsRequestInFlight(), !AuthenticationError.IsEmpty() ? AuthenticationError
			: DungeonFlow ? DungeonFlow->GetLastErrorCode() : FString(),
		bVictoryResult && GetWorld() && GetWorld()->GetTimeSeconds() >= PollUntil,
		WebManager && WebManager->HasAccessToken(), bAuthenticating || (Authentication && Authentication->IsLoginRequestInFlight()), bAccountMatches,
		bDefeated, bRetryPending);
	View.bCanRetry &= Controller && Controller->CanRequestBossBattleRestart();
	if (DungeonFlow && DungeonFlow->HasUnresolvedDungeonAbandonment()) View.bCanRetry = false;
	if (bAbandonmentConfirmationOpen)
	{
		View.bCanRetry = View.bCanReturn = View.bCanRefresh = View.bCanAuthenticate = View.bCanDisconnect = false;
		View.Status = NSLOCTEXT("RPGDungeonResult", "ConfirmingAbandon", "아직 포기 요청을 보내지 않았습니다. 아래 내용을 확인하거나 취소하세요.");
	}
	if (View.bCanRetry && AuthenticationError.IsEmpty() && Controller && !Controller->GetBossBattleRestartErrorCode().IsEmpty())
		View.Status = NSLOCTEXT("RPGDungeonResult", "RetryUnavailable", "Retry was not confirmed. Recheck the dungeon status or try again.");
	return View;
}

void URPGDungeonResultActionsWidget::RefreshPresentation()
{
	BindPlayerState();
	if (bAbandonmentConfirmationOpen && !CanConfirmAbandonment()) CloseAbandonmentConfirmation();
	const auto View = CurrentPresentation();
	if (AbandonButton)
	{
		AbandonButton->SetVisibility(!bVictoryResult && DungeonFlow && (bGameplayAbandonment
			? DungeonFlow->IsGameplayAbandonmentTestingEnabled() : DungeonFlow->IsResultAbandonmentTestingEnabled())
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		AbandonButton->SetIsEnabled(!bAbandonmentConfirmationOpen && CanOpenAbandonment());
	}
	if (AbandonmentConfirmation) AbandonmentConfirmation->SetVisibility(bAbandonmentConfirmationOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (ConfirmAbandonButton) ConfirmAbandonButton->SetIsEnabled(bAbandonmentConfirmationOpen && CanConfirmAbandonment());
	if (CancelAbandonButton) CancelAbandonButton->SetIsEnabled(bAbandonmentConfirmationOpen);
	if (StatusText) StatusText->SetText(View.Status);
	if (RewardText) RewardText->SetText(View.RewardSummary);
	if (ReturnButton) ReturnButton->SetIsEnabled(View.bCanReturn);
	if (RetryButton)
	{
		RetryButton->SetIsEnabled(View.bCanRetry);
		RetryButton->SetVisibility(!bVictoryResult && View.bSessionMatches ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (RefreshButton) RefreshButton->SetIsEnabled(View.bCanRefresh);
	if (DisconnectButton)
	{
		DisconnectButton->SetIsEnabled(View.bCanDisconnect);
		DisconnectButton->SetVisibility(View.bCanDisconnect ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	auto ShowLogin = [&](UButton* Button)
	{
		if (!Button) return;
		Button->SetIsEnabled(View.bCanAuthenticate);
		Button->SetVisibility(View.bSessionMatches && WebManager && !WebManager->HasAccessToken()
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	};
	ShowLogin(LoginSteamButton);
	ShowLogin(LoginDevelopmentButton);
	if (bGameplayAbandonment)
	{
		for (auto* Action : {ReturnButton.Get(), RetryButton.Get(), RefreshButton.Get(), DisconnectButton.Get(), LoginSteamButton.Get(), LoginDevelopmentButton.Get()})
			if (Action) Action->SetVisibility(ESlateVisibility::Collapsed);
		if (RewardText) RewardText->SetVisibility(ESlateVisibility::Collapsed);
	}
	if (bAbandonmentConfirmationOpen) RPGMenuFocus::Update(*this, {CancelAbandonButton});
	else RPGMenuFocus::Update(*this, {ReturnButton, RetryButton, RefreshButton, LoginDevelopmentButton, LoginSteamButton});
	RefreshInputNavigation();
}

void URPGDungeonResultActionsWidget::RefreshInputNavigation()
{
	// Only the class's native layout owns these fields. An authored BP root
	// retains its existing navigation; this does not set focus or invoke actions.
	if (!WidgetTree || !ReturnButton || !RetryButton || !RefreshButton || !AbandonmentConfirmation) return;
	RPGOnlineLobbyNavigation::ConfigureButtonRows(*this, AbandonmentConfirmation, bAbandonmentConfirmationOpen);
}

void URPGDungeonResultActionsWidget::PollSession()
{
	RefreshPresentation();
	if (bGameplayAbandonment) return; // Never poll result/settlement HTTP from the combat confirmation.
	// Admission may have cached Loading before travel. Read once after defeat;
	// unlike victory, defeat must not keep polling for reward settlement.
	if (!bVictoryResult && !bDefeatStatusChecked && CurrentPresentation().bCanRefresh)
	{
		bDefeatStatusChecked = true;
		DungeonFlow->RefreshDungeonSession();
		return;
	}
	if (bVictoryResult && GetWorld() && CurrentPresentation().bCanRefresh && GetWorld()->GetTimeSeconds() < PollUntil)
	{
		const FString State = DungeonFlow->GetCurrentDungeonSession().State;
		if (!State.Equals(TEXT("Cleared"), ESearchCase::CaseSensitive)
			&& !State.Equals(TEXT("Failed"), ESearchCase::CaseSensitive)) DungeonFlow->RefreshDungeonSession();
	}
}

void URPGDungeonResultActionsWidget::HandleReturnClicked()
{
	RefreshPresentation();
	if (CurrentPresentation().bCanReturn)
		if (ARPGPlayerController* Controller = Cast<ARPGPlayerController>(GetOwningPlayer()))
			Controller->RequestDungeonReturnFromResult();
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleRetryClicked()
{
	RefreshPresentation();
	if (CurrentPresentation().bCanRetry)
		if (auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer())) Controller->TryRequestBossBattleRestart();
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleRetryRequestChanged() { RefreshPresentation(); }

bool URPGDungeonResultActionsWidget::CanOpenAbandonment() const
{
	return !bVictoryResult && DungeonFlow && WebManager && !bAuthenticating
		&& !(Authentication && Authentication->IsLoginRequestInFlight())
		&& (bGameplayAbandonment ? DungeonFlow->CanQueueDungeonAbandonmentFromGameplay(GetOwningPlayer())
			: DungeonFlow->CanQueueDungeonAbandonmentFromResult(GetOwningPlayer()));
}

bool URPGDungeonResultActionsWidget::CanConfirmAbandonment() const
{
	const auto* Battle = GetWorld() ? GetWorld()->GetGameState<ARPGBossBattleGameState>() : nullptr;
	return bAbandonmentConfirmationOpen && CanOpenAbandonment() && Battle
		&& ConfirmAbandonmentRevision == Battle->GetBattleSnapshot().Revision
		&& ConfirmAbandonmentSessionId.Equals(DungeonFlow->GetCurrentDungeonSession().DungeonSessionId, ESearchCase::CaseSensitive)
		&& ConfirmAbandonmentCharacterId.Equals(DungeonFlow->GetSelectedCharacterId(), ESearchCase::CaseSensitive)
		&& ConfirmAbandonmentSteamId.Equals(WebManager->GetAuthenticatedSteamId(), ESearchCase::CaseSensitive);
}

void URPGDungeonResultActionsWidget::CloseAbandonmentConfirmation()
{
	bAbandonmentConfirmationOpen = false;
	ConfirmAbandonmentRevision = INDEX_NONE;
	ConfirmAbandonmentSessionId.Reset(); ConfirmAbandonmentCharacterId.Reset(); ConfirmAbandonmentSteamId.Reset();
}

void URPGDungeonResultActionsWidget::HandleAbandonClicked()
{
	if (bAbandonmentConfirmationOpen || !CanOpenAbandonment()) return;
	ConfirmAbandonmentRevision = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot().Revision;
	ConfirmAbandonmentSessionId = DungeonFlow->GetCurrentDungeonSession().DungeonSessionId;
	ConfirmAbandonmentCharacterId = DungeonFlow->GetSelectedCharacterId();
	ConfirmAbandonmentSteamId = WebManager->GetAuthenticatedSteamId();
	bAbandonmentConfirmationOpen = true;
	RefreshPresentation();
	RPGConfirmationInput::Focus(*this, CancelAbandonButton);
}

void URPGDungeonResultActionsWidget::HandleCancelAbandonClicked()
{
	const bool bWasOpen = bAbandonmentConfirmationOpen;
	CloseAbandonmentConfirmation(); // Never cancel a submitted POST or clear a queued/uncertain identity.
	RefreshPresentation();
	if (bGameplayAbandonment)
	{
		if (auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer())) Controller->CloseDungeonAbandonmentConfirmation(this);
		return;
	}
	if (bWasOpen) RPGConfirmationInput::Focus(*this, AbandonButton);
}

FReply URPGDungeonResultActionsWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bAbandonmentConfirmationOpen && RPGConfirmationInput::IsBack(InKeyEvent))
	{
		// A held key must not dismiss a newly opened confirmation. Consume it
		// without falling through to gameplay or cancelling an earlier POST.
		if (!InKeyEvent.IsRepeat()) HandleCancelAbandonClicked();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

void URPGDungeonResultActionsWidget::HandleConfirmAbandonClicked()
{
	if (!CanConfirmAbandonment()) { CloseAbandonmentConfirmation(); RefreshPresentation(); return; }
	const int32 Revision = ConfirmAbandonmentRevision;
	const FString SessionId = ConfirmAbandonmentSessionId;
	const FString CharacterId = ConfirmAbandonmentCharacterId;
	const FString SteamId = ConfirmAbandonmentSteamId;
	CloseAbandonmentConfirmation();
	if (bGameplayAbandonment)
	{
		DungeonFlow->QueueDungeonAbandonmentFromGameplay(GetOwningPlayer(), Revision, SessionId, CharacterId, SteamId);
		if (auto* Controller = Cast<ARPGPlayerController>(GetOwningPlayer())) Controller->CloseDungeonAbandonmentConfirmation(this);
		return;
	}
	DungeonFlow->QueueDungeonAbandonmentFromResult(GetOwningPlayer(), Revision, SessionId, CharacterId, SteamId);
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleRefreshClicked()
{
	RefreshPresentation();
	if (GetWorld() && CurrentPresentation().bCanRefresh)
	{
		PollUntil = GetWorld()->GetTimeSeconds() + 120.0;
		DungeonFlow->RefreshDungeonSession();
	}
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleDisconnectClicked()
{
	RefreshPresentation();
	if (DungeonFlow && CurrentPresentation().bCanDisconnect)
		DungeonFlow->DisconnectToLobby(GetOwningPlayer());
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleSteamLoginClicked() { BeginAuthentication(false); }
void URPGDungeonResultActionsWidget::HandleDevelopmentLoginClicked() { BeginAuthentication(true); }

void URPGDungeonResultActionsWidget::BeginAuthentication(const bool bDevelopment)
{
	RefreshPresentation();
	if (!CurrentPresentation().bCanAuthenticate || !Authentication || !WebManager
		|| (bDevelopment && !URPGOnlineLobbyWidget::IsDevelopmentUIEnabled())) return;
	LoginSteamId = WebManager->GetAuthenticatedSteamId();
	LoginCharacterId = DungeonFlow->GetSelectedCharacterId();
	LoginSessionId = DungeonFlow->GetCurrentDungeonSession().DungeonSessionId;
	AuthenticationError.Reset();
	bAuthenticating = true;
	RefreshPresentation();
	if (bDevelopment) Authentication->LoginToDevelopmentBackend(LoginSteamId);
	else Authentication->LoginToBackendWithSteam();
}

void URPGDungeonResultActionsWidget::HandleLoginCompleted(const bool bSuccess)
{
	if (!bAuthenticating) return;
	bAuthenticating = false;
	const bool bContextMatches = bSuccess && WebManager && WebManager->HasAccessToken()
		&& CurrentPresentation().bSessionMatches
		&& WebManager->GetAuthenticatedSteamId().Equals(LoginSteamId, ESearchCase::CaseSensitive)
		&& DungeonFlow->GetSelectedCharacterId().Equals(LoginCharacterId, ESearchCase::CaseSensitive)
		&& DungeonFlow->GetCurrentDungeonSession().DungeonSessionId.Equals(LoginSessionId, ESearchCase::CaseSensitive);
	LoginSteamId.Reset(); LoginCharacterId.Reset(); LoginSessionId.Reset();
	if (!bContextMatches) AuthenticationError = TEXT("result_reauthentication_failed_or_context_changed");
	else
	{
		AuthenticationError.Reset();
		if (GetWorld()) PollUntil = GetWorld()->GetTimeSeconds() + 120.0;
		// Successful authentication is not permission to travel or grant rewards.
		// Re-read the original session; ReturnToLobby still makes a separate fresh terminal-state check.
		if (!DungeonFlow->RefreshDungeonSession()) AuthenticationError = TEXT("result_reauthentication_refresh_failed");
	}
	RefreshPresentation();
}

void URPGDungeonResultActionsWidget::HandleFlowChanged(ERPGDungeonClientFlowState, const FString&) { RefreshPresentation(); }
void URPGDungeonResultActionsWidget::HandleSessionChanged(const FRPGDungeonSession&) { RefreshPresentation(); }
void URPGDungeonResultActionsWidget::HandleRewardDelivered(const FRPGDungeonRewardDeliveryNotice&) { RefreshPresentation(); }
FText URPGDungeonResultActionsWidget::GetDisplayedStatus() const { return StatusText ? StatusText->GetText() : FText::GetEmpty(); }
FText URPGDungeonResultActionsWidget::GetDisplayedRewardSummary() const { return RewardText ? RewardText->GetText() : FText::GetEmpty(); }
