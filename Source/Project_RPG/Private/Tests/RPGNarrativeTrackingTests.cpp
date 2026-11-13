#if WITH_DEV_AUTOMATION_TESTS
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "UI/Narrative/RPGNarrativeQuestTrackerWidget.h"
#include "Misc/AutomationTest.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGNarrativeTrackingTestAccess
{
    static void Destruct(URPGNarrativeQuestTrackerWidget* W) { W->NativeDestruct(); }
};
namespace RPGNarrativeTrackingTests
{
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
const FGuid Character(1,2,3,4), AltCharacter(5,6,7,8);
struct FFixture
{
    FRPGNarrativeTrackingContext Current;
    bool bAuthenticated = true;
    TSharedPtr<FRPGNarrativeQuestTracking> Model;
    FFixture()
    {
        Current.Character = Character; Current.Endpoint = TEXT("http://owner/api"); Current.Account = TEXT("player"); Current.bReadable = true;
        FRPGQuestProgress Quest; Quest.QuestId = TEXT("Internal.Main"); Quest.Title = FText::FromString(TEXT("구조 임무")); Quest.Category = ERPGQuestCategory::Main;
        FRPGQuestObjectiveView Goal; Goal.Id = TEXT("Internal.Goal"); Goal.Text = FText::FromString(TEXT("늑대 처치")); Goal.bHasCount = true; Goal.CurrentCount = 1; Goal.RequiredCount = 3;
        Quest.Objectives.Add(Goal); Current.Quests.Add(Quest);
        Quest.QuestId = TEXT("Internal.Side"); Quest.Category = ERPGQuestCategory::Side; Current.Quests.Add(Quest); // same title, distinct choices
        Quest.QuestId = TEXT("Internal.Done"); Quest.Status = ERPGQuestStatus::Completed; Current.Quests.Add(Quest);
        Model = MakeShared<FRPGNarrativeQuestTracking>([this](auto& Out) { Out = Current; return bAuthenticated; });
    }
};
void Click(URPGNarrativeQuestTrackerWidget* Widget, const TCHAR* Name)
{ if (auto* Button = Cast<UButton>(Widget->GetWidgetFromName(Name))) Button->OnClicked.Broadcast(); }
}
using namespace RPGNarrativeTrackingTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrackingSelectionTest,"ProjectRPG.Narrative.Tracking.Selection",Flags)
bool FRPGTrackingSelectionTest::RunTest(const FString&)
{
    FFixture F; FRPGQuestProgress View;
    TestFalse(TEXT("Never auto-select a quest"),F.Model->Read(View));
    TestFalse(TEXT("Unknown quest rejected"),F.Model->Track(Character,TEXT("Missing")));
    TestFalse(TEXT("ID comparison is exact"),F.Model->Track(Character,TEXT("internal.main")));
    TestFalse(TEXT("Completed quest cannot be pinned"),F.Model->Track(Character,TEXT("Internal.Done")));
    TestTrue(TEXT("Explicit active main pin"),F.Model->Track(Character,TEXT("Internal.Main")));
    TestTrue(TEXT("Authoritative read"),F.Model->Read(View));TestEqual(TEXT("Exact count"),View.Objectives[0].CurrentCount,1);
    TestTrue(TEXT("Optional side may be pinned"),F.Model->Track(Character,TEXT("Internal.Side")));
    TestTrue(TEXT("Side read"),F.Model->Read(View));TestTrue(TEXT("Side category"),View.Category==ERPGQuestCategory::Side);
    TestTrue(TEXT("Clear is local"),F.Model->Clear(Character));TestFalse(TEXT("Clear removes display"),F.Model->Read(View));
    TestEqual(TEXT("Snapshot unchanged by controls"),F.Current.Quests[0].Objectives[0].CurrentCount,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrackingIdentityTest,"ProjectRPG.Narrative.Tracking.Identity",Flags)
bool FRPGTrackingIdentityTest::RunTest(const FString&)
{
    FFixture F;FRPGQuestProgress View;
    F.Model->Track(Character,TEXT("Internal.Main"));
    F.Current.Character = AltCharacter;
    TestFalse(TEXT("AltCharacter character cannot see pin"),F.Model->Read(View));
    TestFalse(TEXT("Old control cannot change new character"),F.Model->Track(Character,TEXT("Internal.Side")));
    TestFalse(TEXT("New character cannot clear old pin"),F.Model->Clear(AltCharacter));
    F.Current.Character = Character;TestTrue(TEXT("Returning original owner restores pin"),F.Model->Read(View));
    F.Current.Account = TEXT("another");TestFalse(TEXT("Account isolation"),F.Model->Read(View));
    TestFalse(TEXT("Another account cannot clear prior pin"),F.Model->Clear(Character));
    F.Current.Account = TEXT("player");F.Current.Endpoint += TEXT("/other");TestFalse(TEXT("Endpoint isolation"),F.Model->Read(View));
    F.Current.Endpoint = TEXT("http://owner/api");F.bAuthenticated = false;
    TestFalse(TEXT("Auth loss hides pin"),F.Model->Read(View));TestFalse(TEXT("Unauthenticated tracking blocked"),F.Model->Track(Character,TEXT("Internal.Side")));
    F.bAuthenticated = true;TestTrue(TEXT("Same owner recovery"),F.Model->Read(View));
    F.Model->Reset();TestFalse(TEXT("Shutdown clears selection"),F.Model->Read(View));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrackingProgressTest,"ProjectRPG.Narrative.Tracking.Progress",Flags)
bool FRPGTrackingProgressTest::RunTest(const FString&)
{
    FFixture F;FRPGQuestProgress View;F.Model->Track(Character,TEXT("Internal.Main"));
    F.Current.bReadable=false;F.Current.Quests.Reset();TestFalse(TEXT("Unreadable cache hides HUD"),F.Model->Read(View));
    TestFalse(TEXT("No cache cannot replace pin"),F.Model->Track(Character,TEXT("Internal.Side")));
    TestEqual(TEXT("No cache never retires pin"),F.Model->PinnedId(),FString(TEXT("Internal.Main")));
    F.Current.bReadable=true;FRPGQuestProgress Updated;Updated.QuestId=TEXT("Internal.Main");Updated.Status=ERPGQuestStatus::ReadyToReport;F.Current.Quests.Add(Updated);
    TestTrue(TEXT("Ready-to-report remains tracked"),F.Model->Read(View));TestTrue(TEXT("Report status comes from snapshot"),View.Status==ERPGQuestStatus::ReadyToReport);
    F.Current.Quests[0].Status=ERPGQuestStatus::Completed;TestFalse(TEXT("Authoritative report retires pin"),F.Model->Read(View));
    TestTrue(TEXT("No automatic next quest"),F.Model->PinnedId().IsEmpty());
    F.Current.Quests[0].Status=ERPGQuestStatus::Active;F.Model->Track(Character,TEXT("Internal.Main"));F.Current.Quests.Reset();
    TestFalse(TEXT("Authoritative removal/skip retires pin"),F.Model->Read(View));TestTrue(TEXT("Removal does not select another"),F.Model->PinnedId().IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrackingWidgetTest,"ProjectRPG.Narrative.Tracking.Widget",Flags)
bool FRPGTrackingWidgetTest::RunTest(const FString&)
{
    FFixture F;TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>(GEngine));
    Game->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/Tracking_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
    UWorld* World=Game->GetWorld();if(!TestNotNull(TEXT("Private world"),World))return false;
    {
        TStrongObjectPtr<URPGNarrativeQuestTrackerWidget> Panel(CreateWidget<URPGNarrativeQuestTrackerWidget>(Game.Get()));
        Panel->Configure(F.Model,Character,true);const auto Slate=Panel->TakeWidget();
        TStrongObjectPtr<URPGNarrativeQuestTrackerWidget> Hud(CreateWidget<URPGNarrativeQuestTrackerWidget>(Game.Get()));
        Hud->Configure(F.Model,FGuid(),false);const auto HudSlate=Hud->TakeWidget();
        auto* Combo=Cast<UComboBoxString>(Panel->GetWidgetFromName(TEXT("TrackedQuestOptions")));
        if(!TestNotNull(TEXT("Actual quest selector"),Combo))return false;
        TestEqual(TEXT("Only unfinished quests listed"),Combo->GetOptionCount(),2);
        TestFalse(TEXT("Logical IDs hidden"),Combo->GetOptionAtIndex(0).Contains(TEXT("Internal.")));
        TestNotEqual(TEXT("Duplicate titles distinguishable"),Combo->GetOptionAtIndex(0),Combo->GetOptionAtIndex(1));
        TestTrue(TEXT("HUD initially empty"),Hud->GetDisplayedTracking().IsEmpty());
        Click(Panel.Get(),TEXT("TrackQuest"));
        TestTrue(TEXT("HUD receives local pin"),Hud->GetDisplayedTracking().ToString().Contains(TEXT("(1/3)")));
        TestTrue(TEXT("HUD cannot intercept input"),Hud->GetVisibility()==ESlateVisibility::HitTestInvisible);
        TestNull(TEXT("HUD has no buttons"),Hud->GetWidgetFromName(TEXT("TrackQuest")));
        FRPGNarrativeTrackingTestAccess::Destruct(Panel.Get());
        F.Current.Quests[0].Objectives[0].CurrentCount=2;
        TestTrue(TEXT("Closed journal still updates HUD"),Hud->GetDisplayedTracking().ToString().Contains(TEXT("(2/3)")));
        TStrongObjectPtr<URPGNarrativeQuestTrackerWidget> Reopened(CreateWidget<URPGNarrativeQuestTrackerWidget>(Game.Get()));
        Reopened->Configure(F.Model,Character,true);const auto ReopenedSlate=Reopened->TakeWidget();
        TestTrue(TEXT("Reopen keeps pin"),Reopened->GetDisplayedTracking().ToString().Contains(TEXT("(2/3)")));
        F.Current.Quests[0].Status=ERPGQuestStatus::ReadyToReport;
        TestTrue(TEXT("Report instruction"),Hud->GetDisplayedTracking().ToString().Contains(TEXT("보고해 주세요")));
        F.Current.Character=AltCharacter;Click(Reopened.Get(),TEXT("UntrackQuest"));
        TestTrue(TEXT("AltCharacter character hides HUD immediately"),Hud->GetDisplayedTracking().IsEmpty());
        TestEqual(TEXT("Stale clear preserved original pin"),F.Model->PinnedId(),FString(TEXT("Internal.Main")));
        F.Current.Character=Character;Click(Reopened.Get(),TEXT("UntrackQuest"));TestTrue(TEXT("Explicit unpin hides HUD"),Hud->GetDisplayedTracking().IsEmpty());
        FRPGNarrativeTrackingTestAccess::Destruct(Reopened.Get());FRPGNarrativeTrackingTestAccess::Destruct(Hud.Get());
    }
    Game->Shutdown();GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return true;
}
#endif
