#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Progression/RPGIntroOpportunity.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"
#include "UI/Lobby/RPGOnlineLobbyDevelopmentProfile.h"

struct FRPGIntroOpportunityTestAccess
{
    static void Forge(URPGIntroOpportunityObjectiveComponent& C) { C.Progress.bActive=true; C.Progress.Completed=2; }
    static void Refresh(URPGIntroOpportunityObjectiveComponent& C) { C.RefreshScope(); }
    static bool Credit(URPGIntroOpportunityObjectiveComponent& C, ARPGIntroOpportunityTarget& T) { return C.Credit(T,1); }
    static void Open(ARPGIntroOpportunityTarget& T) { T.Phase=ERPGOpportunityPhase::Open; T.Round=1; T.Deadline=100000; }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroOpportunityFailClosed, "ProjectRPG.Progression.Intro2.FailClosed", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGIntroOpportunityFailClosed::RunTest(const FString&)
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));
    if(!W)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W); W->InitializeActorsForPlay(FURL());
    auto* P=W->SpawnActor<APawn>(); auto* C=NewObject<URPGIntroOpportunityObjectiveComponent>(P);
    P->AddInstanceComponent(C); C->RegisterComponent();
    // Spatial presence or forged progress without a current authenticated dedicated-server lifetime grants nothing.
    FRPGIntroOpportunityTestAccess::Forge(*C); FRPGIntroOpportunityTestAccess::Refresh(*C);
    TestFalse(TEXT("Forged facts discarded"),C->GetProgress().bActive); TestEqual(TEXT("Count cleared"),C->GetProgress().Completed,0);
    auto* T=W->SpawnActorDeferred<ARPGIntroOpportunityTarget>(ARPGIntroOpportunityTarget::StaticClass(),FTransform::Identity);
    T->AutoPossessAI=EAutoPossessAI::Disabled; T->FinishSpawning(FTransform::Identity);
    TestTrue(TEXT("Native target starts its phase tick"),T->PrimaryActorTick.bCanEverTick&&T->PrimaryActorTick.bStartWithTickEnabled);
    FRPGIntroOpportunityTestAccess::Open(*T);
    TestFalse(TEXT("Open phase alone is insufficient"),T->IsWindowOpen());
    TestFalse(TEXT("Unadmitted pawn cannot credit"),FRPGIntroOpportunityTestAccess::Credit(*C,*T));
    using namespace RPGOnlineLobbyDevelopmentProfile;
    const auto Profile=Resolve(TEXT("-RPGDungeonProfile=Intro2"),true);
    TestTrue(TEXT("Intro2 opt-in keeps prior stages"),Profile.bTraining&&Profile.bIntro1&&Profile.bIntro2&&Profile.Error.IsEmpty());
    TestEqual(TEXT("Fresh character trains"),Profile.EntryDungeonId(0),FString(TEXT("Progression.Training")));
    TestEqual(TEXT("Training receipt opens Intro1"),Profile.EntryDungeonId(1),FString(TEXT("Progression.Intro1")));
    TestEqual(TEXT("Intro1 receipt opens Intro2"),Profile.EntryDungeonId(3),FString(TEXT("Progression.Intro2")));
    TestFalse(TEXT("Production gate closed"),Resolve(TEXT("-RPGDungeonProfile=Intro2"),false).Error.IsEmpty());
    TestFalse(TEXT("Duplicate opt-in closed"),Resolve(TEXT("-RPGDungeonProfile=Intro2 -RPGDungeonProfile=Training"),true).Error.IsEmpty());
    TestTrue(TEXT("Owner component replicates"),C->GetIsReplicated());
    for(TFieldIterator<UFunction> It(C->GetClass());It;++It)
        TestFalse(TEXT("No client/server RPC can submit progress"),It->GetOuterUClass()==C->GetClass()&&It->HasAnyFunctionFlags(FUNC_Net));
    RPGTestWorld::Destroy(W); return true;
}
#endif
