#include "UI/Riding/RPGRidingCollectionWidget.h"
#include "Online/RPGBackendPlayerAuthState.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
struct FRPGRidingCollectionWidgetTestAccess
{
    static void Read(URPGRidingCollectionWidget& W) { W.ReloadButton->OnClicked.Broadcast(); }
    static bool CanRead(URPGRidingCollectionWidget& W) { return W.ReloadButton->GetIsEnabled(); }
    static int32 Count(URPGRidingCollectionWidget& W) { return W.Rows->GetChildrenCount(); }
    static FString Rows(URPGRidingCollectionWidget& W)
    { FString S; for (auto* Child : W.Rows->GetAllChildren()) if (auto* Text = Cast<UTextBlock>(Child)) S += Text->GetText().ToString(); return S; }
    static FString Settings(URPGRidingCollectionWidget& W) { return W.Settings->GetText().ToString(); }
    static FString Status(URPGRidingCollectionWidget& W) { return W.Status->GetText().ToString(); }
    static FString Empty(URPGRidingCollectionWidget& W) { return W.Empty->GetText().ToString(); }
    static void Search(URPGRidingCollectionWidget& W, const FString& Value)
    { const auto Text = FText::FromString(Value); W.Search->SetText(Text); W.Search->OnTextChanged.Broadcast(Text); }
    static void Favorites(URPGRidingCollectionWidget& W, bool Value) { W.FavoritesOnly->SetIsChecked(Value); W.FavoritesChanged(Value); }
    static void Refresh(URPGRidingCollectionWidget& W) { W.RefreshView(); }
    static bool Ticking(URPGRidingCollectionWidget& W) { return W.TickHandle.IsValid(); }
    static bool Escape(URPGRidingCollectionWidget& W)
    { return W.NativeOnKeyDown(FGeometry(), FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0)).IsEventHandled(); }
};
namespace
{
    constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
    TSharedPtr<FJsonObject> Object(const FString& Text)
    { TSharedPtr<FJsonObject> O; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), O); return O; }
    FString Json(const TSharedPtr<FJsonObject>& O)
    { FString Text; FJsonSerializer::Serialize(O.ToSharedRef(), TJsonWriterFactory<>::Create(&Text)); return Text; }
    TSharedPtr<FJsonObject> Fixture()
    {
        FString Text; FFileHelper::LoadFileToString(Text, *FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_FIXTURE")));
        auto O = Object(Text); return O && O->GetStringField(TEXT("run")) == FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_RPG_RIDING_OWNER_RUN")) ? O : nullptr;
    }
    FGuid Id(const TSharedPtr<FJsonObject>& O, const TCHAR* Key) { FGuid G; FGuid::Parse(O->GetStringField(Key), G); return G; }
    FText Name(const FString& Mount) { return Mount == TEXT("horse") ? FText::FromString(TEXT("갈색 말")) : FText::GetEmpty(); }
    struct FFake : IRPGRidingOwnerTransport
    {
        FRPGRidingCollectionContext* Context; FCompletion Done; int32 Reads = 0; bool bAuth = true;
        explicit FFake(FRPGRidingCollectionContext& C) : Context(&C) {}
        virtual bool GetContext(FString& E, FString& A) const override { E = Context->Endpoint; A = Context->Account; return true; }
        virtual bool HasCredential() const override { return bAuth; }
        virtual bool CanSend() const override { return bAuth && !Done; }
        virtual bool ReadCollection(FGuid, FCompletion F) override { if (!CanSend()) return false; ++Reads; Done = MoveTemp(F); return true; }
        virtual void Cancel() override { Done = {}; }
        void Reply(const FString& Body, int32 Code = 200, double Delay = 0) { auto F = MoveTemp(Done); if (F) F({true, true, Code, Body, Delay}); }
    };
    struct FScreen
    {
        FRPGRidingCollectionContext Context;
        TSharedPtr<FFake> Transport;
        TSharedPtr<FRPGRidingCollectionReader> Reader;
        TStrongObjectPtr<URPGRidingCollectionWidget> Widget;
        TSharedPtr<SWidget> Slate;
        FString Body;
        explicit FScreen(bool bEmpty = false)
        {
            auto F = Fixture(); if (!F) return;
            Context = {F->GetStringField(TEXT("endpoint")), F->GetStringField(bEmpty ? TEXT("unownedAccount") : TEXT("account")), Id(F, bEmpty ? TEXT("unownedCharacter") : TEXT("character")), true};
            Body = F->GetStringField(bEmpty ? TEXT("unownedBody") : TEXT("body"));
            Transport = MakeShared<FFake>(Context); Reader = MakeShared<FRPGRidingCollectionReader>(Transport.ToSharedRef(), [this] { return Context; });
            Widget.Reset(NewObject<URPGRidingCollectionWidget>()); Widget->SetReadOwner(Reader.ToSharedRef(), Name); Slate = Widget->TakeWidget();
        }
        ~FScreen() { if (Widget.IsValid()) Widget->CloseScreen(); if (Reader) Reader->Shutdown(); Slate.Reset(); Widget.Reset(); }
        void Load(const FString& Override = FString()) { FRPGRidingCollectionWidgetTestAccess::Read(*Widget); Transport->Reply(Override.IsEmpty() ? Body : Override); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenBrowse, "ProjectRPG.Riding.CollectionScreen.Browse", Flags)
bool FRidingCollectionScreenBrowse::RunTest(const FString&)
{
    using A = FRPGRidingCollectionWidgetTestAccess; FScreen F; if (!F.Widget.IsValid()) return false;
    TestEqual(TEXT("Opening does not dispatch"), F.Transport->Reads, 0); TestTrue(TEXT("Read enabled"), A::CanRead(*F.Widget));
    A::Read(*F.Widget); TestFalse(TEXT("No parallel reload while loading"), A::CanRead(*F.Widget));
    F.Transport->Reply(F.Body); TestEqual(TEXT("Owned row"), A::Count(*F.Widget), 1);
    TestTrue(TEXT("Readable names and saved settings"), A::Rows(*F.Widget).Contains(TEXT("갈색 말")) && A::Settings(*F.Widget).Contains(TEXT("갈색 말")));
    TestFalse(TEXT("Internal identity absent from presentation"), A::Rows(*F.Widget).Contains(TEXT("horse")) || A::Settings(*F.Widget).Contains(F.Context.Character.ToString()));
    FScreen Empty(true); Empty.Load(); TestEqual(TEXT("No synthetic mount"), A::Count(*Empty.Widget), 0); TestFalse(TEXT("Empty collection explained"), A::Empty(*Empty.Widget).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenFilters, "ProjectRPG.Riding.CollectionScreen.Filters", Flags)
bool FRidingCollectionScreenFilters::RunTest(const FString&)
{
    using A = FRPGRidingCollectionWidgetTestAccess; FScreen F; if (!F.Widget.IsValid()) return false;
    auto O = Object(F.Body); auto Mounts = O->GetArrayField(TEXT("mounts")); auto Wolf = Object(Json(Mounts[0]->AsObject())); Wolf->SetStringField(TEXT("mountId"), TEXT("wolf"));
    Mounts.Add(MakeShared<FJsonValueObject>(Wolf)); O->SetArrayField(TEXT("mounts"), Mounts); O->SetStringField(TEXT("rosterRevision"), TEXT("2")); F.Load(Json(O));
    TestEqual(TEXT("Owned with missing presentation is still visible"), A::Count(*F.Widget), 2);
    TestFalse(TEXT("No fallback definition ID in UI"), A::Rows(*F.Widget).Contains(TEXT("wolf")));
    A::Search(*F.Widget, TEXT("말")); TestEqual(TEXT("Name filter"), A::Count(*F.Widget), 1);
    A::Search(*F.Widget, TEXT("없는 이름")); TestEqual(TEXT("Empty filter"), A::Count(*F.Widget), 0); TestFalse(TEXT("Empty search explained"), A::Empty(*F.Widget).IsEmpty());
    A::Search(*F.Widget, TEXT("")); A::Favorites(*F.Widget, true); TestEqual(TEXT("Server favorites only"), A::Count(*F.Widget), 1);
    TestEqual(TEXT("Filters never send HTTP or persist settings"), F.Transport->Reads, 1); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenContext, "ProjectRPG.Riding.CollectionScreen.Context", Flags)
bool FRidingCollectionScreenContext::RunTest(const FString&)
{
    using A = FRPGRidingCollectionWidgetTestAccess; FScreen F; if (!F.Widget.IsValid()) return false;
    F.Load(); int32 Closed = 0; F.Widget->OnClosed.AddLambda([&Closed] { ++Closed; });
    A::Read(*F.Widget); auto Late = MoveTemp(F.Transport->Done); F.Context.Character = FGuid::NewGuid(); A::Refresh(*F.Widget);
    TestEqual(TEXT("Changed selection closes once"), Closed, 1); TestEqual(TEXT("Rows cleared"), A::Count(*F.Widget), 0);
    TestTrue(TEXT("Settings cleared"), A::Settings(*F.Widget).IsEmpty()); TestFalse(TEXT("Ticker removed"), A::Ticking(*F.Widget));
    Late({true, true, 200, F.Body, 0}); TestNull(TEXT("Late read cannot populate new character"), F.Reader->GetCollection());
    F.Widget->CloseScreen(); TestEqual(TEXT("Close idempotent"), Closed, 1); F.Widget->OnClosed.Clear(); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenLifetime, "ProjectRPG.Riding.CollectionScreen.Lifetime", Flags)
bool FRidingCollectionScreenLifetime::RunTest(const FString&)
{
    using A = FRPGRidingCollectionWidgetTestAccess; FScreen F; if (!F.Widget.IsValid()) return false;
    F.Load(); TestTrue(TEXT("Escape handled"), A::Escape(*F.Widget));
    TestFalse(TEXT("Unsubscribed on close"), F.Reader->OnChanged.IsBoundToObject(F.Widget.Get())); TestFalse(TEXT("Stopped polling view"), A::Ticking(*F.Widget));
    TestEqual(TEXT("Close does not read or mutate"), F.Transport->Reads, 1);
    FScreen Auth; Auth.Load(); Auth.Transport->bAuth = false; A::Refresh(*Auth.Widget);
    TestEqual(TEXT("Authentication loss hides screen"), Auth.Widget->GetVisibility(), ESlateVisibility::Collapsed); TestEqual(TEXT("Auth loss clears rows"), A::Count(*Auth.Widget), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenFailures, "ProjectRPG.Riding.CollectionScreen.Failures", Flags)
bool FRidingCollectionScreenFailures::RunTest(const FString&)
{
    using A = FRPGRidingCollectionWidgetTestAccess; FScreen F; if (!F.Widget.IsValid()) return false;
    A::Read(*F.Widget); F.Transport->Reply(TEXT("internal-error-with-private-data"), 500);
    TestTrue(TEXT("Failure is actionable"), A::CanRead(*F.Widget)); TestFalse(TEXT("Server body not displayed"), A::Status(*F.Widget).Contains(TEXT("private-data")));
    A::Read(*F.Widget); F.Transport->Reply(TEXT("{}"), 503, 30); TestFalse(TEXT("Retry-after disables read"), A::CanRead(*F.Widget));
    A::Read(*F.Widget); TestEqual(TEXT("Click cannot bypass server delay"), F.Transport->Reads, 2);
    auto O = Object(F.Body); O->SetBoolField(TEXT("catalogReady"), false); O->SetBoolField(TEXT("registrationEnabled"), false); O->SetBoolField(TEXT("preferencesEnabled"), false);
    O->SetField(TEXT("catalogVersion"), MakeShared<FJsonValueNull>()); O->SetField(TEXT("gameplayHash"), MakeShared<FJsonValueNull>());
    FScreen Disabled; Disabled.Load(Json(O)); TestEqual(TEXT("Disabled catalog still shows ownership"), A::Count(*Disabled.Widget), 1);
    TestTrue(TEXT("Registration availability explained"), A::Status(*Disabled.Widget).Contains(TEXT("설정 변경"))); return true;
}

namespace
{
    struct FHttpScreen
    {
        FAutomationTestBase* Test = nullptr; FRPGBackendPlayerAuthState Auth; FRPGRidingCollectionContext Context;
        TSharedPtr<FRPGHttpRidingOwnerTransport> Transport; TSharedPtr<FRPGRidingCollectionReader> Reader;
        TStrongObjectPtr<URPGRidingCollectionWidget> Widget; TSharedPtr<SWidget> Slate; double Deadline = 0;
        ~FHttpScreen() { if (Widget.IsValid()) Widget->CloseScreen(); if (Reader) Reader->Shutdown(); Slate.Reset(); Widget.Reset(); }
    };
    DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FRidingCollectionHttpViewWait, TSharedPtr<FHttpScreen>, Run);
    bool FRidingCollectionHttpViewWait::Update()
    {
        const auto State = Run->Reader->GetState();
        if (State == ERPGRidingCollectionReadState::Loading && FPlatformTime::Seconds() < Run->Deadline) return false;
        using A = FRPGRidingCollectionWidgetTestAccess;
        Run->Test->TestEqual(TEXT("Actual UMG click to HTTP to reader"), State, ERPGRidingCollectionReadState::Ready);
        Run->Test->TestEqual(TEXT("Actual backend ownership presented"), A::Count(*Run->Widget), 1);
        Run->Test->TestTrue(TEXT("Actual server preferences presented"), A::Settings(*Run->Widget).Contains(TEXT("갈색 말")));
        Run->Widget->CloseScreen(); return true;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRidingCollectionScreenHttp, "ProjectRPG.Riding.CollectionScreen.MemoryHttp", Flags)
bool FRidingCollectionScreenHttp::RunTest(const FString&)
{
    auto F = Fixture(); if (!F) return false; auto Run = MakeShared<FHttpScreen>(); Run->Test = this;
    Run->Context = {F->GetStringField(TEXT("endpoint")), F->GetStringField(TEXT("account")), Id(F, TEXT("character")), true};
    Run->Auth.SetOpaqueToken(F->GetStringField(TEXT("token"))); const TWeakPtr<FHttpScreen> Weak = Run;
    Run->Transport = MakeShared<FRPGHttpRidingOwnerTransport>([Weak](FRPGRidingOwnerHttpContext& C)
    { auto R = Weak.Pin(); if (!R) return false; C = {R->Context.Endpoint, R->Context.Account, R->Auth.Authorization(FPlatformTime::Seconds())}; return true; },
    [Weak](const FRPGRidingOwnerHttpContext& C, int32 Code)
    { auto R = Weak.Pin(); bool bCurrent = R && R->Context.Endpoint == C.Endpoint && R->Context.Account == C.Account && R->Auth.MatchesRequest(C.Authorization);
      if (bCurrent) R->Auth.InvalidateIfUnauthorized(C.Authorization, Code); return bCurrent; });
    Run->Reader = MakeShared<FRPGRidingCollectionReader>(Run->Transport.ToSharedRef(), [Weak]
    { const auto R = Weak.Pin(); return R ? R->Context : FRPGRidingCollectionContext{}; });
    Run->Widget.Reset(NewObject<URPGRidingCollectionWidget>()); Run->Widget->SetReadOwner(Run->Reader.ToSharedRef(), Name); Run->Slate = Run->Widget->TakeWidget();
    FRPGRidingCollectionWidgetTestAccess::Read(*Run->Widget); Run->Deadline = FPlatformTime::Seconds() + 15;
    ADD_LATENT_AUTOMATION_COMMAND(FRidingCollectionHttpViewWait(Run)); return true;
}
#endif
