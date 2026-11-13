#include "Riding/RPGRidingSnapshot.h"
#include "Riding/RPGRidingSessionComponent.h"
#include "Online/RPGDungeonConnectionRecovery.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "HttpRequestAdapter.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "Containers/Ticker.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

struct FRPGRidingSnapshotTestAccess
{
    static void Configure(URPGRidingSessionComponent& S, const FRPGRidingSnapshotContext& C, TFunction<bool()> Valid,
        TFunction<FHttpRequestPtr(const FRPGRidingSnapshotContext&, TFunction<void(const FRPGRidingSnapshotResponse&)>)> Factory)
    {
        S.Policy = NewObject<URPGRidingPolicy>(&S); S.Policy->Values = C.Policy; S.ValidateAdmission = MoveTemp(Valid);
        S.SnapshotFactoryForTests = MoveTemp(Factory); S.SnapshotServerForTests = C.ServerId;
        S.UpdateAuthenticatedContext(C.Ticket.Identity.CharacterId.ToString(), TEXT("fixture-authenticated"), C.Ticket.Identity.DungeonSessionId.ToString(), C.Ticket.Identity.ConnectionGeneration, false, true);
    }
    static void Generation(URPGRidingSessionComponent& S, int64 Generation)
    { const FString Character = S.BoundCharacter, Steam = S.BoundSteam, Session = S.BoundSession; S.UpdateAuthenticatedContext(Character, Steam, Session, Generation, false, true); }
    static ERPGRidingSessionStatus Status(URPGRidingSessionComponent& S) { return S.Runtime->GetStatus(); }
    static bool Copy(URPGRidingSessionComponent& S, FRPGRidingAdmissionSnapshot& Out) { return S.Runtime->CopySnapshot(Out); }
    static void Expire(URPGRidingSessionComponent& S) { S.ExpireAdmissionSnapshot(S.AuthorityContext); }
    static bool RetryTimer(URPGRidingSessionComponent& S) { return S.SnapshotRetryTicker.IsValid(); }
    static void Clear(URPGRidingSessionComponent& S, bool bRevoke = false)
    { S.Invalidate(bRevoke); S.ValidateAdmission = nullptr; S.SnapshotFactoryForTests = nullptr; }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    FString Wire()
    {
        FString Path, Body; FParse::Value(FCommandLine::Get(), TEXT("RidingSnapshotWire="), Path);
        if (!Path.IsEmpty()) FFileHelper::LoadFileToString(Body, *Path);
        return Body;
    }
    TSharedPtr<FJsonObject> Object(const FString& Body)
    { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O)
    { FString Text; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    FRPGRidingSnapshotContext Expected(const FString& Body)
    {
        FRPGRidingSnapshotContext C; auto O = Object(Body); if (!O) return C;
        FGuid::Parse(O->GetStringField(TEXT("characterId")), C.Ticket.Identity.CharacterId);
        FGuid::Parse(O->GetStringField(TEXT("dungeonSessionId")), C.Ticket.Identity.DungeonSessionId);
        C.Ticket.Identity.AuthorityContext = FGuid::NewGuid(); C.Ticket.Identity.ConnectionGeneration = FCString::Atoi64(*O->GetStringField(TEXT("connectionGeneration")));
        C.Ticket.RequestId = FGuid::NewGuid(); C.Policy.CatalogVersion = static_cast<int32>(O->GetNumberField(TEXT("catalogVersion")));
        C.Policy.GameplayHash = O->GetStringField(TEXT("gameplayHash")); C.ServerId = O->GetStringField(TEXT("serverId"));
        FDateTime::ParseIso8601(*O->GetStringField(TEXT("observedAtUtc")), C.SentUtc); C.SentMonotonic = 10;
        return C;
    }
    FString Fresh(const FRPGRidingSnapshotContext& C)
    {
        auto O = Object(Wire()); if (!O) return {};
        O->SetStringField(TEXT("characterId"), C.Ticket.Identity.CharacterId.ToString(EGuidFormats::DigitsWithHyphensLower));
        O->SetStringField(TEXT("dungeonSessionId"), C.Ticket.Identity.DungeonSessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
        O->SetStringField(TEXT("connectionGeneration"), LexToString(C.Ticket.Identity.ConnectionGeneration));
        O->SetStringField(TEXT("observedAtUtc"), FDateTime::UtcNow().ToIso8601());
        O->SetStringField(TEXT("validUntilUtc"), (FDateTime::UtcNow() + FTimespan::FromMinutes(1)).ToIso8601());
        return Json(O); // Actor tests use a fresh clock fixture; Wire test below consumes unmodified real backend bytes.
    }
    class FPendingRequest : public FHttpRequestAdapterBase
    {
    public:
        FPendingRequest() : FHttpRequestAdapterBase(FHttpModule::Get().CreateRequest()) {}
        bool bCancelled = false;
        virtual bool ProcessRequest() override { return true; } // No network or product implementation replacement.
        virtual void CancelRequest() override { bCancelled = true; }
    };
    struct FActor
    {
        UWorld* World; APlayerState* State; URPGRidingSessionComponent* Session;
        bool bAdmitted = true; FRPGRidingSnapshotContext Context;
        TFunction<void(const FRPGRidingSnapshotResponse&)> Completion;
        TSharedPtr<FPendingRequest, ESPMode::ThreadSafe> Request;
        FActor()
        {
            const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
            auto* PC = World->SpawnActor<APlayerController>(); State = World->SpawnActor<APlayerState>(); PC->SetPlayerState(State); State->SetOwner(PC);
            Session = NewObject<URPGRidingSessionComponent>(State); Session->RegisterComponent();
            FRPGRidingSnapshotTestAccess::Configure(*Session, Expected(Wire()), [this]() { return bAdmitted; },
                [this](const FRPGRidingSnapshotContext& C, TFunction<void(const FRPGRidingSnapshotResponse&)> Done) -> FHttpRequestPtr
                { Context = C; Completion = MoveTemp(Done); Request = MakeShared<FPendingRequest, ESPMode::ThreadSafe>(); return Request; });
        }
        FRPGRidingSnapshotResponse Response() const { return {true, true, 200, Fresh(Context)}; }
        ~FActor() { FRPGRidingSnapshotTestAccess::Clear(*Session); World->DestroyWorld(false); }
    };
    struct FHost : IRPGDungeonRecoveryHost
    {
        UObject* Owner; FString Api = TEXT("http://127.0.0.1:1/"), Server, Session, Token = TEXT("Bearer fixture-only");
        virtual UObject& GetRecoveryOwner() const override { return *Owner; }
        virtual UGameInstance* GetRecoveryGameInstance() const override { return nullptr; }
        virtual const FString& GetRecoveryApiUrl() const override { return Api; }
        virtual const FString& GetRecoveryServerId() const override { return Server; }
        virtual const FString& GetRecoverySessionId() const override { return Session; }
        virtual bool HasRecoveryCredential() const override { return true; }
        virtual bool HasValidRecoveryServiceCredential() const override { return true; }
        virtual bool IsRecoverySettlementActive() const override { return false; }
        virtual void ApplyRecoveryServiceAuthorization(const FHttpRequestRef& R) const override { R->SetHeader(TEXT("Authorization"), Token); }
        virtual float GetRecoveryRequestTimeout() const override { return 10; }
        virtual bool IsRecoveryServerRuntime() const override { return true; }
        virtual bool IsRecoveryServerConfigured() const override { return true; }
        virtual void ApplyRecoveryHeaders(const FHttpRequestRef&) const override {}
        virtual void BroadcastConsumedTicket(const FString&, const FString&, const FString&, const FString&, int64, bool) override {}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotWire, "ProjectRPG.Riding.Snapshot.Wire", Flags)
bool FRidingSnapshotWire::RunTest(const FString&)
{
    const auto Body = Wire(); TestFalse(TEXT("Real backend response is supplied"), Body.IsEmpty()); if (Body.IsEmpty()) return false;
    const auto C = Expected(Body); FRPGRidingDecodedSnapshot Out;
    TestTrue(TEXT("Unmodified authenticated backend JSON decodes"), RPGRidingSnapshot::Decode(Body, C, C.SentUtc + FTimespan::FromSeconds(.1), 10.1, Out));
    TestTrue(TEXT("Owned and selected match actual server wire"), Out.Snapshot.OwnedMountIds.Contains(TEXT("horse")) && Out.Snapshot.SelectedMountId == TEXT("horse"));
    TestTrue(TEXT("Settings and expiry captured"), Out.Snapshot.PreferencesRevision == 1 && Out.Snapshot.Shortcut.Kind == ERPGRidingSelectorKind::Specific && Out.ValidForSeconds > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotMalformed, "ProjectRPG.Riding.Snapshot.Malformed", Flags)
bool FRidingSnapshotMalformed::RunTest(const FString&)
{
    const FString Body = Wire(); const auto C = Expected(Body); if (Body.IsEmpty()) return false;
    const auto Reject = [&](const FString& Text)
    { FRPGRidingDecodedSnapshot O; O.ValidForSeconds = 123; TestFalse(TEXT("Malformed wire fails closed"), RPGRidingSnapshot::Decode(Text, C, C.SentUtc + FTimespan::FromSeconds(.1), 10.1, O)); TestTrue(TEXT("Output cleared"), O.ValidForSeconds == 0 && !O.Snapshot.RosterId.IsValid()); };
    Reject(Body.LeftChop(1) + TEXT(",\"connectionGeneration\":\"1\"}")); Reject(TEXT("null")); Reject(FString::ChrN(RPGRidingSnapshot::MaximumBytes + 1, TEXT('x')));
    for (const auto* Field : {TEXT("serverId"), TEXT("characterId"), TEXT("dungeonSessionId"), TEXT("gameplayHash"), TEXT("connectionGeneration")})
    { auto O = Object(Body); O->SetStringField(Field, TEXT("wrong")); Reject(Json(O)); }
    auto Numeric = Object(Body); Numeric->SetNumberField(TEXT("preferencesRevision"), 9007199254740992.); Reject(Json(Numeric));
    auto NumericSmall = Object(Body); NumericSmall->SetNumberField(TEXT("rosterRevision"), 1); Reject(Json(NumericSmall));
    auto StringVersion = Object(Body); StringVersion->SetStringField(TEXT("catalogVersion"), LexToString(C.Policy.CatalogVersion)); Reject(Json(StringVersion));
    auto Expired = Object(Body); Expired->SetStringField(TEXT("validUntilUtc"), (C.SentUtc - FTimespan::FromSeconds(1)).ToIso8601()); Reject(Json(Expired));
    auto Duplicate = Object(Body); auto Mounts = Duplicate->GetArrayField(TEXT("mounts")); const auto First = Mounts[0]; Mounts.Add(First); Duplicate->SetArrayField(TEXT("mounts"), Mounts); Reject(Json(Duplicate));
    auto Shortcut = Object(Body); Shortcut->GetObjectField(TEXT("preferences"))->GetObjectField(TEXT("shortcut"))->SetStringField(TEXT("mountId"), TEXT("unowned")); Reject(Json(Shortcut));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotActor, "ProjectRPG.Riding.Snapshot.Actor", Flags)
bool FRidingSnapshotActor::RunTest(const FString&)
{
    FActor F; if (!F.Completion) { AddError(TEXT("Backend wire/factory unavailable")); return false; }
    TestFalse(TEXT("Confirmed admission alone is NotReady"), F.Session->GetOwnerState().bReady);
    TestTrue(TEXT("Transport request pending"), F.Request.IsValid()); auto Done = F.Completion; const auto Response = F.Response(); Done(Response);
    TestTrue(TEXT("Only validated current response sets Ready"), F.Session->GetOwnerState().bReady);
    FRPGRidingAdmissionSnapshot Copy; TestTrue(TEXT("Owner metadata is server-private"), FRPGRidingSnapshotTestAccess::Copy(*F.Session, Copy) && Copy.Shortcut.MountId == TEXT("horse"));
    Done({true, true, 200, TEXT("invalid")}); TestTrue(TEXT("Late duplicate cannot replace installed state"), F.Session->GetOwnerState().bReady);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotStale, "ProjectRPG.Riding.Snapshot.Stale", Flags)
bool FRidingSnapshotStale::RunTest(const FString&)
{
    FActor F; if (!F.Completion) { AddError(TEXT("Backend wire/factory unavailable")); return false; }
    auto Old = F.Completion; const auto OldResponse = F.Response(); auto OldRequest = F.Request;
    FRPGRidingSnapshotTestAccess::Generation(*F.Session, F.Context.Ticket.Identity.ConnectionGeneration + 1);
    TestTrue(TEXT("Old work cancelled at generation change"), OldRequest->bCancelled);
    Old(OldResponse); TestFalse(TEXT("Old successful response cannot ready a new generation"), F.Session->GetOwnerState().bReady);
    F.Completion(F.Response()); TestTrue(TEXT("New response can ready current context"), F.Session->GetOwnerState().bReady);
    FRPGRidingSnapshotTestAccess::Clear(*F.Session, true); Old(OldResponse); TestFalse(TEXT("Revoked context cannot resurrect"), F.Session->GetOwnerState().bReady);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotFailure, "ProjectRPG.Riding.Snapshot.Failure", Flags)
bool FRidingSnapshotFailure::RunTest(const FString&)
{
    FActor F; if (!F.Completion) { AddError(TEXT("Backend wire/factory unavailable")); return false; }
    auto Bad = F.Response(); Bad.bScopeCurrent = false; F.Completion(Bad);
    TestFalse(TEXT("Credential/assignment drift cannot ready"), F.Session->GetOwnerState().bReady);
    TestTrue(TEXT("First failure schedules bounded retry"), FRPGRidingSnapshotTestAccess::RetryTimer(*F.Session));
    FRPGRidingSnapshotTestAccess::Clear(*F.Session); TestFalse(TEXT("Teardown clears timer"), FRPGRidingSnapshotTestAccess::RetryTimer(*F.Session));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotExpiry, "ProjectRPG.Riding.Snapshot.Expiry", Flags)
bool FRidingSnapshotExpiry::RunTest(const FString&)
{
    FActor F; if (!F.Completion) { AddError(TEXT("Backend wire/factory unavailable")); return false; }
    F.Completion(F.Response()); TestTrue(TEXT("Installed"), F.Session->GetOwnerState().bReady);
    FRPGRidingSnapshotTestAccess::Expire(*F.Session); TestFalse(TEXT("Expiry withdraws Riding Ready"), F.Session->GetOwnerState().bReady);
    TestTrue(TEXT("Expiry leaves recoverable context, no automatic polling"), FRPGRidingSnapshotTestAccess::Status(*F.Session) == ERPGRidingSessionStatus::RetryableFailure && !FRPGRidingSnapshotTestAccess::RetryTimer(*F.Session));
    FRPGRidingSession S; auto C = Expected(Wire()); S.SetIdentity(C.Ticket.Identity, 0); S.ConfirmAdmission(C.Ticket.Identity, C.Policy);
    auto Ticket = S.BeginSnapshot(0); FRPGRidingDecodedSnapshot Data;
    RPGRidingSnapshot::Decode(Wire(), C, C.SentUtc + FTimespan::FromSeconds(.1), 10.1, Data); S.ApplySnapshot(Ticket, Data.Snapshot, .1);
    const FGuid Epoch = S.BindPawn(); S.RecordApprovedHostileOutcome(1); S.ExpireSnapshot(2); const auto Timing = S.GetTiming(2);
    TestTrue(TEXT("Identity/admission and cooldowns survive cache expiry"), Timing.bValid && Timing.CombatRemaining > 0 && Timing.DashRemaining > 0 && Epoch.IsValid());
    TestFalse(TEXT("Expired cache does not trigger autonomous refresh"), S.BeginSnapshot(2).RequestId.IsValid());
    TestTrue(TEXT("A fresh user request may recover"), S.BeginSnapshot(2, true).RequestId.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingSnapshotTransport, "ProjectRPG.Riding.Snapshot.Transport", Flags)
bool FRidingSnapshotTransport::RunTest(const FString&)
{
    FActor F; FHost Host; Host.Owner = F.State; Host.Server = F.Context.ServerId; Host.Session = F.Context.Ticket.Identity.DungeonSessionId.ToString();
    FRPGRidingSnapshotResponse Result; auto Request = RPGRidingSnapshot::Create(Host, F.Context, [&](const auto& R) { Result = R; });
    TestTrue(TEXT("Uses existing server credential without manager mutation"), Request.IsValid() && Request->GetVerb() == TEXT("GET") && Request->GetHeader(TEXT("Authorization")) == Host.Token);
    if (!Request) return false;
    TestTrue(TEXT("Exact configured endpoint and generation"), Request->GetURL().Contains(TEXT("/api/game-servers/sessions/")) && Request->GetURL().Contains(TEXT("connectionGeneration=")));
    Host.Token = TEXT("Bearer changed-fixture"); Request->OnProcessRequestComplete().ExecuteIfBound(Request, nullptr, false);
    TestFalse(TEXT("Rotated credential invalidates completion scope"), Result.bScopeCurrent);
    Host.Session = FGuid::NewGuid().ToString(); TestFalse(TEXT("Different assignment cannot create request"), RPGRidingSnapshot::Create(Host, F.Context, [](const auto&) {}).IsValid());
    return true;
}
#endif
