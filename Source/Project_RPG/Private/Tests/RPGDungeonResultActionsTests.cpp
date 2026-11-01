#if WITH_DEV_AUTOMATION_TESTS
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultActionsTest, "ProjectRPG.Online.DungeonResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonResultActionsTest::RunTest(const FString& Parameters)
{
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("own-session");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("own-character");
	Session.Members.Add(Member);
	FRPGDungeonRewardDeliveryNotice Notice;
	Notice.DungeonSessionId = Session.DungeonSessionId;
	Notice.bApplied = true;
	Notice.InventoryQuantity = 2;
	Notice.MailQuantity = 2;
	auto Present = [&](ERPGDungeonClientFlowState State = ERPGDungeonClientFlowState::InSession,
		bool bBusy = false, const FString& Error = FString(), bool bExpired = false)
	{
		return URPGDungeonResultActionsWidget::BuildPresentation(Session, Member.CharacterId,
			Member.CharacterId, TEXT("own-session"), Notice, State, bBusy, Error, bExpired);
	};

	for (const TCHAR* State : { TEXT("Waiting"), TEXT("Loading"), TEXT("InProgress"),
		TEXT("SettlementPending"), TEXT("cleared"), TEXT("Unknown") })
	{
		Session.State = State;
		const auto View = Present();
		TestTrue(TEXT("Bound character can see session status"), View.bSessionMatches);
		TestFalse(TEXT("A reward notice alone cannot authorize lobby return"), View.bCanReturn);
		TestTrue(TEXT("Nonterminal sessions permit manual refresh"), View.bCanRefresh);
		TestFalse(TEXT("A delivered notice can be shown before terminal HTTP state"), View.RewardSummary.IsEmpty());
	}

	for (const TCHAR* State : { TEXT("Cleared"), TEXT("Failed") })
	{
		Session.State = State;
		TestTrue(TEXT("Only terminal backend states enable return"), Present().bCanReturn);
		Notice.bApplied = false;
		TestTrue(TEXT("A terminal backend read does not depend on notice replication order"), Present().bCanReturn);
		TestTrue(TEXT("An unapplied reward notice is hidden"), Present().RewardSummary.IsEmpty());
		Notice.bApplied = true;
	}
	Session.State = TEXT("Cleared");
	const auto Ready = Present();
	TestTrue(TEXT("Partial-stack overflow displays inventory quantity"), Ready.RewardSummary.ToString().Contains(TEXT("Inventory 2")));
	TestTrue(TEXT("Partial-stack overflow displays mail quantity"), Ready.RewardSummary.ToString().Contains(TEXT("Mail 2")));
	Notice.InventoryQuantity = 0;
	Notice.MailQuantity = 4;
	TestTrue(TEXT("Full-inventory overflow displays all mail units"), Present().RewardSummary.ToString().Contains(TEXT("Mail 4")));
	Notice.DungeonSessionId = TEXT("old-session");
	TestTrue(TEXT("A previous dungeon reward notice is never displayed"), Present().RewardSummary.IsEmpty());
	Notice.DungeonSessionId = TEXT("OWN-SESSION");
	TestTrue(TEXT("Reward notice scope is case sensitive"), Present().RewardSummary.IsEmpty());
	Notice.DungeonSessionId = Session.DungeonSessionId;
	Notice.InventoryQuantity = -1;
	TestTrue(TEXT("Invalid negative inventory quantities are hidden"), Present().RewardSummary.IsEmpty());
	Notice.InventoryQuantity = 0;
	Notice.MailQuantity = -1;
	TestTrue(TEXT("Invalid negative mail quantities are hidden"), Present().RewardSummary.IsEmpty());
	Notice.MailQuantity = 4;

	for (const auto View : { Present(ERPGDungeonClientFlowState::RefreshingSession, true),
		Present(ERPGDungeonClientFlowState::ReturningToLobby) })
	{
		TestFalse(TEXT("In-flight requests/travel disable return"), View.bCanReturn);
		TestFalse(TEXT("In-flight requests/travel disable refresh"), View.bCanRefresh);
	}
	const auto FailedRequest = Present(ERPGDungeonClientFlowState::Error, false, TEXT("refresh_dungeon_session_failed"));
	TestTrue(TEXT("A request failure keeps a terminal session retryable"), FailedRequest.bCanReturn);
	TestTrue(TEXT("A request failure keeps manual refresh available"), FailedRequest.bCanRefresh);
	TestTrue(TEXT("A stable request error code is visible"), FailedRequest.Status.ToString().Contains(TEXT("refresh_dungeon_session_failed")));
	Session.State = TEXT("SettlementPending");
	const auto Expired = Present(ERPGDungeonClientFlowState::InSession, false, FString(), true);
	TestFalse(TEXT("Polling expiry never treats settlement as completed"), Expired.bCanReturn);
	TestTrue(TEXT("Polling expiry permits a user-triggered refresh"), Expired.bCanRefresh);
	TestFalse(TEXT("Polling expiry has an actionable status"), Expired.Status.IsEmpty());
	TestFalse(TEXT("Polling expiry differs from ordinary pending status"), Expired.Status.EqualTo(Present().Status));

	Session.State = TEXT("Cleared");
	auto CheckUnbound = [&](const FRPGDungeonSession& OtherSession, const FString& Selected,
		const FString& AuthenticatedCharacter, const FString& AuthenticatedSession)
	{
		const auto View = URPGDungeonResultActionsWidget::BuildPresentation(OtherSession, Selected,
			AuthenticatedCharacter, AuthenticatedSession, Notice, ERPGDungeonClientFlowState::Idle, false, FString(), false);
		TestFalse(TEXT("Identity/session mismatch rejects presentation scope"), View.bSessionMatches);
		TestFalse(TEXT("Identity/session mismatch disables return"), View.bCanReturn);
		TestFalse(TEXT("Identity/session mismatch disables refresh"), View.bCanRefresh);
		TestFalse(TEXT("Identity/session mismatch disables local disconnect"), View.bCanDisconnect);
		TestTrue(TEXT("Identity/session mismatch hides reward summary"), View.RewardSummary.IsEmpty());
	};
	CheckUnbound(Session, FString(), Member.CharacterId, Session.DungeonSessionId);
	CheckUnbound(Session, Member.CharacterId, TEXT("other-character"), Session.DungeonSessionId);
	CheckUnbound(Session, Member.CharacterId, TEXT("OWN-CHARACTER"), Session.DungeonSessionId);
	CheckUnbound(Session, Member.CharacterId, Member.CharacterId, TEXT("other-session"));
	CheckUnbound(Session, Member.CharacterId, Member.CharacterId, TEXT("OWN-SESSION"));
	FRPGDungeonSession Other = Session;
	Other.Members.Reset();
	CheckUnbound(Other, Member.CharacterId, Member.CharacterId, Session.DungeonSessionId);
	Other = Session;
	Other.Members[0].CharacterId = TEXT("OWN-CHARACTER");
	CheckUnbound(Other, Member.CharacterId, Member.CharacterId, Session.DungeonSessionId);
	Other = Session;
	Other.DungeonSessionId.Reset();
	CheckUnbound(Other, Member.CharacterId, Member.CharacterId, FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultWidgetTest, "ProjectRPG.Online.DungeonResultWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonResultWidgetTest::RunTest(const FString& Parameters)
{
	// Exercise the actual native widget tree/Slate lifecycle without a viewport,
	// authenticated player, HTTP requests or loading a production map.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Result widget test world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	URPGDungeonResultActionsWidget* Widget = CreateWidget<URPGDungeonResultActionsWidget>(World,
		URPGDungeonResultActionsWidget::StaticClass());
	if (!TestNotNull(TEXT("Native result controls can be created"), Widget)) { return false; }
	Widget->InitializeResult(true);
	UButton* Return = nullptr;
	UButton* Refresh = nullptr;
	UButton* Login = nullptr;
	UButton* Disconnect = nullptr;
	UButton* Retry = nullptr;
	{
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		Return = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ReturnToLobbyButton")));
		Refresh = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RefreshDungeonButton")));
		Login = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ResultLoginSteamButton")));
		Disconnect = Cast<UButton>(Widget->GetWidgetFromName(TEXT("DisconnectToLobbyButton")));
		Retry = Cast<UButton>(Widget->GetWidgetFromName(TEXT("RetryBattleButton")));
		if (!TestNotNull(TEXT("Native party retry action"), Retry)) return false;
		TestFalse(TEXT("Unbound result cannot retry"), Retry->GetIsEnabled());
		TestTrue(TEXT("Retry action has a guarded handler"), Retry->OnClicked.IsBound());
		Retry->OnClicked.Broadcast();
		if (!TestNotNull(TEXT("Native local disconnect action"), Disconnect)) return false;
		TestFalse(TEXT("Unbound result cannot disconnect"), Disconnect->GetIsEnabled());
		TestTrue(TEXT("Disconnect action has a guarded handler"), Disconnect->OnClicked.IsBound());
		Disconnect->OnClicked.Broadcast();
		if (!TestNotNull(TEXT("Native lobby return button"), Return)
			|| !TestNotNull(TEXT("Native refresh button"), Refresh)
			|| !TestNotNull(TEXT("Native result re-login button"), Login)) { return false; }
		TestFalse(TEXT("Without a bound identity the real return button is disabled"), Return->GetIsEnabled());
		TestFalse(TEXT("Without a bound identity the real refresh button is disabled"), Refresh->GetIsEnabled());
		TestTrue(TEXT("The real return button has a click handler"), Return->OnClicked.IsBound());
		TestTrue(TEXT("The real refresh button has a click handler"), Refresh->OnClicked.IsBound());
		TestFalse(TEXT("Unbound result cannot authenticate"), Login->GetIsEnabled());
		TestTrue(TEXT("Re-login button has a handler"), Login->OnClicked.IsBound());
		TestNull(TEXT("Result development login is absent without opt-in flags"), Widget->GetWidgetFromName(TEXT("ResultLoginDevelopmentButton")));
		TestFalse(TEXT("The real status text is populated"), Widget->GetDisplayedStatus().IsEmpty());
		TestTrue(TEXT("Unbound widgets hide the real reward summary"), Widget->GetDisplayedRewardSummary().IsEmpty());
		// Calling handlers directly cannot bypass the identity check.
		Return->OnClicked.Broadcast();
		Refresh->OnClicked.Broadcast();
		Login->OnClicked.Broadcast();
	}
	TestFalse(TEXT("Slate teardown removes the return click handler"), Return->OnClicked.IsBound());
	TestFalse(TEXT("Slate teardown removes the refresh click handler"), Refresh->OnClicked.IsBound());
	TestFalse(TEXT("Slate teardown removes the authentication click handler"), Login->OnClicked.IsBound());
	TestFalse(TEXT("Slate teardown removes the disconnect handler"), Disconnect->OnClicked.IsBound());
	TestFalse(TEXT("Slate teardown removes the retry handler"), Retry->OnClicked.IsBound());
	{
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		TestTrue(TEXT("Reconstruction restores one return handler"), Return->OnClicked.IsBound());
		TestTrue(TEXT("Reconstruction restores one refresh handler"), Refresh->OnClicked.IsBound());
		TestTrue(TEXT("Reconstruction restores the authentication handler"), Login->OnClicked.IsBound());
		TestTrue(TEXT("Reconstruction restores the disconnect handler"), Disconnect->OnClicked.IsBound());
		TestTrue(TEXT("Reconstruction restores the retry handler"), Retry->OnClicked.IsBound());
	}
	TestFalse(TEXT("Repeated teardown remains safe"), Return->OnClicked.IsBound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonDefeatActionsTest, "ProjectRPG.Online.DungeonResultDefeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonDefeatActionsTest::RunTest(const FString&)
{
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("session");
	Session.State = TEXT("InProgress");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("character");
	Session.Members.Add(Member);
	FRPGDungeonRewardDeliveryNotice Notice;
	auto Present = [&](bool bAuthenticated = true, bool bRetryPending = false, bool bHttpBusy = false,
		bool bAccountMatches = true, bool bDefeated = true,
		ERPGDungeonClientFlowState Flow = ERPGDungeonClientFlowState::InSession)
	{
		return URPGDungeonResultActionsWidget::BuildPresentation(Session, Member.CharacterId, Member.CharacterId,
			Session.DungeonSessionId, Notice, Flow, bHttpBusy, FString(), false,
			bAuthenticated, false, bAccountMatches, bDefeated, bRetryPending);
	};
	const auto Ready = Present();
	TestTrue(TEXT("Admitted defeat permits party retry"), Ready.bCanRetry);
	TestTrue(TEXT("Defeat permits explicit session-retaining escape without an HTTP error"), Ready.bCanDisconnect);
	TestFalse(TEXT("Defeat never authorizes normal settled return"), Ready.bCanReturn);
	TestTrue(TEXT("Defeat describes membership retention"), Ready.Status.ToString().Contains(TEXT("not abandonment")));
	TestFalse(TEXT("Defeat is not waiting for rewards"), Ready.Status.ToString().Contains(TEXT("reward settlement")));
	for (const auto Busy : { Present(true, true), Present(true, false, true),
		Present(true, false, false, true, true, ERPGDungeonClientFlowState::ReturningToLobby) })
	{
		TestFalse(TEXT("In-flight retry, HTTP or travel blocks duplicate retry"), Busy.bCanRetry);
		TestFalse(TEXT("In-flight retry, HTTP or travel blocks disconnect"), Busy.bCanDisconnect);
		TestFalse(TEXT("In-flight retry, HTTP or travel blocks refresh"), Busy.bCanRefresh);
	}
	TestFalse(TEXT("Expired credential cannot retry"), Present(false).bCanRetry);
	TestTrue(TEXT("Expired credential retains local-only escape"), Present(false).bCanDisconnect);
	TestFalse(TEXT("Foreign account cannot retry"), Present(true, false, false, false).bCanRetry);
	TestFalse(TEXT("Foreign account cannot escape through this result"), Present(true, false, false, false).bCanDisconnect);
	TestFalse(TEXT("Victory has no persistent retry action"), Present(true, false, false, true, false).bCanRetry);
	Notice.bApplied = true; Notice.DungeonSessionId = TEXT("old-session");
	TestTrue(TEXT("Old reward notice cannot suppress new defeat retry"), Present().bCanRetry);
	Notice.DungeonSessionId = Session.DungeonSessionId;
	TestFalse(TEXT("Current-session applied reward forbids retry"), Present().bCanRetry);
	Notice.bApplied = false;
	for (const TCHAR* State : { TEXT("Loading"), TEXT("SettlementPending"), TEXT("Cleared"), TEXT("Failed"), TEXT("inprogress") })
	{
		Session.State = State;
		TestFalse(TEXT("Only exact InProgress defeat permits retry"), Present().bCanRetry);
	}
	Session.State = TEXT("InProgress"); Session.Members.Reset();
	TestFalse(TEXT("Lost membership disables retry"), Present().bCanRetry);
	TestFalse(TEXT("Lost membership disables local escape presentation"), Present().bCanDisconnect);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultAuthenticationTest, "ProjectRPG.Online.DungeonResultAuthentication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonResultAuthenticationTest::RunTest(const FString&)
{
	FRPGDungeonSession Session;
	Session.DungeonSessionId = TEXT("session");
	FRPGDungeonSessionMember Member;
	Member.CharacterId = TEXT("character");
	Session.Members.Add(Member);
	FRPGDungeonRewardDeliveryNotice Notice;
	Notice.bApplied = true; Notice.DungeonSessionId = Session.DungeonSessionId;
	Notice.InventoryQuantity = 4;
	auto View = [&](bool bAuthenticated = false, bool bLoginBusy = false, bool bFlowBusy = false,
		bool bAccountMatches = true, ERPGDungeonClientFlowState State = ERPGDungeonClientFlowState::Error)
	{
		return URPGDungeonResultActionsWidget::BuildPresentation(Session, Member.CharacterId,
			Member.CharacterId, Session.DungeonSessionId, Notice, State, bFlowBusy,
			TEXT("refresh_dungeon_session_failed"), true, bAuthenticated, bLoginBusy, bAccountMatches);
	};
	for (const TCHAR* State : {TEXT("InProgress"), TEXT("SettlementPending"), TEXT("Cleared"), TEXT("Failed")})
	{
		Session.State = State;
		TestTrue(TEXT("Admitted same-account player may reauthenticate at the result"), View().bCanAuthenticate);
		TestFalse(TEXT("Expiry cannot enable return using cached completion/reward notice"), View().bCanReturn);
		TestFalse(TEXT("Expiry suspends HTTP polling/manual refresh"), View().bCanRefresh);
		TestTrue(TEXT("Expiry permits a local-only escape without approving settlement"), View().bCanDisconnect);
		TestFalse(TEXT("Login in flight blocks duplicate escape actions"), View(false, true).bCanDisconnect);
		TestFalse(TEXT("In-flight HTTP keeps the result action locked"), View(false, false, true).bCanDisconnect);
		TestFalse(TEXT("Foreign account cannot use the result escape action"), View(false, false, false, false).bCanDisconnect);
		TestFalse(TEXT("Already delivered owner notice remains visible"), View().RewardSummary.IsEmpty());
		TestTrue(TEXT("Expiry has an actionable authentication message"), View().Status.ToString().Contains(TEXT("Authentication expired")));
		TestFalse(TEXT("Login blocks duplicate authentication"), View(false, true).bCanAuthenticate);
		TestFalse(TEXT("Login blocks return"), View(true, true).bCanReturn);
		TestFalse(TEXT("Login blocks refresh"), View(true, true).bCanRefresh);
		TestFalse(TEXT("In-flight HTTP blocks authentication"), View(false, false, true).bCanAuthenticate);
		TestFalse(TEXT("Return travel blocks authentication"), View(false, false, false, true, ERPGDungeonClientFlowState::ReturningToLobby).bCanAuthenticate);
		TestFalse(TEXT("A different account is not a reauthentication context"), View(false, false, false, false).bSessionMatches);
		TestFalse(TEXT("A different account cannot authenticate through this result"), View(false, false, false, false).bCanAuthenticate);
	}
	Session.State = TEXT("SettlementPending");
	TestFalse(TEXT("Reauthentication alone never completes settlement"), View(true).bCanReturn);
	TestTrue(TEXT("A renewed credential enables a fresh read"), View(true).bCanRefresh);
	Session.State = TEXT("Cleared");
	TestTrue(TEXT("Renewed credential plus terminal backend state permits the guarded return action"), View(true).bCanReturn);
	Session.Members.Reset();
	TestFalse(TEXT("Reauthentication requires current membership"), View().bCanAuthenticate);
	TestFalse(TEXT("Disconnect presentation requires current membership"), View().bCanDisconnect);
	return true;
}
#endif
