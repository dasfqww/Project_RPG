#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Development/RPGSoloRaidControl.h"
#include "Development/RPGSoloRaidBot.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSoloRaidControlTest, "ProjectRPG.Development.SoloRaid.ControlRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSoloRaidControlTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("SoloRaid") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Path = Directory / TEXT("control.txt");
    FString Mode, Identity, First;
    TestTrue(TEXT("publish fight"), RPGSoloRaidControl::WriteCommand(Path, TEXT("fight")));
    TestTrue(TEXT("read fight"), RPGSoloRaidControl::ReadCommand(Path, Mode, Identity));
    TestEqual(TEXT("fight mode"), Mode, FString(TEXT("fight")));
    First = Identity;
    TestTrue(TEXT("repeat is another command"), RPGSoloRaidControl::WriteCommand(Path, TEXT("fight")));
    RPGSoloRaidControl::ReadCommand(Path, Mode, Identity);
    TestNotEqual(TEXT("identity changes for repeated commands"), Identity, First);
    TestFalse(TEXT("unknown command rejected"), RPGSoloRaidControl::WriteCommand(Path, TEXT("cheat")));
    RPGSoloRaidControl::WriteAtomic(Path, TEXT("fight 1 extra"));
    TestFalse(TEXT("malformed command cannot execute"), RPGSoloRaidControl::ReadCommand(Path, Mode, Identity));
    RPGSoloRaidControl::WriteAtomic(Path, FString::ChrN(300, TCHAR('x')));
    TestFalse(TEXT("oversized input bounded"), RPGSoloRaidControl::ReadCommand(Path, Mode, Identity));
    TestEqual(TEXT("missing policy defaults to assist"), RPGSoloRaidControl::ReadPolicy(Directory, 2), FString(TEXT("assist")));
    TestTrue(TEXT("per-bot hold"), RPGSoloRaidControl::WritePolicy(Directory, 2, TEXT("hold")));
    TestEqual(TEXT("other bot unchanged"), RPGSoloRaidControl::ReadPolicy(Directory, 1), FString(TEXT("assist")));
    RPGSoloRaidControl::WriteAtomic(Directory / TEXT("bot2.txt"), TEXT("invalid"));
    TestEqual(TEXT("broken policy releases bot input"), RPGSoloRaidControl::ReadPolicy(Directory, 2), FString(TEXT("hold")));
    TestTrue(TEXT("recover policy"), RPGSoloRaidControl::WritePolicy(Directory, 2, TEXT("assist")));
    TestFalse(TEXT("invalid slot rejected"), RPGSoloRaidControl::WritePolicy(Directory, 4, TEXT("assist")));
    // Only this test's newly generated directory; no assets, links or shared caches.
    TestTrue(TEXT("own files removed"), IFileManager::Get().DeleteDirectory(*Directory, false, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSoloRaidDangerTest, "ProjectRPG.Development.SoloRaid.DangerClearance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGSoloRaidDangerTest::RunTest(const FString&)
{
    FRPGBossTelegraphGeometry Circle;
    Circle.bEnabled = true; Circle.Shape.Type = ERPGHitQueryShape::Sphere; Circle.Shape.Radius = 200;
    Circle.QueryTransform.SetLocation(FVector(100, 200, 0));
    TArray<FRPGBossTelegraphGeometry> Dangers{Circle};
    TestTrue(TEXT("capsule edge touching is dangerous"), RPGSoloRaidBot::TouchesDanger(Dangers, FVector(320,200,95), 40));
    TestFalse(TEXT("outside including capsule"), RPGSoloRaidBot::TouchesDanger(Dangers, FVector(380,200,95), 40));
    FRPGBossTelegraphGeometry Box;
    Box.bEnabled = true; Box.Shape.Type = ERPGHitQueryShape::Box;
    Box.Shape.BoxHalfExtent = FVector(80,30,100);
    Box.Shape.LocalOffset = FVector(100,0,0);
    Box.QueryTransform = FTransform(FRotator(0,90,0), FVector(500,500,0));
    TestTrue(TEXT("rotated local offset matches gameplay shape"), RPGSoloRaidBot::TouchesDanger({Box}, FVector(500,600,95), 0));
    TestFalse(TEXT("untransformed center not used"), RPGSoloRaidBot::TouchesDanger({Box}, FVector(600,500,95), 0));
    Circle.QueryTransform.SetLocation(FVector(300,200,0));
    Dangers.Add(Circle);
    FVector Escape;
    TestTrue(TEXT("overlapping hazards have escape"), RPGSoloRaidBot::FindEscape(Dangers, FVector(200,200,95), 45, Escape));
    TestFalse(TEXT("escape outside every hazard with clearance"), RPGSoloRaidBot::TouchesDanger(Dangers, Escape, 75));
    TestFalse(TEXT("safe location needs no evasion"), RPGSoloRaidBot::FindEscape(Dangers, FVector(2000,2000,95),45,Escape));
    return true;
}
#endif
