#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeStorySkip.h"
#include "Narrative/RPGNarrativeHistory.h"
#include "UI/Narrative/RPGNarrativeStorySkipPanel.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGNarrativeSkipUiTestAccess
{
    static void Tick(URPGNarrativeStorySkipPanel* P) { P->NativeTick(FGeometry(), 0.f); }
    static void Destruct(URPGNarrativeStorySkipPanel* P) { P->NativeDestruct(); }
};
namespace RPGNarrativeSkipTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid Character(1, 2, 3, 4), Alt(5, 6, 7, 8);
FString Plan(const FString& Chapter = TEXT("Chapter.A"), const FString& Revision = TEXT("5"), const FGuid Id = Character)
{
    return FString::Printf(TEXT(R"({"characterId":"%s","chapterId":"%s","title":"서장","catalogVersion":21,"expectedRevision":"%s","quests":[{"questId":"Quest.First","title":"남은 의뢰"}],"storyUnlocks":["Story.Next"],"cost":0,"grantsQuestRewards":false,"requiresOriginalTraining":true,"previewHash":"%s"})"),
        *Id.ToString(), *Chapter, *Revision, *FString::ChrN(64, 'a'));
}
FString Snapshot(const FString& Chapter = TEXT("Chapter.A"), const FString& Revision = TEXT("6"), const FGuid Id = Character)
{
    return FString::Printf(TEXT(R"({"characterId":"%s","catalogVersion":21,"schemaVersion":1,"revision":"%s","quests":[],"completedChapters":["%s"],"storyUnlocks":["Story.Next"],"availableDialogues":[],"storySkippedChapters":["%s"]})"),
        *Id.ToString(), *Revision, *Chapter, *Chapter);
}
FRPGNarrativeHttpResult Reply(const FString& Body, const int32 Status = 200)
{
    FRPGNarrativeHttpResult R; R.bCurrentPlayerContext = true; R.bTransportSuccessful = Status != 0; R.StatusCode = Status; R.Body = Body; return R;
}
struct FCall
{
    FString Kind, Body, Chapter;
    FGuid Character;
    TFunction<void(FRPGNarrativeHttpResult)> Done;
};
struct FWire final : IRPGNarrativeTransport
{
    FString Endpoint = TEXT("http://127.0.0.1:1234/api"), Account = TEXT("76561198000000001");
    bool bAuthenticated = true, bSend = true;
    int32 Writes = 0, Previews = 0, Histories = 0, Cancels = 0;
    TArray<FCall> Calls;
    virtual bool GetContext(FString& E, FString& S) const override { E = Endpoint; S = Account; return bAuthenticated; }
    virtual bool Send(FGuid, const FString&, TFunction<void(FRPGNarrativeHttpResult)>) override { return false; }
    virtual bool SendDungeon(FGuid, const FString&, TFunction<void(FRPGNarrativeHttpResult)>) override { return false; }
    virtual bool SendHistory(FGuid Id, TFunction<void(FRPGNarrativeHttpResult)> Done) override
    { if (!bSend) return false; ++Histories; Calls.Add({TEXT("history"), {}, {}, Id, MoveTemp(Done)}); return true; }
    virtual bool SendSkipPreview(FGuid Id, const FString& Chapter, TFunction<void(FRPGNarrativeHttpResult)> Done) override
    { if (!bSend) return false; ++Previews; Calls.Add({TEXT("preview"), {}, Chapter, Id, MoveTemp(Done)}); return true; }
    virtual bool SendSkip(FGuid Id, const FString& Body, TFunction<void(FRPGNarrativeHttpResult)> Done) override
    { if (!bSend) return false; ++Writes; Calls.Add({TEXT("skip"), Body, {}, Id, MoveTemp(Done)}); return true; }
    virtual void Cancel() override { ++Cancels; } // Preserve callbacks to deliver cancelled/late replies deliberately.
    void Complete(int32 Index, FRPGNarrativeHttpResult R) { Calls[Index].Done(MoveTemp(R)); }
};
struct FFixture
{
    TSharedRef<FWire> Wire = MakeShared<FWire>(), HistoryWire = MakeShared<FWire>();
    TSharedPtr<FRPGNarrativeSkipClient> Client;
    TSharedPtr<FRPGNarrativeHistoryClient> History;
    FGuid Selected = Character;
    int64 Revision = 5;
    int32 Catalog = 21, Applied = 0;
    bool bContext = true, bMainBusy = false, bMainPending = false, bDungeon = false;
    TSet<FString> Finished;
    FFixture()
    {
        Client = MakeShared<FRPGNarrativeSkipClient>(Wire,
            [this](FGuid Id, int64& R, int32& C, bool bNew)
            { R = Revision; C = Catalog; return bContext && Id == Selected && !bMainBusy && !bMainPending && (!bNew || !bDungeon); },
            [this](FGuid, const FString& Chapter) { return Finished.Contains(Chapter); },
            [this](FRPGNarrativeSnapshot S) { ++Applied; Revision = FMath::Max(Revision, S.Revision); for (const auto& C : S.CompletedChapters) Finished.Add(C); });
        History = MakeShared<FRPGNarrativeHistoryClient>(HistoryWire, [this](FGuid Id) { return bContext && Id == Selected; });
    }
    void Ready(const FString& Chapter = TEXT("Chapter.A"))
    { Client->Preview(Character, Chapter); Wire->Complete(Wire->Calls.Num() - 1, Reply(Plan(Chapter))); }
    void HistoryReady()
    {
        History->Refresh(Character);
        const FString Body = FString::Printf(TEXT(R"({"characterId":"%s","catalogVersion":21,"chapters":[{"chapterId":"Chapter.A","title":"서장","completedOnSelectedCharacter":false,"completedOnOtherCharacter":true},{"chapterId":"Chapter.B","title":"서장","completedOnSelectedCharacter":false,"completedOnOtherCharacter":true},{"chapterId":"Chapter.C","completedOnSelectedCharacter":true,"completedOnOtherCharacter":false}]})"), *Character.ToString());
        HistoryWire->Complete(HistoryWire->Calls.Num() - 1, Reply(Body));
    }
};
}
using namespace RPGNarrativeSkipTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipCodecTest, "ProjectRPG.Narrative.SkipUI.Codec", Flags)
bool FRPGSkipCodecTest::RunTest(const FString&)
{
    FRPGNarrativeSkipPreview P;
    TestTrue(TEXT("Server preview decoded"), FRPGNarrativeSkipCodec::DecodePreview(Plan(), Character, TEXT("Chapter.A"), P));
    TestEqual(TEXT("Authored title retained"), P.Title.ToString(), FString(TEXT("서장")));
    TestEqual(TEXT("No invented quest completions"), P.Quests.Num(), 1);
    TestTrue(TEXT("Revision above double precision remains exact"), FRPGNarrativeSkipCodec::DecodePreview(Plan(TEXT("Chapter.A"), TEXT("9007199254740993")), Character, TEXT("Chapter.A"), P));
    TestEqual(TEXT("Exact integer"), P.Revision, 9007199254740993LL);
    FString Body; const FGuid Request(10, 20, 30, 40);
    TestTrue(TEXT("Explicit confirmation encoded"), FRPGNarrativeSkipCodec::Encode(P, Request, Body));
    TestTrue(TEXT("Pinned string revision and actual boolean"), Body.Contains(TEXT("\"9007199254740993\"")) && Body.Replace(TEXT(" "), TEXT("")).Contains(TEXT("\"confirmed\":true")));
    TestFalse(TEXT("No client reward/milestone payload"), Body.Contains(TEXT("milestone")) || Body.Contains(TEXT("reward")));
    FRPGNarrativeSkipCodec::DecodePreview(Plan(), Character, TEXT("Chapter.A"), P); FRPGNarrativeSnapshot S;
    TestTrue(TEXT("Confirmed receipt requires skipped chapter metadata"), FRPGNarrativeSkipCodec::DecodeCommit(Snapshot(), P, S));
    TestEqual(TEXT("Authoritative revision"), S.Revision, 6LL);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipInvalidTest, "ProjectRPG.Narrative.SkipUI.Invalid", Flags)
