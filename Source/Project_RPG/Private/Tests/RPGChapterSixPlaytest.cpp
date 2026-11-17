#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterSix.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGQuestManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "UnrealClient.h"
namespace ChapterSixPlaytest
{
struct FProbe
{
    FString Character,Steam,Session,Screenshot;int32 Stage=0;bool bContinue=false,bPreviewShown=false,bRecall=false,bWrongPointTried=false;
    double Until=FPlatformTime::Seconds()+600,Next=0;
    bool Fail(const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("M06_PLAY FAIL %s Stage=%d"),Why,Stage);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();if(Now>Until)return Fail(TEXT("Timeout"));
        UWorld* W=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.World()&&C.World()->IsGameWorld()){W=C.World();break;}
        if(!W||!W->GetGameInstance()||W->GetNetMode()==NM_DedicatedServer)return true;
        auto* GI=W->GetGameInstance();auto* Web=GI->GetSubsystem<UHttpWebManager>();auto* D=GI->GetSubsystem<URPGDungeonSessionSubsystem>();
        if(!Web||!D||!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))return Fail(TEXT("PrivateContext"));
        if(Stage==0){GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);Stage=1;return true;}
        if(Stage==1){if(!Web->HasAccessToken())return true;if(!D->SelectCharacter(Character)||!D->ResumeDungeonSession())return Fail(TEXT("Resume"));Stage=2;return true;}
        if(Stage==2){if(D->IsRequestInFlight())return true;auto S=D->GetCurrentDungeonSession();if(S.DungeonSessionId!=Session||S.State!=TEXT("InProgress"))return Fail(TEXT("Session"));if(!D->ConnectToDungeon(W->GetFirstPlayerController(),S.ServerAddress))return Fail(TEXT("Connect"));Stage=3;return true;}
        if(W->GetNetMode()!=NM_Client)return true;
        auto* PC=W->GetFirstPlayerController();APawn* P=PC?PC->GetPawn():nullptr;auto* PS=PC?PC->GetPlayerState<ARPGPlayerState>():nullptr;
        if(!P||!PS||!PS->HasAuthenticatedCharacter())return true;
        auto* Flow=PC->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>();FGuid Id;FGuid::Parse(Character,Id);
        if(Stage==3){if(!Flow->SelectCharacter(Id)||!Flow->Refresh())return true;Stage=4;return true;}
        if(Flow->IsBusy()||Flow->HasPendingChoice()||!Flow->HasCurrentSnapshot())return true;
        const auto Quests=Flow->GetQuestManager()->GetQuests(Id);const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M06");});
        if(Stage==8){if(Now<Next)return true;FPlatformMisc::RequestExitWithStatus(false,0);return false;}
        auto Move=[&](FVector Goal){FVector D=Goal-P->GetActorLocation();D.Z=0;PC->SetControlRotation((Goal-P->GetActorLocation()).Rotation());if(D.Size()<10)return true;P->AddMovementInput(D.GetSafeNormal(),1);return false;};
        auto Input=[&](){auto* I=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();auto* A=LoadObject<UInputAction>(nullptr,TEXT("/Game/Blueprints/Character/Player/Input/Actions/IA_PickUp.IA_PickUp"));if(!I||!A)return false;I->GetPlayerInput()->InjectInputForAction(A,FInputActionValue(true));return true;};
        ARPGChapterSixDaon* NPC=nullptr;for(TActorIterator<ARPGChapterSixDaon> It(W);It;++It){NPC=*It;break;}if(!NPC)return true;
        if(Stage==4){if(bContinue&&(!Q||!Q->CompletedObjectives.Contains(TEXT("survivors"))))return Fail(TEXT("PersonalSceneRestore"));UE_LOG(LogTemp,Display,TEXT("M06_PLAY RESTORED Survivors=%d Recall=%d"),bContinue,bRecall);Stage=5;}
        if(Stage==5)
        {
            if(Q&&Q->CompletedObjectives.Contains(TEXT("briefing"))){if(auto* Panel=NPC->GetOpenDialogue(PC))Panel->CloseScreen();Stage=6;Next=Now+.5;return true;}
            if(!Move(NPC->GetActorLocation()-FVector(150,0,0)))return true;if(Now<Next)return true;
            if(auto* Panel=NPC->GetOpenDialogue(PC))
            {TArray<UWidget*> Widgets;Panel->WidgetTree->GetAllWidgets(Widgets);
             for(auto* Widget:Widgets)if(auto* Row=Cast<URPGNarrativeActionWidget>(Widget);Row&&Row->GetIsEnabled()&&(Row->GetDialogueId()==TEXT("CH01.M06.Offer")||Row->GetDialogueId()==TEXT("CH01.D06")))
                 if(auto* B=Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                 {if(Row->GetDialogueId()==TEXT("CH01.D06")&&!Row->GetChoiceId().IsEmpty()&&!bPreviewShown){bPreviewShown=true;if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot.Replace(TEXT(".png"),TEXT("-D06.png")),true,false);Next=Now+1;return true;}B->OnClicked.Broadcast();Next=Now+.7;return true;}}
            else {PC->SetControlRotation((NPC->GetActorLocation()-P->GetActorLocation()).Rotation());if(!Input())return Fail(TEXT("DialogueInput"));Next=Now+1;}return true;
        }
        ARPGChapterSixPatient* Target=nullptr;for(TActorIterator<ARPGChapterSixPatient> It(W);It;++It)if(It->IsRecallPoint()==(bRecall&&bWrongPointTried)){Target=*It;break;}if(!Target)return Fail(TEXT("PatientMissing"));
        if(Stage==6){if(!Move(Target->GetActorLocation()-FVector(150,0,0)))return true;if(Now<Next)return true;PC->SetControlRotation((Target->GetActorLocation()-P->GetActorLocation()).Rotation());if(bRecall&&!bWrongPointTried)Target->Interact(PC);else if(!Input())return Fail(TEXT("PatientInput"));Stage=7;Next=Now+1;return true;}
        if(Stage==7&&Now>=Next)
        {
            if(bRecall&&!bWrongPointTried){if(Q&&Q->CompletedObjectives.Contains(TEXT("survivors")))return Fail(TEXT("WrongRecallPointCredited"));UE_LOG(LogTemp,Display,TEXT("M06_PLAY WRONG_RECALL_POINT_REJECTED"));bWrongPointTried=true;Stage=6;Next=Now+.5;return true;}
            if(!Q||!Q->CompletedObjectives.Contains(TEXT("survivors"))){Flow->Refresh();Next=Now+1;return true;}
            if(bRecall&&Q->Status!=ERPGQuestStatus::Completed){Flow->Refresh();Next=Now+1;return true;}
            if(!bRecall&&Q->Status==ERPGQuestStatus::Completed)return Fail(TEXT("CombatFabricated"));
            if(bContinue)Target->Interact(PC); // repeat the same normal server-approved observation
            UE_LOG(LogTemp,Display,TEXT("M06_PLAY PATIENT_SAVED Recall=%d Reinvestigate=%d Status=%d"),bRecall,bContinue,int32(Q->Status));
            if(bRecall)UE_LOG(LogTemp,Display,TEXT("M06_PLAY RECALL_COMPLETE CombatReplay=0"));
            if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);Stage=8;Next=Now+2;return true;
        }
        return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGChapterSixPlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);
    FParse::Value(FCommandLine::Get(),TEXT("M06Screenshot="),P->Screenshot);
    P->bRecall=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterSixRecall"));P->bContinue=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterSixContinue"));FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
