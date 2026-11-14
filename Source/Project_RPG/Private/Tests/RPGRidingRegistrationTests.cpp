#include "Riding/RPGRidingRegistrationReader.h"
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
        FString Endpoint, Account; bool bCredential = true, bActive = false, bAccept = true; int32 Sends = 0;
        FCompletion Completion;
        virtual bool GetContext(FString& E, FString& A) const override { E = Endpoint; A = Account; return true; }
        virtual bool HasCredential() const override { return bCredential; }
        virtual bool CanSend() const override { return bCredential && !bActive; }
        virtual bool ReadCollection(FGuid, FCompletion) override { return false; }
        virtual bool ReadRegistrationOptions(FGuid, FCompletion Done) override
        { if (!bAccept || !CanSend()) return false; ++Sends; bActive = true; Completion = MoveTemp(Done); return true; }
        virtual void Cancel() override { bActive = false; }
        void Reply(const FString& Body, int32 Code = 200, double Delay = 0)
        { bActive = false; auto Done = MoveTemp(Completion); if (Done) Done({true, true, Code, Body, Delay}); }
    };
    struct FReader
    {
        TSharedRef<FFake> Transport = MakeShared<FFake>();
        FRPGRidingCollectionContext Context;
        TSharedPtr<FRPGRidingRegistrationReader> Reader;
        FString Body;
        FReader()
        {
            auto F = Fixture(); if (!F) return;
            Context = {F->GetStringField(TEXT("endpoint")), F->GetStringField(TEXT("account")), Id(F, TEXT("character")), true};
            Transport->Endpoint = Context.Endpoint; Transport->Account = Context.Account; Body = F->GetStringField(TEXT("registrationOptionsBody"));
            Reader = MakeShared<FRPGRidingRegistrationReader>(Transport, [this] { return Context; });
        }
        ~FReader() { if (Reader) Reader->Shutdown(); }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationWire, "ProjectRPG.Riding.RegistrationClient.Wire", Flags)
bool FRidingRegistrationWire::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) { AddError(TEXT("Actual backend fixture missing")); return false; }
    const auto Character = Id(F, TEXT("character")); FRPGRidingRegistrationOptions O;
    TestTrue(TEXT("Actual registration metadata"), RPGRidingRegistrationOptions::Decode(F->GetStringField(TEXT("registrationOptionsBody")), Character, O));
    TestTrue(TEXT("Owned definition mapping and string revision"), O.RosterRevision == 1 && O.CatalogVersion == 7 && O.bCatalogReady && O.Options.Num() == 1
        && O.Options[0].bAlreadyOwned && O.Options[0].RequiredQuantity == 1 && O.Options[0].MaximumStackSize == 99);
    TestNotNull(TEXT("Versioned exact definition lookup"), O.Find(TEXT("RPGItemDefinition"), TEXT("GameItem.Consume.Mount.Horse"), 1));
    TestNull(TEXT("Wrong version cannot match"), O.Find(TEXT("RPGItemDefinition"), TEXT("GameItem.Consume.Mount.Horse"), 2));
    FRPGRidingCollection C; RPGRidingCollection::Decode(F->GetStringField(TEXT("body")), Character, C);
    TestTrue(TEXT("Same collection/catalog/ownership observation"), O.MatchesCollection(C));
    ++C.Mounts[0].DefinitionVersion; TestTrue(TEXT("Unlock-time mount version does not replace current catalog definition"), O.MatchesCollection(C));
    ++C.RosterRevision; TestFalse(TEXT("Stale collection rejected"), O.MatchesCollection(C)); --C.RosterRevision;
    C.Mounts.Reset(); TestFalse(TEXT("Inconsistent owned observation rejected"), O.MatchesCollection(C));
    TestTrue(TEXT("Sibling character retains shared ownership"), RPGRidingRegistrationOptions::Decode(F->GetStringField(TEXT("emptyOptionsBody")), Id(F, TEXT("emptyCharacter")), O) && O.Options[0].bAlreadyOwned);
    TestTrue(TEXT("Unowned mapping is only an observation"), RPGRidingRegistrationOptions::Decode(F->GetStringField(TEXT("unownedOptionsBody")), Id(F, TEXT("unownedCharacter")), O) && !O.Options[0].bAlreadyOwned);
    TestTrue(TEXT("Disabled definitions retain current roster"), RPGRidingRegistrationOptions::Decode(F->GetStringField(TEXT("disabledOptionsBody")), Character, O)
        && !O.bCatalogReady && !O.bRegistrationEnabled && O.Options.IsEmpty() && O.RosterRevision == 1);
    const auto Maximum = F->GetStringField(TEXT("maximumOptionsBody"));
    TestTrue(TEXT("Largest legal server response fits bounded 256KiB limit"), FTCHARToUTF8(*Maximum).Length() > 220 * 1024 && FTCHARToUTF8(*Maximum).Length() <= RPGRidingRegistrationOptions::MaximumBytes);
    TestTrue(TEXT("All 512 maximum-length definitions accepted"), RPGRidingRegistrationOptions::Decode(Maximum, Character, O) && O.Options.Num() == 512
        && O.Options[0].DefinitionType.Len() == 64 && O.Options[0].DefinitionName.Len() == 128 && O.Options[0].MountId.Len() == 64
        && O.Options[0].DefinitionVersion == MAX_int32 && O.Options[0].MaximumStackSize == MAX_int32);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationMalformed, "ProjectRPG.Riding.RegistrationClient.Malformed", Flags)
