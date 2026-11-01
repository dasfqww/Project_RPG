#include "Progression/RPGEquipmentMutationSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Progression/RPGEquipmentMutationCodec.h"
#include "Engine/GameInstance.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Manager/HttpWebManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace RPGEquipmentMutationTests
{
	bool Load(const FString&, FRPGEquipmentMutationCommand&, FString&, FString&);
	FString TemporaryRoot();
	void Cleanup(const FString&);
	class FResponse final : public IHttpResponse
	{
	public:
		FResponse(int32 InCode, FString InBody) : Code(InCode), Body(MoveTemp(InBody))
		{ const FTCHARToUTF8 Utf8(*Body); Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length()); }
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
		int32 Code; FString Body, Url = TEXT("http://equipment-fixture.invalid/api"); TArray<uint8> Bytes;
	};
	void Complete(const FHttpRequestPtr& Request, int32 Code, const FString& Body, bool Transport = true)
	{
		FHttpResponsePtr Response = MakeShared<FResponse, ESPMode::ThreadSafe>(Code, Body);
		Request->OnProcessRequestComplete().ExecuteIfBound(Request, Response, Transport);
	}
}

struct FRPGEquipmentMutationTestAccess
{
	static bool Login(UHttpWebManager& Web, const FString& Steam = TEXT("76561198000000001"), const FString& Token = TEXT("equipment-player-fixture"))
	{
		Web.ApiUrl = TEXT("http://127.0.0.1:1/api"); Web.GameServerId.Reset(); Web.ConfiguredDungeonSessionId.Reset();
		Web.BackendServiceToken = TEXT("fixture-service-must-not-be-sent"); Web.PlayerAuth.Reset();
		const FDateTime Now = FDateTime::UtcNow(); const double Seconds = FPlatformTime::Seconds();
		return Web.PlayerAuth.AcceptLogin(Token, Steam, (Now + FTimespan::FromMinutes(5)).ToIso8601(), Now.ToIso8601(), Seconds, Seconds);
	}
	static void Configure(URPGEquipmentMutationSubsystem& Service, UHttpWebManager& Web, const FString& Root, FHttpRequestPtr& Sent)
	{
		Service.TestManager = &Web; Service.JournalRoot = Root;
		Service.TestDispatch = [&Sent](const FHttpRequestRef& Request) { Sent = Request; return true; };
	}
	static void Endpoint(UHttpWebManager& Web, const FString& Value) { Web.ApiUrl = Value; }
	static void Server(UHttpWebManager& Web) { Web.GameServerId = TEXT("fixture-server"); Web.ConfiguredDungeonSessionId = TEXT("fixture-session"); }
	static void Expire(UHttpWebManager& Web) { Web.PlayerAuth.Invalidate(); }
	static bool RealLogin(UHttpWebManager& Web, const TSharedPtr<FJsonObject>& Context)
	{
		Web.ApiUrl = Context->GetStringField(TEXT("endpoint")); Web.GameServerId.Reset(); Web.ConfiguredDungeonSessionId.Reset();
		Web.BackendServiceToken.Reset(); Web.PlayerAuth.Reset(); const double Now = FPlatformTime::Seconds();
		return Web.PlayerAuth.AcceptLogin(Context->GetStringField(TEXT("token")), Context->GetStringField(TEXT("steam")),
			Context->GetStringField(TEXT("expiresAt")), Context->GetStringField(TEXT("issuedAt")), Now, Now);
	}
	static void ConfigureReal(URPGEquipmentMutationSubsystem& Service, UHttpWebManager& Web, const FString& Root)
	{ Service.TestManager = &Web; Service.JournalRoot = Root; }
	static void Authenticating(UHttpWebManager& Web, bool Enabled) { Web.ActiveAuthenticationRequest = Enabled ? FHttpModule::Get().CreateRequest() : FHttpRequestPtr(); }
	static FRPGEquipmentMutationScope Scope(UHttpWebManager& Web, const FGuid& Character)
	{ FRPGEquipmentMutationScope Result; FRPGEquipmentMutationScope::Create(Web.ApiUrl, Web.GetAuthenticatedSteamId(), Character, Result); return Result; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentClientTransportTest, "ProjectRPG.Equipment.Client.Transport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentClientTransportTest::RunTest(const FString&)
{
	using Access = FRPGEquipmentMutationTestAccess;
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UHttpWebManager> Web(NewObject<UHttpWebManager>(Instance.Get()));
	if (!TestTrue(TEXT("Fixture login"), Access::Login(*Web))) return false;
	FRPGEquipmentMutationCommand Command; FString Receipt, Replay;
	if (!RPGEquipmentMutationTests::Load(TEXT("upgrade"), Command, Receipt, Replay)) return false;
	FRPGEquipmentMutationHttpResult Result; bool Called = false;
	auto Request = Web->CreatePlayerEquipmentMutationRequest(Command, [&](FRPGEquipmentMutationHttpResult Value) { Called = true; Result = MoveTemp(Value); });
	if (!TestTrue(TEXT("Player narrow request constructed"), Request.IsValid())) return false;
	TestEqual(TEXT("Uses PLAYER not service bearer"), Request->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer equipment-player-fixture")));
	TestEqual(TEXT("Fixed route"), Request->GetURL(), FString(TEXT("http://127.0.0.1:1/api/characters/10203040-5060-7080-90a0-b0c0d0e0f001/equipment/upgrade")));
	TestEqual(TEXT("Only POST"), Request->GetVerb(), FString(TEXT("POST")));
	RPGEquipmentMutationTests::Complete(Request, 200, Receipt);
	TestTrue(TEXT("Current account callback accepted"), Called && Result.bCurrentPlayerContext);
	Called = false; Access::Login(*Web, TEXT("76561198000000002"));
	RPGEquipmentMutationTests::Complete(Request, 200, Receipt);
	TestTrue(TEXT("Another account callback quarantined"), Called && !Result.bCurrentPlayerContext);
	Access::Login(*Web); Access::Endpoint(*Web, TEXT("http://127.0.0.1:2/api"));
	RPGEquipmentMutationTests::Complete(Request, 200, Receipt); TestFalse(TEXT("Another endpoint quarantined"), Result.bCurrentPlayerContext);
	Access::Login(*Web, TEXT("76561198000000001"), TEXT("new-player-fixture"));
	RPGEquipmentMutationTests::Complete(Request, 200, Receipt); TestFalse(TEXT("Old credential callback quarantined"), Result.bCurrentPlayerContext);
	Access::Authenticating(*Web, true);
	TestFalse(TEXT("No request during login transition"), Web->CreatePlayerEquipmentMutationRequest(Command, [](auto) {}).IsValid());
	Access::Authenticating(*Web, false); Access::Expire(*Web);
	TestFalse(TEXT("Expired credential cannot dispatch"), Web->CreatePlayerEquipmentMutationRequest(Command, [](auto) {}).IsValid());
	Access::Login(*Web); Access::Server(*Web);
	TestFalse(TEXT("Dungeon server cannot borrow player route"), Web->CreatePlayerEquipmentMutationRequest(Command, [](auto) {}).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentClientRecoveryTest, "ProjectRPG.Equipment.Client.Recovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentClientRecoveryTest::RunTest(const FString&)
{
	using Access = FRPGEquipmentMutationTestAccess; using Journal = FRPGEquipmentMutationJournal;
	TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>());
	TStrongObjectPtr<UHttpWebManager> Web(NewObject<UHttpWebManager>(Instance.Get()));
	if (!Access::Login(*Web)) return false;
	FRPGEquipmentMutationCommand Template; FString Receipt, Replay;
	if (!RPGEquipmentMutationTests::Load(TEXT("upgrade"), Template, Receipt, Replay)) return false;
	const FString Root = RPGEquipmentMutationTests::TemporaryRoot();
	ON_SCOPE_EXIT { RPGEquipmentMutationTests::Cleanup(Root); };
	FHttpRequestPtr Sent;
	TStrongObjectPtr<URPGEquipmentMutationSubsystem> Service(NewObject<URPGEquipmentMutationSubsystem>(Instance.Get()));
	Access::Configure(*Service, *Web, Root, Sent);
	TestTrue(TEXT("BP entry persists and dispatches"), Service->UpgradeEquipment(Template.CharacterId, Template.Equipment, Template.Costs));
	if (!TestTrue(TEXT("Held request"), Sent.IsValid())) return false;
	const auto Scope = Access::Scope(*Web, Template.CharacterId); FRPGEquipmentPendingMutation Pending;
	TestEqual(TEXT("Journal exists BEFORE delivery"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Ready);
	const auto Original = Pending.Command; const TArray<uint8> OriginalBody = Sent->GetContent();
	TestFalse(TEXT("Double click never changes intent"), Service->UpgradeEquipment(Template.CharacterId, Template.Equipment, Template.Costs));
	RPGEquipmentMutationTests::Complete(Sent, 0, TEXT(""), false);
	TestEqual(TEXT("Lost response requires recovery"), Service->GetState(Template.CharacterId).State, ERPGEquipmentMutationState::RecoveryRequired);
	Service->Deinitialize(); Service.Reset(); Sent.Reset();
	Service.Reset(NewObject<URPGEquipmentMutationSubsystem>(Instance.Get())); Access::Configure(*Service, *Web, Root, Sent);
	TestEqual(TEXT("Fresh subsystem restores same ID"), Service->OpenCharacter(Template.CharacterId).RequestId, Original.RequestId);
	TestFalse(TEXT("New command blocked until resolved"), Service->DismantleEquipment(Template.CharacterId, Template.Equipment));
	TestTrue(TEXT("Explicit retry"), Service->RetryPending(Template.CharacterId));
	TestTrue(TEXT("Retry exact original body bytes"), Sent->GetContent() == OriginalBody);
	RPGEquipmentMutationTests::Complete(Sent, 409, TEXT("{\"error\":\"equipment_lease_busy\"}"));
	TestEqual(TEXT("Lease contention keeps intent"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Ready);
	TestTrue(TEXT("Retry after lease clears"), Service->RetryPending(Template.CharacterId));
	Access::Login(*Web, TEXT("76561198000000002"));
	RPGEquipmentMutationTests::Complete(Sent, 200, Receipt);
	TestFalse(TEXT("Another account sees no original request"), Service->GetState(Template.CharacterId).RequestId.IsValid());
	TestEqual(TEXT("Stale-account callback keeps original"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Ready);
	Access::Login(*Web); Service->OpenCharacter(Template.CharacterId);
	TestTrue(TEXT("Original account retries"), Service->RetryPending(Template.CharacterId));
	FString TemplateBody, TemplateFingerprint; FRPGEquipmentMutationCodec::Encode(Template, TemplateBody, TemplateFingerprint);
	// This is a mocked lifecycle test; independent vectors establish actual server hash compatibility.
	const FString MatchingReplay = Replay.Replace(*Template.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*Original.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)).Replace(*TemplateFingerprint, *Pending.Fingerprint);
	RPGEquipmentMutationTests::Complete(Sent, 200, MatchingReplay);
	TestEqual(TEXT("Matching historical success resolves"), Service->GetState(Template.CharacterId).State, ERPGEquipmentMutationState::Committed);
	TestTrue(TEXT("Refresh latest projection, never apply old records"), Service->GetState(Template.CharacterId).bNeedsInventoryRefresh);
	TestEqual(TEXT("Confirmed intent cleared"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Missing);
	TestTrue(TEXT("BP fresh owner query available"), Service->RefreshItems(Template.CharacterId));
	TestEqual(TEXT("Read cannot mutate"), Sent->GetVerb(), FString(TEXT("GET")));
	TestTrue(TEXT("No terminal history requested"), Sent->GetURL().Contains(TEXT("includeTerminal=false")));
	TestEqual(TEXT("Read uses player bearer"), Sent->GetHeader(TEXT("Authorization")), FString(TEXT("Bearer equipment-player-fixture")));
	RPGEquipmentMutationTests::Complete(Sent, 200, TEXT("{\"items\":[]}"));
	TestEqual(TEXT("Fresh owner result ready"), Service->GetState(Template.CharacterId).ItemsLoadState, ERPGInventoryProjectionLoadState::Ready);
	TestFalse(TEXT("Fresh read clears refresh request"), Service->GetState(Template.CharacterId).bNeedsInventoryRefresh);
	TestTrue(TEXT("Next command can begin"), Service->UpgradeEquipment(Template.CharacterId, Template.Equipment, Template.Costs));
	RPGEquipmentMutationTests::Complete(Sent, 200, TEXT("{}"));
	TestEqual(TEXT("Malformed success cannot clear"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Ready);
	TestTrue(TEXT("Retry immutable revision rejection"), Service->RetryPending(Template.CharacterId));
	RPGEquipmentMutationTests::Complete(Sent, 409, TEXT("{\"error\":\"item_revision_conflict\"}"));
	TestEqual(TEXT("Definitive rejection resolves"), Service->GetState(Template.CharacterId).State, ERPGEquipmentMutationState::Rejected);
	TestEqual(TEXT("Rejected intent cleared safely"), Journal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Missing);
	Service->Deinitialize(); Sent.Reset();
	return true;
}

namespace RPGEquipmentMutationTests
{
	class FHttpFlow final : public IAutomationLatentCommand
	{
	public:
		FHttpFlow(FAutomationTestBase& InTest, TSharedPtr<FJsonObject> InContext)
			: Test(InTest), Context(MoveTemp(InContext)), Root(TemporaryRoot()), Started(FPlatformTime::Seconds()),
			Instance(NewObject<UGameInstance>()), Web(NewObject<UHttpWebManager>(Instance.Get())),
			Service(NewObject<URPGEquipmentMutationSubsystem>(Instance.Get()))
		{
			Ready = FRPGEquipmentMutationTestAccess::RealLogin(*Web, Context);
			FRPGEquipmentMutationTestAccess::ConfigureReal(*Service, *Web, Root);
		}
		~FHttpFlow() override { if (Service.IsValid()) Service->Deinitialize(); Cleanup(Root); }
		bool Update() override
		{
			if (!Ready || FPlatformTime::Seconds() - Started > 60)
			{ Test.AddError(TEXT("Owned equipment HTTP flow could not start or timed out")); return true; }
			if (Service->IsRequestInFlight() || Service->IsItemsReadInFlight()) return false;
			if (Step == 1)
			{
				if (!Test.TestEqual(TEXT("Actual dropped response unresolved"), Service->GetState(Command.CharacterId).State,
					ERPGEquipmentMutationState::RecoveryRequired)) return true;
				FRPGEquipmentPendingMutation Pending;
				if (!Test.TestEqual(TEXT("Real lost response keeps original"), FRPGEquipmentMutationJournal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Ready)) return true;
				Original = Pending.Command.RequestId;
				Service->Deinitialize(); Service.Reset(NewObject<URPGEquipmentMutationSubsystem>(Instance.Get()));
				FRPGEquipmentMutationTestAccess::ConfigureReal(*Service, *Web, Root);
				if (!Test.TestEqual(TEXT("Fresh subsystem restores actual original"), Service->OpenCharacter(Command.CharacterId).RequestId, Original)
					|| !Test.TestTrue(TEXT("Real retry during lease"), Service->RetryPending(Command.CharacterId))) return true;
				Step = 2; return false;
			}
			if (Step == 2)
			{
				const auto View = Service->GetState(Command.CharacterId);
				if (!Test.TestEqual(TEXT("Actual server receipt accepted"), View.State, ERPGEquipmentMutationState::Committed)) return true;
				Test.TestTrue(TEXT("Real historical receipt requests current inventory refresh"), View.bNeedsInventoryRefresh);
				if (Index == 0) Test.TestEqual(TEXT("Same original ID after wire recovery"), View.RequestId, Original);
				FRPGEquipmentPendingMutation Pending;
				Test.TestEqual(TEXT("Real confirmed journal cleared"), FRPGEquipmentMutationJournal::Load(Root, Scope, Pending), ERPGEquipmentJournalRead::Missing);
				if (!Test.TestTrue(TEXT("BP fresh owner query reaches actual server"), Service->RefreshItems(Command.CharacterId))) return true;
				Step = 3; return false;
			}
			if (Step == 3)
			{
				const auto View = Service->GetState(Command.CharacterId);
				if (!Test.TestEqual(TEXT("Actual owner projection ready"), View.ItemsLoadState, ERPGInventoryProjectionLoadState::Ready)) return true;
				Test.TestFalse(TEXT("Fresh server query clears refresh flag"), View.bNeedsInventoryRefresh);
				if (Index == 0)
				{
					const auto* Current = View.Items.FindByPredicate([this](const auto& Entry) { return Entry.GetItemId() == Command.Equipment.ItemId; });
					if (!Test.TestNotNull(TEXT("Current equipment read back"), Current)) return true;
					Test.TestEqual(TEXT("Newer revision survives historical receipt"), Current->GetRevision(), 3LL);
					Test.TestEqual(TEXT("Newer location survives historical receipt"), Current->GetSlotIndex(), 30);
					Test.TestEqual(TEXT("Current level from owner GET"), Current->GetEquipmentProgression().Level, 1);
				}
				++Index; Step = 0;
			}
			const auto& Scenarios = Context->GetArrayField(TEXT("scenarios"));
			if (Index == Scenarios.Num()) { Test.AddInfo(TEXT("Actual narrow Player HTTP4, committed response loss, fresh subsystem same-intent recovery and receipt-before-lease succeeded. Memory fixture, not Steam/PG/process restart.")); return true; }
			const auto Scenario = Scenarios[Index]->AsObject(); FGuid Character;
			FGuid::Parse(Scenario->GetStringField(TEXT("characterId")), Character);
			const auto Op = static_cast<ERPGEquipmentMutationOperation>(Index);
			if (!Test.TestTrue(TEXT("Decode actual fixture expectations"), FRPGEquipmentMutationCodec::Decode(Character, Op, Scenario->GetStringField(TEXT("body")), Command))) return true;
			Scope = FRPGEquipmentMutationTestAccess::Scope(*Web, Character); bool Began = false;
			switch (Op)
			{
			case ERPGEquipmentMutationOperation::Upgrade: Began = Service->UpgradeEquipment(Character, Command.Equipment, Command.Costs); break;
			case ERPGEquipmentMutationOperation::Transfer: Began = Service->TransferEquipment(Character, Command.Equipment, Command.Target); break;
			case ERPGEquipmentMutationOperation::Craft: Began = Service->CraftEquipment(Character, Command.TargetDefinitionName, Command.Costs); break;
			case ERPGEquipmentMutationOperation::Dismantle: Began = Service->DismantleEquipment(Character, Command.Equipment); break;
			default: break;
			}
			if (!Test.TestTrue(TEXT("BP method dispatches real HTTP"), Began)) return true;
			Step = Index == 0 ? 1 : 2; return false;
		}
	private:
		FAutomationTestBase& Test; TSharedPtr<FJsonObject> Context; FString Root; double Started; bool Ready = false;
		int32 Index = 0, Step = 0; FGuid Original; FRPGEquipmentMutationCommand Command; FRPGEquipmentMutationScope Scope;
		TStrongObjectPtr<UGameInstance> Instance; TStrongObjectPtr<UHttpWebManager> Web;
		TStrongObjectPtr<URPGEquipmentMutationSubsystem> Service;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGEquipmentClientHttpTest, "ProjectRPG.Equipment.Client.HttpRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGEquipmentClientHttpTest::RunTest(const FString&)
{
	const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_EQUIPMENT_HTTP_FIXTURE"));
	if (Path.IsEmpty()) { AddInfo(TEXT("Opt-in private HTTP fixture not supplied; no network test performed.")); return true; }
	FString Text; TSharedPtr<FJsonObject> Context;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Context) || !Context)
	{ AddError(TEXT("Private HTTP context unavailable")); return false; }
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<RPGEquipmentMutationTests::FHttpFlow>(*this, Context));
	return true;
}
#endif
