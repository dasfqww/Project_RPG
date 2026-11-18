#if WITH_DEV_AUTOMATION_TESTS
#include "Achievements/RPGAchievementSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGAchievementSnapshotTest, "ProjectRPG.Achievements.SnapshotAndNotifications",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGAchievementSnapshotTest::RunTest(const FString&)
{
    const FGuid Character = FGuid::NewGuid();
    const FGuid Roster = FGuid::NewGuid();
    const auto Body = [&](const TCHAR* Count, const TCHAR* Stamp)
    {
        return FString::Printf(TEXT(R"({"characterId":"%s","rosterId":"%s","entries":[{"id":"mine.ten","title":"폐광 일반 10회 클리어","target":10,"progress":%s,"earnedAt":%s}]})"),
            *Character.ToString(), *Roster.ToString(), Count, Stamp);
    };
    FRPGAchievementSnapshot Nine, Ten;
    const FString Completed = Body(TEXT("10"), TEXT("\"2026-11-19T01:02:03.445678+00:00\""));
    TestTrue(TEXT("Pending projection at nine"), FRPGAchievementSnapshot::Parse(Body(TEXT("9"), TEXT("null")), Character, Nine));
    TestTrue(TEXT("Postgres threshold date"), FRPGAchievementSnapshot::Parse(Completed, Character, Ten));
    TestEqual(TEXT("First observation is silent"), Ten.NewlyEarnedSince({}).Num(), 0);
    TestEqual(TEXT("Nine to ten shows one notification"), Ten.NewlyEarnedSince(Nine).Num(), 1);
    TestEqual(TEXT("Repeated snapshot is silent"), Ten.NewlyEarnedSince(Ten).Num(), 0);
    Nine.Roster = FGuid::NewGuid();
    TestEqual(TEXT("Different roster is silent"), Ten.NewlyEarnedSince(Nine).Num(), 0);
    TestFalse(TEXT("Wrong character response"), FRPGAchievementSnapshot::Parse(Completed, FGuid::NewGuid(), Ten));
    TestFalse(TEXT("Completed count requires its date"), FRPGAchievementSnapshot::Parse(Body(TEXT("10"), TEXT("null")), Character, Ten));
    TestFalse(TEXT("Fractional progress rejected"), FRPGAchievementSnapshot::Parse(Body(TEXT("8.5"), TEXT("null")), Character, Ten));
    return true;
}
#endif