bool FRidingRegistrationMalformed::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) return false; const auto Character = Id(F, TEXT("character")); const auto Body = F->GetStringField(TEXT("registrationOptionsBody"));
    auto Reject = [&](const FString& Text)
    { FRPGRidingRegistrationOptions O; O.CharacterId = Character; TestFalse(TEXT("Invalid metadata rejected"), RPGRidingRegistrationOptions::Decode(Text, Character, O)); TestFalse(TEXT("No partial output"), O.CharacterId.IsValid()); };
    Reject(TEXT("null")); Reject(TEXT("[]")); Reject(Body.LeftChop(1) + TEXT(",\"rosterRevision\":\"1\"}")); Reject(FString::ChrN(RPGRidingRegistrationOptions::MaximumBytes + 1, 'x'));
    auto MutateRoot = [&](TFunction<void(TSharedPtr<FJsonObject>)> Change) { auto O = Object(Body); Change(O); Reject(Json(O)); };
    auto MutateRow = [&](TFunction<void(TSharedPtr<FJsonObject>)> Change) { MutateRoot([&](auto O) { Change(O->GetArrayField(TEXT("options"))[0]->AsObject()); }); };
    MutateRoot([](auto O) { O->SetNumberField(TEXT("protocolVersion"), 2); });
    MutateRoot([](auto O) { O->SetNumberField(TEXT("rosterRevision"), 1); });
    MutateRoot([](auto O) { O->SetStringField(TEXT("rosterRevision"), TEXT("01")); });
    MutateRoot([](auto O) { O->SetStringField(TEXT("rosterRevision"), TEXT("9223372036854775808")); });
    MutateRoot([](auto O) { O->SetStringField(TEXT("characterId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens)); });
    MutateRoot([](auto O) { O->SetBoolField(TEXT("registrationEnabled"), false); });
    MutateRoot([](auto O) { O->SetStringField(TEXT("gameplayHash"), FString::ChrN(64, 'A')); });
    MutateRoot([](auto O) { O->SetNumberField(TEXT("catalogVersion"), 1.5); });
    MutateRoot([](auto O) { O->SetBoolField(TEXT("canRegister"), true); });
    MutateRow([](auto O) { O->SetStringField(TEXT("definitionType"), TEXT("/asset")); });
    MutateRow([](auto O) { O->SetStringField(TEXT("definitionName"), FString::ChrN(129, 'A')); });
    MutateRow([](auto O) { O->SetStringField(TEXT("definitionVersion"), TEXT("1")); });
    MutateRow([](auto O) { O->SetNumberField(TEXT("requiredQuantity"), 2); });
    MutateRow([](auto O) { O->SetNumberField(TEXT("maximumStackSize"), 0); });
    MutateRow([](auto O) { O->SetStringField(TEXT("mountId"), TEXT("Horse")); });
    MutateRow([](auto O) { O->SetNumberField(TEXT("alreadyOwned"), 1); });
    MutateRow([](auto O) { O->SetStringField(TEXT("itemId"), FGuid::NewGuid().ToString()); });
    MutateRoot([](auto O) { auto Rows = O->GetArrayField(TEXT("options")); const auto First = Rows[0]; Rows.Add(First); O->SetArrayField(TEXT("options"), Rows); });
    MutateRoot([](auto O) { auto Rows = O->GetArrayField(TEXT("options")); auto Alias = Object(Json(Rows[0]->AsObject())); Alias->SetStringField(TEXT("definitionName"), TEXT("Alias"));
        Alias->SetBoolField(TEXT("alreadyOwned"), false); Rows.Add(MakeShared<FJsonValueObject>(Alias)); O->SetArrayField(TEXT("options"), Rows); });
    auto O = Object(Body); O->SetStringField(TEXT("rosterRevision"), TEXT("9223372036854775807")); FRPGRidingRegistrationOptions Result;
    TestTrue(TEXT("Exact int64 maximum retained"), RPGRidingRegistrationOptions::Decode(Json(O), Character, Result) && Result.RosterRevision == MAX_int64);
    O = Object(Body); auto Rows = O->GetArrayField(TEXT("options")); auto Alias = Object(Json(Rows[0]->AsObject()));
    Alias->SetStringField(TEXT("definitionName"), TEXT("gameItem.Consume.Mount.Horse")); Rows.Add(MakeShared<FJsonValueObject>(Alias)); O->SetArrayField(TEXT("options"), Rows);
    TestTrue(TEXT("Case-sensitive definitions remain distinct"), RPGRidingRegistrationOptions::Decode(Json(O), Character, Result) && Result.Options.Num() == 2
        && Result.Find(TEXT("RPGItemDefinition"), TEXT("gameItem.Consume.Mount.Horse"), 1) == &Result.Options[1]);
    O = Object(F->GetStringField(TEXT("disabledOptionsBody"))); O->SetArrayField(TEXT("options"), Rows); Reject(Json(O));
    O = Object(F->GetStringField(TEXT("maximumOptionsBody"))); Rows = O->GetArrayField(TEXT("options")); const auto First = Rows[0]; Rows.Add(First); O->SetArrayField(TEXT("options"), Rows); Reject(Json(O));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationLifetime, "ProjectRPG.Riding.RegistrationClient.Lifetime", Flags)
bool FRidingRegistrationLifetime::RunTest(const FString&)
{
    FReader F; if (!F.Reader) return false;
    F.Reader->RefreshContext(); TestEqual(TEXT("Opening never sends a request"), F.Transport->Sends, 0);
    TestTrue(TEXT("Explicit read"), F.Reader->Read()); TestFalse(TEXT("Single flight"), F.Reader->Read()); F.Transport->Reply(F.Body);
    TestNotNull(TEXT("Current metadata available"), F.Reader->GetOptions());
    F.Transport->bCredential = false; TestNull(TEXT("Auth loss clears observations"), F.Reader->GetOptions());
    F.Transport->bCredential = true; TestNull(TEXT("Auth restoration requires new read"), F.Reader->GetOptions());
    F.Reader->Read(); auto Old = MoveTemp(F.Transport->Completion); F.Context.Character = FGuid::NewGuid(); F.Reader->RefreshContext();
    Old({true, true, 200, F.Body, 0}); TestNull(TEXT("Late character response ignored"), F.Reader->GetOptions());
    F.Reader->Read(); Old = MoveTemp(F.Transport->Completion); F.Context.Endpoint += TEXT("/other"); F.Transport->Endpoint = F.Context.Endpoint; F.Reader->RefreshContext();
    Old({true, true, 200, F.Body, 0}); TestNull(TEXT("Late endpoint response ignored"), F.Reader->GetOptions());
    F.Reader->Read(); Old = MoveTemp(F.Transport->Completion); F.Context.Account = TEXT("76561700000000999"); F.Transport->Account = F.Context.Account; F.Reader->RefreshContext();
    Old({true, true, 200, F.Body, 0}); TestNull(TEXT("Late account response ignored"), F.Reader->GetOptions());
    F.Reader->Read(); Old = MoveTemp(F.Transport->Completion); F.Reader->Shutdown(); Old({true, true, 200, F.Body, 0});
    TestFalse(TEXT("Shutdown rejects sends"), F.Reader->Read()); TestNull(TEXT("Shutdown suppresses callbacks"), F.Reader->GetOptions());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationFailures, "ProjectRPG.Riding.RegistrationClient.Failures", Flags)
bool FRidingRegistrationFailures::RunTest(const FString&)
{
    FReader F; if (!F.Reader) return false;
    F.Transport->bAccept = false; TestFalse(TEXT("Dispatch rejection reported"), F.Reader->Read());
    TestEqual(TEXT("Rejected read not left loading"), F.Reader->GetState(), ERPGRidingCollectionReadState::Failed); F.Transport->bAccept = true;
    F.Reader->Read(); F.Transport->Reply(TEXT("{}")); TestNull(TEXT("Malformed 200 hidden"), F.Reader->GetOptions());
    F.Reader->Read(); F.Transport->Reply(TEXT("{}"), 503, 30); const auto Sends = F.Transport->Sends;
    TestTrue(TEXT("Retry-after applies to explicit reads"), F.Reader->GetRetryAfterSeconds() > 0 && !F.Reader->Read());
    F.Reader->Invalidate(); TestTrue(TEXT("Invalidation cannot bypass backoff"), !F.Reader->Read() && F.Transport->Sends == Sends);
    F.Context.Account = TEXT("76561700000000999"); F.Transport->Account = F.Context.Account; F.Reader->RefreshContext();
    TestTrue(TEXT("Backoff belongs to old identity"), F.Reader->Read()); F.Transport->Reply(F.Body);
    F.Reader->Invalidate(); TestNull(TEXT("Confirmed mutation invalidates old observations"), F.Reader->GetOptions());
    TestEqual(TEXT("No automatic reread after invalidation"), F.Transport->Sends, Sends + 1);
    F.Reader->Read(); auto Late = MoveTemp(F.Transport->Completion); F.Reader->Invalidate(); Late({true, true, 200, F.Body, 0});
    TestNull(TEXT("Invalidation cancels in-flight generation"), F.Reader->GetOptions());
    F.Context.bEligible = false; TestFalse(TEXT("Leaving lobby blocks read"), F.Reader->Read());
    return true;
}
namespace
{
    struct FHttpRun : TSharedFromThis<FHttpRun>
    {
        FAutomationTestBase* Test = nullptr; TSharedPtr<FJsonObject> Data;
        TSharedPtr<FRPGHttpRidingOwnerTransport> Transport; TSharedPtr<FRPGRidingRegistrationReader> Reader;
        FRPGBackendPlayerAuthState Auth; FString Endpoint, Account; bool bWaiting = false; int32 Stage = 0; double Deadline = 0;
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingRegistrationHttpStep, TSharedPtr<FHttpRun>, R);
    bool FRidingRegistrationHttpStep::Update()
    {
        if (R->bWaiting) { if (FPlatformTime::Seconds() < R->Deadline) return false; R->Test->AddError(TEXT("Registration HTTP timeout")); R->Reader->Shutdown(); return true; }
        const int32 Stage = R->Stage++; auto& T = *R->Test; auto F = R->Data; const auto Character = Id(F, TEXT("character"));
        if (Stage >= 9) { R->Reader->Shutdown(); return true; }
        R->Endpoint = F->GetStringField(TEXT("endpoint")); R->Account = F->GetStringField(TEXT("account")); R->Auth.SetOpaqueToken(F->GetStringField(TEXT("token")));
        FGuid Target = Character;
        if (Stage == 1) R->Endpoint = F->GetStringField(TEXT("disabledEndpoint"));
        if (Stage == 2) R->Endpoint = F->GetStringField(TEXT("maximumEndpoint"));
        if (Stage == 3) Target = Id(F, TEXT("foreignCharacter"));
        if (Stage == 4) R->Auth.SetOpaqueToken(F->GetStringField(TEXT("admin")));
        if (Stage == 5) R->Auth.SetOpaqueToken(TEXT("invalid-token"));
        R->bWaiting = true; R->Deadline = FPlatformTime::Seconds() + 15;
        if (Stage == 0) T.TestTrue(TEXT("Actual reader to Player HTTP"), R->Reader->Read());
        else
        {
            T.TestTrue(TEXT("Actual options GET dispatch"), R->Transport->ReadRegistrationOptions(Target, [R = R, Stage, Target](FRPGRidingOwnerHttpResult V)
            {
                auto& T = *R->Test; FRPGRidingRegistrationOptions O;
                if (Stage <= 2 || Stage == 8)
                {
                    T.TestTrue(TEXT("Actual server contract decoded"), V.bCurrentPlayerContext && V.bTransportSuccessful && V.StatusCode == 200 && RPGRidingRegistrationOptions::Decode(V.Body, Target, O));
                    if (Stage == 1) T.TestTrue(TEXT("Disabled collection visible with no registration definitions"), !O.bCatalogReady && O.Options.IsEmpty());
                    if (Stage == 2) T.TestTrue(TEXT("Transport accepts all maximum-length rows within 256KiB"), V.Body.Len() > 220 * 1024 && O.Options.Num() == 512);
                }
                else if (Stage == 3 || Stage == 4) T.TestEqual(TEXT("Foreign character/admin denied"), V.StatusCode, 403);
                else if (Stage == 5) T.TestTrue(TEXT("401 invalidates only current credential"), V.StatusCode == 401 && !R->Auth.HasAccessToken(FPlatformTime::Seconds()));
                else if (Stage == 6) T.TestTrue(TEXT("Late scope payload suppressed"), !V.bCurrentPlayerContext && V.Body.IsEmpty() && V.StatusCode == 0);
                else T.AddError(TEXT("Cancelled read must not callback"));
                R->bWaiting = false;
            }));
            if (Stage == 6) R->Endpoint += TEXT("/changed");
            if (Stage == 7) { R->Transport->Cancel(); R->bWaiting = false; }
        }
        ADD_LATENT_AUTOMATION_COMMAND(FRidingRegistrationHttpStep(R)); return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationHttp, "ProjectRPG.Riding.RegistrationClient.MemoryHttp", Flags)
bool FRidingRegistrationHttp::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) return false; auto R = MakeShared<FHttpRun>(); R->Test = this; R->Data = F;
    R->Endpoint = F->GetStringField(TEXT("endpoint")); R->Account = F->GetStringField(TEXT("account")); const TWeakPtr<FHttpRun> Weak = R;
    R->Transport = MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& C)
    { auto R = Weak.Pin(); if (!R) return false; C = {R->Endpoint, R->Account, R->Auth.Authorization(FPlatformTime::Seconds())}; return true; },
    [Weak](const FRPGRidingOwnerHttpContext& C, int32 Code)
    { auto R = Weak.Pin(); const bool bCurrent = R && R->Endpoint == C.Endpoint && R->Account == C.Account && R->Auth.MatchesRequest(C.Authorization);
      if (bCurrent) R->Auth.InvalidateIfUnauthorized(C.Authorization, Code); return bCurrent; });
    R->Reader = MakeShared<FRPGRidingRegistrationReader>(R->Transport.ToSharedRef(), [Weak]
    { auto R = Weak.Pin(); return R ? FRPGRidingCollectionContext{R->Endpoint, R->Account, Id(R->Data, TEXT("character")), true} : FRPGRidingCollectionContext{}; });
    R->Reader->OnChanged.AddLambda([Weak]
    {
        auto R = Weak.Pin(); if (!R || R->Stage != 1) return;
        const auto State = R->Reader->GetState();
        if (State == ERPGRidingCollectionReadState::Loading || State == ERPGRidingCollectionReadState::Empty || State == ERPGRidingCollectionReadState::Unavailable) return;
        const auto* Options = R->Reader->GetOptions();
        R->Test->TestTrue(TEXT("Actual reader cached current ownership"), Options && Options->Options.Num() == 1 && Options->Options[0].bAlreadyOwned);
        R->bWaiting = false;
    });
    TestFalse(TEXT("No credential means no request"), R->Transport->ReadRegistrationOptions(Id(F, TEXT("character")), [](auto) {}));
    ADD_LATENT_AUTOMATION_COMMAND(FRidingRegistrationHttpStep(R)); return true;
}
#endif
