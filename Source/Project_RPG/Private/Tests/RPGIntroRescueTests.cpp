#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Progression/RPGIntroRescue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"
#include "UI/Lobby/RPGTutorialEntryPolicy.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroRescueAuthority, "ProjectRPG.Progression.Intro3.Authority", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGIntroRescueAuthority::RunTest(const FString&)
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));
    if(!W)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->InitializeActorsForPlay(FURL());
    auto* Pawn=W->SpawnActor<APawn>();auto* C=NewObject<URPGIntroRescueObjectives>(Pawn);
    Pawn->AddInstanceComponent(C);C->RegisterComponent();
    TestFalse(TEXT("An unadmitted pawn cannot participate"),C->IsReady());
    TestFalse(TEXT("Spatial presence cannot finish"),C->GetProgress().IsComplete());
    TestTrue(TEXT("Progress replicates"),C->GetIsReplicated());
    for(TFieldIterator<UFunction> It(C->GetClass());It;++It)
        TestFalse(TEXT("No progress submission RPC"),It->GetOuterUClass()==C->GetClass()&&It->HasAnyFunctionFlags(FUNC_Net));
    const auto* Target=GetDefault<ARPGIntroRescuePawn>();
    TestTrue(TEXT("Native threat tick starts"),Target->PrimaryActorTick.bCanEverTick&&Target->PrimaryActorTick.bStartWithTickEnabled);
    using namespace RPGOnlineLobbyDevelopmentProfile;
    const auto P=Resolve(TEXT("-RPGDungeonProfile=Intro3"),true);
    TestTrue(TEXT("Four explicit stages"),P.bTraining&&P.bIntro1&&P.bIntro2&&P.bIntro3&&P.Error.IsEmpty());
    TestEqual(TEXT("Intro2 receipt required"),P.EntryDungeonId(3),FString(TEXT("Progression.Intro2")));
    TestEqual(TEXT("Intro3 unlocked"),P.EntryDungeonId(7),FString(TEXT("Progression.Intro3")));
    TestFalse(TEXT("Production rejects development selection"),Resolve(TEXT("-RPGDungeonProfile=Intro3"),false).Error.IsEmpty());
    FRPGDungeonSession Session;
    TestEqual(TEXT("Explicit Intro3 selection can create"),FRPGTutorialEntryPolicy::Decide(Session,TEXT("hero"),false,false,false,TEXT("Progression.Intro3")),ERPGTutorialEntryAction::Create);
    TestEqual(TEXT("Unresolved departure still blocks Intro3"),FRPGTutorialEntryPolicy::Decide(Session,TEXT("hero"),false,true,false,TEXT("Progression.Intro3")),ERPGTutorialEntryAction::Reject);
    RPGTestWorld::Destroy(W);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroRescueBundle, "ProjectRPG.Progression.Intro3.Bundle", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGIntroRescueBundle::RunTest(const FString&)
{
    FRPGIntroRescueProgress P;P.bActive=true;P.Phase=ERPGRescuePhase::Complete;
    P.bContribution=true;TestFalse(TEXT("Contribution alone cannot clear"),P.IsComplete());
    P.bDefense=true;TestFalse(TEXT("Escape still required"),P.IsComplete());P.bEscaped=true;
    TestTrue(TEXT("Whole bundle"),P.IsComplete());
    P.Phase=ERPGRescuePhase::Failed;TestFalse(TEXT("Failed attempt never clears"),P.IsComplete());
    P.Phase=ERPGRescuePhase::Escape;TestFalse(TEXT("Personal facts before whole-party completion cannot clear"),P.IsComplete());
    P.Phase=ERPGRescuePhase::Complete;P.bActive=false;TestFalse(TEXT("Stale inactive facts cannot clear"),P.IsComplete());return true;
}
#endif
