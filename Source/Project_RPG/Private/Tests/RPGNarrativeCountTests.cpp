#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeTypes.h"
#include "Narrative/RPGNarrativePresentation.h"
#include "Misc/AutomationTest.h"

namespace RPGNarrativeCountTests
{
const FGuid Character(0x11111111, 0x22222222, 0x33333333, 0x44444444);
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
FString Snapshot(const FString& Counts, bool bDone = false)
{
    return FString::Printf(TEXT(R"({"characterId":"%s","schemaVersion":1,"catalogVersion":1,"revision":"9007199254740993","completedChapters":[],"storyUnlocks":[],"quests":[{"questId":"Q.Hunt","definitionVersion":1,"status":"%s","completedObjectives":%s,"objectives":[{"id":"wolves","text":"Hunt wolves","completed":%s%s}]}],"availableDialogues":[]})"),
        *Character.ToString(), bDone ? TEXT("ReadyToReport") : TEXT("Active"), bDone ? TEXT("[\"wolves\"]") : TEXT("[]"),
        bDone ? TEXT("true") : TEXT("false"), *Counts);
}
}
using namespace RPGNarrativeCountTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCountDecodeTest, "ProjectRPG.Narrative.Count.Decode", Flags)
bool FRPGNarrativeCountDecodeTest::RunTest(const FString&)
{
    FRPGNarrativeSnapshot View;
    TestTrue(TEXT("Partial count decodes"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":1,\"requiredCount\":3")), Character, View));
    if (View.Quests.Num() != 1 || View.Quests[0].Objectives.Num() != 1) return false;
    const auto Partial = View.Quests[0].Objectives[0];
    TestTrue(TEXT("Count capability retained"), Partial.bHasCount); TestEqual(TEXT("Current count"), Partial.CurrentCount, 1);
    TestEqual(TEXT("Required count"), Partial.RequiredCount, 3); TestFalse(TEXT("Partial is not complete"), Partial.bCompleted);
    TestEqual(TEXT("Revision still exact"), View.Revision, 9007199254740993LL);
    TestTrue(TEXT("Zero count accepted"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":0,\"requiredCount\":3")), Character, View));
    TestTrue(TEXT("Completed counter accepted"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":3,\"requiredCount\":3"), true), Character, View));
    TestTrue(TEXT("Completed state retained"), View.Quests[0].Objectives[0].bCompleted);
    TestTrue(TEXT("Backend upper bound accepted"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":10000,\"requiredCount\":10000"), true), Character, View));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCountInvalidTest, "ProjectRPG.Narrative.Count.Invalid", Flags)
bool FRPGNarrativeCountInvalidTest::RunTest(const FString&)
{
    for (const FString Fields : {
        FString(TEXT(",\"currentCount\":1")), FString(TEXT(",\"requiredCount\":3")),
        FString(TEXT(",\"currentCount\":null,\"requiredCount\":3")), FString(TEXT(",\"currentCount\":1,\"requiredCount\":null")),
        FString(TEXT(",\"currentCount\":null")), FString(TEXT(",\"currentCount\":-1,\"requiredCount\":3")),
        FString(TEXT(",\"currentCount\":4,\"requiredCount\":3")), FString(TEXT(",\"currentCount\":0,\"requiredCount\":0")),
        FString(TEXT(",\"currentCount\":1.5,\"requiredCount\":3")), FString(TEXT(",\"currentCount\":1,\"requiredCount\":3.5")),
        FString(TEXT(",\"currentCount\":\"1\",\"requiredCount\":3")), FString(TEXT(",\"currentCount\":1,\"requiredCount\":\"3\"")),
        FString(TEXT(",\"currentCount\":true,\"requiredCount\":3")), FString(TEXT(",\"currentCount\":1,\"requiredCount\":[]")),
        FString(TEXT(",\"currentCount\":1,\"requiredCount\":10001")), FString(TEXT(",\"currentCount\":1e100,\"requiredCount\":3")),
        FString(TEXT(",\"currentCount\":3,\"requiredCount\":3")) })
    {
        FRPGNarrativeSnapshot View;
        TestFalse(TEXT("Invalid count pair is rejected: ")+Fields, FRPGNarrativeCodec::Decode(Snapshot(Fields), Character, View));
    }
    FRPGNarrativeSnapshot View;
    TestFalse(TEXT("Completion cannot contradict count"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":1,\"requiredCount\":3"), true), Character, View));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCountLegacyTest, "ProjectRPG.Narrative.Count.Legacy", Flags)
