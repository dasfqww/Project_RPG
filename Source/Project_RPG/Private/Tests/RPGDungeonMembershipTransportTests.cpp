#if WITH_DEV_AUTOMATION_TESTS
#include "Online/RPGDungeonMembershipTransport.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include <limits>

namespace RPGMembershipTransportTests
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
		uint64 GetContentLength() const override { return ReportedBytes > 0 ? ReportedBytes : Bytes.Num(); }
		const TArray<uint8>& GetContent() const override { return Bytes; }
		TArray<uint8> TakeContent() override { return MoveTemp(Bytes); }
		int32 GetResponseCode() const override { return Code; }
		FString GetContentAsString() const override { return Body; }
		FUtf8StringView GetContentAsUtf8StringView() const override
		{ return FUtf8StringView(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num()); }
	public:
		uint64 ReportedBytes = 0;
	private:
		int32 Code;
		FString Body, Url = TEXT("http://membership-fixture.invalid/api");
		TArray<uint8> Bytes;
	};
	struct FHost final : IRPGDungeonMembershipTransportHost
	{
		FRPGDungeonMembershipTransportConfig Config{TEXT("http://membership-fixture.invalid/api"),
			TEXT("10000000-0000-0000-0000-000000000001"), TEXT("server-fixture"), 7.f, true, true};
		bool bValidCredential = true;
		mutable int32 AuthorizationCalls = 0;
		UObject& GetMembershipTransportOwner() const override { return *GetTransientPackage(); }
		FRPGDungeonMembershipTransportConfig GetMembershipTransportConfig() const override { return Config; }
		bool HasValidMembershipCredential() const override { return bValidCredential; }
		void ApplyMembershipAuthorization(const FHttpRequestRef& Request) const override
		{ ++AuthorizationCalls; Request->SetHeader(TEXT("Authorization"), TEXT("Bearer membership-server-fixture")); }
	};
	FString Body(const FHost& Host)
	{
		return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"serverId\":\"%s\",\"state\":\"InProgress\",\"updatedAt\":\"2026-01-01T00:00:00Z\",\"expiresAt\":\"2026-01-01T01:00:00Z\",\"members\":[],\"abandonments\":[]}"),
			*Host.Config.SessionId, *Host.Config.ServerId);
	}
	void Complete(const FHttpRequestPtr& Request, int32 Code, const FString& Json, bool bTransport = true)
	{
		Request->OnProcessRequestComplete().ExecuteIfBound(Request,
			MakeShared<FResponse, ESPMode::ThreadSafe>(Code, Json), bTransport);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportWireTest, "ProjectRPG.Online.MembershipTransport.Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportWireTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	FRPGDungeonMembershipSnapshotResult Result;
	const auto Request = Service->CreateSnapshotRequest([&](auto Value) { Result = MoveTemp(Value); });
	if (!TestTrue(TEXT("Assigned server prepares a real request without sending"), Request.IsValid())) return false;
	TestEqual(TEXT("Membership route unchanged"), Request->GetURL(), Host.Config.ApiUrl / (TEXT("dungeon-sessions/") + Host.Config.SessionId + TEXT("/membership-snapshot")));
	TestEqual(TEXT("Read remains GET"), Request->GetVerb(), FString(TEXT("GET")));
	TestEqual(TEXT("Only server credential policy is used"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer membership-server-fixture")));
	TestEqual(TEXT("Cache bypass retained"), Request->GetHeader(TEXT("Cache-Control")), FString(TEXT("no-cache")));
	TestEqual(TEXT("Original finite timeout retained"), Request->GetTimeout().GetValue(), 7.f);
	Complete(Request, 200, Body(Host));
	TestTrue(TEXT("Real snapshot codec validates response"), Result.bSuccess);
	TestFalse(TEXT("Empty membership is not opt-out evidence"), Result.Snapshot.IsExplicitlyAbandoned(
		Host.Config.SessionId, Host.Config.ServerId, TEXT("20000000-0000-0000-0000-000000000001"), TEXT("76561198000000001")));
	Host.Config.TimeoutSeconds = std::numeric_limits<float>::quiet_NaN();
	const auto Expire = Service->CreateExpirationRequest([] {});
	if (!Expire) return false;
	TestEqual(TEXT("Expiry verb retained"), Expire->GetVerb(), FString(TEXT("POST")));
	TestTrue(TEXT("Expiry route retained"), Expire->GetURL().EndsWith(TEXT("/disconnection-expirations")));
	TestEqual(TEXT("Nonfinite timeout falls back to ten seconds"), Expire->GetTimeout().GetValue(), 10.f);
	Service->Cancel(); Host.Config.TimeoutSeconds = .5f;
	const auto Clamped = Service->CreateSnapshotRequest({});
	if (!Clamped) return false;
	TestEqual(TEXT("Small timeout clamps to one second"), Clamped->GetTimeout().GetValue(), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportOrderingTest, "ProjectRPG.Online.MembershipTransport.Ordering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportOrderingTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	int32 Reads = 0, Posts = 0;
	const auto Old = Service->CreateSnapshotRequest([&](auto) { ++Reads; });
	if (!Old) return false;
	TestFalse(TEXT("GET and POST share a single active slot"), Service->CreateExpirationRequest([] {}).IsValid());
	auto Late = Old->OnProcessRequestComplete();
	Service->Cancel();
	FHttpRequestPtr Reentrant;
	const auto Current = Service->CreateExpirationRequest([&]
	{
		++Posts;
		Reentrant = Service->CreateSnapshotRequest([&](auto) { ++Reads; });
	});
	if (!Current) return false;
	Late.ExecuteIfBound(Old, MakeShared<FResponse, ESPMode::ThreadSafe>(200, Body(Host)), true);
	TestEqual(TEXT("Canceled generation cannot complete a read"), Reads, 0);
	TestTrue(TEXT("Canceled callback cannot clear current POST"), Service->IsActive());
	Complete(Current, 403, TEXT("{}"));
	if (!TestTrue(TEXT("Slot released before callback starts the required follow-up GET"), Reentrant.IsValid())) return false;
	Complete(Current, 200, TEXT("{}"));
	TestEqual(TEXT("POST completion consumed only once"), Posts, 1);
	TestTrue(TEXT("Duplicate POST cannot clear reentrant GET"), Service->IsActive());
	Complete(Reentrant, 200, Body(Host));
	TestEqual(TEXT("Current read completes normally"), Reads, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportAssignmentTest, "ProjectRPG.Online.MembershipTransport.Assignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportAssignmentTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	int32 Reads = 0, Posts = 0;
	FRPGDungeonMembershipSnapshotResult Result;
	const auto Request = Service->CreateSnapshotRequest([&](auto Value) { ++Reads; Result = MoveTemp(Value); });
	if (!Request) return false;
	const FString OldBody = Body(Host);
	Host.Config.SessionId = TEXT("10000000-0000-0000-0000-000000000002");
	Complete(Request, 200, OldBody);
	TestEqual(TEXT("Changed assignment completes GET as failure instead of leaving caller busy"), Reads, 1);
	TestFalse(TEXT("Old assignment cannot publish successful membership"), Result.bSuccess);
	TestEqual(TEXT("Observed HTTP status retained on scope failure"), Result.HttpStatus, 200);
	const auto Post = Service->CreateExpirationRequest([&] { ++Posts; });
	if (!Post) return false;
	Host.Config.ServerId = TEXT("replacement-server");
	Complete(Post, 204, FString());
	TestEqual(TEXT("Old POST cannot schedule a read for another assignment"), Posts, 0);
	TestFalse(TEXT("Stale operation releases its own slot"), Service->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportBoundsTest, "ProjectRPG.Online.MembershipTransport.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportBoundsTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	FRPGDungeonMembershipSnapshotResult Result;
	auto Request = Service->CreateSnapshotRequest([&](auto Value) { Result = MoveTemp(Value); });
	if (!Request) return false;
	auto Response = MakeShared<FResponse, ESPMode::ThreadSafe>(200, Body(Host));
	Response->ReportedBytes = 65537;
	Request->OnProcessRequestComplete().ExecuteIfBound(Request, Response, true);
	TestFalse(TEXT("Advertised size over 64KiB rejected"), Result.bSuccess);
	FString UnicodeBody = Body(Host); UnicodeBody.RemoveFromEnd(TEXT("}"));
	UnicodeBody += TEXT(",\"padding\":\"") + FString::ChrN(30000, TCHAR(0xac00)) + TEXT("\"}");
	FRPGDungeonMembershipSnapshot Decoded;
	TestTrue(TEXT("Fixture is valid JSON within the existing character limit"), FRPGDungeonMembershipSnapshotCodec::Decode(
		UnicodeBody, 200, Host.Config.SessionId, Host.Config.ServerId, Decoded));
	Request = Service->CreateSnapshotRequest([&](auto Value) { Result = MoveTemp(Value); });
	if (!Request) return false;
	Response = MakeShared<FResponse, ESPMode::ThreadSafe>(200, UnicodeBody); Response->ReportedBytes = 1;
	Request->OnProcessRequestComplete().ExecuteIfBound(Request, Response, true);
	TestFalse(TEXT("Actual UTF8 bytes over 64KiB rejected despite small advertised size"), Result.bSuccess);
	Request = Service->CreateSnapshotRequest([&](auto Value) { Result = MoveTemp(Value); });
	if (!Request) return false;
	Complete(Request, 500, Body(Host));
	TestFalse(TEXT("HTTP failure is never revocation evidence"), Result.bSuccess);
	TestEqual(TEXT("HTTP error code retained"), Result.HttpStatus, 500);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportExpiryTest, "ProjectRPG.Online.MembershipTransport.ExpiryOutcomes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportExpiryTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	int32 Followups = 0;
	for (int32 Code : {200, 204, 401, 403, 500})
	{
		const auto Request = Service->CreateExpirationRequest([&] { ++Followups; });
		if (!Request) return false;
		Complete(Request, Code, TEXT("{}"));
		TestFalse(TEXT("POST releases shared slot for the required GET"), Service->IsActive());
	}
	const auto FailedTransport = Service->CreateExpirationRequest([&] { ++Followups; });
	if (!FailedTransport) return false;
	Complete(FailedTransport, 0, FString(), false);
	TestEqual(TEXT("Every current POST outcome still requests authoritative follow-up instead of asserting ejection"), Followups, 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportLifetimeTest, "ProjectRPG.Online.MembershipTransport.ShutdownLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportLifetimeTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	TSharedPtr<FRPGDungeonMembershipTransport> Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	int32 Replies = 0;
	const auto Request = Service->CreateSnapshotRequest([&](auto) { ++Replies; });
	if (!Request) return false;
	auto Late = Request->OnProcessRequestComplete();
	Service->BeginShutdown(); Complete(Request, 200, Body(Host));
	TestEqual(TEXT("Shutdown suppresses callbacks before other services are torn down"), Replies, 0);
	TestTrue(TEXT("Beginning shutdown does not reorder the separate cancellation step"), Service->IsActive());
	Service->Cancel();
	TestFalse(TEXT("Shutdown blocks new requests"), Service->CreateExpirationRequest([] {}).IsValid());
	Service->Initialize();
	const auto Current = Service->CreateSnapshotRequest([&](auto) { ++Replies; });
	if (!Current) return false;
	Late.ExecuteIfBound(Request, MakeShared<FResponse, ESPMode::ThreadSafe>(200, Body(Host)), true);
	TestTrue(TEXT("Prior lifecycle callback cannot clear a new request"), Service->IsActive());
	const TWeakPtr<FRPGDungeonMembershipTransport> Weak = Service;
	auto AfterDestroy = Current->OnProcessRequestComplete();
	Service.Reset();
	TestFalse(TEXT("Request does not retain the service in a cycle"), Weak.IsValid());
	TestFalse(TEXT("Destruction unbinds active request"), Current->OnProcessRequestComplete().IsBound());
	AfterDestroy.ExecuteIfBound(Current, MakeShared<FResponse, ESPMode::ThreadSafe>(200, Body(Host)), true);
	TestEqual(TEXT("Dead service cannot call its host"), Replies, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMembershipTransportRuntimeTest, "ProjectRPG.Online.MembershipTransport.Runtime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGMembershipTransportRuntimeTest::RunTest(const FString&)
{
	using namespace RPGMembershipTransportTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonMembershipTransport>(Host);
	Host.Config.bServerRuntime = false;
	TestFalse(TEXT("Client cannot dispatch membership GET"), Service->LoadSnapshot({}));
	TestFalse(TEXT("Client cannot dispatch expiry POST"), Service->ProcessExpirations({}));
	TestEqual(TEXT("Client dispatch rejection never accesses server credential"), Host.AuthorizationCalls, 0);
	Host.Config.bServerRuntime = true; Host.bValidCredential = false;
	TestFalse(TEXT("Invalid server credential rejected before building"), Service->CreateSnapshotRequest({}).IsValid());
	Host.bValidCredential = true; Host.Config.bServerConfigured = false;
	TestFalse(TEXT("Missing server assignment rejected"), Service->CreateExpirationRequest({}).IsValid());
	Host.Config.bServerConfigured = true; Host.Config.SessionId = TEXT("not-a-session-guid");
	TestFalse(TEXT("Invalid membership scope rejected"), Service->CreateSnapshotRequest({}).IsValid());
	TestEqual(TEXT("No rejected build applies a credential"), Host.AuthorizationCalls, 0);
	return true;
}
#endif