bool FRPGSkipInvalidTest::RunTest(const FString&)
{
    const TArray<FString> Bad = {Plan( TEXT("Chapter.B")), Plan(TEXT("Chapter.A"), TEXT("9223372036854775807")),
        Plan(TEXT("Chapter.A"), TEXT("5"), Alt), Plan().Replace(TEXT("\"cost\":0"), TEXT("\"cost\":1")),
        Plan().Replace(TEXT("\"cost\":0"), TEXT("\"cost\":\"0\"")),
        Plan().Replace(TEXT("\"grantsQuestRewards\":false"), TEXT("\"grantsQuestRewards\":true")),
        Plan().Replace(TEXT("\"grantsQuestRewards\":false"), TEXT("\"grantsQuestRewards\":\"false\"")),
        Plan().Replace(TEXT("\"requiresOriginalTraining\":true"), TEXT("\"requiresOriginalTraining\":false")),
        Plan().Replace(TEXT("\"expectedRevision\":\"5\""), TEXT("\"expectedRevision\":5")),
        Plan().Replace(TEXT("\"catalogVersion\":21"), TEXT("\"catalogVersion\":0")),
        Plan().Replace(TEXT("[\"Story.Next\"]"), TEXT("[\"Story.Next\",\"Story.Next\"]")),
        Plan().Replace(TEXT("\"title\":\"서장\""), TEXT("\"title\":false")), TEXT("not json")};
    for (const auto& Body : Bad)
    {
        FRPGNarrativeSkipPreview P; P.Chapter = TEXT("untouched");
        TestFalse(TEXT("Invalid preview rejected"), FRPGNarrativeSkipCodec::DecodePreview(Body, Character, TEXT("Chapter.A"), P));
        TestEqual(TEXT("Decoder output atomic"), P.Chapter, FString(TEXT("untouched")));
    }
    FRPGNarrativeSkipPreview P; FRPGNarrativeSkipCodec::DecodePreview(Plan(), Character, TEXT("Chapter.A"), P);
    FString Out(TEXT("untouched")); P.Hash = FString::ChrN(64, 'z');
    TestFalse(TEXT("Invalid fingerprint not sent"), FRPGNarrativeSkipCodec::Encode(P, FGuid::NewGuid(), Out));
    TestEqual(TEXT("Encoder output atomic"), Out, FString(TEXT("untouched")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipConfirmTest, "ProjectRPG.Narrative.SkipUI.ConfirmRetry", Flags)
bool FRPGSkipConfirmTest::RunTest(const FString&)
{
    FFixture F;
    TestFalse(TEXT("Confirmation without preview blocked"), F.Client->Confirm(Character));
    F.Client->Preview(Character, TEXT("Chapter.A"));
    TestFalse(TEXT("No confirmation during GET"), F.Client->Confirm(Character));
    F.Wire->Complete(0, Reply(Plan()));
    TestEqual(TEXT("Preview never writes"), F.Wire->Writes, 0);
    TestTrue(TEXT("Explicit confirm sends"), F.Client->Confirm(Character));
    const FString Original = F.Wire->Calls.Last().Body;
    TestFalse(TEXT("Duplicate click blocked"), F.Client->Confirm(Character));
    TestFalse(TEXT("New chapter cannot replace inflight write"), F.Client->Preview(Character, TEXT("Chapter.B")));
    TestEqual(TEXT("No optimistic approval"), F.Applied, 0);
    F.Wire->Complete(1, Reply(TEXT(""), 0));
    TestTrue(TEXT("Lost reply retains pending command"), F.Client->HasPending());
    F.bDungeon = true; F.Revision = 9;
    TestTrue(TEXT("Recovery allowed after revision/session changed"), F.Client->Retry(Character));
    TestEqual(TEXT("Recovery retains identical request ID and body"), F.Wire->Calls.Last().Body, Original);
    F.Wire->Complete(2, Reply(Snapshot()));
    TestEqual(TEXT("Authoritative commit delivered once"), F.Applied, 1);
    TestEqual(TEXT("Later view not rolled back by fixture consumer"), F.Revision, 9LL);
    TestFalse(TEXT("No pending command after receipt"), F.Client->HasPending());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipAuthorityTest, "ProjectRPG.Narrative.SkipUI.Authority", Flags)
bool FRPGSkipAuthorityTest::RunTest(const FString&)
{
    for (int32 Code : {0, 200, 400, 401, 403, 409, 429, 500})
    {
        FFixture F; F.Ready(); F.Client->Confirm(Character); F.Wire->Complete(1, Reply(TEXT("bad"), Code));
        TestTrue(TEXT("Unproven response retains uncertain command"), F.Client->HasPending());
        TestEqual(TEXT("Malformed or unauthorized response never applies"), F.Applied, 0);
    }
    for (const auto& Bad : {Snapshot(TEXT("Chapter.B")), Snapshot(TEXT("Chapter.A"), TEXT("7")), Snapshot(TEXT("Chapter.A"), TEXT("6"), Alt),
        Snapshot().Replace(TEXT("\"storySkippedChapters\""), TEXT("\"wrongField\""))})
    {
        FFixture F; F.Ready(); F.Client->Confirm(Character); F.Wire->Complete(1, Reply(Bad));
        TestTrue(TEXT("Forged/mismatched success retained"), F.Client->HasPending());
    }
    for (const auto* Code : {TEXT("invalid_narrative_state"), TEXT("invalid_story_skip_state"), TEXT("incompatible_narrative_state"),
        TEXT("inconsistent_story_unlocks"), TEXT("idempotency_conflict")})
    {
        FFixture Receipt; Receipt.Ready(); Receipt.Client->Confirm(Character);
        const FString Body = Receipt.Wire->Calls.Last().Body;
        Receipt.Wire->Complete(1, Reply(FString::Printf(TEXT("{\"error\":\"%s\"}"), Code), 409));
        TestTrue(TEXT("Receipt rendering/recovery error retains original command"), Receipt.Client->HasPending());
        Receipt.Client->Retry(Character); TestEqual(TEXT("Recovery error cannot generate a replacement intent"), Receipt.Wire->Calls.Last().Body, Body);
    }
    FFixture F; F.Ready(); F.Client->Confirm(Character);
    F.Wire->Complete(1, Reply(TEXT("{\"error\":\"story_skip_preview_changed\"}"), 409));
    TestFalse(TEXT("Typed non-commit rejection clears command"), F.Client->HasPending());
    TestNull(TEXT("Rejected preview cannot be reused"), F.Client->GetPreview(Character));
    F.bDungeon = true; TestFalse(TEXT("New preview blocked in dungeon"), F.Client->Preview(Character, TEXT("Chapter.A")));
    F.bDungeon = false; F.bMainPending = true; TestFalse(TEXT("Main uncertain write blocks preview"), F.Client->Preview(Character, TEXT("Chapter.A")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipIdentityTest, "ProjectRPG.Narrative.SkipUI.Identity", Flags)
bool FRPGSkipIdentityTest::RunTest(const FString&)
{
    FFixture F; F.Client->Preview(Character, TEXT("Chapter.A")); F.Client->Reset(); F.Client->Preview(Character, TEXT("Chapter.B"));
    F.Wire->Complete(0, Reply(Plan())); TestNull(TEXT("Cancelled reply cannot publish"), F.Client->GetPreview(Character));
    F.Wire->Complete(1, Reply(Plan(TEXT("Chapter.B"))));
    F.Selected = Alt; TestFalse(TEXT("Stale selection cannot confirm"), F.Client->Confirm(Character));
    F.Selected = Character; ++F.Revision; TestFalse(TEXT("Changed revision requires new preview"), F.Client->Confirm(Character));
    --F.Revision; F.Wire->Account += TEXT("2"); TestNull(TEXT("Other account cannot see old review"), F.Client->GetPreview(Character));
    F.Wire->Account = TEXT("76561198000000001"); F.Client->Confirm(Character);
    auto Stale = Reply(Snapshot(TEXT("Chapter.B"))); Stale.bCurrentPlayerContext = false;
    F.Wire->Complete(2, Stale); TestTrue(TEXT("Changed transport context retains pending"), F.Client->HasPending());
    F.Client->Reset(); TestFalse(TEXT("Explicit owner reset clears old pending scope"), F.Client->HasPending());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipLifetimeTest, "ProjectRPG.Narrative.SkipUI.Lifetime", Flags)
bool FRPGSkipLifetimeTest::RunTest(const FString&)
{
    FFixture F;
    F.Client->OnChanged.AddLambda([&F] { if (F.Client && F.Client->IsBusy()) F.Selected = Alt; });
    TestFalse(TEXT("Reentrant selection change sends no read"), F.Client->Preview(Character, TEXT("Chapter.A")));
    TestFalse(TEXT("No stranded loading state"), F.Client->IsBusy()); TestEqual(TEXT("No stale request"), F.Wire->Previews, 0);
    FFixture G;
    G.Client->OnChanged.AddLambda([&G] { G.Client.Reset(); });
    G.Client->Preview(Character, TEXT("Chapter.A"));
    TestFalse(TEXT("Owner can release client during notification"), G.Client.IsValid());
    G.Wire->Complete(0, Reply(Plan())); TestEqual(TEXT("Weak callback after destruction has no side effect"), G.Applied, 0);
    FFixture H; H.Ready();
    H.Client->OnChanged.AddLambda([&H] { if (H.Client->IsBusy()) H.Selected = Alt; });
    TestFalse(TEXT("Reentrant write selection change sends nothing"), H.Client->Confirm(Character));
    TestTrue(TEXT("Original uncertain intent remains"), H.Client->HasPending()); TestFalse(TEXT("No orphan busy state"), H.Client->IsBusy());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkipPanelTest, "ProjectRPG.Narrative.SkipUI.Panel", Flags)
bool FRPGSkipPanelTest::RunTest(const FString&)
{
    FFixture F; TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));
    Game->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Skip_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World = Game->GetWorld(); if (!TestNotNull(TEXT("Private world"), World)) return false;
    {
        TStrongObjectPtr<URPGNarrativeStorySkipPanel> Panel(CreateWidget<URPGNarrativeStorySkipPanel>(Game.Get()));
        const auto Slate = Panel->TakeWidget(); Panel->Configure(F.Client, F.History, Character);
        auto Click = [](URPGNarrativeStorySkipPanel* P, const TCHAR* Name)
        { if (auto* B = Cast<UButton>(P->GetWidgetFromName(Name))) B->OnClicked.Broadcast(); };
        TestEqual(TEXT("Panel open issues no implicit request"), F.Wire->Calls.Num() + F.HistoryWire->Calls.Num(), 0);
        F.HistoryReady();
        auto* Options = Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("StorySkipChapters")));
        if (!TestNotNull(TEXT("Chapter selector"), Options)) return false;
        TestEqual(TEXT("Only other-character completions listed"), Options->GetOptionCount(), 2);
        TestFalse(TEXT("Option labels hide logical keys"), Options->GetOptionAtIndex(0).Contains(TEXT("Chapter.")));
        TestNotEqual(TEXT("Duplicate authored titles remain distinguishable"), Options->GetOptionAtIndex(0), Options->GetOptionAtIndex(1));
        Options->SetSelectedIndex(1); Click(Panel.Get(), TEXT("StorySkipPreview"));
        Click(Panel.Get(), TEXT("StorySkipConfirm")); TestEqual(TEXT("Cannot confirm loading preview"), F.Wire->Writes, 0);
        F.Wire->Complete(0, Reply(Plan(TEXT("Chapter.B"))));
        const FString Text = Panel->GetDisplayedPreview().ToString();
        TestTrue(TEXT("Policy and remaining quests visible"), Text.Contains(TEXT("무료")) && Text.Contains(TEXT("미지급")) && Text.Contains(TEXT("훈련")) && Text.Contains(TEXT("남은 의뢰")));
        TestFalse(TEXT("Internal unlock/quest keys hidden"), Text.Contains(TEXT("Story.Next")) || Text.Contains(TEXT("Quest.First")));
        Options->SetSelectedIndex(0); Click(Panel.Get(), TEXT("StorySkipConfirm")); TestEqual(TEXT("Stale selected chapter cannot confirm"), F.Wire->Writes, 0);
        Options->SetSelectedIndex(1); Click(Panel.Get(), TEXT("StorySkipConfirm")); const FString Body = F.Wire->Calls.Last().Body;
        F.Wire->Complete(1, Reply(TEXT(""), 0));
        FRPGNarrativeSkipUiTestAccess::Destruct(Panel.Get());
        TestTrue(TEXT("Window close preserves uncertain command"), F.Client->HasPending());
        TStrongObjectPtr<URPGNarrativeStorySkipPanel> Reopened(CreateWidget<URPGNarrativeStorySkipPanel>(Game.Get()));
        const auto ReopenedSlate = Reopened->TakeWidget(); Reopened->Configure(F.Client, F.History, Character);
        Click(Reopened.Get(), TEXT("StorySkipRetry")); TestEqual(TEXT("Reopened recovery reuses body"), F.Wire->Calls.Last().Body, Body);
        F.Wire->Complete(2, Reply(Snapshot(TEXT("Chapter.B"))));
        TestEqual(TEXT("Confirmed chapter removed from candidates"), Cast<UComboBoxString>(Reopened->GetWidgetFromName(TEXT("StorySkipChapters")))->GetOptionCount(), 1);
        F.Wire->Account += TEXT("2"); FRPGNarrativeSkipUiTestAccess::Tick(Reopened.Get());
        TestFalse(TEXT("Account change hides prior titles"), Reopened->GetDisplayedPreview().ToString().Contains(TEXT("서장")));
        FRPGNarrativeSkipUiTestAccess::Destruct(Reopened.Get());
        TestFalse(TEXT("Close unsubscribes from history"), F.History->OnChanged.IsBound());
    }
    Game->Shutdown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); return true;
}
#endif
