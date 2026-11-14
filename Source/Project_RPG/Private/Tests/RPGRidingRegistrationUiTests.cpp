#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Riding/RPGRidingFreshRead.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingRegistrationUiTestAccess
{
    static void Load(URPGRidingCollectionWidget& W) { W.RegistrationReload->OnClicked.Broadcast(); }
    static void Select(URPGRidingCollectionWidget& W) { W.RegistrationRows->SetSelectedIndex(0); W.RegistrationSelection(FString(), ESelectInfo::Direct); }
    static void Review(URPGRidingCollectionWidget& W) { W.RegistrationReviewButton->OnClicked.Broadcast(); }
    static void Confirm(URPGRidingCollectionWidget& W) { W.RegistrationConfirm->OnClicked.Broadcast(); }
    static void Recover(URPGRidingCollectionWidget& W) { W.RegistrationRecover->OnClicked.Broadcast(); }
    static void Assets(URPGRidingCollectionWidget& W) { W.RegistrationAssets->OnClicked.Broadcast(); }
    static FString Consent(URPGRidingCollectionWidget& W) { return W.RegistrationConsent->GetText().ToString(); }
    static bool CanConfirm(URPGRidingCollectionWidget& W) { return W.RegistrationConfirm->GetIsEnabled(); }
    static void Expire(FRPGRidingRegistrationClient& C) { C.ReceivedAt -= 61; }
};
namespace
{
    using A = FRPGRidingRegistrationUiTestAccess;
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& Text) { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O) { FString S; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&S)); return S; }
    TSharedPtr<FJsonObject> Fixture()
    {
        FString S; FFileHelper::LoadFileToString(S, *FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE")));
        auto O = Object(S); return O && O->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? O : nullptr;
    }
    FGuid Id(const TSharedPtr<FJsonObject>& O, const TCHAR* Key) { FGuid G; FGuid::Parse(O->GetStringField(Key), G); return G; }
    FString Root() { FString R; FParse::Value(FCommandLine::Get(), TEXT("RidingRegistrationRoot="), R); return R / FGuid::NewGuid().ToString(EGuidFormats::Digits); }
    FText Mount(const FString&) { return FText::FromString(TEXT("갈색 말")); }
    bool Names(const FRPGItemRecord&, const FRPGRidingRegistrationOption&, FText& I, FText& M)
    { I = FText::FromString(TEXT("승용마 등록증")); M = Mount(FString()); return true; }
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCollectionContext* C; FCompletion Done; int32 Reads = 0, Writes = 0; FString LastBody;
        explicit FFake(FRPGRidingCollectionContext& In) : C(&In) {}
        virtual bool GetContext(FString& E, FString& Account) const override { E = C->Endpoint; Account = C->Account; return true; }
        virtual bool HasCredential() const override { return C->bEligible; }
        virtual bool CanSend() const override { return HasCredential() && !Done; }
        bool Read(FCompletion F) { if (!CanSend()) return false; ++Reads; Done = MoveTemp(F); return true; }
        virtual bool ReadCollection(FGuid, FCompletion F) override { return Read(MoveTemp(F)); }
        virtual bool ReadRegistrationOptions(FGuid, FCompletion F) override { return Read(MoveTemp(F)); }
        virtual bool ReadInventory(FGuid, FCompletion F) override { return Read(MoveTemp(F)); }
        virtual bool SendIntent(const FRPGRidingPendingIntent& I, FCompletion F) override { if (!CanSend()) return false; ++Writes; LastBody = I.Body; Done = MoveTemp(F); return true; }
        virtual void Cancel() override { Done = {}; }
        void Reply(const FString& Body, int32 Code = 200, double Delay = 0) { auto F = MoveTemp(Done); if (F) F({true, true, Code, Body, Delay}); }
    };
    struct FOwner
    {
        FRPGRidingCollectionContext C;
        TSharedPtr<FFake> CT, DT, BT, WT;
        TSharedPtr<FRPGRidingCollectionReader> Collection;
        TSharedPtr<FRPGRidingRegistrationReader> Definitions;
        TSharedPtr<FRPGRidingCommandCoordinator> Commands;
        TSharedPtr<FRPGRidingRegistrationClient> Client;
        TStrongObjectPtr<URPGRidingCollectionWidget> Widget; TSharedPtr<SWidget> Slate;
        FString CollectionBody, OptionsBody, ItemsBody, Journal; bool bNames = true;
        FOwner()
        {
            auto F = Fixture(); C = {F->GetStringField(TEXT("endpoint")), F->GetStringField(TEXT("unownedAccount")), Id(F, TEXT("unownedCharacter")), true};
            CollectionBody = F->GetStringField(TEXT("unownedBody")); OptionsBody = F->GetStringField(TEXT("candidateOptions")); ItemsBody = F->GetStringField(TEXT("candidateItems")); Journal = Root();
            CT = MakeShared<FFake>(C); DT = MakeShared<FFake>(C); BT = MakeShared<FFake>(C); WT = MakeShared<FFake>(C);
            Collection = MakeShared<FRPGRidingCollectionReader>(CT.ToSharedRef(), [this] { return C; }); Definitions = MakeShared<FRPGRidingRegistrationReader>(DT.ToSharedRef(), [this] { return C; });
            Commands = MakeShared<FRPGRidingCommandCoordinator>(WT.ToSharedRef(), [this] { FRPGRidingCommandContext X; FRPGRidingIntentScope::Create(C.Endpoint,C.Account,C.Character,X.Scope); X.bEligible=C.bEligible; return X; }, Journal);
            Client = MakeShared<FRPGRidingRegistrationClient>(Collection.ToSharedRef(), Definitions.ToSharedRef(), BT.ToSharedRef(), Commands.ToSharedRef(), [this] { return C; },
                [this](const auto& R, const auto& O, FText& I, FText& M) { return bNames && Names(R, O, I, M); }, [] { return false; });
            Widget.Reset(NewObject<URPGRidingCollectionWidget>()); Widget->SetReadOwner(Collection.ToSharedRef(), Mount); Widget->SetRegistrationOwner(Client.ToSharedRef()); Slate = Widget->TakeWidget();
        }
        ~FOwner() { Widget->CloseScreen(); Client->Shutdown(); Collection->Shutdown(); Definitions->Shutdown(); Commands->Shutdown(); Slate.Reset(); Widget.Reset(); }
        void Load() { A::Load(*Widget); CT->Reply(CollectionBody); DT->Reply(OptionsBody); BT->Reply(ItemsBody); }
        void Choose() { A::Select(*Widget); A::Review(*Widget); }
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationConsent, "ProjectRPG.Riding.RegistrationUI.Consent", Flags)
bool FRidingRegistrationConsent::RunTest(const FString&)
{
    FOwner F; TestEqual(TEXT("Open never dispatches"), F.CT->Reads + F.DT->Reads + F.BT->Reads + F.WT->Writes, 0);
    A::Load(*F.Widget); TestEqual(TEXT("Collection must precede metadata and bag"), F.CT->Reads, 1); TestEqual(TEXT("No early bag read"), F.BT->Reads, 0);
    F.CT->Reply(F.CollectionBody); TestEqual(TEXT("Metadata second"), F.DT->Reads, 1); F.DT->Reply(F.OptionsBody); TestEqual(TEXT("Bag last"), F.BT->Reads, 1); F.BT->Reply(F.ItemsBody);
    TestEqual(TEXT("Only current registration item offered"), F.Client->GetView().Rows.Num(), 1); F.Choose();
    TestTrue(TEXT("Readable consent contains item, mount and consumption"), A::Consent(*F.Widget).Contains(TEXT("승용마 등록증")) && A::Consent(*F.Widget).Contains(TEXT("갈색 말")) && A::Consent(*F.Widget).Contains(TEXT("1개")));
    TestFalse(TEXT("Protocol identifiers not shown"), A::Consent(*F.Widget).Contains(Fixture()->GetStringField(TEXT("candidateItem"))));
    TestEqual(TEXT("Review sends no command"), F.WT->Writes, 0); TestTrue(TEXT("Explicit consent enables final action"), A::CanConfirm(*F.Widget));
    A::Confirm(*F.Widget); A::Confirm(*F.Widget); TestEqual(TEXT("Double click dispatches one durable request"), F.WT->Writes, 1);
    const auto* Pending = F.Commands->GetPending(); TestTrue(TEXT("Original command preserved"), Pending && Pending->IsValid() && Pending->Body == F.WT->LastBody);
    FRPGRidingPendingIntent Stored; TestTrue(TEXT("Journal exists before response"), Pending && FRPGRidingIntentJournal::Load(F.Journal, Pending->Scope, Stored) == ERPGRidingJournalRead::Ready && Stored == *Pending);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationEligibility, "ProjectRPG.Riding.RegistrationUI.Eligibility", Flags)
