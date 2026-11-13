#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonPlayerTransport.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

namespace RPGDungeonPlayerTransportTests
{
	class FResponse final : public IHttpResponse
	{
	public:
		FResponse(int32 InCode, FString InBody) : Code(InCode), Body(MoveTemp(InBody))
		{
			const FTCHARToUTF8 Utf8(*Body);
			Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
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
		FString GetContentAsString() const override { return Body; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	private:
		int32 Code;
		FString Body, Url = TEXT("http://player-dungeon-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	const FString Session = TEXT("10000000-0000-0000-0000-000000000001");
	const FString Character = TEXT("20000000-0000-0000-0000-000000000001");
	const FString Account = TEXT("76561198000000001");
	struct FHost final : IRPGDungeonPlayerTransportHost
	{
		FRPGDungeonPlayerTransportConfig Config{TEXT("http://player-dungeon-fixture.invalid/api"), Account, 7.f, 0, false};
		bool bHasToken = true, bAccept = true, bDispatch = true;
		int32 AcceptCalls = 0, TicketReplies = 0, AbandonReplies = 0;
		mutable int32 TicketHeaders = 0, AbandonHeaders = 0;
		TArray<FHttpRequestPtr> Requests;
		FString ReplySession, ReplyCharacter, Ticket, Expires, TicketError, AbandonError;
		bool bTicketSuccess = false, bAbandonSuccess = false;
		FRPGDungeonAbandonmentReceipt Receipt;
		TFunction<void()> OnAbandon;
		UObject& GetDungeonPlayerTransportOwner() const override { return *GetTransientPackage(); }
		FRPGDungeonPlayerTransportConfig GetDungeonPlayerTransportConfig() const override { return Config; }
		bool HasDungeonPlayerToken() const override { return bHasToken; }
		void ApplyJoinTicketHeaders(const FHttpRequestRef& Request) const override
		{ ++TicketHeaders; Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ticket-policy-fixture")); }
		void ApplyAbandonmentAuthorization(const FHttpRequestRef& Request) const override
		{ ++AbandonHeaders; Request->SetHeader(TEXT("Authorization"), TEXT("Bearer player-only-fixture")); }
		bool AcceptDungeonPlayerResponse(const FHttpRequestPtr&, const FHttpResponsePtr&, bool&) override
		{ ++AcceptCalls; return bAccept; }
		bool DispatchDungeonPlayerRequest(const FHttpRequestRef& Request) override { Requests.Add(Request); return bDispatch; }
		void BroadcastPlayerJoinTicketResult(const FString& InSession, const FString& InCharacter,
			const FString& InTicket, const FString& InExpires, bool bSuccess, const FString& Error) override
		{ ++TicketReplies; ReplySession = InSession; ReplyCharacter = InCharacter; Ticket = InTicket; Expires = InExpires; TicketError = Error; bTicketSuccess = bSuccess; }
		void BroadcastPlayerAbandonmentResult(const FRPGDungeonAbandonmentReceipt& InReceipt, bool bSuccess, const FString& Error) override
		{ ++AbandonReplies; Receipt = InReceipt; bAbandonSuccess = bSuccess; AbandonError = Error; if (OnAbandon) OnAbandon(); }
	};
	void Complete(const FHttpRequestPtr& Request, int32 Status, const FString& Body, bool bTransport = true)
	{ Request->OnProcessRequestComplete().ExecuteIfBound(Request, MakeShared<FResponse, ESPMode::ThreadSafe>(Status, Body), bTransport); }
	FString TicketBody(const FString& Id = Character)
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"joinTicket\":\"ticket-fixture\",\"expiresAt\":\"expiry-fixture\"}"), *Session, *Id);
	}
	FString ReceiptBody(const FString& Id = Character)
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"characterId\":\"%s\",\"abandonedAt\":\"2026-01-01T00:00:00Z\",\"sessionStateAtAbandonment\":\"InProgress\",\"rewardExcluded\":true}"), *Session, *Id);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerTicketWireTest, "ProjectRPG.Online.DungeonPlayerTransport.TicketWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerTicketWireTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	Service->RequestJoinTicket(Character, Session); Service->RequestJoinTicket(Character, Session);
	if (!TestEqual(TEXT("Independent ticket requests remain parallel"), Host.Requests.Num(), 2)) return false;
	const auto Request = Host.Requests[0];
	TestEqual(TEXT("Ticket route unchanged"), Request->GetURL(), Host.Config.ApiUrl / TEXT("join-tickets"));
	TestEqual(TEXT("Ticket verb unchanged"), Request->GetVerb(), FString(TEXT("POST")));
	TestEqual(TEXT("Ticket header policy retained"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer ticket-policy-fixture")));
	TestEqual(TEXT("Ticket timeout retained"), Request->GetTimeout().GetValue(), 7.f);
	const auto& Bytes = Request->GetContent();
	const FUTF8ToTCHAR Text(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Json) || !Json) return false;
	TestEqual(TEXT("Ticket body character preserved"), Json->GetStringField(TEXT("characterId")), Character);
	TestEqual(TEXT("Ticket body session preserved"), Json->GetStringField(TEXT("dungeonSessionId")), Session);
	Complete(Request, 200, TicketBody());
	TestTrue(TEXT("Matched ticket accepted"), Host.bTicketSuccess);
	TestEqual(TEXT("Original ticket value delivered"), Host.Ticket, FString(TEXT("ticket-fixture")));
	TestEqual(TEXT("Original expiry field delivered"), Host.Expires, FString(TEXT("expiry-fixture")));
	Host.bHasToken = false; Service->RequestJoinTicket(Character, Session);
	TestEqual(TEXT("No credential prevents dispatch"), Host.Requests.Num(), 2);
	TestFalse(TEXT("Rejected request completes as failure"), Host.bTicketSuccess);
	TestTrue(TEXT("Rejected request cannot retain an old ticket"), Host.Ticket.IsEmpty() && Host.Expires.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerDenialTest, "ProjectRPG.Online.DungeonPlayerTransport.TicketDenials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerDenialTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	Service->RequestJoinTicket(Character, Session);
	if (Host.Requests.IsEmpty()) return false;
	const auto Request = Host.Requests.Last();
	AddExpectedError(TEXT("Join ticket request failed"), EAutomationExpectedErrorFlags::Contains, 8);
	for (const TCHAR* Code : {TEXT("dungeon_reconnect_grace_expired"), TEXT("dungeon_member_not_active")})
	{
		Complete(Request, 409, FString::Printf(TEXT("{\"error\":\"%s\"}"), Code));
		TestEqual(TEXT("Only defined conflicts reach UI"), Host.TicketError, FString(Code));
	}
	const FString Known = TEXT("{\"error\":\"dungeon_reconnect_grace_expired\"}");
	for (const FString& Body : {FString(TEXT("{\"error\":\"DUNGEON_RECONNECT_GRACE_EXPIRED\"}")),
		FString(TEXT("{\"error\":\"arbitrary backend text\"}")), Known + FString::ChrN(4096, TEXT(' '))})
	{
		Complete(Request, 409, Body);
		TestTrue(TEXT("Case changes, arbitrary text and oversized errors are not authority"), Host.TicketError.IsEmpty());
	}
	Complete(Request, 500, Known); TestTrue(TEXT("Server error cannot forge a defined conflict"), Host.TicketError.IsEmpty());
	Complete(Request, 200, Known); TestTrue(TEXT("Malformed successful body cannot forge a conflict"), Host.TicketError.IsEmpty());
	Complete(Request, 409, Known, false); TestTrue(TEXT("Transport failure cannot forge a conflict"), Host.TicketError.IsEmpty());
	TestTrue(TEXT("All failures clear ticket and expiry"), Host.Ticket.IsEmpty() && Host.Expires.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerScopeTest, "ProjectRPG.Online.DungeonPlayerTransport.Scope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerScopeTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	Service->RequestJoinTicket(Character, Session);
	if (Host.Requests.IsEmpty()) return false;
	const auto Ticket = Host.Requests.Last();
	const auto Abandon = Service->CreateAbandonRequest(Session, Character, Account);
	++Host.Config.PlayerGeneration;
	Complete(Ticket, 401, TEXT("{}")); Complete(Abandon, 401, TEXT("{}"));
	TestEqual(TEXT("Old generation rejected before 401/credential handling"), Host.AcceptCalls, 0);
	TestEqual(TEXT("Old ticket callback ignored"), Host.TicketReplies, 0);
	TestEqual(TEXT("Old abandonment callback ignored"), Host.AbandonReplies, 0);
	const auto Current = Service->CreateAbandonRequest(Session, Character, Account);
	Host.Config.SteamId = TEXT("76561198000000002");
	Complete(Current, 200, ReceiptBody());
	TestEqual(TEXT("Different account rejected before credential handling"), Host.AcceptCalls, 0);
	TestEqual(TEXT("Changed account signals context failure"), Host.AbandonError, FString(TEXT("dungeon_abandonment_authentication_changed")));
	Host.Config.SteamId = Account; Host.Config.bServerRuntime = true;
	Complete(Current, 200, ReceiptBody());
	TestEqual(TEXT("Server cannot consume a player abandonment callback"), Host.AbandonReplies, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerAbandonWireTest, "ProjectRPG.Online.DungeonPlayerTransport.AbandonWire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerAbandonWireTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	Service->Abandon(Session, Character, Account);
	if (!TestEqual(TEXT("Valid player abandonment dispatched"), Host.Requests.Num(), 1)) return false;
	const auto Request = Host.Requests.Last();
	TestTrue(TEXT("Original abandonment route retained"), Request->GetURL().EndsWith(TEXT("/members/") + Character + TEXT("/abandon")));
	TestEqual(TEXT("Abandonment stays bodyless"), Request->GetContent().Num(), 0);
	TestEqual(TEXT("Player-only credential policy used"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer player-only-fixture")));
	TestEqual(TEXT("Common ticket header policy never called"), Host.TicketHeaders, 0);
	Host.Config.bServerRuntime = true; Service->Abandon(Session, Character, Account);
	Host.Config.bServerRuntime = false; Host.bHasToken = false; Service->Abandon(Session, Character, Account);
	Host.bHasToken = true; Service->Abandon(Session, Character, TEXT("76561198000000002"));
	Service->Abandon(Session, TEXT("00000000-0000-0000-0000-000000000000"), Account);
	TestEqual(TEXT("Server, missing credential, account mismatch and invalid GUID never dispatch"), Host.Requests.Num(), 1);
	TestEqual(TEXT("Rejected attempts preserve their failure category"), Host.AbandonError, FString(TEXT("dungeon_abandonment_authentication_required")));
	Host.bDispatch = false; Service->Abandon(Session, Character, Account);
	TestFalse(TEXT("Synchronous dispatch failure is not success"), Host.bAbandonSuccess);
	TestEqual(TEXT("Dispatch failure stays uncertain"), Host.AbandonError, FString(TEXT("dungeon_abandonment_unconfirmed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerReceiptTest, "ProjectRPG.Online.DungeonPlayerTransport.Receipts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerReceiptTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	const auto Request = Service->CreateAbandonRequest(Session, Character, Account);
	Complete(Request, 200, ReceiptBody());
	TestTrue(TEXT("Real existing receipt codec accepts exact acknowledgement"), Host.bAbandonSuccess);
	TestTrue(TEXT("Receipt remains scoped to original session/character"), Host.Receipt.IsValidFor(Session, Character));
	TestTrue(TEXT("Confirmed receipt has no error"), Host.AbandonError.IsEmpty());
	Complete(Request, 200, ReceiptBody(TEXT("20000000-0000-0000-0000-000000000002")));
	TestFalse(TEXT("Other character receipt rejected"), Host.bAbandonSuccess);
	TestFalse(TEXT("Rejected receipt cannot carry exclusion proof"), Host.Receipt.bRewardExcluded);
	TestEqual(TEXT("Failure retains original request identity"), Host.Receipt.CharacterId, Character);
	Host.bAccept = false; Complete(Request, 200, ReceiptBody());
	TestEqual(TEXT("Old credential cannot finalize a current-account operation"), Host.AbandonError, FString(TEXT("dungeon_abandonment_authentication_changed")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerUncertaintyTest, "ProjectRPG.Online.DungeonPlayerTransport.Uncertainty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerUncertaintyTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	const auto Request = Service->CreateAbandonRequest(Session, Character, Account);
	const FString Known = TEXT("{\"error\":\"dungeon_abandonment_not_available\"}");
	Complete(Request, 409, Known);
	TestEqual(TEXT("Only exact current authenticated conflict is definitive"), Host.AbandonError, FString(TEXT("dungeon_abandonment_not_available")));
	for (int32 Code : {200, 401, 403, 404, 500})
	{
		Complete(Request, Code, Known);
		TestEqual(TEXT("Other HTTP status cannot close the uncertain operation"), Host.AbandonError, FString(TEXT("dungeon_abandonment_unconfirmed")));
	}
	Complete(Request, 409, Known, false);
	TestEqual(TEXT("Transport failure cannot impersonate definitive rejection"), Host.AbandonError, FString(TEXT("dungeon_abandonment_unconfirmed")));
	TestFalse(TEXT("Unconfirmed operation is never marked successful"), Host.bAbandonSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDungeonPlayerLifetimeTest, "ProjectRPG.Online.DungeonPlayerTransport.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGDungeonPlayerLifetimeTest::RunTest(const FString&)
{
	using namespace RPGDungeonPlayerTransportTests;
	FHost Host;
	TSharedPtr<FRPGDungeonPlayerTransport> Service = MakeShared<FRPGDungeonPlayerTransport>(Host);
	Service->Abandon(Session, Character, Account);
	if (Host.Requests.IsEmpty()) return false;
	const auto Request = Host.Requests.Last();
	Host.OnAbandon = [&] { Service->RequestJoinTicket(Character, Session); };
	Complete(Request, 200, ReceiptBody());
	Host.OnAbandon = nullptr;
	TestEqual(TEXT("Result notification permits existing reentrant requests"), Host.Requests.Num(), 2);
	const auto TicketRequest = Host.Requests.Last();
	const TWeakPtr<FRPGDungeonPlayerTransport> Weak = Service;
	Service.Reset();
	TestFalse(TEXT("No request cycle retains transport"), Weak.IsValid());
	Complete(TicketRequest, 200, TicketBody()); Complete(Request, 200, ReceiptBody());
	TestEqual(TEXT("Dead transport cannot deliver ticket"), Host.TicketReplies, 0);
	TestEqual(TEXT("Dead transport cannot deliver another receipt"), Host.AbandonReplies, 1);
	return true;
}
#endif
