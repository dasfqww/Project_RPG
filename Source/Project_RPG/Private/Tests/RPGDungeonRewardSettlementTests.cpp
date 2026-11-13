#if WITH_DEV_AUTOMATION_TESTS

#include "Economy/Backend/RPGDungeonRewardSettlement.h"
#include "Economy/RPGCurrencyTypes.h"
#include "Economy/RPGDungeonRewardTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

namespace RPGRewardSettlementTests
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
		FString Body, Url = TEXT("http://reward-fixture.invalid/api");
		TArray<uint8> Bytes;
	};

	struct FHost final : IRPGDungeonRewardSettlementHost
	{
		FRPGDungeonRewardSettlementConfig Config{
			TEXT("http://reward-fixture.invalid/api"), TEXT("server-fixture"),
			TEXT("10000000-0000-0000-0000-000000000001"), 7.f, true };
		bool bDispatch = true;
		int32 Generation = 1, Replies = 0, Status = 0;
		bool bSucceeded = false;
		FString State;
		TFunction<void()> OnReply;
		UObject& GetRewardSettlementOwner() const override { return *GetTransientPackage(); }
		FRPGDungeonRewardSettlementConfig GetRewardSettlementConfig() const override { return Config; }
		bool CanDispatchRewardSettlement() const override { return bDispatch; }
		TFunction<bool()> CaptureRewardSettlementScope() const override
		{ return [this, Captured = Generation]() { return Generation == Captured; }; }
		void ApplyRewardSettlementHeaders(const FHttpRequestRef& Request) const override
		{
			Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
			Request->SetHeader(TEXT("Authorization"), TEXT("Bearer reward-test-fixture"));
		}
		void BroadcastRewardSettlement(const FString& InState, int32 InStatus, bool bSuccess) override
		{
			++Replies; State = InState; Status = InStatus; bSucceeded = bSuccess;
			if (OnReply) OnReply();
		}
		FString Receipt(const FString& Version = TEXT("reward_v1")) const
		{
			return FString::Printf(TEXT("{\"dungeonSessionId\":\"%s\",\"rewardVersion\":\"%s\",\"state\":\"Pending\"}"),
				*Config.SessionId, *Version);
		}
	};
	FHttpRequestPtr Build(const TSharedRef<FRPGDungeonRewardSettlement>& Service)
	{ return Service->CreateRequest(TEXT("reward_v1"), {}, {}); }
	void Complete(const FHttpRequestPtr& Request, int32 Status, const FString& Body)
	{
		FHttpResponsePtr Response = MakeShared<FResponse, ESPMode::ThreadSafe>(Status, Body);
		Request->OnProcessRequestComplete().ExecuteIfBound(Request, Response, true);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSettlementWireTest, "ProjectRPG.Economy.RewardSettlement.Wire",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSettlementWireTest::RunTest(const FString&)
{
	using namespace RPGRewardSettlementTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonRewardSettlement>(Host);
	FRPGCurrencyChange Change; Change.CurrencyCode = TEXT("Gold"); Change.Delta = MAX_int64;
	FRPGDungeonItemReward Item; Item.DefinitionType = TEXT("RPGItemDefinition"); Item.DefinitionName = TEXT("Potion");
	Item.Quantity = 2; Item.MaxStackSize = 5; Item.PersonalLootWeight = 2500; Item.BindState = ERPGItemBindState::BindOnEquip;
	const auto Request = Service->CreateRequest(TEXT(" reward_v1 "), {Change}, {Item});
	if (!TestTrue(TEXT("Valid authored command builds"), Request.IsValid())) return false;
	TestEqual(TEXT("Session-scoped URL"), Request->GetURL(), Host.Config.ApiUrl / (TEXT("dungeon-sessions/") + Host.Config.SessionId + TEXT("/settle-rewards")));
	TestEqual(TEXT("POST remains unchanged"), Request->GetVerb(), FString(TEXT("POST")));
	TestEqual(TEXT("Credential policy supplied by host"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer reward-test-fixture")));
	const auto& Bytes = Request->GetContent();
	const FUTF8ToTCHAR Converted(reinterpret_cast<const UTF8CHAR*>(Bytes.GetData()), Bytes.Num());
	const FString Body(Converted.Length(), Converted.Get());
	TestTrue(TEXT("int64 never rounded through double"), Body.Contains(TEXT("\"delta\":9223372036854775807")));
	TestTrue(TEXT("Original personal loot weight retained"), Body.Contains(TEXT("\"personalLootWeight\":2500")));
	TestTrue(TEXT("Original bind state retained"), Body.Contains(TEXT("\"bindState\":\"BindOnEquip\"")));
	Service->Cancel();
	const auto Retry = Service->CreateRequest(TEXT("reward_v1"), {Change}, {Item});
	if (!TestTrue(TEXT("Same command can rebuild after explicit cancellation"), Retry.IsValid())) return false;
	TestTrue(TEXT("Rebuild preserves exact body"), Retry->GetContent() == Request->GetContent());
	Service->Cancel();
	Change.Delta = 0;
	TestFalse(TEXT("Zero reward delta rejected"), Service->CreateRequest(TEXT("reward_v1"), {Change}, {}).IsValid());
	Item.PersonalLootWeight = 10001;
	TestFalse(TEXT("Invalid pool rejected"), Service->CreateRequest(TEXT("reward_v1"), {}, {Item}).IsValid());
	Host.Config.bServerConfigured = false;
	TestFalse(TEXT("Unassigned server rejected"), Build(Service).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSettlementOrderingTest, "ProjectRPG.Economy.RewardSettlement.Ordering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSettlementOrderingTest::RunTest(const FString&)
{
	using namespace RPGRewardSettlementTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonRewardSettlement>(Host);
	const auto First = Build(Service);
	if (!TestTrue(TEXT("First request ready"), First.IsValid())) return false;
	TestFalse(TEXT("Second request denied while active"), Build(Service).IsValid());
	Host.bDispatch = false;
	Service->Settle(TEXT("reward_v1"), {}, {});
	TestEqual(TEXT("Later disconnect does not publish false deferral for active request"), Host.Replies, 0);
	FHttpRequestPtr Next;
	Host.OnReply = [&]() { Host.bDispatch = true; Next = Build(Service); };
	Complete(First, 202, Host.Receipt());
	TestTrue(TEXT("In-flight acknowledgement survives later disconnect"), Host.bSucceeded);
	TestEqual(TEXT("Pending acknowledgement is not delivery success"), Host.State, FString(TEXT("Pending")));
	if (!TestTrue(TEXT("Active slot released before notification/reentrant request"), Next.IsValid())) return false;
	Host.OnReply = nullptr;
	Complete(First, 202, Host.Receipt());
	TestEqual(TEXT("Old callback does not notify twice"), Host.Replies, 1);
	TestTrue(TEXT("Old callback does not clear replacement request"), Service->IsActive());
	Complete(Next, 202, Host.Receipt());
	TestEqual(TEXT("Replacement completes once"), Host.Replies, 2);
	Host.bDispatch = false;
	Service->Settle(TEXT("reward_v1"), {}, {});
	TestEqual(TEXT("Barrier defers new settlement"), Host.Status, 425);
	TestFalse(TEXT("Deferral does not reserve an HTTP slot"), Service->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSettlementScopeTest, "ProjectRPG.Economy.RewardSettlement.ScopeAndCancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSettlementScopeTest::RunTest(const FString&)
{
	using namespace RPGRewardSettlementTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonRewardSettlement>(Host);
	const auto Old = Build(Service);
	if (!Old) return false;
	++Host.Generation;
	Complete(Old, 202, Host.Receipt());
	TestEqual(TEXT("Old assignment cannot notify"), Host.Replies, 0);
	TestFalse(TEXT("Old assignment releases its own request"), Service->IsActive());
	const auto Canceled = Build(Service);
	if (!Canceled) return false;
	auto Late = Canceled->OnProcessRequestComplete();
	Service->Cancel();
	TestFalse(TEXT("Cancellation unbinds request"), Canceled->OnProcessRequestComplete().IsBound());
	const auto Current = Build(Service);
	if (!Current) return false;
	Late.ExecuteIfBound(Canceled, MakeShared<FResponse, ESPMode::ThreadSafe>(202, Host.Receipt()), true);
	TestTrue(TEXT("Copied late callback cannot clear current operation"), Service->IsActive());
	Complete(Current, 202, Host.Receipt());
	TestEqual(TEXT("Current assignment completes normally"), Host.Replies, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSettlementReceiptTest, "ProjectRPG.Economy.RewardSettlement.Receipt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSettlementReceiptTest::RunTest(const FString&)
{
	using namespace RPGRewardSettlementTests;
	FHost Host;
	const auto Service = MakeShared<FRPGDungeonRewardSettlement>(Host);
	AddExpectedError(TEXT("Dungeon reward settlement request failed"), EAutomationExpectedErrorFlags::Contains, 3);
	for (const FString& Body : {Host.Receipt(TEXT("other_version")), FString(TEXT("{}")),
		FString(TEXT("{\"dungeonSessionId\":\"other_session\",\"rewardVersion\":\"reward_v1\",\"state\":\"Pending\"}"))})
	{
		const auto Request = Build(Service);
		if (!Request) return false;
		Complete(Request, 202, Body);
		TestFalse(TEXT("Malformed or mismatched receipt rejected"), Host.bSucceeded);
		TestTrue(TEXT("Failure clears displayed state"), Host.State.IsEmpty());
		TestFalse(TEXT("Failure releases slot for retry"), Service->IsActive());
	}
	TestEqual(TEXT("Every current failure completes once"), Host.Replies, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGRewardSettlementLifetimeTest, "ProjectRPG.Economy.RewardSettlement.Lifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGRewardSettlementLifetimeTest::RunTest(const FString&)
{
	using namespace RPGRewardSettlementTests;
	FHost Host;
	TSharedPtr<FRPGDungeonRewardSettlement> Service = MakeShared<FRPGDungeonRewardSettlement>(Host);
	const auto Request = Build(Service.ToSharedRef());
	if (!Request) return false;
	auto Late = Request->OnProcessRequestComplete();
	const TWeakPtr<FRPGDungeonRewardSettlement> Weak = Service;
	Service.Reset();
	TestFalse(TEXT("No request/delegate ownership cycle retains service"), Weak.IsValid());
	TestFalse(TEXT("Destruction unbinds current request"), Request->OnProcessRequestComplete().IsBound());
	Late.ExecuteIfBound(Request, MakeShared<FResponse, ESPMode::ThreadSafe>(202, Host.Receipt()), true);
	TestEqual(TEXT("Copied callback cannot access destroyed service/host"), Host.Replies, 0);
	return true;
}
#endif