bool FRidingRegistrationEligibility::RunTest(const FString&)
{
    for (int32 Mode = 0; Mode < 5; ++Mode)
    {
        FOwner F;
        if (Mode == 0) F.bNames = false;
        else
        {
            auto O = Object(F.ItemsBody); TSharedPtr<FJsonObject> Target;
            for (const auto& Row : O->GetArrayField(TEXT("items"))) if (Row->AsObject()->GetObjectField(TEXT("state"))->GetStringField(TEXT("instanceId")) == Fixture()->GetStringField(TEXT("candidateItem"))) Target = Row->AsObject();
            if (!TestNotNull(TEXT("Actual token fixture"), Target.Get())) return false;
            if (Mode == 1) Target->GetObjectField(TEXT("metadata"))->SetBoolField(TEXT("isLocked"), true);
            if (Mode == 2) Target->GetObjectField(TEXT("metadata"))->SetStringField(TEXT("expiresAtUtc"), (FDateTime::UtcNow() - FTimespan::FromDays(1)).ToIso8601());
            if (Mode == 3) Target->GetObjectField(TEXT("location"))->SetStringField(TEXT("containerType"), TEXT("Equipment"));
            if (Mode == 4) Target->GetObjectField(TEXT("owner"))->SetStringField(TEXT("ownerId"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
            F.ItemsBody = Json(O);
        }
        F.Load(); F.Choose(); A::Confirm(*F.Widget); TestEqual(TEXT("Unavailable/missing metadata/foreign items never dispatch"), F.WT->Writes, 0);
    }
    FOwner Owned; auto C = Object(Owned.CollectionBody); auto D = Object(Owned.OptionsBody);
    C->SetArrayField(TEXT("mounts"), Object(Fixture()->GetStringField(TEXT("body")))->GetArrayField(TEXT("mounts"))); C->SetStringField(TEXT("rosterRevision"), TEXT("1"));
    D->SetStringField(TEXT("rosterRevision"), TEXT("1")); D->GetArrayField(TEXT("options"))[0]->AsObject()->SetBoolField(TEXT("alreadyOwned"), true);
    Owned.CollectionBody = Json(C); Owned.OptionsBody = Json(D); Owned.Load(); TestTrue(TEXT("Already owned mount is not offered for consumption"), Owned.Client->GetView().Rows.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationRevalidation, "ProjectRPG.Riding.RegistrationUI.Revalidation", Flags)
bool FRidingRegistrationRevalidation::RunTest(const FString&)
{
    FOwner F; F.Load(); F.Choose(); const auto Token = F.Client->GetView().Review; F.Client->Dismiss();
    TestFalse(TEXT("Dismissed consent cannot be submitted"), F.Client->Confirm(Token)); F.Choose();
    F.CT->ReadCollection(F.C.Character, [](auto) {}); // Transport activity alone is not a new trusted snapshot.
    F.CT->Cancel(); F.Collection->Read(); TestFalse(TEXT("Fresh collection read invalidates prior consent"), F.Client->Confirm(F.Client->GetView().Review));
    F.CT->Reply(F.CollectionBody); F.Load(); F.Choose(); A::Expire(*F.Client); TestFalse(TEXT("Old observations cannot authorize consumption"), F.Client->Confirm(F.Client->GetView().Review));
    TestEqual(TEXT("No stale confirmation writes"), F.WT->Writes, 0);
    F.Load(); F.Choose(); F.bNames = false; A::Confirm(*F.Widget); TestEqual(TEXT("Metadata must still match at final action"), F.WT->Writes, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationContextClose, "ProjectRPG.Riding.RegistrationUI.ContextClose", Flags)
bool FRidingRegistrationContextClose::RunTest(const FString&)
{
    { FOwner F; A::Load(*F.Widget); F.CT->Reply(F.CollectionBody); F.DT->Reply(F.OptionsBody); auto Late = MoveTemp(F.BT->Done);
      F.C.Character = FGuid::NewGuid(); F.Client->RefreshContext(); if (Late) Late({true,true,200,F.ItemsBody,0});
      TestFalse(TEXT("Late old character cannot populate view"), F.Client->GetView().bCanReview); TestEqual(TEXT("Context loss never writes"), F.WT->Writes, 0); }
    FOwner F; F.Load(); F.Choose(); A::Confirm(*F.Widget); const auto* P = F.Commands->GetPending(); if (!P) return false; const auto Original = *P;
    F.Widget->CloseScreen(); F.Client->Shutdown(); TestTrue(TEXT("Closing UI leaves original pending command"), F.Commands->GetPending() && *F.Commands->GetPending() == Original);
    F.Commands->Shutdown(); auto Restored = MakeShared<FRPGRidingCommandCoordinator>(F.WT.ToSharedRef(), [&F] { return FRPGRidingCommandContext{FRPGRidingIntentScope{F.C.Endpoint,F.C.Account,F.C.Character},true}; }, F.Journal);
    TestEqual(TEXT("Restart requires explicit recovery"), Restored->GetState(), ERPGRidingCommandState::RecoveryRequired);
    TestTrue(TEXT("Original UUID/body restored"), Restored->GetPending() && *Restored->GetPending() == Original); TestEqual(TEXT("Restore never resends"), F.WT->Writes, 1); Restored->Shutdown();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationFailures, "ProjectRPG.Riding.RegistrationUI.Failures", Flags)
bool FRidingRegistrationFailures::RunTest(const FString&)
{
    FOwner F; A::Load(*F.Widget); F.CT->Reply(F.CollectionBody); F.DT->Reply(F.OptionsBody); F.BT->Reply(TEXT("private-server-detail"), 503, 30);
    TestFalse(TEXT("Retry-after disables new reads"), F.Client->GetView().bCanReload); A::Load(*F.Widget); TestEqual(TEXT("No polling/bypass"), F.CT->Reads, 1);
    TestFalse(TEXT("Raw errors not displayed"), F.Client->GetView().Status.ToString().Contains(TEXT("private-server-detail")));
    FOwner Bad; Bad.ItemsBody = TEXT("{\"items\":[],\"items\":[]}"); Bad.Load(); TestFalse(TEXT("Duplicate keys cannot create empty success"), Bad.Client->GetView().bCanReview);
    FOwner Retry; Retry.Load(); Retry.Choose(); A::Confirm(*Retry.Widget); const auto Body = Retry.WT->LastBody; Retry.WT->Reply(TEXT("{}"), 500);
    TestFalse(TEXT("Uncertain request blocks new registration"), Retry.Client->GetView().bCanReload); TestTrue(TEXT("Same request retry explicit"), Retry.Client->Retry());
    TestEqual(TEXT("Retry preserves original bytes"), Retry.WT->LastBody, Body); return true;
}
namespace
{
    struct FHttp : TSharedFromThis<FHttp>
    {
        FAutomationTestBase* Test = nullptr; FRPGRidingCollectionContext C; FRPGBackendPlayerAuthState Auth;
        TSharedPtr<FRPGRidingCollectionReader> Collection; TSharedPtr<FRPGRidingRegistrationReader> Definitions;
        TSharedPtr<FRPGRidingCommandCoordinator> Commands; TSharedPtr<FRPGRidingRegistrationClient> Client; TSharedPtr<FRPGRidingFreshRead> Fresh;
        TStrongObjectPtr<URPGRidingCollectionWidget> Widget; TSharedPtr<SWidget> Slate; FGuid Target; int32 Quantity = -1, Stage = 0; double Deadline = 0, RecoverAfter = 0;
        TSharedRef<FRPGHttpRidingOwnerTransport> Transport()
        {
            const TWeakPtr<FHttp> Weak = AsShared(); return MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& O)
            { const auto S = Weak.Pin(); if (!S) return false; O = {S->C.Endpoint,S->C.Account,S->Auth.Authorization(FPlatformTime::Seconds())}; return true; },
            [Weak](const FRPGRidingOwnerHttpContext& O, int32 Code) { auto S = Weak.Pin(); const bool Current = S && O.Endpoint == S->C.Endpoint && O.Account == S->C.Account && S->Auth.MatchesRequest(O.Authorization);
              if (Current) S->Auth.InvalidateIfUnauthorized(O.Authorization, Code); return Current; });
        }
        void Close() { Widget->CloseScreen(); Client->Shutdown(); Fresh->Shutdown(); Collection->Shutdown(); Definitions->Shutdown(); Commands->Shutdown(); Slate.Reset(); Widget.Reset(); }
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingRegistrationUiHttpWait, TSharedPtr<FHttp>, R);
    bool FRidingRegistrationUiHttpWait::Update()
    {
        auto& T = *R->Test; if (FPlatformTime::Seconds() > R->Deadline) { T.AddError(TEXT("Registration UMG/HTTP timed out")); R->Close(); return true; }
        if (R->Stage == 0 && R->Client->GetView().bCanReview)
        { A::Select(*R->Widget); A::Review(*R->Widget); T.TestTrue(TEXT("Actual server-backed consent"), A::CanConfirm(*R->Widget)); A::Confirm(*R->Widget); A::Confirm(*R->Widget); R->Stage = 1; }
        else if (R->Stage == 1 && R->Commands->GetState() == ERPGRidingCommandState::RecoveryRequired)
        { T.TestTrue(TEXT("Lost success keeps original request"), R->Commands->GetPending() && R->Commands->BlocksTransitions()); R->RecoverAfter = FPlatformTime::Seconds() + 1.2; R->Stage = 2; }
        else if (R->Stage == 2 && FPlatformTime::Seconds() > R->RecoverAfter) { A::Recover(*R->Widget); R->Stage = 3; }
        else if (R->Stage == 3 && R->Commands->NeedsFreshReads()) { A::Assets(*R->Widget); R->Stage = 4; }
        else if (R->Stage == 4 && R->Fresh->GetState() == ERPGRidingFreshReadState::Ready)
        { T.TestFalse(TEXT("Current reads release transition barrier"), R->Commands->BlocksTransitions()); T.TestEqual(TEXT("One item consumed"), R->Quantity, 1);
          T.TestTrue(TEXT("Current collection installed in open view"), R->Collection->GetCollection() && R->Collection->GetCollection()->Mounts.Num() == 1); R->Close(); return true; }
        return false;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingRegistrationMemoryHttp, "ProjectRPG.Riding.RegistrationUI.MemoryHttp", Flags)
bool FRidingRegistrationMemoryHttp::RunTest(const FString&)
{
    auto F = Fixture(); auto R = MakeShared<FHttp>(); R->Test = this; R->C = {F->GetStringField(TEXT("endpoint")),F->GetStringField(TEXT("liveAccount")),Id(F,TEXT("liveCharacter")),true};
    R->Auth.SetOpaqueToken(F->GetStringField(TEXT("liveToken"))); R->Target = Id(F,TEXT("liveItem")); const TWeakPtr<FHttp> Weak = R;
    auto Context = [Weak] { auto S=Weak.Pin(); return S ? S->C : FRPGRidingCollectionContext{}; };
    auto CommandContext = [Weak] { auto S=Weak.Pin(); FRPGRidingCommandContext C; if(S){FRPGRidingIntentScope::Create(S->C.Endpoint,S->C.Account,S->C.Character,C.Scope);C.bEligible=S->C.bEligible;} return C; };
    R->Collection = MakeShared<FRPGRidingCollectionReader>(R->Transport(), Context); R->Definitions = MakeShared<FRPGRidingRegistrationReader>(R->Transport(), Context);
    R->Commands = MakeShared<FRPGRidingCommandCoordinator>(R->Transport(),CommandContext,Root());
    FRPGRidingFreshRead::FSink Sink{[](FGuid){return true;},[]{return true;},[Weak](const auto& Items,const auto&)
        { auto S=Weak.Pin();if(!S)return false;S->Quantity=0;for(const auto& I:Items)if(I.GetItemId()==S->Target)S->Quantity=I.GetQuantity();return true; },
        [Weak,Context](const auto& C){auto S=Weak.Pin();if(!S)return false;S->Definitions->Invalidate();return S->Collection->InstallFreshSnapshot(Context(),C);} };
    R->Fresh = MakeShared<FRPGRidingFreshRead>(R->Commands.ToSharedRef(),R->Transport(),CommandContext,MoveTemp(Sink));
    R->Client = MakeShared<FRPGRidingRegistrationClient>(R->Collection.ToSharedRef(),R->Definitions.ToSharedRef(),R->Transport(),R->Commands.ToSharedRef(),Context,Names,[Weak]{auto S=Weak.Pin();return S && S->Fresh->Refresh();});
    R->Widget.Reset(NewObject<URPGRidingCollectionWidget>());R->Widget->SetReadOwner(R->Collection.ToSharedRef(),Mount);R->Widget->SetRegistrationOwner(R->Client.ToSharedRef());R->Slate=R->Widget->TakeWidget();
    A::Load(*R->Widget); R->Deadline=FPlatformTime::Seconds()+25; ADD_LATENT_AUTOMATION_COMMAND(FRidingRegistrationUiHttpWait(R)); return true;
}
#endif
