#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonPartyCombatTypes.h"
#include "Blueprint/WidgetTree.h"
#include "Component/RPGDungeonPartyCombatComponent.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameMode/RPGGameModeBase.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Player/RPGPlayerState.h"
#include "UI/Content/RPGDungeonPartyCombatWidget.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include <limits>

struct FRPGDungeonPartyCombatTestAccess
{
	static void Refresh(URPGDungeonPartyCombatComponent& Component) { Component.RefreshServerSnapshot(); }
	static bool Empty(const URPGDungeonPartyCombatComponent& Component)
	{
		return Component.Snapshot.Members.IsEmpty() && Component.Snapshot.Notices.IsEmpty() && Component.PendingNotices.IsEmpty();
	}
	static bool Read(ARPGGameModeBase& Mode, const FRPGDungeonMembershipSnapshot& Snapshot, bool bSuccess = true)
	{
		if (Mode.DungeonMembershipMonitor.GetSessionId().IsEmpty()
			&& !Mode.DungeonMembershipMonitor.Configure(Snapshot.DungeonSessionId, Snapshot.ServerId)) return false;
		uint64 Serial = 0;
		if (!Mode.DungeonMembershipMonitor.BeginRead(Serial)) return false;
		FRPGDungeonMembershipSnapshotResult Result;
		Result.Snapshot = Snapshot; Result.bSuccess = bSuccess; Result.HttpStatus = bSuccess ? 200 : 503;
		return Mode.CompleteDungeonMembershipRead(Serial, Result, FPlatformTime::Seconds() - .5);
	}
	static void Age(ARPGGameModeBase& Mode) { Mode.DungeonMembershipPresentation.ReceivedAtSeconds -= 30.0; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyCombatPresentationTest, "ProjectRPG.Online.DungeonPartyCombat.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyCombatPresentationTest::RunTest(const FString&)
{
	using Presentation = FRPGDungeonPartyCombatPresentation;
	using State = ERPGDungeonPartyCombatState;
	FRPGDungeonPartyCombatMember Member;
	Presentation::SetVitals(Member, true, 25.f, 100.f, false);
	TestTrue(TEXT("Valid server attributes display health"), Member.bHasHealth);
	TestEqual(TEXT("Positive health is alive"), Member.State, State::Alive);
	TestEqual(TEXT("Health bar uses the bounded ratio"), Presentation::HealthFraction(Member), .25f);
	Presentation::SetVitals(Member, true, 0.f, 100.f, false);
	TestEqual(TEXT("Confirmed zero health is incapacitated"), Member.State, State::Incapacitated);
	Presentation::SetVitals(Member, true, 50.f, 100.f, true);
	TestEqual(TEXT("Authoritative death tag is respected"), Member.State, State::Incapacitated);
	Presentation::SetVitals(Member, false, 0.f, 0.f, false);
	TestEqual(TEXT("Missing pawn/ASC data is not a death"), Member.State, State::Unconfirmed);
	TestFalse(TEXT("Unknown health has no numeric bar"), Member.bHasHealth);
	for (const float Invalid : {-1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
	{
		Presentation::SetVitals(Member, true, Invalid, 100.f, false);
		TestEqual(TEXT("Invalid attributes are unconfirmed, not a fabricated death"), Member.State, State::Unconfirmed);
		TestFalse(TEXT("Invalid attributes do not retain previous health"), Member.bHasHealth);
		TestEqual(TEXT("Invalid health cannot escape into progress percent"), Presentation::HealthFraction(Member), 0.f);
	}
	Presentation::SetVitals(Member, true, 5.f, 0.f, false);
	TestFalse(TEXT("Zero maximum health is not ready"), Member.bHasHealth);
	Presentation::SetVitals(Member, true, 150.f, 100.f, false);
	TestEqual(TEXT("Display health is clamped without changing GAS"), Member.CurrentHealth, 100.f);
	TestEqual(TEXT("Over-max display remains a bounded bar"), Presentation::HealthFraction(Member), 1.f);
	TestEqual(TEXT("Public names are trimmed"), Presentation::SanitizeDisplayName(TEXT("  파티원  ")), FString(TEXT("파티원")));
	for (const FString Name : {FString(TEXT("a\nb")), FString::ChrN(65, TEXT('a')), FString(TEXT(" \t "))})
		TestTrue(TEXT("Unsafe names are removed before replication"), Presentation::SanitizeDisplayName(Name).IsEmpty());
	Member.DisplayName = TEXT("한글 이름"); Member.bIsSelf = true;
	TestTrue(TEXT("Public name remains plain display text"), Presentation::MemberText(Member).ToString().Contains(TEXT("한글 이름")));
	FRPGDungeonPartyCombatSnapshot Snapshot;
	Snapshot.DungeonSessionId = TEXT("ExactSession"); Snapshot.RecipientCharacterId = TEXT("ExactCharacter");
	TestTrue(TEXT("Only current authenticated recipient reads a snapshot"), Presentation::MatchesOwner(Snapshot, TEXT("ExactSession"), TEXT("ExactCharacter"), true));
	TestFalse(TEXT("Session scope is exact"), Presentation::MatchesOwner(Snapshot, TEXT("exactsession"), TEXT("ExactCharacter"), true));
	TestFalse(TEXT("Character scope is exact"), Presentation::MatchesOwner(Snapshot, TEXT("ExactSession"), TEXT("OtherCharacter"), true));
	TestFalse(TEXT("Lost identity cannot read cached health"), Presentation::MatchesOwner(Snapshot, TEXT("ExactSession"), TEXT("ExactCharacter"), false));
	double EndServerTime = 0.0;
	TestTrue(TEXT("Display deadline uses backend relative time and full request age"), Presentation::TryBuildReconnectDeadline(
		TEXT("2026-10-25T00:03:00Z"), TEXT("2026-10-25T00:10:00Z"), 2.0, 100.0, EndServerTime));
	TestEqual(TEXT("Latency is charged, not added to the grace"), EndServerTime, 518.0);
	Member.Presence = ERPGDungeonPartyPresence::ReconnectGrace;
	Member.bHasReconnectDeadline = true; Member.ReconnectEndServerTime = EndServerTime;
	TestFalse(TEXT("Reconnecting member has a display status"), Presentation::ReconnectText(Member, 100.0).IsEmpty());
	TestTrue(TEXT("Display zero still awaits server receipt"), Presentation::ReconnectText(Member, 600.0).ToString().Contains(TEXT("서버 확인 대기")));
	TestEqual(TEXT("Formatting time cannot revoke or alter the row"), Member.Presence, ERPGDungeonPartyPresence::ReconnectGrace);
	TestFalse(TEXT("Future/invalid metadata cannot extend the display beyond policy"), Presentation::TryBuildReconnectDeadline(
		TEXT("2026-10-25T00:00:00Z"), TEXT("2026-10-25T00:11:00Z"), 0.0, 100.0, EndServerTime));
	TestFalse(TEXT("Counter reversal is not a valid clock observation"), Presentation::TryBuildReconnectDeadline(
		TEXT("2026-10-25T00:03:00Z"), TEXT("2026-10-25T00:10:00Z"), -1.0, 100.0, EndServerTime));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyCombatWidgetTest, "ProjectRPG.Online.DungeonPartyCombat.Widget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyCombatWidgetTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Party HUD world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGDungeonPartyCombatWidget>(World, URPGDungeonPartyCombatWidget::StaticClass());
	if (!TestNotNull(TEXT("Native party HUD"), Widget)) return false;
	FRPGDungeonPartyCombatSnapshot Snapshot;
	FRPGDungeonPartyCombatMember Known;
	FRPGDungeonPartyCombatPresentation::SetVitals(Known, true, 50.f, 100.f, false);
	Snapshot.Members.Add(Known);
	Snapshot.Members.Add(FRPGDungeonPartyCombatMember());
	Widget->SetPartySnapshot(Snapshot); // Safe even before the Slate tree exists.
	const auto Slate = Widget->TakeWidget();
	auto* Rows = Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("DungeonPartyCombatRows")));
	if (!TestNotNull(TEXT("Party health rows"), Rows)) return false;
	TestEqual(TEXT("HUD does not intercept gameplay input"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	if (!TestEqual(TEXT("Each member has a label and health wrapper"), Rows->GetChildrenCount(), 4)) return false;
	TestTrue(TEXT("Known health has a visible bar"), Rows->GetChildAt(1)->IsVisible());
	TestEqual(TEXT("Unknown health collapses the wrapper, not a fake empty bar"), Rows->GetChildAt(3)->GetVisibility(), ESlateVisibility::Collapsed);
	Snapshot.Members[1] = Known;
	Widget->SetPartySnapshot(Snapshot);
	TestTrue(TEXT("Late health readiness reveals the existing row"), Rows->GetChildAt(3)->IsVisible());
	auto* Feed = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("DungeonPartyCombatNotices")));
	if (!TestNotNull(TEXT("Passive party notices"), Feed)) return false;
	TestEqual(TEXT("No notifications leave no empty feed"), Feed->GetVisibility(), ESlateVisibility::Collapsed);
	Known.MemberSlot = 1; Known.DisplayName = TEXT("재접속 용사");
	FRPGDungeonPartyCombatPresentation::AddNotice(Snapshot.Notices, Known, ERPGDungeonPartyNoticeKind::Reconnected, 100.0);
	Widget->SetPartySnapshot(Snapshot, 102.0);
	TestEqual(TEXT("Notices remain passive and do not consume clicks"), Feed->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("HUD shows public name and reconnection, not an identity"), Feed->GetText().ToString().Contains(TEXT("재접속 용사")));
	Widget->SetPartySnapshot(Snapshot, 108.0);
	TestEqual(TEXT("Client display expires without waiting for another network update"), Feed->GetVisibility(), ESlateVisibility::Collapsed);
	Snapshot.Members.Empty(); Widget->SetPartySnapshot(Snapshot);
	TestEqual(TEXT("Cleared snapshot cannot retain previous member rows"), Rows->GetChildrenCount(), 0);
	TestEqual(TEXT("A hidden party scope cannot retain notifications"), Feed->GetVisibility(), ESlateVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyNoticeTest, "ProjectRPG.Online.DungeonPartyCombat.NoticeTransitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyNoticeTest::RunTest(const FString&)
{
	using Presence = ERPGDungeonPartyPresence;
	using Kind = ERPGDungeonPartyNoticeKind;
	using Presentation = FRPGDungeonPartyCombatPresentation;
	FRPGDungeonPartyPresenceHistory History;
	TestFalse(TEXT("Initial live roster is not a burst of arrival notices"), History.Observe(Presence::ServerObserved).IsSet());
	TestFalse(TEXT("An absent actor/read is not a disconnect notice"), History.Observe(Presence::Unconfirmed).IsSet());
	auto Event = History.Observe(Presence::ReconnectGrace);
	if (!TestTrue(TEXT("Server-confirmed disconnect produces one notice"), Event.IsSet())) return false;
	TestEqual(TEXT("Correct disconnect notice kind"), Event.GetValue(), Kind::ConnectionLost);
	TestFalse(TEXT("Polling/timer updates do not repeat the disconnect"), History.Observe(Presence::ReconnectGrace).IsSet());
	TestFalse(TEXT("Uncertain reads preserve a known wait without inventing an event"), History.Observe(Presence::Unconfirmed).IsSet());
	TestFalse(TEXT("Restored metadata does not repeat the same disconnect"), History.Observe(Presence::ReconnectGrace).IsSet());
	Event = History.Observe(Presence::ServerObserved);
	if (!TestTrue(TEXT("Confirmed active player after the wait produces a reconnect notice"), Event.IsSet())) return false;
	TestEqual(TEXT("Correct reconnect notice kind"), Event.GetValue(), Kind::Reconnected);
	TestFalse(TEXT("Steady live samples do not repeat reconnect"), History.Observe(Presence::ServerObserved).IsSet());
	TestFalse(TEXT("Temporary unknown presence does not start another wait"), History.Observe(Presence::Unconfirmed).IsSet());
	TestFalse(TEXT("Health/actor readiness alone is not a reconnect"), History.Observe(Presence::ServerObserved).IsSet());
	FRPGDungeonPartyPresenceHistory ExistingDisconnect;
	TestFalse(TEXT("Initial disconnected roster is not a new-disconnect burst"), ExistingDisconnect.Observe(Presence::ReconnectGrace).IsSet());
	TestTrue(TEXT("Its later real reconnection is still announced"), ExistingDisconnect.Observe(Presence::ServerObserved).IsSet());

	FRPGDungeonPartyCombatMember Member;
	Member.MemberSlot = 0; Member.DisplayName = TEXT("a\nb");
	TArray<FRPGDungeonPartyNotice> Notices;
	Presentation::AddNotice(Notices, Member, Kind::ConnectionLost, 100.0);
	if (!TestEqual(TEXT("One public notification"), Notices.Num(), 1)) return false;
	TestTrue(TEXT("Control characters cannot enter the replicated notice"), Notices[0].DisplayName.IsEmpty());
	TestTrue(TEXT("Missing name falls back to a recipient-local slot"), Presentation::NoticeText(Notices[0]).ToString().Contains(TEXT("파티원 1")));
	TestTrue(TEXT("Notice is visible at server event time"), Presentation::IsNoticeVisible(Notices[0], 100.0));
	TestFalse(TEXT("Expired notice is hidden exactly at its deadline"), Presentation::IsNoticeVisible(Notices[0], 108.0));
	TestFalse(TEXT("Clock reversal cannot prolong a notice"), Presentation::IsNoticeVisible(Notices[0], 99.0));
	TestFalse(TEXT("Nonfinite clock is not usable for presentation"), Presentation::IsNoticeVisible(Notices[0], std::numeric_limits<double>::quiet_NaN()));
	Presentation::AddNotice(Notices, Member, Kind::Reconnected, -1.0);
	Presentation::AddNotice(Notices, Member, static_cast<Kind>(255), 100.0);
	TestEqual(TEXT("Invalid event/clock does not append a notice"), Notices.Num(), 1);
	Presentation::AddNotice(Notices, Member, Kind::Reconnected, 101.0);
	TestEqual(TEXT("A member has only its latest notice, not conflicting wait/reconnect messages"), Notices.Num(), 1);
	TestEqual(TEXT("Reconnection replaces the earlier wait message"), Notices[0].Kind, Kind::Reconnected);
	Notices.Reset();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Member.MemberSlot = Index;
		Presentation::AddNotice(Notices, Member, Kind::Reconnected, 100.0);
	}
	TestEqual(TEXT("Retained feed is bounded"), Notices.Num(), Presentation::MaxNotices);
	TestEqual(TEXT("Oldest notices are discarded first"), Notices[0].MemberSlot, 2);
	FRPGDungeonPartyCombatSnapshot Before, After;
	After.Notices = Notices;
	TestFalse(TEXT("Notice changes participate in owner-only snapshot publication"), Before == After);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyDesignerPresentationTest, "ProjectRPG.Online.DungeonPartyCombat.DesignerPresentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyDesignerPresentationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Designer presentation world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Widget = CreateWidget<URPGDungeonPartyCombatWidget>(World, URPGDungeonPartyCombatWidget::StaticClass());
	if (!TestNotNull(TEXT("Extensible party HUD"), Widget)) return false;
	if (!Widget->WidgetTree) Widget->WidgetTree = NewObject<UWidgetTree>(Widget);
	auto* Root = Widget->WidgetTree->ConstructWidget<UVerticalBox>();
	auto* CustomLabel = Widget->WidgetTree->ConstructWidget<UTextBlock>();
	CustomLabel->SetText(FText::FromString(TEXT("Designer-owned content")));
	Root->AddChildToVerticalBox(CustomLabel);
	Widget->WidgetTree->RootWidget = Root;
	FRPGDungeonPartyCombatSnapshot Snapshot;
	Snapshot.bHasMembershipSnapshot = true; Snapshot.bRosterStale = false;
	FRPGDungeonPartyCombatMember Member;
	Member.MemberSlot = 1; Member.DisplayName = TEXT("내 파티원");
	FRPGDungeonPartyCombatPresentation::SetVitals(Member, true, 50.f, 100.f, false);
	Snapshot.Members.Add(Member);
	FRPGDungeonPartyCombatPresentation::AddNotice(Snapshot.Notices, Member, ERPGDungeonPartyNoticeKind::Reconnected, 100.0);
	Widget->SetPartySnapshot(Snapshot, 102.0);
	TestEqual(TEXT("Read-only rows are ready before the first Construct"), Widget->GetPartyMembers().Num(), 1);
	TestFalse(TEXT("Fresh roster metadata is available to WBP"), Widget->IsLastKnownPartyRoster());
	TestEqual(TEXT("Shared WBP/native health formatter"), Widget->GetPartyHealthFraction(Member), .5f);
	TestTrue(TEXT("Shared label formatter preserves the public name"), Widget->GetPartyMemberText(Member).ToString().Contains(Member.DisplayName));
	TestFalse(TEXT("Shared header is available without native header controls"), Widget->GetPartyHeaderText().IsEmpty());
	if (!TestEqual(TEXT("WBP notice getter returns active notices"), Widget->GetActivePartyNotices().Num(), 1)) return false;
	TestTrue(TEXT("Shared notice formatter preserves the public name"), Widget->GetPartyNoticeText(Snapshot.Notices[0]).ToString().Contains(Member.DisplayName));
	auto Copy = Widget->GetPartyMembers(); Copy[0].DisplayName = TEXT("Presentation-local copy");
	TestEqual(TEXT("WBP cannot mutate the stored rows through a returned copy"), Widget->GetPartyMembers()[0].DisplayName, Member.DisplayName);
	const auto Slate = Widget->TakeWidget();
	TestTrue(TEXT("Native fallback does not replace a designer-owned root"), Widget->WidgetTree->RootWidget == Root);
	TestEqual(TEXT("Native refresh does not insert controls into the designer layout"), Root->GetChildrenCount(), 1);
	TestEqual(TEXT("Designer root remains passive"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	Widget->SetIsFocusable(true); Widget->SetVisibility(ESlateVisibility::Visible);
	Snapshot.bRosterStale = true;
	Widget->SetPartySnapshot(Snapshot, 108.0);
	TestTrue(TEXT("Stale roster flag also reaches custom layouts"), Widget->IsLastKnownPartyRoster());
	TestTrue(TEXT("WBP notices expire on the same server timeline"), Widget->GetActivePartyNotices().IsEmpty());
	TestTrue(TEXT("An expired notice has no display text"), Widget->GetPartyNoticeText(Snapshot.Notices[0]).IsEmpty());
	TestEqual(TEXT("A presentation refresh restores passive hit testing"), Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("A presentation refresh restores nonfocusable state"), Widget->IsFocusable());
	Snapshot = {}; Widget->SetPartySnapshot(Snapshot, 109.0);
	TestTrue(TEXT("Cleared presentation exposes no old rows"), Widget->GetPartyMembers().IsEmpty());
	TestTrue(TEXT("Cleared presentation exposes no old notices"), Widget->GetActivePartyNotices().IsEmpty());
	TestEqual(TEXT("Designer-owned content is preserved after scope clear"), Root->GetChildrenCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyCombatScopeTest, "ProjectRPG.Online.DungeonPartyCombat.ServerScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyCombatScopeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Party combat world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Party GameState"), GameState)) return false;
	World->SetGameState(GameState);
	auto Add = [&](const FString& Character, const FString& Session, int32 Number)
	{
		auto* Controller = World->SpawnActor<ARPGPlayerController>();
		auto* Player = World->SpawnActor<ARPGPlayerState>();
		if (!Controller || !Player) return static_cast<ARPGPlayerController*>(nullptr);
		Controller->SetPlayerState(Player); Player->SetOwner(Controller);
		Player->SetPlayerId(Number); Player->SetPlayerName(FString::Printf(TEXT("PartyMember%d"), Number));
		if (!Character.IsEmpty()) Player->SetAuthenticatedIdentity(Character, TEXT("76561198000000901"), Session);
		GameState->PlayerArray.AddUnique(Player);
		return Controller;
	};
	auto* Owner = Add(TEXT("owner"), TEXT("session-a"), 1);
	auto* Peer = Add(TEXT("peer"), TEXT("session-a"), 2);
	auto* Foreign = Add(TEXT("foreign"), TEXT("session-b"), 3);
	auto* Pending = Add(FString(), FString(), 4);
	if (!TestNotNull(TEXT("Owner"), Owner) || !TestNotNull(TEXT("Peer"), Peer)
		|| !TestNotNull(TEXT("Foreign session"), Foreign) || !TestNotNull(TEXT("Pending admission"), Pending)) return false;
	auto* Party = Owner->FindComponentByClass<URPGDungeonPartyCombatComponent>();
	if (!TestNotNull(TEXT("Controller owns the production party component"), Party)) return false;
	using Access = FRPGDungeonPartyCombatTestAccess;
	Access::Refresh(*Party);
	const auto Snapshot = Party->GetPartyCombatSnapshot();
	if (!TestEqual(TEXT("Only same-session authenticated members are shared"), Snapshot.Members.Num(), 2)) return false;
	TestTrue(TEXT("Self is first without using a name as identity"), Snapshot.Members[0].bIsSelf);
	TestFalse(TEXT("Peer is not recipient"), Snapshot.Members[1].bIsSelf);
	TestEqual(TEXT("Missing Pawn is explicitly unconfirmed"), Snapshot.Members[1].State, ERPGDungeonPartyCombatState::Unconfirmed);
	TestEqual(TEXT("Recipient's own character scopes the snapshot"), Snapshot.RecipientCharacterId, FString(TEXT("owner")));
	auto* Duplicate = Add(TEXT("peer"), TEXT("session-a"), 5);
	if (!TestNotNull(TEXT("Duplicate fixture"), Duplicate)) return false;
	Access::Refresh(*Party);
	TestTrue(TEXT("Ambiguous duplicate identity fails closed without guessing"), Party->GetPartyCombatSnapshot().Members.IsEmpty());
	Duplicate->GetPlayerState<ARPGPlayerState>()->SetIsInactive(true);
	Access::Refresh(*Party);
	TestEqual(TEXT("Inactive states are excluded"), Party->GetPartyCombatSnapshot().Members.Num(), 2);
	auto* PeerState = Peer->GetPlayerState<ARPGPlayerState>();
	const int32 OriginalSlot = Party->GetPartyCombatSnapshot().Members[1].MemberSlot;
	PeerState->SetIsInactive(true); Peer->SetPlayerState(nullptr);
	Access::Refresh(*Party);
	const auto MissingPeer = Party->GetPartyCombatSnapshot();
	if (!TestEqual(TEXT("Lost actor without a receipt retains the member row"), MissingPeer.Members.Num(), 2)) return false;
	TestEqual(TEXT("Missing actor is not marked dead"), MissingPeer.Members[1].State, ERPGDungeonPartyCombatState::Unconfirmed);
	TestFalse(TEXT("Missing actor does not retain previous health"), MissingPeer.Members[1].bHasHealth);
	Peer->SetPlayerState(PeerState); PeerState->SetIsInactive(false);
	Access::Refresh(*Party);
	TestEqual(TEXT("Returning actor keeps the same recipient-local row slot"), Party->GetPartyCombatSnapshot().Members[1].MemberSlot, OriginalSlot);
	TestTrue(TEXT("Peer revocation uses the existing immutable boundary"), Peer->GetPlayerState<ARPGPlayerState>()->RevokeAuthenticatedIdentity(
		TEXT("session-a"), TEXT("peer"), TEXT("76561198000000901")));
	Access::Refresh(*Party);
	TestEqual(TEXT("Revoked member disappears, not a dead/eligible member row"), Party->GetPartyCombatSnapshot().Members.Num(), 1);
	TestTrue(TEXT("Native quarantine alone is not an immutable departure announcement"), Party->GetPartyCombatSnapshot().Notices.IsEmpty());
	TestTrue(TEXT("Owner revocation succeeds"), Owner->GetPlayerState<ARPGPlayerState>()->RevokeAuthenticatedIdentity(
		TEXT("session-a"), TEXT("owner"), TEXT("76561198000000901")));
	TestTrue(TEXT("Recipient cannot read cached data immediately after identity revocation"), Party->GetPartyCombatSnapshot().Members.IsEmpty());
	TestTrue(TEXT("Recipient cannot read cached notifications after revocation"), Party->GetPartyCombatSnapshot().Notices.IsEmpty());
	Access::Refresh(*Party);
	TestTrue(TEXT("Server also clears the replicated snapshot"), Access::Empty(*Party));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPartyRosterRecoveryTest, "ProjectRPG.Online.DungeonPartyCombat.RosterRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGDungeonPartyRosterRecoveryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Roster recovery world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Mode = World->SpawnActor<ARPGGameModeBase>();
	auto* GameState = World->SpawnActor<AGameStateBase>();
	const auto* ModeProperty = FindFProperty<FObjectPropertyBase>(UWorld::StaticClass(), TEXT("AuthorityGameMode"));
	if (!TestNotNull(TEXT("GameMode"), Mode) || !TestNotNull(TEXT("GameState"), GameState)
		|| !TestNotNull(TEXT("Authority world fixture property"), ModeProperty)) return false;
	// Only the synthetic world relationship/read response is supplied. No HTTP,
	// real ticket, socket ejection, backend expiry or gameplay grant is executed.
	ModeProperty->SetObjectPropertyValue_InContainer(World, Mode); World->SetGameState(GameState);
	const FString Session = TEXT("00000000-0000-0000-0000-000000000101");
	const FString OwnerId = TEXT("00000000-0000-0000-0000-000000000102");
	const FString PeerId = TEXT("00000000-0000-0000-0000-000000000103");
	const FString Steam = TEXT("76561198000000901");
	auto Add = [&](const FString& Character, int64 Generation)
	{
		auto* Controller = World->SpawnActor<ARPGPlayerController>();
		auto* Player = World->SpawnActor<ARPGPlayerState>();
		if (!Controller || !Player) return static_cast<ARPGPlayerController*>(nullptr);
		Controller->SetPlayerState(Player); Player->SetOwner(Controller);
		Player->SetAuthenticatedIdentity(Character, Steam, Session, Generation);
		GameState->PlayerArray.AddUnique(Player);
		return Controller;
	};
	auto* Owner = Add(OwnerId, 1);
	if (!TestNotNull(TEXT("Recipient"), Owner)) return false;
	auto* Party = Owner->FindComponentByClass<URPGDungeonPartyCombatComponent>();
	if (!TestNotNull(TEXT("Party component"), Party)) return false;
	FRPGDungeonMembershipSnapshot Read;
	Read.DungeonSessionId = Session; Read.ServerId = TEXT("party-server-fixture"); Read.State = TEXT("InProgress");
	Read.ObservedAt = TEXT("2026-10-25T00:03:00Z");
	FRPGDungeonMembershipRecord OwnRecord;
	OwnRecord.CharacterId = OwnerId; OwnRecord.SteamId = Steam; OwnRecord.CharacterName = TEXT("내 캐릭터");
	OwnRecord.ConnectionGeneration = 1; OwnRecord.bHasConnectionPresentation = true;
	FRPGDungeonMembershipRecord PeerRecord = OwnRecord;
	PeerRecord.CharacterId = PeerId; PeerRecord.CharacterName = TEXT("재접속 용사");
	PeerRecord.DisconnectedAt = TEXT("2026-10-25T00:00:00Z"); PeerRecord.ReconnectExpiresAt = TEXT("2026-10-25T00:10:00Z");
	Read.Members = {OwnRecord, PeerRecord};
	using Access = FRPGDungeonPartyCombatTestAccess;
	if (!TestTrue(TEXT("Validated server read is accepted"), Access::Read(*Mode, Read))) return false;
	Access::Refresh(*Party);
	auto View = Party->GetPartyCombatSnapshot();
	if (!TestEqual(TEXT("Backend roster preserves a peer with no Controller"), View.Members.Num(), 2)) return false;
	const int32 StableSlot = View.Members[1].MemberSlot;
	TestEqual(TEXT("Backend character name is used without publishing its ID"), View.Members[1].DisplayName, PeerRecord.CharacterName);
	TestEqual(TEXT("Confirmed disconnect has a distinct presence"), View.Members[1].Presence, ERPGDungeonPartyPresence::ReconnectGrace);
	TestFalse(TEXT("Disconnected peer does not retain health"), View.Members[1].bHasHealth);
	TestTrue(TEXT("Initial membership hydration does not announce historical disconnects"), View.Notices.IsEmpty());
	TestFalse(TEXT("Failed read is not accepted as a new roster"), Access::Read(*Mode, Read, false));
	Access::Age(*Mode); Access::Refresh(*Party);
	View = Party->GetPartyCombatSnapshot();
	TestEqual(TEXT("Failed/stale reads preserve both rows"), View.Members.Num(), 2);
	TestTrue(TEXT("Old membership is visibly stale"), View.bRosterStale);
	TestFalse(TEXT("Old reads cannot keep a precise countdown running"), View.Members[1].bHasReconnectDeadline);
	Read.Members = {OwnRecord};
	if (!TestTrue(TEXT("Member absence read accepted"), Access::Read(*Mode, Read))) return false;
	Access::Refresh(*Party);
	View = Party->GetPartyCombatSnapshot();
	TestEqual(TEXT("Absence without an immutable receipt is not removal"), View.Members.Num(), 2);
	TestEqual(TEXT("Absent unconfirmed row is not guessed to be disconnected/dead"), View.Members[1].Presence, ERPGDungeonPartyPresence::Unconfirmed);
	TestTrue(TEXT("Failed/stale reads and missing rows do not announce a departure"), View.Notices.IsEmpty());
	Read.Members = {OwnRecord, PeerRecord};
	if (!TestTrue(TEXT("Restored server read"), Access::Read(*Mode, Read))) return false;
	// GET can run after ticket consumption but before its admission confirmation.
	// The disconnect belongs to generation 1 even though the ticket advanced to 2.
	PeerRecord.ConnectionGeneration = 2;
	PeerRecord.DisconnectedGeneration = 1; PeerRecord.bHasDisconnectedGeneration = true;
	Read.Members = {OwnRecord, PeerRecord};
	if (!TestTrue(TEXT("Pending reconnect read accepted"), Access::Read(*Mode, Read))) return false;
	Access::Refresh(*Party);
	TestEqual(TEXT("Ticket consumption alone cannot hide the original disconnect"), Party->GetPartyCombatSnapshot().Members[1].Presence,
		ERPGDungeonPartyPresence::ReconnectGrace);
	auto* Peer = Add(PeerId, 2);
	if (!TestNotNull(TEXT("Confirmed newer admission"), Peer)) return false;
	Access::Refresh(*Party);
	View = Party->GetPartyCombatSnapshot();
	TestEqual(TEXT("Newer confirmed admission overrides an older disconnect read"), View.Members[1].Presence, ERPGDungeonPartyPresence::ServerObserved);
	TestEqual(TEXT("Reconnection preserves the same row slot"), View.Members[1].MemberSlot, StableSlot);
	if (!TestEqual(TEXT("Confirmed reconnection is announced once"), View.Notices.Num(), 1)) return false;
	TestEqual(TEXT("Reconnect event uses the recipient-local slot"), View.Notices[0].MemberSlot, StableSlot);
	TestEqual(TEXT("Reconnect event is explicit"), View.Notices[0].Kind, ERPGDungeonPartyNoticeKind::Reconnected);
	Peer->GetPlayerState<ARPGPlayerState>()->SetIsInactive(true);
	Access::Refresh(*Party);
	View = Party->GetPartyCombatSnapshot();
	TestEqual(TEXT("A second loss does not resurrect the older generation's timer"), View.Members[1].Presence, ERPGDungeonPartyPresence::Unconfirmed);
	TestEqual(TEXT("Losing an actor alone adds no new notice"), View.Notices.Num(), 1);
	PeerRecord.DisconnectedGeneration = 2;
	Read.Members = {OwnRecord, PeerRecord};
	if (!TestTrue(TEXT("Current connection disconnect read accepted"), Access::Read(*Mode, Read))) return false;
	Access::Refresh(*Party);
	TestEqual(TEXT("A disconnect for the newer connection is still displayed"), Party->GetPartyCombatSnapshot().Members[1].Presence,
		ERPGDungeonPartyPresence::ReconnectGrace);
	View = Party->GetPartyCombatSnapshot();
	if (!TestEqual(TEXT("Server-confirmed disconnect replaces its old reconnect notice"), View.Notices.Num(), 1)) return false;
	TestEqual(TEXT("Current disconnect notice kind"), View.Notices.Last().Kind, ERPGDungeonPartyNoticeKind::ConnectionLost);
	Read.Members = {OwnRecord};
	Read.DisconnectionExpulsions.Add({PeerId, Steam, TEXT("2026-10-25T00:00:00Z"),
		TEXT("2026-10-25T00:10:00Z"), TEXT("2026-10-25T00:10:01Z"), true});
	if (!TestTrue(TEXT("Exact server expulsion receipt is accepted"), Access::Read(*Mode, Read))) return false;
	Access::Refresh(*Party);
	TestEqual(TEXT("Only receipt-confirmed departed member is removed"), Party->GetPartyCombatSnapshot().Members.Num(), 1);
	View = Party->GetPartyCombatSnapshot();
	if (!TestEqual(TEXT("Immutable departure replaces its waiting notice"), View.Notices.Num(), 1)) return false;
	TestEqual(TEXT("Receipt-confirmed departure kind"), View.Notices.Last().Kind, ERPGDungeonPartyNoticeKind::DepartureConfirmed);
	Access::Refresh(*Party);
	TestTrue(TEXT("Polling the same receipt does not repeat departure"), Party->GetPartyCombatSnapshot().Notices == View.Notices);
	TestTrue(TEXT("Remaining participant keeps authenticated gameplay identity"), Owner->GetPlayerState<ARPGPlayerState>()->HasAuthenticatedCharacter());
	return true;
}
#endif
