#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterOneDaon.h"
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
namespace ChapterFivePlaytest
{
struct FProbe
{
    FString Character,Steam,Session,Screenshot;int32 Stage=0;bool bContinue=false,bPreviewShown=false;
    double Until=FPlatformTime::Seconds()+240,Next=0;
    bool Fail(const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("M05_PLAY FAIL %s Stage=%d"),Why,Stage);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
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
        const auto Quests=Flow->GetQuestManager()->GetQuests(Id);const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M05");});
        if(!Q)return Fail(TEXT("MissingM05"));
        if(Stage==4)
        {
            if(Q->CompletedObjectives.Contains(TEXT("discovery"))!=bContinue)return Fail(TEXT("DiscoveryRestore"));
            UE_LOG(LogTemp,Display,TEXT("M05_PLAY RESTORED Discovery=%d"),bContinue);Stage=5;
        }
        if(Stage==8){if(Now<Next)return true;FPlatformMisc::RequestExitWithStatus(false,0);return false;}
        ARPGChapterOneDaon* NPC=nullptr;for(TActorIterator<ARPGChapterOneDaon> It(W);It;++It){NPC=*It;break;}if(!NPC)return true;
        if(Stage==5)
        {
            const FVector Goal=NPC->GetActorLocation()-FVector(150,0,0);FVector Delta=Goal-P->GetActorLocation();Delta.Z=0;
            PC->SetControlRotation((NPC->GetActorLocation()-P->GetActorLocation()).Rotation());
            if(Delta.Size()>10){P->AddMovementInput(Delta.GetSafeNormal(),1);Next=Now+.5;return true;}
            if(Now<Next)return true;
            auto* Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
            auto* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/Blueprints/Character/Player/Input/Actions/IA_PickUp.IA_PickUp"));
            if(!Input||!Action)return Fail(TEXT("InputMissing"));Input->GetPlayerInput()->InjectInputForAction(Action,FInputActionValue(true));Stage=6;Next=Now+1;return true;
        }
        if(Now<Next)return true;
        if(!bContinue)
        {
            if(!Q->CompletedObjectives.Contains(TEXT("discovery")))return true;
            UE_LOG(LogTemp,Display,TEXT("M05_PLAY DISCOVERY_SAVED"));if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot.Replace(TEXT(".png"),TEXT("-Daon.png")),true,false);Stage=8;Next=Now+1;return true;
        }
        if(Q->CompletedObjectives.Contains(TEXT("testimony")))
        {
            if(auto* Panel=NPC->GetOpenDialogue(PC))Panel->CloseScreen();
            UE_LOG(LogTemp,Display,TEXT("M05_PLAY TESTIMONY_SAVED CombatPending=1"));Stage=8;Next=Now+1;return true;
        }
        if(auto* Panel=NPC->GetOpenDialogue(PC))
        {
            TArray<UWidget*> Widgets;Panel->WidgetTree->GetAllWidgets(Widgets);
            for(auto* Widget:Widgets)if(auto* Row=Cast<URPGNarrativeActionWidget>(Widget);Row&&Row->GetDialogueId()==TEXT("CH01.D05")&&(Row->GetChoiceId().IsEmpty()||Row->GetChoiceId()==TEXT("confirm"))&&Row->GetIsEnabled())
                if(auto* Button=Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
                {
                    if(!Row->GetChoiceId().IsEmpty()&&!bPreviewShown){bPreviewShown=true;if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot.Replace(TEXT(".png"),TEXT("-D05.png")),true,false);Next=Now+1;return true;}
                    Button->OnClicked.Broadcast();Next=Now+1;break;
                }
        }
        return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGChapterFivePlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);
    FParse::Value(FCommandLine::Get(),TEXT("Intro2Screenshot="),P->Screenshot);
    P->bContinue=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterFiveContinue"));FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
