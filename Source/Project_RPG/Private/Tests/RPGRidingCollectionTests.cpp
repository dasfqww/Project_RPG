#include "Riding/RPGRidingCollectionReader.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& Text)
    { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O)
    { FString Text; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    TSharedPtr<FJsonObject> Fixture()
    {
        FString Text; const auto Path = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE"));
        if (!FFileHelper::LoadFileToString(Text, *Path)) return nullptr;
        auto O = Object(Text);
        return O && O->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? O : nullptr;
    }
    FGuid Id(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
    { FGuid G; FGuid::Parse(O->GetStringField(Key), G); return G; }
    class FFake final : public IRPGRidingOwnerTransport
    {
    public:
        FString Endpoint, Account; bool bCredential = true, bActive = false; int32 Sends = 0, Cancels = 0;
        FCompletion Completion;
        virtual bool GetContext(FString& E, FString& A) const override { E = Endpoint; A = Account; return true; }
        virtual bool HasCredential() const override { return bCredential; }
        virtual bool CanSend() const override { return bCredential && !bActive; }
        virtual bool ReadCollection(FGuid, FCompletion Done) override
        { if (!CanSend()) return false; ++Sends; bActive = true; Completion = MoveTemp(Done); return true; }
        virtual void Cancel() override { ++Cancels; bActive = false; }
        void Reply(const FString& Body, int32 Code = 200, double Delay = 0)
        { bActive = false; auto Done = MoveTemp(Completion); if (Done) Done({true, true, Code, Body, Delay}); }
    };
    struct FReader
    {
        TSharedRef<FFake> Transport = MakeShared<FFake>();
        FRPGRidingCollectionContext Context;
        TSharedPtr<FRPGRidingCollectionReader> Reader;
        FString Body;
        FReader()
        {
            auto F = Fixture(); if (!F) return;
            Context = {F->GetStringField(TEXT("endpoint")), F->GetStringField(TEXT("account")), Id(F, TEXT("character")), true};
            Transport->Endpoint = Context.Endpoint; Transport->Account = Context.Account; Body = F->GetStringField(TEXT("body"));
            Reader = MakeShared<FRPGRidingCollectionReader>(Transport, [this] { return Context; });
        }
        ~FReader() { if (Reader) Reader->Shutdown(); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionWire, "ProjectRPG.Riding.OwnerRead.Wire", Flags)
bool FRidingCollectionWire::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) { AddError(TEXT("Real backend fixture unavailable")); return false; }
    FRPGRidingCollection C;
    TestTrue(TEXT("Actual owner GET projection"), RPGRidingCollection::Decode(F->GetStringField(TEXT("body")), Id(F, TEXT("character")), C));
    TestTrue(TEXT("Owned and settings metadata"), C.Mounts.Num() == 1 && C.Mounts[0].MountId == TEXT("horse") && C.RosterRevision == 1
        && C.PreferencesRevision == 1 && C.SelectedMountId == TEXT("horse") && C.Shortcut.MountId == TEXT("horse"));
    TestTrue(TEXT("Empty sibling character shares roster"), RPGRidingCollection::Decode(F->GetStringField(TEXT("emptyBody")), Id(F, TEXT("emptyCharacter")), C)
        && C.Mounts.Num() == 1 && C.PreferencesRevision == 0 && C.SelectedMountId.IsEmpty());
    TestTrue(TEXT("Disabled catalog retains owner data"), RPGRidingCollection::Decode(F->GetStringField(TEXT("disabledBody")), Id(F, TEXT("character")), C)
        && !C.bCatalogReady && !C.bRegistrationEnabled && !C.bPreferencesEnabled && C.Mounts.Num() == 1);
    TestTrue(TEXT("No registration means no mount fallback"), RPGRidingCollection::Decode(F->GetStringField(TEXT("unownedBody")), Id(F, TEXT("unownedCharacter")), C)
        && C.Mounts.IsEmpty() && C.RosterRevision == 0 && C.PreferencesRevision == 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionMalformed, "ProjectRPG.Riding.OwnerRead.Malformed", Flags)
bool FRidingCollectionMalformed::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) return false; const FString Body = F->GetStringField(TEXT("body")); const auto Character = Id(F, TEXT("character"));
    auto Reject = [&](const FString& Text)
    { FRPGRidingCollection C; C.CharacterId = Character; TestFalse(TEXT("Bad wire rejected"), RPGRidingCollection::Decode(Text, Character, C)); TestFalse(TEXT("Output cleared"), C.CharacterId.IsValid()); };
    Reject(Body.LeftChop(1) + TEXT(",\"rosterRevision\":\"1\"}")); Reject(TEXT("null")); Reject(FString::ChrN(RPGRidingCollection::MaximumBytes + 1, TEXT('x')));
    auto O = Object(Body); O->SetNumberField(TEXT("rosterRevision"), 1); Reject(Json(O));
    O = Object(Body); O->SetStringField(TEXT("preferencesRevision"), TEXT("01")); Reject(Json(O));
    O = Object(Body); O->SetStringField(TEXT("preferencesRevision"), TEXT("9223372036854775808")); Reject(Json(O));
    O = Object(Body); O->SetStringField(TEXT("catalogVersion"), TEXT("7")); Reject(Json(O));
    O = Object(Body); O->SetStringField(TEXT("gameplayHash"), TEXT("invalid")); Reject(Json(O));
    O = Object(Body); O->SetBoolField(TEXT("preferencesReady"), false); Reject(Json(O));
    O = Object(Body); O->SetBoolField(TEXT("catalogReady"), false); Reject(Json(O));
    O = Object(Body); auto Mounts = O->GetArrayField(TEXT("mounts")); const auto First = Mounts[0]; Mounts.Add(First); O->SetArrayField(TEXT("mounts"), Mounts); Reject(Json(O));
    O = Object(Body); O->GetObjectField(TEXT("preferences"))->SetStringField(TEXT("selectedMountId"), TEXT("unowned")); Reject(Json(O));
    O = Object(Body); O->GetObjectField(TEXT("preferences"))->GetObjectField(TEXT("shortcut"))->SetStringField(TEXT("mode"), TEXT("Other")); Reject(Json(O));
    FRPGRidingCollection C; TestFalse(TEXT("Different character projection"), RPGRidingCollection::Decode(Body, FGuid::NewGuid(), C));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionReaderLifetime, "ProjectRPG.Riding.OwnerRead.ReaderLifetime", Flags)
