#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonSessionSubsystem.h"
#include "UI/Lobby/RPGOnlineLobbyWidget.h"
#include "UI/Content/RPGDungeonResultActionsWidget.h"
#include "UI/RPGWindowInputState.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/LocalPlayer.h"
#include "IpNetDriver.h"
#include "IpConnection.h"
#include "Player/RPGPlayerState.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"
#include "Tests/RPGDungeonEjectionTestFixture.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetNavigation.h"
#include "InputCoreTypes.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

// Fixture identity/authentication only. Run production guards and callbacks, never
// ProcessRequest/map travel. This is NOT an authenticated game-server/backend E2E.
struct FRPGDungeonAbandonmentTestAccess
{
	static void Login(UHttpWebManager* Web, const FString& Token = TEXT("abandonment-fixture"))
	{
		const double Now = FPlatformTime::Seconds();
		Web->PlayerAuth.AcceptLogin(Token, TEXT("76561198000000001"), TEXT("2026-10-10T01:00:00Z"),
			TEXT("2026-10-10T00:00:00Z"), Now, Now);
	}
	static void Seed(URPGDungeonSessionSubsystem* Flow, UHttpWebManager* Web, UWorld* World,
		const FString& State = TEXT("InProgress"))
	{
		Flow->CancelLocalRecovery(); Flow->ClearAbandonmentIdentity();
		Flow->CurrentDungeonSession = FRPGDungeonSession();
		Flow->CurrentDungeonSession.DungeonSessionId = TEXT("00000000-0000-0000-0000-000000000001");
		Flow->SelectedCharacterId = TEXT("00000000-0000-0000-0000-000000000002");
		Flow->CurrentDungeonSession.State = State;
		FRPGDungeonSessionMember Member; Member.CharacterId = Flow->SelectedCharacterId;
		Flow->CurrentDungeonSession.Members.Add(Member);
		Flow->WebManager = Web; Flow->bAbandonmentTestingEnabled = true;
		Flow->LobbyMapPackage = World->GetOutermost()->GetName();
		Flow->PendingOperation = URPGDungeonSessionSubsystem::EPendingOperation::None;
		Flow->FlowState = ERPGDungeonClientFlowState::InSession; Flow->LastErrorCode.Reset();
		Web->OnDungeonAbandonmentCompleted.AddUniqueDynamic(Flow, &URPGDungeonSessionSubsystem::HandleDungeonAbandonmentCompleted);
	}
	static bool Begin(URPGDungeonSessionSubsystem* Flow, APlayerController* PC)
	{
		return Flow->BeginAbandonment(PC, Flow->CurrentDungeonSession.DungeonSessionId,
			Flow->SelectedCharacterId, TEXT("76561198000000001"));
	}
	static void Receive(URPGDungeonSessionSubsystem* Flow, const FRPGDungeonAbandonmentReceipt& Receipt,
		bool bSuccess = true, const FString& Error = FString())
	{ Flow->HandleDungeonAbandonmentCompleted(Receipt, bSuccess, Error); }
	static void SessionCallback(URPGDungeonSessionSubsystem* Flow)
	{ Flow->HandleDungeonSessionUpdated(FRPGDungeonSession(), false); }
	static void Disable(URPGDungeonSessionSubsystem* Flow) { Flow->bAbandonmentTestingEnabled = false; }
	static void ChangeMap(URPGDungeonSessionSubsystem* Flow) { Flow->LobbyMapPackage = TEXT("/Game/Maps/NotThisWorld"); }
	static void ChangeCachedState(URPGDungeonSessionSubsystem* Flow) { Flow->CurrentDungeonSession.State = TEXT("SettlementPending"); }
	static void SetCachedState(URPGDungeonSessionSubsystem* Flow, const FString& State) { Flow->CurrentDungeonSession.State = State; }
	static void SimulateRecovery(URPGDungeonSessionSubsystem* Flow)
	{
		// Same production path resets pending callbacks but retains uncertain identity.
		Flow->FailOperation(TEXT("lobby_travel_failed")); Flow->WebManager->InvalidatePlayerDungeonRequests();
	}
	static uint64 Generation(UHttpWebManager* Web) { return Web->PlayerDungeonRequestGeneration; }
	static FHttpRequestRef BuildRequest(UHttpWebManager* Web)
	{
		Web->BackendServiceToken = TEXT("server-fixture-must-never-be-used");
		return Web->CreateDungeonAbandonmentRequest(TEXT("00000000-0000-0000-0000-000000000001"),
			TEXT("00000000-0000-0000-0000-000000000002"), TEXT("76561198000000001"));
	}
	static bool BeginWithIdentity(URPGDungeonSessionSubsystem* Flow, APlayerController* PC,
		const FString& Session, const FString& Character, const FString& Steam)
	{ return Flow->BeginAbandonment(PC, Session, Character, Steam); }
	static void Respond(UHttpWebManager* Web, FHttpRequestPtr Request, FHttpResponsePtr Response,
		bool bSuccessful, uint64 Generation)
	{
		Web->OnAbandonDungeonSessionResponse(Request, Response, bSuccessful,
			TEXT("00000000-0000-0000-0000-000000000001"), TEXT("00000000-0000-0000-0000-000000000002"),
			TEXT("76561198000000001"), Generation);
	}
	static void BindWidget(URPGOnlineLobbyWidget* Widget, URPGDungeonSessionSubsystem* Flow,
		UHttpWebManager* Web, URPGSteamAuthSubsystem* Auth)
	{
		Widget->Flow = Flow; Widget->Web = Web; Widget->Auth = Auth;
		FRPGBackendCharacter Character; Character.CharacterId = Flow->GetSelectedCharacterId();
		Widget->OwnedCharacters.Add(Character);
		Widget->ConfirmAbandonmentSessionId = Flow->GetCurrentDungeonSession().DungeonSessionId;
		Widget->ConfirmAbandonmentCharacterId = Character.CharacterId;
		Widget->ConfirmAbandonmentSteamId = Web->GetAuthenticatedSteamId();
		Widget->bAbandonmentConfirmationOpen = true;
	}
	static bool CanConfirm(URPGOnlineLobbyWidget* Widget) { return Widget->CanConfirmAbandonment(); }
	static void ChangeConfirmation(URPGOnlineLobbyWidget* Widget) { Widget->ConfirmAbandonmentSessionId = TEXT("stale-session"); }
	static bool IsOpen(URPGOnlineLobbyWidget* Widget) { return Widget->bAbandonmentConfirmationOpen; }
	static void EnableResult(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->bResultAbandonmentTestingEnabled = true;
		Flow->LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");
	}
	static void EnableGameplay(URPGDungeonSessionSubsystem* Flow)
	{
		Flow->bGameplayAbandonmentTestingEnabled = true;
		Flow->LobbyMapPackage = TEXT("/Game/Maps/LobbyMap");
	}
	static void BindGameplayWidget(URPGDungeonResultActionsWidget* Widget, URPGDungeonSessionSubsystem* Flow, UHttpWebManager* Web)
	{
		Widget->DungeonFlow = Flow; Widget->WebManager = Web;
		Widget->InitializeGameplayAbandonment(); Widget->RefreshPresentation();
	}
	static bool ClaimGameplayWindow(ARPGPlayerController* PC, URPGDungeonResultActionsWidget* Widget)
	{
		PC->GameplayAbandonmentWidget = Widget;
		return PC->WindowInputState->BeginWindow(*PC, *Widget);
	}
	static bool HasGameplayWindow(ARPGPlayerController* PC) { return PC->GameplayAbandonmentWidget != nullptr; }
	static void CancelTravel(URPGDungeonSessionSubsystem* Flow) { Flow->CancelLocalRecovery(); }
	static void Arrive(URPGDungeonSessionSubsystem* Flow, UWorld* World)
	{
		Flow->LobbyMapPackage = World->GetOutermost()->GetName();
		Flow->HandleLobbyMapLoaded(World);
	}
	static void BindResultWidget(URPGDungeonResultActionsWidget* Widget, URPGDungeonSessionSubsystem* Flow, UHttpWebManager* Web)
	{
		Widget->DungeonFlow = Flow; Widget->WebManager = Web;
		Widget->InitializeResult(false); Widget->RefreshPresentation();
	}
	static bool ResultOpen(URPGDungeonResultActionsWidget* Widget) { return Widget->bAbandonmentConfirmationOpen; }
	static bool ResultCanConfirm(URPGDungeonResultActionsWidget* Widget) { return Widget->CanConfirmAbandonment(); }
	static bool Back(URPGOnlineLobbyWidget* Widget, FKey Key, bool bRepeat = false)
	{ return Widget->NativeOnPreviewKeyDown(FGeometry(), FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0)).IsEventHandled(); }
	static bool Back(URPGDungeonResultActionsWidget* Widget, FKey Key, bool bRepeat = false)
	{ return Widget->NativeOnPreviewKeyDown(FGeometry(), FKeyEvent(Key, FModifierKeysState(), 0, bRepeat, 0, 0)).IsEventHandled(); }
};

