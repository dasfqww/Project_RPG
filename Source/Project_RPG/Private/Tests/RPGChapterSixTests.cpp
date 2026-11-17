#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterSix.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGChapterSixGate,"ProjectRPG.Narrative.M06.WorldGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGChapterSixGate::RunTest(const FString&)
{
    UWorld* W=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));if(!W)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->InitializeActorsForPlay(FURL());
    auto* T=W->SpawnActor<ARPGChapterSixPatient>();auto* P=W->SpawnActor<APawn>();auto* C=W->SpawnActor<APlayerController>();C->Possess(P);P->SetActorLocation(T->GetActorLocation());
    TestFalse(TEXT("Physical presence without authenticated restored own combat lifetime grants no scene"),T->CanObserve(C));
    TestEqual(TEXT("Server owns stable personal target ID"),T->GetTargetId(),FString(TEXT("CH01.M06.Patient")));
    const auto* D=GetDefault<ARPGChapterSixDaon>();TestTrue(TEXT("Real acceptance entry"),D->HasEntry(TEXT("CH01.M06.Offer")));TestTrue(TEXT("Active briefing entry"),D->HasEntry(TEXT("CH01.D06")));TestTrue(TEXT("Next route is a preview"),D->HasEntry(TEXT("CH01.D07.Preview")));
    RPGTestWorld::Destroy(W);return true;
}
#endif