bool FRidingCollectionReaderLifetime::RunTest(const FString&)
{
    FReader F; if (!F.Reader) return false;
    TestTrue(TEXT("Explicit read starts"), F.Reader->Read()); TestFalse(TEXT("Single flight"), F.Reader->Read());
    F.Transport->Reply(F.Body); TestNotNull(TEXT("Matching read cached"), F.Reader->GetCollection());
    F.Transport->bCredential = false; TestNull(TEXT("Auth loss hides retained collection on read"), F.Reader->GetCollection());
    TestEqual(TEXT("Auth loss unavailable"), F.Reader->GetState(), ERPGRidingCollectionReadState::Unavailable);
    F.Transport->bCredential = true; TestNull(TEXT("Auth restoration requires explicit fresh read"), F.Reader->GetCollection());
    F.Reader->Read(); auto Old = MoveTemp(F.Transport->Completion);
    F.Context.Character = FGuid::NewGuid(); F.Reader->RefreshContext(); Old({true, true, 200, F.Body, 0});
    TestNull(TEXT("Old character cannot populate new view"), F.Reader->GetCollection()); TestEqual(TEXT("No automatic read"), F.Transport->Sends, 2);
    F.Reader->Shutdown(); TestFalse(TEXT("Shutdown rejects work"), F.Reader->Read());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionReaderFailures, "ProjectRPG.Riding.OwnerRead.ReaderFailures", Flags)
bool FRidingCollectionReaderFailures::RunTest(const FString&)
{
    FReader F; if (!F.Reader) return false;
    F.Reader->Read(); F.Transport->Reply(TEXT("invalid")); TestNull(TEXT("Malformed success never cached"), F.Reader->GetCollection());
    TestEqual(TEXT("Malformed success failed"), F.Reader->GetState(), ERPGRidingCollectionReadState::Failed);
    F.Reader->Read(); F.Transport->Reply(TEXT("{}"), 503, 30);
    TestTrue(TEXT("Retry-after throttles explicit reads without polling"), F.Reader->GetRetryAfterSeconds() > 0 && !F.Reader->Read() && F.Transport->Sends == 2);
    F.Context.Account = TEXT("76561700000000999"); F.Transport->Account = F.Context.Account;
    F.Reader->RefreshContext(); TestEqual(TEXT("New account empty"), F.Reader->GetState(), ERPGRidingCollectionReadState::Empty);
    TestTrue(TEXT("Old account backoff not transferred"), F.Reader->Read());
    F.Context.bEligible = false; F.Reader->RefreshContext();
    TestFalse(TEXT("Leaving eligible lobby cancels view"), F.Reader->Read());
    return true;
}