namespace
{
	void CheckConfirmationNavigation(FAutomationTestBase& Test, UButton* Confirm, UButton* Cancel)
	{
		for (auto* Button : {Confirm, Cancel})
		{
			if (!Test.TestNotNull(TEXT("Confirmation navigation is configured"), Button->Navigation.Get())) continue;
			const auto* Navigation = Button->Navigation.Get();
			for (const auto* Rule : {&Navigation->Up, &Navigation->Down, &Navigation->Left,
				&Navigation->Right, &Navigation->Next, &Navigation->Previous})
			{
				Test.TestTrue(TEXT("Directions and Tab cannot escape the two explicit choices"), Rule->Rule == EUINavigationRule::Explicit);
				Test.TestEqual(TEXT("Navigation alternates confirm/cancel, never a background action"),
					Rule->Widget.Get(), static_cast<UWidget*>(Button == Confirm ? Cancel : Confirm));
			}
		}
	}
	const FString SessionId = TEXT("00000000-0000-0000-0000-000000000001");
	const FString CharacterId = TEXT("00000000-0000-0000-0000-000000000002");
	FRPGDungeonAbandonmentReceipt Receipt(const FString& State = TEXT("InProgress"))
	{
		FRPGDungeonAbandonmentReceipt Value;
		Value.DungeonSessionId = SessionId; Value.CharacterId = CharacterId;
		Value.AbandonedAt = TEXT("2026-10-10T00:00:00+00:00");
		Value.SessionStateAtAbandonment = State; Value.bRewardExcluded = true;
		return Value;
	}
	FString Body(const FRPGDungeonAbandonmentReceipt& Value = Receipt())
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"abandonedAt\":\"%s\",\"sessionStateAtAbandonment\":\"%s\",\"rewardExcluded\":%s}"),
			*Value.DungeonSessionId, *Value.CharacterId, *Value.AbandonedAt, *Value.SessionStateAtAbandonment,
			Value.bRewardExcluded ? TEXT("true") : TEXT("false"));
	}
	class FAbandonmentResponse final : public IHttpResponse
	{
	public:
		FAbandonmentResponse(int32 InCode, FString InBody) : Code(InCode), Content(MoveTemp(InBody))
		{
			const FTCHARToUTF8 Utf8(*Content); Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		}
		const FString& GetURL() const override { return Url; }
		const FString& GetEffectiveURL() const override { return Url; }
		EHttpRequestStatus::Type GetStatus() const override { return EHttpRequestStatus::Succeeded; }
		EHttpFailureReason GetFailureReason() const override { return EHttpFailureReason::None; }
		FString GetURLParameter(const FString&) const override { return {}; }
		FString GetHeader(const FString&) const override { return {}; }
		TArray<FString> GetAllHeaders() const override { return {}; }
		FString GetContentType() const override { return TEXT("application/json"); }
		uint64 GetContentLength() const override { return Bytes.Num(); }
		const TArray<uint8>& GetContent() const override { return Bytes; }
		TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		int32 GetResponseCode() const override { return Code; }
		FString GetContentAsString() const override { return Content; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Code; FString Content; FString Url = TEXT("http://fixture.invalid/api"); TArray<uint8> Bytes;
	};
	FHttpResponsePtr HttpResponse(int32 Code, FString Content = Body())
	{ return MakeShared<FAbandonmentResponse, ESPMode::ThreadSafe>(Code, MoveTemp(Content)); }
	struct FAbandonmentWorld
	{
		UWorld* World;
		URPGDungeonMutationTestGameInstance* Instance;
		APlayerController* PC;
		URPGDungeonSessionSubsystem* Flow;
		UHttpWebManager* Web;
		FAbandonmentWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false,
				MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			Instance = NewObject<URPGDungeonMutationTestGameInstance>();
			World->SetGameInstance(Instance); Context.OwningGameInstance = Instance;
			Instance->SetTestWorldContext(&Context);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			PC = World->SpawnActor<APlayerController>(); PC->SetAsLocalPlayerController();
			Web = NewObject<UHttpWebManager>(Instance); Flow = NewObject<URPGDungeonSessionSubsystem>(Instance);
			FRPGDungeonAbandonmentTestAccess::Login(Web);
			FRPGDungeonAbandonmentTestAccess::Seed(Flow, Web, World);
		}
		~FAbandonmentWorld()
		{
			World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
			Instance->SetTestWorldContext(nullptr);
			GEngine->DestroyWorldContext(World);
		}
	};
	struct FResultAbandonmentWorld : FAbandonmentWorld
	{
		ARPGPlayerController* ResultController;
		ARPGPlayerState* State;
		UIpNetDriver* Driver;
		FRPGBossBattleSnapshot* Snapshot;
		FResultAbandonmentWorld()
		{
			ResultController = World->SpawnActor<ARPGPlayerController>();
			ResultController->SetAsLocalPlayerController();
			auto* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
			ResultController->Player = LocalPlayer; LocalPlayer->PlayerController = ResultController;
			State = World->SpawnActor<ARPGPlayerState>();
			ResultController->SetPlayerState(State); State->SetOwner(ResultController);
			State->SetAuthenticatedIdentity(CharacterId, TEXT("76561198000000001"), SessionId);
			auto* Battle = World->SpawnActor<ARPGBossBattleGameState>(); World->SetGameState(Battle);
			Snapshot = FindFProperty<FStructProperty>(Battle->GetClass(), TEXT("BattleSnapshot"))->ContainerPtrToValuePtr<FRPGBossBattleSnapshot>(Battle);
			Snapshot->BattleState = EBossBattleState::Defeated; Snapshot->Revision = 11;
			FRPGDungeonAbandonmentTestAccess::EnableResult(Flow);
			Driver = NewObject<UIpNetDriver>(); Driver->ServerConnection = NewObject<UIpConnection>(Driver);
			Driver->ServerConnection->SetConnectionState(USOCK_Closed); // No packets, planned-close RPC or sockets.
			Driver->SetWorld(World); World->SetNetDriver(Driver);
		}
		void DetachDriver() { World->SetNetDriver(nullptr); Driver->SetWorld(nullptr); Driver->ServerConnection = nullptr; }
		~FResultAbandonmentWorld()
		{
			FRPGDungeonAbandonmentTestAccess::CancelTravel(Flow);
			DetachDriver();
			GEngine->GetWorldContextFromWorldChecked(World).TravelURL.Reset();
		}
		bool Queue(int32 Revision = 11, const FString& ExpectedSession = SessionId)
		{
			return Flow->QueueDungeonAbandonmentFromResult(ResultController, Revision, ExpectedSession, CharacterId, TEXT("76561198000000001"));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonAbandonmentCodecTest, "ProjectRPG.Online.DungeonAbandonmentCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonAbandonmentCodecTest::RunTest(const FString&)
{
	FRPGDungeonAbandonmentReceipt Parsed;
	for (const FString State : {FString(TEXT("Loading")), FString(TEXT("InProgress")), FString(TEXT("Closed"))})
		TestTrue(TEXT("Only opt-out historical states are receipts"), FRPGDungeonAbandonmentCodec::Decode(Body(Receipt(State)), 200, SessionId, CharacterId, Parsed));
	for (int32 Status : {0, 201, 202, 204, 401, 403, 404, 409, 500})
	{
		TestFalse(TEXT("Only 200 acknowledges a persisted opt-out"), FRPGDungeonAbandonmentCodec::Decode(Body(), Status, SessionId, CharacterId, Parsed));
		TestTrue(TEXT("Failed decode clears a prior receipt"), Parsed.DungeonSessionId.IsEmpty());
	}
	for (const FString Value : {FString(), FString(TEXT("{}")), FString(TEXT("[]")), FString(TEXT("broken")),
		Body().Replace(TEXT("true"), TEXT("\"true\"")), Body().Replace(TEXT("true"), TEXT("1")),
		Body().Replace(TEXT("rewardExcluded"), TEXT("missingField"))})
		TestFalse(TEXT("Malformed/missing/incorrectly typed receipt is untrusted"), FRPGDungeonAbandonmentCodec::Decode(Value, 200, SessionId, CharacterId, Parsed));
	TArray<FRPGDungeonAbandonmentReceipt> Invalid;
	auto Bad = Receipt(); Bad.DungeonSessionId = TEXT("00000000-0000-0000-0000-000000000003"); Invalid.Add(Bad);
	Bad = Receipt(); Bad.CharacterId = TEXT("00000000-0000-0000-0000-000000000003"); Invalid.Add(Bad);
	Bad = Receipt(); Bad.bRewardExcluded = false; Invalid.Add(Bad);
	for (const FString State : {FString(TEXT("Waiting")), FString(TEXT("SettlementPending")), FString(TEXT("Cleared")), FString(TEXT("Failed")), FString(TEXT("inprogress"))})
	{ Bad = Receipt(State); Invalid.Add(Bad); }
	for (const FString Time : {FString(), FString(TEXT("2026-10-10")), FString(TEXT("not-time")), FString(TEXT("2026-10-10T00:00:00")), FString(TEXT("2026-10-10T09:00:00+09:00"))})
	{ Bad = Receipt(); Bad.AbandonedAt = Time; Invalid.Add(Bad); }
	for (const auto& Value : Invalid)
		TestFalse(TEXT("Identity/reward/state/UTC-time must all match"), FRPGDungeonAbandonmentCodec::Decode(Body(Value), 200, SessionId, CharacterId, Parsed));
	Bad = Receipt(); Bad.AbandonedAt = TEXT("2026-10-10T00:00:00Z");
	TestTrue(TEXT("UTC Z and .NET +00:00 are accepted"), FRPGDungeonAbandonmentCodec::Decode(Body(Bad), 200, SessionId, CharacterId, Parsed));
	TestFalse(TEXT("Empty expected identities never validate"), FRPGDungeonAbandonmentCodec::Decode(Body(), 200, FString(), FString(), Parsed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonAbandonmentFlowTest, "ProjectRPG.Online.DungeonAbandonmentFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonAbandonmentFlowTest::RunTest(const FString&)
{
	FAbandonmentWorld Fixture;
	auto* Flow = Fixture.Flow;
	FRPGDungeonAbandonmentTestAccess::Disable(Flow);
	TestFalse(TEXT("Default production/Shipping feature gate cannot opt out"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	FRPGDungeonAbandonmentTestAccess::Seed(Flow, Fixture.Web, Fixture.World);
	FRPGDungeonAbandonmentTestAccess::ChangeMap(Flow);
	TestFalse(TEXT("This milestone cannot opt out in a dungeon world"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	for (const FString State : {FString(TEXT("Waiting")), FString(TEXT("SettlementPending")), FString(TEXT("Cleared")), FString(TEXT("Failed")), FString(TEXT("Closed"))})
	{
		FRPGDungeonAbandonmentTestAccess::Seed(Flow, Fixture.Web, Fixture.World, State);
		TestFalse(TEXT("First attempt is unavailable before start/after settlement"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	}
	for (const FString State : {FString(TEXT("Loading")), FString(TEXT("InProgress"))})
	{
		FRPGDungeonAbandonmentTestAccess::Seed(Flow, Fixture.Web, Fixture.World, State);
		TestFalse(TEXT("Confirmation for another session cannot submit"), FRPGDungeonAbandonmentTestAccess::BeginWithIdentity(Flow,
			Fixture.PC, TEXT("00000000-0000-0000-0000-000000000003"), CharacterId, TEXT("76561198000000001")));
		TestFalse(TEXT("Confirmation for another character cannot submit"), FRPGDungeonAbandonmentTestAccess::BeginWithIdentity(Flow,
			Fixture.PC, SessionId, TEXT("00000000-0000-0000-0000-000000000003"), TEXT("76561198000000001")));
		TestFalse(TEXT("Confirmation for another account cannot submit"), FRPGDungeonAbandonmentTestAccess::BeginWithIdentity(Flow,
			Fixture.PC, SessionId, CharacterId, TEXT("76561198000000002")));
		TestFalse(TEXT("Rejected confirmations never create a mutation marker"), Flow->HasUnresolvedDungeonAbandonment());
		TestTrue(TEXT("Production prepare guards allow an explicit development-lobby attempt"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
		TestTrue(TEXT("Pending request retains membership"), Flow->HasActiveDungeonSession());
		TestFalse(TEXT("Double-click is rejected"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
		FRPGDungeonAbandonmentTestAccess::SessionCallback(Flow);
		TestTrue(TEXT("Ordinary session callback cannot finish opt-out"), Flow->IsRequestInFlight());
		auto Wrong = Receipt(); Wrong.CharacterId = TEXT("other-character");
		FRPGDungeonAbandonmentTestAccess::Receive(Flow, Wrong);
		TestTrue(TEXT("Another character cannot release the request"), Flow->IsRequestInFlight());
		FRPGDungeonAbandonmentTestAccess::Receive(Flow, Receipt(State));
		TestFalse(TEXT("Verified receipt releases pending request"), Flow->IsRequestInFlight());
		TestFalse(TEXT("Own cache is released only after acknowledgement"), Flow->HasActiveDungeonSession());
		TestEqual(TEXT("Selection is retained"), Flow->GetSelectedCharacterId(), CharacterId);
		TestTrue(TEXT("Authentication is retained"), Fixture.Web->HasAccessToken());
		TestFalse(TEXT("Receipt resolves uncertainty"), Flow->HasUnresolvedDungeonAbandonment());
		TestEqual(TEXT("Immutable receipt state is not applied as current party state"), Flow->GetCurrentDungeonSession().State, FString());
	}
	FRPGDungeonAbandonmentTestAccess::Seed(Flow, Fixture.Web, Fixture.World);
	FRPGDungeonAbandonmentTestAccess::Receive(Flow, Receipt(TEXT("Closed")));
	TestTrue(TEXT("Late receipt with no pending opt-out cannot erase another session"), Flow->HasActiveDungeonSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonAbandonmentRetryTest, "ProjectRPG.Online.DungeonAbandonmentRetryPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonAbandonmentRetryTest::RunTest(const FString&)
{
	FAbandonmentWorld Fixture;
	auto* Flow = Fixture.Flow;
	TestTrue(TEXT("Begin initial request"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	FRPGDungeonAbandonmentTestAccess::Receive(Flow, Receipt(), false, TEXT("dungeon_abandonment_unconfirmed"));
	TestTrue(TEXT("Timeout preserves old session and uncertainty"), Flow->HasActiveDungeonSession() && Flow->HasUnresolvedDungeonAbandonment());
	TestFalse(TEXT("Timeout releases busy state for reauthentication/retry"), Flow->IsRequestInFlight());
	TestFalse(TEXT("Unconfirmed opt-out cannot switch character"), Flow->SelectCharacter(TEXT("another-character")));
	TestFalse(TEXT("Unconfirmed opt-out cannot create"), Flow->CreateDungeonSession(TEXT("Dungeon.New"), TEXT("Hard")));
	TestFalse(TEXT("Unconfirmed opt-out cannot join"), Flow->JoinDungeonSession(TEXT("new-session")));
	TestFalse(TEXT("Unconfirmed opt-out cannot resume"), Flow->ResumeDungeonSession());
	TestFalse(TEXT("A GET cannot resolve an uncertain POST"), Flow->RefreshDungeonSession());
	TestFalse(TEXT("Waiting DELETE cannot substitute for opt-out retry"), Flow->LeaveDungeonSession());
	TestFalse(TEXT("Unconfirmed opt-out cannot enter"), Flow->ConnectToDungeon(Fixture.PC, TEXT("127.0.0.1:7777")));
	TestFalse(TEXT("Unconfirmed opt-out cannot normal-return"), Flow->ReturnToLobby(Fixture.PC));
	Flow->ResetDungeonFlow(false);
	TestEqual(TEXT("Reset cannot drop the retry identity/selection"), Flow->GetSelectedCharacterId(), CharacterId);
	FRPGDungeonAbandonmentTestAccess::ChangeCachedState(Flow);
	TestTrue(TEXT("Historical attempt may be retried even if cached state advanced"), Flow->CanAbandonDungeonSession());
	Fixture.Web->ClearAccessToken();
	TestFalse(TEXT("Expiry blocks retry but preserves uncertainty"), Flow->CanAbandonDungeonSession());
	TestTrue(TEXT("Expiry retains attempt"), Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Login(Fixture.Web, TEXT("renewed-fixture"));
	TestTrue(TEXT("Same account can retry after renewal"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	FRPGDungeonAbandonmentTestAccess::SimulateRecovery(Flow);
	TestTrue(TEXT("Recovery/failure cannot forget an uncertain opt-out"), Flow->HasUnresolvedDungeonAbandonment());
	TestTrue(TEXT("Retry after invalidation retains exact old identities"), FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC));
	FRPGDungeonAbandonmentTestAccess::Receive(Flow, Receipt(TEXT("Closed")));
	TestTrue(TEXT("Original immutable receipt completes retry"), Flow->GetCurrentDungeonSession().DungeonSessionId.IsEmpty());
	FRPGDungeonAbandonmentTestAccess::Seed(Flow, Fixture.Web, Fixture.World);
	FRPGDungeonAbandonmentTestAccess::Begin(Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Receive(Flow, Receipt(), false, TEXT("dungeon_abandonment_not_available"));
	TestFalse(TEXT("Definitive settlement rejection resolves attempted mutation"), Flow->HasUnresolvedDungeonAbandonment());
	TestTrue(TEXT("Settlement rejection leaves membership for normal completion"), Flow->HasActiveDungeonSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonAbandonmentTransportTest, "ProjectRPG.Online.DungeonAbandonmentTransport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonAbandonmentTransportTest::RunTest(const FString&)
{
	FAbandonmentWorld Fixture;
	const auto BuiltRequest = FRPGDungeonAbandonmentTestAccess::BuildRequest(Fixture.Web);
	TestEqual(TEXT("Opt-out uses the existing dedicated route"), BuiltRequest->GetURL(),
		FString(TEXT("http://localhost:3000/api/dungeon-sessions/")) + SessionId + TEXT("/members/") + CharacterId + TEXT("/abandon"));
	TestEqual(TEXT("Opt-out is POST, not Waiting DELETE"), BuiltRequest->GetVerb(), FString(TEXT("POST")));
	TestEqual(TEXT("Opt-out has no caller-supplied authority body"), BuiltRequest->GetContent().Num(), 0);
	TestEqual(TEXT("Player credential is used even with a configured server token"), BuiltRequest->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer abandonment-fixture")));
	auto Request = FHttpModule::Get().CreateRequest(); Fixture.Web->ApplyCommonHeaders(Request);
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	const uint64 OldGeneration = FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, nullptr, false, OldGeneration);
	TestTrue(TEXT("Transport failure cannot apply a receipt"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	const uint64 Generation = FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(401), true, OldGeneration);
	TestTrue(TEXT("Pre-retry stale 401 does not erase credentials"), Fixture.Web->HasAccessToken());
	TestTrue(TEXT("Stale response cannot finish retry"), Fixture.Flow->IsRequestInFlight());
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(200, Body().Replace(TEXT("true"), TEXT("false"))), true, Generation);
	TestTrue(TEXT("False reward exclusion preserves membership/identity"), Fixture.Flow->HasActiveDungeonSession() && Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Login(Fixture.Web, TEXT("fresh-credential"));
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(401), true, FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestTrue(TEXT("Old-credential 401 also cannot erase renewal"), Fixture.Web->HasAccessToken());
	TestFalse(TEXT("Ignored old credential releases this busy attempt without applying data"), Fixture.Flow->IsRequestInFlight());
	Request = FHttpModule::Get().CreateRequest(); Fixture.Web->ApplyCommonHeaders(Request);
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(409, TEXT("{\"error\":\"unknown\"}")), true,
		FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestTrue(TEXT("Unknown conflicts remain uncertain"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(409, TEXT("{\"error\":\"DUNGEON_ABANDONMENT_NOT_AVAILABLE\"}")), true,
		FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestTrue(TEXT("Case-mismatched conflict cannot authorize clearing uncertainty"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(409, TEXT("{\"error\":\"dungeon_abandonment_not_available\"}")), true,
		FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestFalse(TEXT("Exact authenticated conflict restores normal flow"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(401), true, FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestFalse(TEXT("Current credential 401 requires reauthentication"), Fixture.Web->HasAccessToken());
	TestTrue(TEXT("Current 401 still preserves old session and attempt"), Fixture.Flow->HasActiveDungeonSession() && Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Login(Fixture.Web, TEXT("final-renewal"));
	Request = FHttpModule::Get().CreateRequest(); Fixture.Web->ApplyCommonHeaders(Request);
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(202), true, FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestTrue(TEXT("Accepted but uncommitted status is not a receipt"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
	FRPGDungeonAbandonmentTestAccess::Respond(Fixture.Web, Request, HttpResponse(200), true, FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web));
	TestFalse(TEXT("Validated response releases own cached membership"), Fixture.Flow->HasActiveDungeonSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonAbandonmentConfirmationTest, "ProjectRPG.Online.DungeonAbandonmentConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonAbandonmentConfirmationTest::RunTest(const FString&)
{
	FAbandonmentWorld Fixture;
	auto* Widget = CreateWidget<URPGOnlineLobbyWidget>(Fixture.World, URPGOnlineLobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("Native confirmation widget"), Widget)) return false;
	TArray<UButton*> Buttons;
	{
		const auto Slate = Widget->TakeWidget();
		for (const FName Name : {TEXT("AbandonDungeonSessionButton"), TEXT("ConfirmDungeonAbandonmentButton"), TEXT("CancelDungeonAbandonmentButton")})
		{
			auto* Button = Cast<UButton>(Widget->GetWidgetFromName(Name));
			if (!TestNotNull(TEXT("Actual native opt-out action"), Button)) return false;
			Buttons.Add(Button); TestTrue(TEXT("Actual click is bound"), Button->OnClicked.IsBound());
			TestFalse(TEXT("Unbound context is disabled"), Button->GetIsEnabled());
			Button->OnClicked.Broadcast(); // Dispatch cannot bypass context or send HTTP.
		}
		TestNotNull(TEXT("Explicit reward-exclusion warning"), Widget->GetWidgetFromName(TEXT("DungeonAbandonmentWarning")));
		auto* Auth = NewObject<URPGSteamAuthSubsystem>(Fixture.Instance);
		FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
		TestTrue(TEXT("Confirmation captures owned account/character/session"), FRPGDungeonAbandonmentTestAccess::CanConfirm(Widget));
		const uint64 Generation = FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web);
		CheckConfirmationNavigation(*this, Buttons[1], Buttons[2]);
		TestFalse(TEXT("Accept is not intercepted as confirmation or cancellation"), FRPGDungeonAbandonmentTestAccess::Back(Widget, EKeys::Enter));
		TestTrue(TEXT("Accept preview alone cannot mutate consent"), FRPGDungeonAbandonmentTestAccess::IsOpen(Widget));
		for (const auto Key : {EKeys::Escape, EKeys::Virtual_Gamepad_Back.GetVirtualKey()})
		{
			FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
			TestTrue(TEXT("Held back is consumed"), FRPGDungeonAbandonmentTestAccess::Back(Widget, Key, true));
			TestTrue(TEXT("Held back cannot dismiss new consent"), FRPGDungeonAbandonmentTestAccess::IsOpen(Widget));
			TestTrue(TEXT("Fresh back closes only the confirmation"), FRPGDungeonAbandonmentTestAccess::Back(Widget, Key));
			TestFalse(TEXT("Back closes confirmation"), FRPGDungeonAbandonmentTestAccess::IsOpen(Widget));
			TestFalse(TEXT("Back outside confirmation is not swallowed"), FRPGDungeonAbandonmentTestAccess::Back(Widget, Key));
		}
		FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
		Buttons[2]->OnClicked.Broadcast();
		TestFalse(TEXT("Cancel closes confirmation"), FRPGDungeonAbandonmentTestAccess::IsOpen(Widget));
		TestFalse(TEXT("Cancel cannot create a pending mutation"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
		TestEqual(TEXT("Cancel cannot invalidate/send dungeon transport"), FRPGDungeonAbandonmentTestAccess::Generation(Fixture.Web), Generation);
		FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
		FRPGDungeonAbandonmentTestAccess::ChangeConfirmation(Widget);
		TestFalse(TEXT("Changed identity invalidates confirmation"), FRPGDungeonAbandonmentTestAccess::CanConfirm(Widget));
		Buttons[1]->OnClicked.Broadcast();
		TestFalse(TEXT("Stale confirmed click cannot submit"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
		FRPGDungeonAbandonmentTestAccess::Begin(Fixture.Flow, Fixture.PC);
		FRPGDungeonAbandonmentTestAccess::Receive(Fixture.Flow, Receipt(), false, TEXT("dungeon_abandonment_unconfirmed"));
		FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
		Buttons[2]->OnClicked.Broadcast();
		TestTrue(TEXT("Cancel cannot undo an earlier uncertain POST"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
		FRPGDungeonAbandonmentTestAccess::BindWidget(Widget, Fixture.Flow, Fixture.Web, Auth);
		FRPGDungeonAbandonmentTestAccess::Back(Widget, EKeys::Escape);
		TestTrue(TEXT("Back cannot undo an earlier uncertain POST"), Fixture.Flow->HasUnresolvedDungeonAbandonment());
	}
	for (auto* Button : Buttons) TestFalse(TEXT("Teardown unbinds confirmation dispatchers"), Button->OnClicked.IsBound());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultAbandonmentQueueTest, "ProjectRPG.Online.DungeonAbandonmentResultQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonResultAbandonmentQueueTest::RunTest(const FString&)
{
	FResultAbandonmentWorld F;
	using Access = FRPGDungeonAbandonmentTestAccess;
	TestTrue(TEXT("Engine final GetWorld uses the bound fixture context"), F.Instance->GetWorld() == F.World);
	TestNull(TEXT("Binding does not initialize HTTP subsystems"), F.Instance->GetSubsystem<UHttpWebManager>());
	TestEqual(TEXT("Isolated remote client fixture"), F.World->GetNetMode(), NM_Client);
	TestTrue(TEXT("Admitted current defeat can queue a confirmed lobby request"), F.Flow->CanQueueDungeonAbandonmentFromResult(F.ResultController));
	TestFalse(TEXT("Old result confirmation cannot queue travel"), F.Queue(10));
	TestFalse(TEXT("Another session cannot replace confirmation"), F.Queue(11, TEXT("00000000-0000-0000-0000-000000000003")));
	TestFalse(TEXT("Rejected confirmation does not create uncertainty"), F.Flow->HasUnresolvedDungeonAbandonment());
	F.Snapshot->BattleState = EBossBattleState::Victory;
	TestFalse(TEXT("Victory cannot opt out of accepted rewards"), F.Queue());
	F.Snapshot->BattleState = EBossBattleState::Defeated;
	Access::Disable(F.Flow);
	TestFalse(TEXT("Default/Shipping gate is not bypassed by result route"), F.Queue());
	Access::Seed(F.Flow, F.Web, F.World); Access::EnableResult(F.Flow);
	const uint64 Before = Access::Generation(F.Web);
	TestTrue(TEXT("Confirmed action queues local recovery"), F.Queue());
	TestEqual(TEXT("Travel is pending, not abandonment completion"), F.Flow->GetFlowState(), ERPGDungeonClientFlowState::ReturningToLobby);
	TestTrue(TEXT("Queued consent survives map transition"), F.Flow->HasQueuedDungeonAbandonment() && F.Flow->HasUnresolvedDungeonAbandonment());
	TestTrue(TEXT("Travel invalidates old callbacks without an abandonment POST"), Access::Generation(F.Web) > Before);
	TestTrue(TEXT("Membership is retained before receipt"), F.Flow->HasActiveDungeonSession());
	TestFalse(TEXT("Duplicate result confirmation cannot queue twice"), F.Queue());
	TestFalse(TEXT("Queued consent blocks another character"), F.Flow->SelectCharacter(TEXT("other")));
	TestFalse(TEXT("Queued consent blocks reconnect"), F.Flow->ConnectToDungeon(F.ResultController, FString()));
	Access::CancelTravel(F.Flow); F.DetachDriver(); F.Web->ClearAccessToken();
	Access::Arrive(F.Flow, F.World); // Expired credential: production callback cannot send HTTP.
	TestTrue(TEXT("Expiry on arrival retains queued identity"), F.Flow->HasQueuedDungeonAbandonment());
	TestFalse(TEXT("Expiry releases busy state for re-login"), F.Flow->IsRequestInFlight());
	Access::Login(F.Web);
	TestTrue(TEXT("Same identity can start existing lobby request after renewal"), Access::Begin(F.Flow, F.PC));
	TestFalse(TEXT("Submission consumes queued consent exactly once"), F.Flow->HasQueuedDungeonAbandonment());
	Access::Receive(F.Flow, Receipt(), false, TEXT("dungeon_abandonment_unconfirmed"));
	TestTrue(TEXT("Lost response retains uncertainty and membership"), F.Flow->HasUnresolvedDungeonAbandonment() && F.Flow->HasActiveDungeonSession());
	TestTrue(TEXT("Same pinned request may be retried"), Access::Begin(F.Flow, F.PC));
	Access::Receive(F.Flow, Receipt());
	TestFalse(TEXT("Only verified receipt releases old membership"), F.Flow->HasActiveDungeonSession());
	TestEqual(TEXT("Receipt retains selected character"), F.Flow->GetSelectedCharacterId(), CharacterId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonResultAbandonmentConfirmationTest, "ProjectRPG.Online.DungeonAbandonmentResultConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonResultAbandonmentConfirmationTest::RunTest(const FString&)
{
	FResultAbandonmentWorld F;
	using Access = FRPGDungeonAbandonmentTestAccess;
	auto* Widget = CreateWidget<URPGDungeonResultActionsWidget>(F.ResultController, URPGDungeonResultActionsWidget::StaticClass());
	if (!TestNotNull(TEXT("Owned defeat actions widget"), Widget)) return false;
	TArray<UButton*> Buttons;
	{
		const auto Slate = Widget->TakeWidget();
		Access::BindResultWidget(Widget, F.Flow, F.Web);
		for (const FName Name : {TEXT("AbandonDungeonFromResultButton"), TEXT("ConfirmResultAbandonmentButton"), TEXT("CancelResultAbandonmentButton")})
		{
			auto* Button = Cast<UButton>(Widget->GetWidgetFromName(Name));
			if (!TestNotNull(TEXT("Actual result confirmation action"), Button)) return false;
			Buttons.Add(Button); TestTrue(TEXT("Result confirmation dispatcher is bound"), Button->OnClicked.IsBound());
		}
		TestNotNull(TEXT("Before-receipt warning is authored in native tree"), Widget->GetWidgetFromName(TEXT("ResultAbandonmentWarning")));
		TestTrue(TEXT("Defeat action is enabled for admitted local client"), Buttons[0]->GetIsEnabled());
		const uint64 Before = Access::Generation(F.Web);
		CheckConfirmationNavigation(*this, Buttons[1], Buttons[2]);
		for (const auto Key : {EKeys::Escape, EKeys::Virtual_Gamepad_Back.GetVirtualKey()})
		{
			Buttons[0]->OnClicked.Broadcast();
			TestFalse(TEXT("Result accept preview never submits"), Access::Back(Widget, EKeys::Enter));
			TestTrue(TEXT("Result held back is consumed"), Access::Back(Widget, Key, true));
			TestTrue(TEXT("Result held back leaves confirmation open"), Access::ResultOpen(Widget));
			TestTrue(TEXT("Result fresh back is handled"), Access::Back(Widget, Key));
			TestFalse(TEXT("Result back closes confirmation"), Access::ResultOpen(Widget));
			TestFalse(TEXT("Result back cannot queue consent"), F.Flow->HasUnresolvedDungeonAbandonment());
			TestFalse(TEXT("Result inactive confirmation does not swallow back"), Access::Back(Widget, Key));
		}
		Buttons[0]->OnClicked.Broadcast();
		TestTrue(TEXT("First click opens confirmation without HTTP/travel"), Access::ResultOpen(Widget) && Access::ResultCanConfirm(Widget));
		Buttons[2]->OnClicked.Broadcast();
		TestFalse(TEXT("Cancel closes confirmation"), Access::ResultOpen(Widget));
		TestEqual(TEXT("Cancel cannot submit or invalidate HTTP"), Access::Generation(F.Web), Before);
		TestFalse(TEXT("Cancel cannot queue a mutation"), F.Flow->HasUnresolvedDungeonAbandonment());
		Buttons[0]->OnClicked.Broadcast(); ++F.Snapshot->Revision;
		Buttons[1]->OnClicked.Broadcast();
		TestFalse(TEXT("A newer encounter invalidates captured consent"), F.Flow->HasUnresolvedDungeonAbandonment());
		Buttons[0]->OnClicked.Broadcast(); Buttons[1]->OnClicked.Broadcast();
		TestTrue(TEXT("Confirmed native dispatcher queues production return route"), F.Flow->HasQueuedDungeonAbandonment());
		Buttons[1]->OnClicked.Broadcast(); Buttons[2]->OnClicked.Broadcast();
		TestTrue(TEXT("Repeated confirm/cancel cannot undo queued identity"), F.Flow->HasQueuedDungeonAbandonment());
	}
	for (auto* Button : Buttons) TestFalse(TEXT("Result teardown removes confirmation handlers"), Button->OnClicked.IsBound());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonGameplayAbandonmentQueueTest, "ProjectRPG.Online.DungeonAbandonmentGameplayQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonGameplayAbandonmentQueueTest::RunTest(const FString&)
{
	FResultAbandonmentWorld F;
	using Access = FRPGDungeonAbandonmentTestAccess;
	F.Snapshot->BattleState = EBossBattleState::InProgress;
	auto Queue = [&](int32 Revision = 11, const FString& Session = SessionId, const FString& Steam = TEXT("76561198000000001"))
	{
		return F.Flow->QueueDungeonAbandonmentFromGameplay(F.ResultController, Revision, Session, CharacterId, Steam);
	};
	TestFalse(TEXT("Defeat opt-in cannot enable combat abandonment"), Queue());
	Access::EnableGameplay(F.Flow);
	TestTrue(TEXT("Admitted opt-in remote combat client can capture consent"), F.Flow->CanQueueDungeonAbandonmentFromGameplay(F.ResultController));
	Access::SetCachedState(F.Flow, TEXT("Loading"));
	TestTrue(TEXT("A stale admission Loading cache does not hide consent during an authoritative running battle"), F.Flow->CanQueueDungeonAbandonmentFromGameplay(F.ResultController));
	Access::SetCachedState(F.Flow, TEXT("InProgress"));
	TestFalse(TEXT("Combat opt-in does not change defeat-only result guard"), F.Flow->CanQueueDungeonAbandonmentFromResult(F.ResultController));
	TestFalse(TEXT("Stale combat revision cannot queue"), Queue(10));
	TestFalse(TEXT("Foreign session cannot queue"), Queue(11, TEXT("00000000-0000-0000-0000-000000000003")));
	TestFalse(TEXT("Foreign account cannot queue"), Queue(11, SessionId, TEXT("76561198000000002")));
	F.Snapshot->BattleState = EBossBattleState::Victory;
	TestFalse(TEXT("Victory cannot queue a new opt-out"), Queue());
	F.Snapshot->BattleState = EBossBattleState::Defeated;
	TestFalse(TEXT("Defeat must use its separate result action"), Queue());
	F.Snapshot->BattleState = EBossBattleState::InProgress;
	Access::ChangeCachedState(F.Flow);
	TestFalse(TEXT("Captured settlement cannot be bypassed"), Queue());
	Access::Seed(F.Flow, F.Web, F.World); Access::EnableGameplay(F.Flow);
	Access::Disable(F.Flow);
	TestFalse(TEXT("Combat flag alone cannot bypass default/Shipping gate"), Queue());
	Access::Seed(F.Flow, F.Web, F.World); Access::EnableGameplay(F.Flow);
	F.DetachDriver();
	TestFalse(TEXT("Standalone/listen-local route is not a remote dungeon"), Queue());
	F.Driver->ServerConnection = NewObject<UIpConnection>(F.Driver);
	F.Driver->ServerConnection->SetConnectionState(USOCK_Closed);
	F.Driver->SetWorld(F.World); F.World->SetNetDriver(F.Driver);
	TestTrue(TEXT("Confirmed combat action queues only local recovery"), Queue());
	TestTrue(TEXT("Consent is pinned without a receipt or membership loss"), F.Flow->HasQueuedDungeonAbandonment() && F.Flow->HasActiveDungeonSession());
	TestFalse(TEXT("Second confirmation cannot queue again"), Queue());
	TestFalse(TEXT("Connected world still cannot send the lobby-only POST"), Access::Begin(F.Flow, F.ResultController));
	TestFalse(TEXT("Queued consent blocks reconnect"), F.Flow->ConnectToDungeon(F.ResultController, FString()));
	Access::CancelTravel(F.Flow); F.DetachDriver(); F.Web->ClearAccessToken(); Access::Arrive(F.Flow, F.World);
	TestTrue(TEXT("Expiry on lobby arrival keeps pinned consent for re-login"), F.Flow->HasQueuedDungeonAbandonment() && F.Flow->HasUnresolvedDungeonAbandonment());
	Access::Login(F.Web);
	TestTrue(TEXT("Same identity uses the existing lobby request flow"), Access::Begin(F.Flow, F.PC));
	Access::Receive(F.Flow, Receipt(), false, TEXT("dungeon_abandonment_unconfirmed"));
	TestTrue(TEXT("Uncertain POST keeps membership and original consent"), F.Flow->HasUnresolvedDungeonAbandonment() && F.Flow->HasActiveDungeonSession());
	TestTrue(TEXT("Only the same pending request is retried"), Access::Begin(F.Flow, F.PC));
	Access::Receive(F.Flow, Receipt());
	TestFalse(TEXT("Verified receipt alone releases old membership"), F.Flow->HasActiveDungeonSession());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonGameplayAbandonmentConfirmationTest, "ProjectRPG.Online.DungeonAbandonmentGameplayConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonGameplayAbandonmentConfirmationTest::RunTest(const FString&)
{
	FResultAbandonmentWorld F;
	using Access = FRPGDungeonAbandonmentTestAccess;
	F.Snapshot->BattleState = EBossBattleState::InProgress; Access::EnableGameplay(F.Flow);
	auto* Widget = CreateWidget<URPGDungeonResultActionsWidget>(F.ResultController, URPGDungeonResultActionsWidget::StaticClass());
	if (!TestNotNull(TEXT("Combat confirmation reuses native controls"), Widget)) return false;
	Widget->InitializeGameplayAbandonment();
	const auto Slate = Widget->TakeWidget();
	Access::BindGameplayWidget(Widget, F.Flow, F.Web);
	auto* Confirm = Cast<UButton>(Widget->GetWidgetFromName(TEXT("ConfirmResultAbandonmentButton")));
	auto* Cancel = Cast<UButton>(Widget->GetWidgetFromName(TEXT("CancelResultAbandonmentButton")));
	if (!TestNotNull(TEXT("Confirm control"), Confirm) || !TestNotNull(TEXT("Cancel control"), Cancel)) return false;
	const uint64 Before = Access::Generation(F.Web);
	TestTrue(TEXT("Opening combat confirmation sends no HTTP"), Widget->OpenGameplayAbandonmentConfirmation());
	CheckConfirmationNavigation(*this, Confirm, Cancel);
	for (const FName Name : {TEXT("RetryBattleButton"), TEXT("ReturnToLobbyButton"), TEXT("RefreshDungeonButton"), TEXT("DisconnectToLobbyButton"), TEXT("ResultLoginSteamButton")})
	{
		auto* Action = Cast<UButton>(Widget->GetWidgetFromName(Name));
		if (!TestNotNull(TEXT("Reused result action exists"), Action)) return false;
		TestEqual(TEXT("Combat hides non-consent result controls"), Action->GetVisibility(), ESlateVisibility::Collapsed);
		TestFalse(TEXT("Hidden actions are also disabled"), Action->GetIsEnabled());
		Action->OnClicked.Broadcast();
	}
	TestEqual(TEXT("Opening and background dispatch never touch HTTP generation"), Access::Generation(F.Web), Before);
	TestFalse(TEXT("Opening is not submission"), F.Flow->HasUnresolvedDungeonAbandonment());
	F.ResultController->SetIgnoreMoveInput(true); // An unrelated counted owner must survive our close.
	TestTrue(TEXT("Window input ownership is acquired"), Access::ClaimGameplayWindow(F.ResultController, Widget));
	F.ResultController->CloseDungeonAbandonmentConfirmation(nullptr);
	TestTrue(TEXT("Foreign close cannot release our window"), Access::HasGameplayWindow(F.ResultController));
	TestTrue(TEXT("Held back is consumed"), Access::Back(Widget, EKeys::Escape, true));
	TestTrue(TEXT("Held back cannot cancel fresh consent"), Access::ResultOpen(Widget));
	TestTrue(TEXT("Fresh back closes only our confirmation window"), Access::Back(Widget, EKeys::Escape));
	TestFalse(TEXT("Cancel releases the owned window"), Access::HasGameplayWindow(F.ResultController));
	TestTrue(TEXT("Cancel preserves unrelated move lock"), F.ResultController->IsMoveInputIgnored());
	TestFalse(TEXT("Our look lock is balanced"), F.ResultController->IsLookInputIgnored());
	F.ResultController->SetIgnoreMoveInput(false);
	TestFalse(TEXT("Cancel sends no request or consent"), F.Flow->HasUnresolvedDungeonAbandonment());
	TestEqual(TEXT("Cancel preserves HTTP generation"), Access::Generation(F.Web), Before);
	Widget->OpenGameplayAbandonmentConfirmation(); ++F.Snapshot->Revision; Confirm->OnClicked.Broadcast();
	TestFalse(TEXT("Changed revision invalidates final confirmation"), F.Flow->HasUnresolvedDungeonAbandonment());
	Widget->OpenGameplayAbandonmentConfirmation(); Confirm->OnClicked.Broadcast();
	TestTrue(TEXT("Native combat confirmation queues the guarded production route"), F.Flow->HasQueuedDungeonAbandonment());
	Cancel->OnClicked.Broadcast(); Confirm->OnClicked.Broadcast();
	TestTrue(TEXT("Late cancel or duplicate click cannot undo queued consent"), F.Flow->HasQueuedDungeonAbandonment());
	return true;
}
#endif
