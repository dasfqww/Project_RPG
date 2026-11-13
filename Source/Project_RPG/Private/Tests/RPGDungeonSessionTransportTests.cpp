#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonSessionTransport.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace RPGSessionTransportTests
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
		FString Body, Url = TEXT("http://session-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	struct FHost final : IRPGDungeonSessionTransportHost
	{
		FRPGDungeonSessionTransportConfig Config{TEXT("http://session-fixture.invalid/api"), TEXT("server-fixture"),
			TEXT("10000000-0000-0000-0000-000000000001"), 7.f, false, false, 0};
		bool bHasToken = true, bAccept = true, bForceFailure = false, bSucceeded = false;
		int32 Replies = 0, AcceptCalls = 0;
		FRPGDungeonSession Session;
		TArray<FHttpRequestPtr> Requests;
		TFunction<void()> OnReply;
		UObject& GetSessionTransportOwner() const override { return *GetTransientPackage(); }
		FRPGDungeonSessionTransportConfig GetSessionTransportConfig() const override { return Config; }
		bool HasSessionAccessToken() const override { return bHasToken; }
		void ApplySessionTransportHeaders(const FHttpRequestRef& Request) const override
		{
			Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer session-fixture"));
		}
		bool AcceptSessionPlayerResponse(const FHttpRequestPtr&, const FHttpResponsePtr&, bool& bSuccess) override
		{ ++AcceptCalls; if (bForceFailure) bSuccess = false; return bAccept; }
		void DispatchSessionTransportRequest(const FHttpRequestRef& Request) override { Requests.Add(Request); }
		void BroadcastSessionTransportResult(const FRPGDungeonSession& InSession, bool bSuccess) override
		{ ++Replies; Session = InSession; bSucceeded = bSuccess; if (OnReply) OnReply(); }
	};
	FString SessionBody(const FString& SessionId = TEXT("10000000-0000-0000-0000-000000000001"))
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"dungeonId\":\"dungeon\",\"difficulty\":\"Normal\",\"state\":\"Waiting\",\"expiresAt\":\"expiry\",\"members\":[{\"characterId\":\"member\",\"joinedAt\":\"joined\",\"leaseExpiresAt\":\"lease\"}]}"), *SessionId);
	}
	void Complete(const FHttpRequestPtr& Request, int32 Code, const FString& Body, bool bTransport = true)
	{
		Request->OnProcessRequestComplete().ExecuteIfBound(Request,
			MakeShared<FResponse, ESPMode::ThreadSafe>(Code, Body), bTransport);
	}
	TSharedPtr<FJsonObject> RequestBody(const FHttpRequestPtr& Request)
	{
		const auto& Bytes = Request->GetContent();
		const FUTF8ToTCHAR Text(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
		TSharedPtr<FJsonObject> Result;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Result);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportWireTest, "ProjectRPG.Online.SessionTransport.Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportWireTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->Create(TEXT("character"), TEXT(" dungeon "), TEXT(" Normal "));
	if (!TestEqual(TEXT("Create dispatches one real request through host"), Host.Requests.Num(), 1)) return false;
	auto Request = Host.Requests.Last();
	TestEqual(TEXT("Create route"), Request->GetURL(), Host.Config.ApiUrl / TEXT("dungeon-sessions"));
	TestEqual(TEXT("Create verb"), Request->GetVerb(), FString(TEXT("POST")));
	const auto Body = RequestBody(Request);
	if (!TestTrue(TEXT("Create JSON body"), Body.IsValid())) return false;
	TestEqual(TEXT("Dungeon trimming unchanged"), Body->GetStringField(TEXT("dungeonId")), FString(TEXT("dungeon")));
	TestEqual(TEXT("Difficulty trimming unchanged"), Body->GetStringField(TEXT("difficulty")), FString(TEXT("Normal")));
	Service->Join(TEXT("session/part"), TEXT("character"));
	TestEqual(TEXT("Join encodes session path"), Host.Requests.Last()->GetURL(), Host.Config.ApiUrl / TEXT("dungeon-sessions/session%2Fpart/members"));
	Service->Load(TEXT("session"));
	TestEqual(TEXT("Read verb"), Host.Requests.Last()->GetVerb(), FString(TEXT("GET")));
	TestEqual(TEXT("Read has no synthetic body"), Host.Requests.Last()->GetContent().Num(), 0);
	Service->LoadActive(TEXT(" character/part "));
	TestEqual(TEXT("Resume trims and encodes character path"), Host.Requests.Last()->GetURL(), Host.Config.ApiUrl / TEXT("characters/character%2Fpart/active-dungeon-session"));
	Service->Leave(TEXT("session"), TEXT("character/part"));
	TestEqual(TEXT("Leave verb"), Host.Requests.Last()->GetVerb(), FString(TEXT("DELETE")));
	TestTrue(TEXT("Leave encodes character path"), Host.Requests.Last()->GetURL().EndsWith(TEXT("/members/character%2Fpart")));
	Service->SetDeparture(TEXT("session"), TEXT("character"), TEXT("request"), 17, true);
	Request = Host.Requests.Last();
	TestTrue(TEXT("Cancel departure route preserved"), Request->GetURL().EndsWith(TEXT("/departure/cancel")));
	TestEqual(TEXT("Departure expected revision preserved"), RequestBody(Request)->GetNumberField(TEXT("expectedLobbyRevision")), 17.0);
	TestEqual(TEXT("Header policy supplied by host"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer session-fixture")));
	TestEqual(TEXT("Request timeout preserved"), Request->GetTimeout().GetValue(), 7.f);
	TestEqual(TEXT("Independent operations remain concurrent"), Host.Requests.Num(), 6);
	Host.bHasToken = false;
	Service->Create(TEXT("character"), TEXT("dungeon"), TEXT("Normal"));
	Service->Join(TEXT("session"), TEXT("character")); Service->Load(TEXT("session"));
	Service->LoadActive(TEXT("character")); Service->Leave(TEXT("session"), TEXT("character"));
	TestEqual(TEXT("Missing player credential prevents all five guarded operations"), Host.Requests.Num(), 6);
	TestEqual(TEXT("Each rejected operation completes existing failure contract"), Host.Replies, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportServerTest, "ProjectRPG.Online.SessionTransport.Server",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportServerTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->SendConfigured(TEXT("start"));
	Host.Config.bServerRuntime = true; Service->SendConfigured(TEXT("heartbeat"));
	TestEqual(TEXT("Client/unassigned server never dispatch server lifecycle writes"), Host.Requests.Num(), 0);
	Host.Config.bServerConfigured = true; Host.bHasToken = false;
	Service->SendConfigured(TEXT("start")); Service->SendConfigured(TEXT("heartbeat"));
	Service->SendConfigured(TEXT("finish"), TEXT("Cleared"));
	TestEqual(TEXT("Assigned server does not require a player token"), Host.Requests.Num(), 3);
	const auto Body = RequestBody(Host.Requests.Last());
	if (!Body) return false;
	TestEqual(TEXT("Authoritative server id preserved"), Body->GetStringField(TEXT("serverId")), Host.Config.ServerId);
	TestEqual(TEXT("Authoritative finish outcome preserved"), Body->GetStringField(TEXT("outcome")), FString(TEXT("Cleared")));
	TestFalse(TEXT("Heartbeat does not invent finish outcome"), RequestBody(Host.Requests[1])->HasField(TEXT("outcome")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportGenerationTest, "ProjectRPG.Online.SessionTransport.Generation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportGenerationTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->Load(Host.Config.SessionId);
	const auto Old = Host.Requests.Last(); ++Host.Config.PlayerGeneration;
	Complete(Old, 401, TEXT("{}"));
	TestEqual(TEXT("Old generation discarded before credential/401 handling"), Host.AcceptCalls, 0);
	TestEqual(TEXT("Old generation cannot notify"), Host.Replies, 0);
	Service->Load(Host.Config.SessionId);
	Host.bAccept = false;
	Complete(Host.Requests.Last(), 200, SessionBody());
	TestEqual(TEXT("Rejected auth correlation cannot notify"), Host.Replies, 0);
	Host.bAccept = true;
	Complete(Host.Requests.Last(), 200, SessionBody());
	TestTrue(TEXT("Current generation and auth context decode normally"), Host.bSucceeded);
	Host.Config.bServerRuntime = true;
	Complete(Old, 200, SessionBody());
	TestEqual(TEXT("Server responses retain independence from player generation"), Host.Replies, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportProtocolTest, "ProjectRPG.Online.SessionTransport.Protocol",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportProtocolTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->LoadActive(TEXT("character")); Complete(Host.Requests.Last(), 204, FString());
	TestTrue(TEXT("Only resume accepts no active session"), Host.bSucceeded && Host.Session.DungeonSessionId.IsEmpty());
	AddExpectedError(TEXT("Dungeon session"), EAutomationExpectedErrorFlags::Contains, 6);
	Service->Load(Host.Config.SessionId); Complete(Host.Requests.Last(), 204, FString());
	TestFalse(TEXT("A missing normal session is not resume success"), Host.bSucceeded);
	const TArray<FString> Invalid{
		TEXT("invalid json"), SessionBody(TEXT("other-session")),
		SessionBody().Replace(TEXT("\"state\":\"Waiting\""), TEXT("\"state\":\"Waiting\",\"lobbyRevision\":1.5")),
		SessionBody().Replace(TEXT("\"state\":\"Waiting\""), TEXT("\"state\":\"Waiting\",\"lobbyRevision\":9007199254740992")),
		SessionBody().Replace(TEXT("\"joinedAt\":\"joined\","), TEXT(""))};
	for (const auto& Body : Invalid)
	{
		Service->Load(Host.Config.SessionId); Complete(Host.Requests.Last(), 200, Body);
		TestFalse(TEXT("Malformed/mismatched/nonintegral/oversized response rejected"), Host.bSucceeded);
		TestTrue(TEXT("Failure never publishes a partial session"), Host.Session.DungeonSessionId.IsEmpty() && Host.Session.Members.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportLegacyTest, "ProjectRPG.Online.SessionTransport.Legacy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportLegacyTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->Load(Host.Config.SessionId);
	Complete(Host.Requests.Last(), 200, SessionBody().Replace(TEXT("\"joinedAt\":"), TEXT("\"characterName\":17,\"joinedAt\":")));
	if (!TestTrue(TEXT("Legacy metadata remains readable"), Host.bSucceeded && Host.Session.Members.Num() == 1)) return false;
	TestTrue(TEXT("Invalid optional name never becomes identity"), Host.Session.Members[0].CharacterName.IsEmpty());
	TestTrue(TEXT("Missing leader metadata never grants authority"), Host.Session.LeaderCharacterId.IsEmpty());
	TestEqual(TEXT("Missing revision remains read-only legacy zero"), Host.Session.LobbyRevision, int64(0));
	const FString WithMetadata = SessionBody().Replace(TEXT("\"state\":\"Waiting\""), TEXT("\"state\":\"Waiting\",\"lobbyRevision\":9007199254740991"));
	Complete(Host.Requests.Last(), 200, WithMetadata);
	TestTrue(TEXT("Largest existing safe integer accepted"), Host.bSucceeded);
	TestEqual(TEXT("Revision precision preserved"), Host.Session.LobbyRevision, int64(9007199254740991LL));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportLifetimeTest, "ProjectRPG.Online.SessionTransport.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportLifetimeTest::RunTest(const FString&)
{
	using namespace RPGSessionTransportTests;
	FHost Host;
	TSharedPtr<FRPGDungeonSessionTransport> Service = MakeShared<FRPGDungeonSessionTransport>(Host);
	Service->Load(Host.Config.SessionId);
	Host.OnReply = [&] { Service->LoadActive(TEXT("character")); };
	Complete(Host.Requests.Last(), 200, SessionBody());
	Host.OnReply = nullptr;
	TestEqual(TEXT("Result notification permits independent reentrant reads"), Host.Requests.Num(), 2);
	const TWeakPtr<FRPGDungeonSessionTransport> Weak = Service;
	Service.Reset();
	TestFalse(TEXT("Requests do not retain transport in a cycle"), Weak.IsValid());
	Complete(Host.Requests.Last(), 204, FString());
	TestEqual(TEXT("Dead service callback cannot notify host"), Host.Replies, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSessionTransportTypesTest, "ProjectRPG.Online.SessionTransport.Reflection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSessionTransportTypesTest::RunTest(const FString&)
{
	const auto* Session = FRPGDungeonSession::StaticStruct();
	const auto* Member = FRPGDungeonSessionMember::StaticStruct();
	TestEqual(TEXT("Session serialized identity unchanged"), Session->GetPathName(), FString(TEXT("/Script/Project_RPG.RPGDungeonSession")));
	TestEqual(TEXT("Member serialized identity unchanged"), Member->GetPathName(), FString(TEXT("/Script/Project_RPG.RPGDungeonSessionMember")));
	for (const TCHAR* Name : {TEXT("DungeonSessionId"), TEXT("DungeonId"), TEXT("Difficulty"), TEXT("State"),
		TEXT("ServerId"), TEXT("ServerAddress"), TEXT("LeaderCharacterId"), TEXT("LobbyRevision"), TEXT("DepartureRequestId"),
		TEXT("DepartureRequestRevision"), TEXT("DepartureRequestedAt"), TEXT("ExpiresAt"), TEXT("Members")})
	{
		const auto* Property = Session->FindPropertyByName(Name);
		if (!TestNotNull(Name, Property)) return false;
		TestTrue(TEXT("Blueprint read-only visibility preserved"), Property->HasAllPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly));
	}
	for (const TCHAR* Name : {TEXT("CharacterId"), TEXT("JoinedAt"), TEXT("LeaseExpiresAt"), TEXT("CharacterName")})
		TestNotNull(Name, Member->FindPropertyByName(Name));
	return true;
}
#endif
