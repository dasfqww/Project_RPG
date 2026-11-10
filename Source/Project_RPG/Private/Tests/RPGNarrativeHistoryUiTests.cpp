#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeHistory.h"
#include "Narrative/RPGNarrativeTransportInterface.h"
#include "UI/Narrative/RPGNarrativeHistoryPanel.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGNarrativeHistoryUiTestAccess
{
    static void Render(URPGNarrativeHistoryPanel* P) { P->Render(); }
    static void Refresh(URPGNarrativeHistoryPanel* P) { if (P->RefreshButton) P->RefreshButton->OnClicked.Broadcast(); }
    static void Destruct(URPGNarrativeHistoryPanel* P) { P->NativeDestruct(); }
    static bool Enabled(URPGNarrativeHistoryPanel* P) { return P->RefreshButton && P->RefreshButton->GetIsEnabled(); }
};
namespace RPGNarrativeHistoryUiTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid Character(1, 2, 3, 4), Alt(5, 6, 7, 8);
FString Entry(const FString& Id, const FString& Selected = TEXT("false"), const FString& Other = TEXT("true"), const FString& Title = TEXT("null"))
{
    return FString::Printf(TEXT(R"({"chapterId":"%s","completedOnSelectedCharacter":%s,"completedOnOtherCharacter":%s,"title":%s})"), *Id, *Selected, *Other, *Title);
}
FString Snapshot(const FString& Chapters, const FGuid Owner = Character, const FString& Version = TEXT("11"))
{
    return FString::Printf(TEXT(R"({"characterId":"%s","catalogVersion":%s,"chapters":%s})"), *Owner.ToString(), *Version, *Chapters);
}
FRPGNarrativeHttpResult Reply(const FString& Body)
{
    FRPGNarrativeHttpResult R; R.bCurrentPlayerContext = R.bTransportSuccessful = true; R.StatusCode = 200; R.Body = Body; return R;
}
struct FTransport final : IRPGNarrativeTransport
{
    FString Account = TEXT("76561198000000001"), Endpoint = TEXT("http://127.0.0.1:1234/api");
    bool bAuthenticated = true, bSend = true, bSynchronous = false;
    int32 Reads = 0, Writes = 0, Cancels = 0;
    FGuid LastCharacter;
    TArray<TFunction<void(FRPGNarrativeHttpResult)>> Callbacks;
    virtual bool GetContext(FString& E, FString& S) const override { E = Endpoint; S = Account; return bAuthenticated; }
    virtual bool Send(FGuid, const FString&, TFunction<void(FRPGNarrativeHttpResult)>) override { ++Writes; return false; }
    virtual bool SendDungeon(FGuid, const FString&, TFunction<void(FRPGNarrativeHttpResult)>) override { ++Writes; return false; }
    virtual bool SendHistory(FGuid Id, TFunction<void(FRPGNarrativeHttpResult)> Done) override
    {
        if (!bSend) return false;
        ++Reads; LastCharacter = Id; Callbacks.Add(MoveTemp(Done));
        if (bSynchronous) Callbacks.Last()(Reply(Snapshot(TEXT("[]"), Id)));
        return true;
    }
    virtual void Cancel() override { ++Cancels; } // Retain old callbacks to deliberately deliver late replies.
    void Complete(int32 I, const FString& Json) { Callbacks[I](Reply(Json)); }
};
struct FFixture
{
    TSharedRef<FTransport> Wire = MakeShared<FTransport>();
    TSharedRef<FGuid> Selected = MakeShared<FGuid>(Character);
    TSharedRef<FRPGNarrativeHistoryClient> Client = MakeShared<FRPGNarrativeHistoryClient>(Wire,
        [S = Selected](FGuid Id) { return *S == Id; });
};
}
using namespace RPGNarrativeHistoryUiTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryCodecTest, "ProjectRPG.Narrative.HistoryUI.Codec", Flags)
bool FRPGHistoryCodecTest::RunTest(const FString&)
{
    FRPGNarrativeAccountHistory V;
    const FString Chapters = TEXT("[") + Entry(TEXT("A"), TEXT("true"), TEXT("true"), TEXT("\"서장\"")) + TEXT(",") + Entry(TEXT("B")) + TEXT("]");
    TestTrue(TEXT("Named and legacy-null titles decode"), FRPGNarrativeAccountHistory::Decode(Snapshot(Chapters), Character, V));
    TestEqual(TEXT("Two chapters"), V.Chapters.Num(), 2);
    if (V.Chapters.Num() != 2) return false;
    TestEqual(TEXT("UTF title"), V.Chapters[0].Title.ToString(), FString(TEXT("서장")));
    TestTrue(TEXT("Both completion flags"), V.Chapters[0].bSelected && V.Chapters[0].bOther);
    TestTrue(TEXT("Legacy title is absent"), V.Chapters[1].Title.IsEmpty());
    TestTrue(TEXT("Empty history is valid"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[]")), Character, V));
    TestTrue(TEXT("Empty list retained"), V.Chapters.IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryInvalidTest, "ProjectRPG.Narrative.HistoryUI.Invalid", Flags)
bool FRPGHistoryInvalidTest::RunTest(const FString&)
{
    FRPGNarrativeAccountHistory V; V.Character = Alt; V.CatalogVersion = 99;
    const FString Good = TEXT("[") + Entry(TEXT("A")) + TEXT("]");
    TestFalse(TEXT("Other character rejected"), FRPGNarrativeAccountHistory::Decode(Snapshot(Good, Alt), Character, V));
    for (const FString Version : {TEXT("0"), TEXT("-1"), TEXT("1.5"), TEXT("\"11\""), TEXT("2147483648"), TEXT("null")})
        TestFalse(TEXT("Strict integer catalog"), FRPGNarrativeAccountHistory::Decode(Snapshot(Good, Character, Version), Character, V));
    for (const FString Invalid : {Entry(TEXT("A"), TEXT("false"), TEXT("false")), Entry(TEXT("A"), TEXT("\"true\"")),
        Entry(TEXT("A"), TEXT("null")), Entry(TEXT("../A")), Entry(TEXT("A"), TEXT("false"), TEXT("true"), TEXT("5")),
        Entry(TEXT("A"), TEXT("false"), TEXT("true"), TEXT("\"   \""))})
        TestFalse(TEXT("Bad entry rejected"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[") + Invalid + TEXT("]")), Character, V));
    TestFalse(TEXT("Duplicate IDs"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[") + Entry(TEXT("A")) + TEXT(",") + Entry(TEXT("A")) + TEXT("]")), Character, V));
    TestFalse(TEXT("Unsorted IDs"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[") + Entry(TEXT("B")) + TEXT(",") + Entry(TEXT("A")) + TEXT("]")), Character, V));
    TestFalse(TEXT("Oversize title"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[") + Entry(TEXT("A"), TEXT("true"), TEXT("false"), TEXT("\"") + FString::ChrN(257, 'x') + TEXT("\"")) + TEXT("]")), Character, V));
    TArray<FString> Many; for (int32 I = 0; I < 257; ++I) Many.Add(Entry(FString::Printf(TEXT("A%03d"), I)));
    TestFalse(TEXT("Bounded chapter list"), FRPGNarrativeAccountHistory::Decode(Snapshot(TEXT("[") + FString::Join(Many, TEXT(",")) + TEXT("]")), Character, V));
    TestEqual(TEXT("Failure does not partially overwrite caller"), V.Character, Alt);
    TestEqual(TEXT("Prior metadata retained on failure"), V.CatalogVersion, 99);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryClientTest, "ProjectRPG.Narrative.HistoryUI.Client", Flags)
bool FRPGHistoryClientTest::RunTest(const FString&)
{
    FFixture F;
    TestTrue(TEXT("GET starts"), F.Client->Refresh(Character));
    TestEqual(TEXT("Loading"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Loading);
    TestFalse(TEXT("Repeated click doesn't replace read"), F.Client->Refresh(Character));
    F.Wire->Complete(0, Snapshot(TEXT("[") + Entry(TEXT("A")) + TEXT("]")));
    TestEqual(TEXT("Ready"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Ready);
    TestTrue(TEXT("Valid view exposed"), F.Client->GetView(Character) && F.Client->GetView(Character)->Chapters.Num() == 1);
    TestTrue(TEXT("Fresh read"), F.Client->Refresh(Character));
    TestNull(TEXT("Old view hidden while refreshing"), F.Client->GetView(Character));
    F.Wire->Complete(0, Snapshot(TEXT("[]")));
    TestEqual(TEXT("Old generation cannot finish new read"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Loading);
    F.Wire->Complete(1, Snapshot(TEXT("[]")));
    TestTrue(TEXT("Genuine empty result exposed"), F.Client->GetView(Character) && F.Client->GetView(Character)->Chapters.IsEmpty());
    TestEqual(TEXT("No choice/dungeon mutations"), F.Wire->Writes, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryIdentityTest, "ProjectRPG.Narrative.HistoryUI.Identity", Flags)
bool FRPGHistoryIdentityTest::RunTest(const FString&)
{
    FFixture F; F.Client->Refresh(Character); F.Wire->Complete(0, Snapshot(TEXT("[]")));
    F.Wire->Account = TEXT("76561198000000002"); TestNull(TEXT("Account switch hides cache"), F.Client->GetView(Character));
    F.Wire->Account = TEXT("76561198000000001"); F.Wire->Endpoint += TEXT("2"); TestNull(TEXT("Endpoint switch hides cache"), F.Client->GetView(Character));
    F.Wire->Endpoint = TEXT("http://127.0.0.1:1234/api"); F.Wire->bAuthenticated = false; TestNull(TEXT("Auth loss hides cache"), F.Client->GetView(Character));
    F.Wire->bAuthenticated = true; *F.Selected = Alt; TestFalse(TEXT("Stale window cannot select old character"), F.Client->Refresh(Character));
    F.Client->Reset(); F.Client->Refresh(Alt); F.Wire->Complete(0, Snapshot(TEXT("[]")));
    TestEqual(TEXT("Old callback ignored after character switch"), F.Client->GetState(Alt), ERPGNarrativeHistoryState::Loading);
    F.Wire->Complete(1, Snapshot(TEXT("[]"), Alt)); TestNotNull(TEXT("Only new character readable"), F.Client->GetView(Alt));
    TestNull(TEXT("Old character view hidden"), F.Client->GetView(Character));
    F.Client->Reset(); TestNull(TEXT("Reset clears view"), F.Client->GetView(Alt));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryFailuresTest, "ProjectRPG.Narrative.HistoryUI.Failures", Flags)
bool FRPGHistoryFailuresTest::RunTest(const FString&)
{
    FFixture F; F.Wire->bSend = false;
    TestFalse(TEXT("Dispatch failure returned"), F.Client->Refresh(Character));
    TestEqual(TEXT("Dispatch failure can retry"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Failed);
    F.Wire->bSend = true;
    for (int32 Code : {0, 401, 403, 409, 503, 200})
    {
        F.Client->Refresh(Character); auto R = Reply(TEXT("malformed")); R.StatusCode = Code; F.Wire->Callbacks.Last()(R);
        TestEqual(TEXT("Failure isn't an empty history"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Failed);
        TestNull(TEXT("Failure doesn't expose view"), F.Client->GetView(Character));
    }
    F.Client->Refresh(Character); auto Stale = Reply(Snapshot(TEXT("[]"))); Stale.bCurrentPlayerContext = false; F.Wire->Callbacks.Last()(Stale);
    TestNull(TEXT("Stale credential result rejected"), F.Client->GetView(Character));
    F.Wire->bSynchronous = true; TestTrue(TEXT("Synchronous completion"), F.Client->Refresh(Character));
    TestNotNull(TEXT("Synchronous valid result isn't overwritten"), F.Client->GetView(Character));
    F.Client->Reset(); F.Wire->bSynchronous = false;
    const auto H = F.Client->OnChanged.AddLambda([S = F.Selected] { *S = Alt; });
    TestFalse(TEXT("Reentrant selection change prevents dispatch"), F.Client->Refresh(Character));
    F.Client->OnChanged.Remove(H); *F.Selected = Character;
    TestEqual(TEXT("Reentrant change cannot leave loading without request"), F.Client->GetState(Character), ERPGNarrativeHistoryState::Failed);
    auto Wire = MakeShared<FTransport>();
    TSharedPtr<FRPGNarrativeHistoryClient> Released = MakeShared<FRPGNarrativeHistoryClient>(Wire, [](FGuid) { return true; });
    TWeakPtr<FRPGNarrativeHistoryClient> Weak = Released;
    Released->OnChanged.AddLambda([&Released] { Released.Reset(); });
    auto* Raw = Released.Get();
    TestTrue(TEXT("Owner can close during loading notification"), Raw->Refresh(Character));
    TestFalse(TEXT("Read client released after callback unwinds"), Weak.IsValid());
    Wire->Complete(0, Snapshot(TEXT("[]"))); // A late reply must not resurrect the closed client.
    Released = MakeShared<FRPGNarrativeHistoryClient>(Wire, [](FGuid) { return true; }); Weak = Released;
    Released->OnChanged.AddLambda([&Released] { Released.Reset(); });
    Released->Reset(); TestFalse(TEXT("Reset notification can release the owner safely"), Weak.IsValid());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHistoryPanelTest, "ProjectRPG.Narrative.HistoryUI.Panel", Flags)
bool FRPGHistoryPanelTest::RunTest(const FString&)
{
    FFixture F;
    TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));
    Game->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/History_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World = Game->GetWorld(); if (!TestNotNull(TEXT("Private world"), World)) return false;
    {
        TStrongObjectPtr<URPGNarrativeHistoryPanel> Panel(CreateWidget<URPGNarrativeHistoryPanel>(Game.Get()));
        if (TestNotNull(TEXT("Panel"), Panel.Get()))
        {
            const auto Slate = Panel->TakeWidget(); Panel->Configure(F.Client, Character);
            TestEqual(TEXT("Opening panel does not issue an implicit request"), F.Wire->Reads, 0);
            TestTrue(TEXT("Read control enabled"), FRPGNarrativeHistoryUiTestAccess::Enabled(Panel.Get()));
            FRPGNarrativeHistoryUiTestAccess::Refresh(Panel.Get());
            TestFalse(TEXT("Loading disables duplicate clicks"), FRPGNarrativeHistoryUiTestAccess::Enabled(Panel.Get()));
            F.Wire->Complete(0, Snapshot(TEXT("[") + Entry(TEXT("Chapter.A"), TEXT("true"), TEXT("true"), TEXT("\"서장\"")) + TEXT(",") + Entry(TEXT("Chapter.B")) + TEXT("]")));
            const auto Text = Panel->GetDisplayedHistory().ToString();
            TestTrue(TEXT("Named completion visible"), Text.Contains(TEXT("서장")) && Text.Contains(TEXT("이 캐릭터 · 다른 캐릭터 완료")));
            TestTrue(TEXT("Untitled content has a readable fallback"), Text.Contains(TEXT("제목 없는 이야기 2")));
            TestFalse(TEXT("Internal chapter keys not used as captions"), Text.Contains(TEXT("Chapter.")));
            F.Wire->Account += TEXT("2"); FRPGNarrativeHistoryUiTestAccess::Render(Panel.Get());
            TestFalse(TEXT("Account change clears rendered titles"), Panel->GetDisplayedHistory().ToString().Contains(TEXT("서장")));
            F.Wire->Account = TEXT("76561198000000001"); FRPGNarrativeHistoryUiTestAccess::Refresh(Panel.Get());
            F.Wire->Complete(1, Snapshot(TEXT("[]")));
            TestTrue(TEXT("Empty result distinct from failure"), Panel->GetDisplayedHistory().ToString().Contains(TEXT("아직 보고 완료한 이야기가 없습니다")));
            FRPGNarrativeHistoryUiTestAccess::Refresh(Panel.Get()); F.Wire->Callbacks.Last()(Reply(TEXT("bad")));
            TestTrue(TEXT("Failure has retry explanation"), Panel->GetDisplayedHistory().ToString().Contains(TEXT("불러오지 못했습니다")));
            FRPGNarrativeHistoryUiTestAccess::Destruct(Panel.Get());
            TestFalse(TEXT("Panel listener removed on close"), F.Client->OnChanged.IsBound());
            TestEqual(TEXT("UI has no mutation transport calls"), F.Wire->Writes, 0);
        }
    }
    Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return true;
}
#endif