namespace
{
    struct FHttpRun : TSharedFromThis<FHttpRun>
    {
        FAutomationTestBase* Test = nullptr;
        TSharedPtr<FRPGHttpRidingOwnerTransport> Transport;
        TSharedPtr<FJsonObject> Data;
        FRPGBackendPlayerAuthState Auth;
        FString Endpoint, Account;
        bool bWaiting = false;
        int32 Stage = 0, Callbacks = 0;
        double Deadline = 0;
        IRPGRidingOwnerTransport::FCompletion Reply(TFunction<void(const FRPGRidingOwnerHttpResult&)> Check)
        { auto Self = AsShared(); return [Self, Check = MoveTemp(Check)](FRPGRidingOwnerHttpResult R) { ++Self->Callbacks; Check(R); Self->bWaiting = false; }; }
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingOwnerHttpStep, TSharedPtr<FHttpRun>, R);
    bool FRidingOwnerHttpStep::Update()
    {
        if (R->bWaiting) { if (FPlatformTime::Seconds() < R->Deadline) return false; R->Test->AddError(TEXT("Owned HTTP callback timed out")); R->Transport->Cancel(); return true; }
        auto& T = *R->Test; const int32 Stage = R->Stage++;
        if (Stage >= 12) { T.TestEqual(TEXT("Cancelled request never calls consumer"), R->Callbacks, 11); R->Transport->Cancel(); return true; }
        auto F = R->Data; const auto Character = Id(F, TEXT("character"));
        R->Endpoint = F->GetStringField(TEXT("endpoint")); R->Account = F->GetStringField(TEXT("account")); R->Auth.SetOpaqueToken(F->GetStringField(TEXT("token")));
        FGuid Target = Character;
        if (Stage == 1) { Target = Id(F, TEXT("unownedCharacter")); R->Account = F->GetStringField(TEXT("unownedAccount")); R->Auth.SetOpaqueToken(F->GetStringField(TEXT("unownedToken"))); }
        if (Stage == 2) R->Endpoint = F->GetStringField(TEXT("disabledEndpoint"));
        if (Stage == 3) Target = Id(F, TEXT("foreignCharacter"));
        if (Stage == 4) R->Auth.SetOpaqueToken(F->GetStringField(TEXT("admin")));
        if (Stage == 5) R->Auth.SetOpaqueToken(TEXT("invalid-fixture-token"));
        R->bWaiting = true; R->Deadline = FPlatformTime::Seconds() + 15;
        T.TestTrue(TEXT("Actual owner GET dispatched"), R->Transport->ReadCollection(Target, R->Reply([R = R, Stage, Target](const FRPGRidingOwnerHttpResult& V)
        {
            auto& T = *R->Test;
            if (Stage <= 2)
            {
                FRPGRidingCollection C;
                T.TestTrue(TEXT("Actual backend JSON and owner credential"), V.bCurrentPlayerContext && V.bTransportSuccessful && V.StatusCode == 200 && RPGRidingCollection::Decode(V.Body, Target, C));
                if (Stage == 2) T.TestFalse(TEXT("Disabled catalog remains read-only"), C.bCatalogReady);
            }
            else if (Stage == 3 || Stage == 4) T.TestEqual(TEXT("Foreign owner/admin cannot substitute"), V.StatusCode, 403);
            else if (Stage == 5) { T.TestEqual(TEXT("Invalid token denied"), V.StatusCode, 401); T.TestFalse(TEXT("Current credential invalidated"), R->Auth.HasAccessToken(FPlatformTime::Seconds())); }
            else if (Stage >= 6 && Stage <= 8)
                T.TestTrue(TEXT("Rotated credential/account/endpoint suppresses stale payload"), !V.bCurrentPlayerContext && V.Body.IsEmpty() && V.StatusCode == 0 && !V.bTransportSuccessful);
            else T.TestEqual(TEXT("Fresh scope after cancelled request"), V.StatusCode, 200);
        })));
        T.TestFalse(TEXT("Single active request"), R->Transport->CanSend());
        if (Stage == 6) R->Auth.SetOpaqueToken(TEXT("rotated-fixture-token"));
        if (Stage == 7) R->Account = TEXT("76561700000000999");
        if (Stage == 8) R->Endpoint += TEXT("/other");
        if (Stage == 9)
        {
            R->Transport->Cancel(); R->bWaiting = false;
            T.TestTrue(TEXT("Cancellation releases slot"), R->Transport->CanSend());
        }
        ADD_LATENT_AUTOMATION_COMMAND(FRidingOwnerHttpStep(R)); return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionHttp, "ProjectRPG.Riding.OwnerRead.MemoryHttp", Flags)
bool FRidingCollectionHttp::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) { AddError(TEXT("Private host fixture missing")); return false; }
    auto R = MakeShared<FHttpRun>(); R->Test = this; R->Data = F;
    R->Endpoint = F->GetStringField(TEXT("endpoint")); R->Account = F->GetStringField(TEXT("account"));
    const TWeakPtr<FHttpRun> Weak = R;
    R->Transport = MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& C)
    { auto R = Weak.Pin(); if (!R) return false; C = {R->Endpoint, R->Account, R->Auth.Authorization(FPlatformTime::Seconds())}; return true; },
    [Weak](const FRPGRidingOwnerHttpContext& C, int32 Code)
    { auto R = Weak.Pin(); const bool bCurrent = R && R->Endpoint == C.Endpoint && R->Account == C.Account && R->Auth.MatchesRequest(C.Authorization);
      if (bCurrent) R->Auth.InvalidateIfUnauthorized(C.Authorization, Code); return bCurrent; });
    TestFalse(TEXT("Cannot send without player token"), R->Transport->CanSend());
    ADD_LATENT_AUTOMATION_COMMAND(FRidingOwnerHttpStep(R)); return true;
}
#endif
