#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/HUD/RPGHUDLayout.h"
#include "UI/HUD/RPGHUD.h"
#include "UI/Content/RPGTrainingGuideWidget.h"
#include "UI/Narrative/RPGNarrativeQuestTrackerWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHUDReservationsTest, "ProjectRPG.UI.HUDLayout.ReservedRegions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHUDReservationsTest::RunTest(const FString&)
{
    using namespace RPGHUDLayout;
    TestEqual(TEXT("Quest keeps its normal position without the minimap"), RightCardTop(false, false), QuestTop);
    TestEqual(TEXT("Quest begins below the minimap"), RightCardTop(false, true), MinimapTop + MinimapSize + MinimapCardGap);
    TestTrue(TEXT("Quest and practice keep their separation below the minimap"),
        RightCardTop(false, true) + QuestHeight + 12.f <= RightCardTop(true, true));
    for (const FVector2D Viewport : {FVector2D(1280,720), FVector2D(1920,1080), FVector2D(2560,1440)})
    {
        TestTrue(TEXT("Quest ends before practice begins"), QuestTop + QuestHeight + 12.f <= PracticeTop);
        TestTrue(TEXT("Practice ends above the combat band at minimum supported size"), PracticeTop + PracticeHeight <= Viewport.Y - CombatHeight);
        TestTrue(TEXT("Both side panels leave the central combat view clear"), 2.f * (SideWidth + Edge) < Viewport.X - 600.f);
        TestTrue(TEXT("Nearby centered world caption has space"), IsWorldLabelInSafeArea(FVector2D(Viewport.X/2, Viewport.Y/2), Viewport));
        TestFalse(TEXT("Caption cannot paint into quest/practice"), IsWorldLabelInSafeArea(FVector2D(Viewport.X - 150, 150), Viewport));
        TestFalse(TEXT("Caption cannot paint into party list"), IsWorldLabelInSafeArea(FVector2D(150, 150), Viewport));
        TestFalse(TEXT("Caption cannot paint into skills or vitals"), IsWorldLabelInSafeArea(FVector2D(Viewport.X/2, Viewport.Y - 40), Viewport));
    }
    TestFalse(TEXT("Offscreen/invalid projections never get clamped over the HUD"),
        IsWorldLabelInSafeArea(FVector2D(-1,100), FVector2D(1280,720)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHUDGuideContainersTest, "ProjectRPG.UI.HUDLayout.BoundedScrollableGuides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHUDGuideContainersTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
    if (!TestNotNull(TEXT("World"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    {
        TStrongObjectPtr<URPGTrainingGuideWidget> Practice(NewObject<URPGTrainingGuideWidget>(World));
        TStrongObjectPtr<URPGNarrativeQuestTrackerWidget> Quest(NewObject<URPGNarrativeQuestTrackerWidget>(World));
        Quest->Configure(nullptr, FGuid(), false);
        Practice->Initialize(); Quest->Initialize();
        const auto PracticeSlate = Practice->TakeWidget(); const auto QuestSlate = Quest->TakeWidget();
        auto* Card = Cast<UBorder>(Practice->GetWidgetFromName(TEXT("TrainingGuideCard")));
        auto* QuestSize = Cast<USizeBox>(Quest->GetWidgetFromName(TEXT("TrackedQuestCard")));
        if (TestNotNull(TEXT("Practice card"), Card) && TestNotNull(TEXT("Quest card"), QuestSize))
        {
            auto* PracticeScroll = Cast<UScrollBox>(Card->GetParent());
            auto* PracticeSize = PracticeScroll ? Cast<USizeBox>(PracticeScroll->GetParent()) : nullptr;
            TestNotNull(TEXT("Practice text scrolls inside its reserved region"), PracticeScroll);
            TestNotNull(TEXT("Quest text scrolls inside its own card"), Cast<UScrollBox>(CastChecked<UBorder>(QuestSize->GetContent())->GetContent()));
            if (TestNotNull(TEXT("Practice size constraint"), PracticeSize))
                TestEqual(TEXT("Practice cap"), PracticeSize->GetMaxDesiredHeight(), RPGHUDLayout::PracticeHeight);
            TestEqual(TEXT("Quest cap"), QuestSize->GetMaxDesiredHeight(), RPGHUDLayout::QuestHeight);
            TestTrue(TEXT("Missing tracking data does not leave an empty panel"), QuestSize->GetVisibility() == ESlateVisibility::Collapsed);
            TestTrue(TEXT("Practice root allows child scrolling without a fullscreen hit target"), Practice->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
            TestTrue(TEXT("Quest root allows child scrolling without a fullscreen hit target"), Quest->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
        }
        Practice->ReleaseSlateResources(true); Quest->ReleaseSlateResources(true);
    }
    RPGTestWorld::Destroy(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGHUDProgressLayoutTest, "ProjectRPG.UI.HUDLayout.AuthoredActionProgressRegion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGHUDProgressLayoutTest::RunTest(const FString&)
{
    auto* Class=LoadClass<ARPGHUD>(nullptr,TEXT("/Game/Blueprints/UI/PlayerHUD/BP_RPGHUD.BP_RPGHUD_C"));
    if (!TestNotNull(TEXT("Authored HUD configuration"),Class)) return false;
    const auto* HUD=Class->GetDefaultObject<ARPGHUD>();
    TestTrue(TEXT("Action progress follows bottom center at every viewport height"),
        HUD->ActionProgressAnchor.Equals(FVector2D(.5f,1)) && HUD->ActionProgressAlignment.Equals(FVector2D(.5f,1)));
    TestEqual(TEXT("Common action progress has an independent vertical reservation"),float(-HUD->ActionProgressOffset.Y),RPGHUDLayout::ProgressBottom);
    TestTrue(TEXT("Progress ends above the mounted context and combat band"),-HUD->ActionProgressOffset.Y > 256.f);
    return true;
}
#endif