bool FRPGNarrativeCountLegacyTest::RunTest(const FString&)
{
    for (const FString Fields : {FString(), FString(TEXT(",\"currentCount\":null,\"requiredCount\":null"))})
    {
        FRPGNarrativeSnapshot View;
        TestTrue(TEXT("Legacy or non-count goal accepted"), FRPGNarrativeCodec::Decode(Snapshot(Fields), Character, View));
        if (View.Quests.Num() != 1 || View.Quests[0].Objectives.Num() != 1) return false;
        const auto& Goal = View.Quests[0].Objectives[0];
        TestFalse(TEXT("No fabricated counter"), Goal.bHasCount);
        TestFalse(TEXT("Non-count line has no quantity"), FRPGNarrativePresentation::ObjectiveLine(Goal, 0).ToString().Contains(TEXT("/")));
        TestTrue(TEXT("Completed non-count goal accepted"), FRPGNarrativeCodec::Decode(Snapshot(Fields, true), Character, View));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCountPresentationTest, "ProjectRPG.Narrative.Count.Presentation", Flags)
bool FRPGNarrativeCountPresentationTest::RunTest(const FString&)
{
    FRPGQuestObjectiveView Goal;
    Goal.Text = FText::FromString(TEXT("늑대 처치")); Goal.bHasCount = true; Goal.CurrentCount = 1; Goal.RequiredCount = 3;
    const auto Partial = FRPGNarrativePresentation::ObjectiveLine(Goal, 0).ToString();
    TestTrue(TEXT("Authored label retained"), Partial.Contains(TEXT("늑대 처치")));
    TestTrue(TEXT("Partial quantity visible"), Partial.Contains(TEXT("(1/3)")));
    TestTrue(TEXT("Unfinished marker retained"), Partial.Contains(TEXT("[ ]")));
    Goal.CurrentCount = 3; Goal.bCompleted = true;
    const auto Complete = FRPGNarrativePresentation::ObjectiveLine(Goal, 0).ToString();
    TestTrue(TEXT("Complete quantity visible"), Complete.Contains(TEXT("(3/3)")));
    TestFalse(TEXT("Complete line is not unfinished"), Complete.Contains(TEXT("[ ]")));
    Goal.bHasCount = false; Goal.Text = FText::GetEmpty();
    const auto Legacy = FRPGNarrativePresentation::ObjectiveLine(Goal, 1).ToString();
    TestTrue(TEXT("Fallback label uses objective position"), Legacy.Contains(TEXT("2")));
    TestFalse(TEXT("No quantity when capability absent"), Legacy.Contains(TEXT("/")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGNarrativeCountAtomicTest, "ProjectRPG.Narrative.Count.Atomic", Flags)
bool FRPGNarrativeCountAtomicTest::RunTest(const FString&)
{
    FRPGNarrativeSnapshot View;
    if (!FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":1,\"requiredCount\":3")), Character, View)) return false;
    TestFalse(TEXT("Malformed refresh rejected"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":9,\"requiredCount\":3")), Character, View));
    TestEqual(TEXT("Existing personal progress not overwritten"), View.Quests[0].Objectives[0].CurrentCount, 1);
    TestEqual(TEXT("Existing exact revision retained"), View.Revision, 9007199254740993LL);
    TestFalse(TEXT("Other owner's snapshot rejected"), FRPGNarrativeCodec::Decode(Snapshot(TEXT(",\"currentCount\":0,\"requiredCount\":3")), FGuid::NewGuid(), View));
    TestEqual(TEXT("Cross-owner refresh cannot replace progress"), View.Quests[0].Objectives[0].CurrentCount, 1);
    return true;
}
#endif
