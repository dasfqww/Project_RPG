#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGDialogueManager.h"
#include "Narrative/RPGQuestManager.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"

namespace ChapterOnePlaytest
{
struct FProbe
{
    FString Character, Steam, Session, Screenshot;
    int Stage=0, Clue=1;
    bool bContinue=false, bActed=false;
    double Deadline=FPlatformTime::Seconds()+240, Next=0;
    bool Fail(const TCHAR* Reason) { UE_LOG(LogTemp,Error,TEXT("M03_PLAY FAIL %s stage=%d clue=%d"),Reason,Stage,Clue); FPlatformMisc::RequestExitWithStatus(false,1); return false; }
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();
        if(Now>Deadline) return Fail(TEXT("Timeout"));
        UWorld* W=nullptr;
        for(const auto& C:GEngine->GetWorldContexts()) if(C.World()&&C.World()->IsGameWorld()){W=C.World();break;}
        if(!W||!W->GetGameInstance()||W->GetNetMode()==NM_DedicatedServer) return true;
        auto* GI=W->GetGameInstance(); auto* Web=GI->GetSubsystem<UHttpWebManager>(); auto* D=GI->GetSubsystem<URPGDungeonSessionSubsystem>();
        if(!Web||!D||!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))return Fail(TEXT("PrivateContext"));
        if(Stage==0){GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);Stage=1;return true;}
        if(Stage==1){if(!Web->HasAccessToken())return true;if(!D->SelectCharacter(Character)||!D->ResumeDungeonSession())return Fail(TEXT("Resume"));Stage=2;return true;}
        if(Stage==2){if(D->IsRequestInFlight())return true;const auto Current=D->GetCurrentDungeonSession();if(Current.DungeonSessionId!=Session||Current.State!=TEXT("InProgress"))return Fail(TEXT("Session"));if(!D->ConnectToDungeon(W->GetFirstPlayerController(),Current.ServerAddress))return Fail(TEXT("Connect"));Stage=3;return true;}
        if(W->GetNetMode()!=NM_Client)return true;
        auto* PC=W->GetFirstPlayerController(); APawn* Pawn=PC?PC->GetPawn():nullptr; auto* State=PC?PC->GetPlayerState<ARPGPlayerState>():nullptr;
        if(!Pawn||!State||!State->HasAuthenticatedCharacter()||State->GetBackendCharacterId()!=Character)return true;
        auto* Flow=PC->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>(); FGuid Id; FGuid::Parse(Character,Id);
        if(Stage==3){if(!Flow->SelectCharacter(Id)||!Flow->Refresh())return true;Stage=4;return true;}
        if(Flow->IsBusy()||Flow->HasPendingChoice()||!Flow->HasCurrentSnapshot())return true;
        auto Quests=Flow->GetQuestManager()->GetQuests(Id);
        const auto* Q=Quests.FindByPredicate([](const auto& X){return X.QuestId==TEXT("CH01.M03");});
        if(!Q)return Fail(TEXT("MissingAcceptedM03"));
        if(Stage==4){
            if(bContinue&&!Q->CompletedObjectives.Contains(TEXT("cart")))return Fail(TEXT("CartNotRestored"));
            if(!bContinue&&Q->CompletedObjectives.Num()!=0)return Fail(TEXT("UnexpectedInitialProgress"));
            UE_LOG(LogTemp,Display,TEXT("M03_PLAY RESTORED objectives=%d"),Q->CompletedObjectives.Num());
            Stage=5;Clue=bContinue?2:1;
        }
        if(Stage==7){if(Now<Next)return true;FPlatformMisc::RequestExitWithStatus(false,0);return false;}
        if(Stage==8){
            ARPGNarrativeNPC* Board=nullptr;
            for(TActorIterator<ARPGNarrativeNPC> It(W);It;++It)if(It->HasEntry(TEXT("CH01.M04.Offer"))){Board=*It;break;}
            if(!Board)return Fail(TEXT("M04BoardMissing"));
            FVector Delta=Board->GetActorLocation()+FVector(0,60,0)-Pawn->GetActorLocation();Delta.Z=0;
            if(Delta.Size()>15){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
            if(auto* Panel=Board->GetOpenDialogue(PC);Panel&&Panel->IsInViewport()){
                if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);
                UE_LOG(LogTemp,Display,TEXT("M03_PLAY COMPLETE M04_OFFERED UMG_OPEN"));Stage=7;Next=Now+4;return true;
            }
            if(Now>=Next){Board->Interact(PC);Next=Now+2;}return true;
        }
        if(Stage==6){
            const FString Objective=Clue==1?TEXT("cart"):Clue==2?TEXT("brace"):TEXT("signal");
            if(!Q->CompletedObjectives.Contains(Objective))return true;
            UE_LOG(LogTemp,Display,TEXT("M03_PLAY APPROVED clue=%d objectives=%d"),Clue,Q->CompletedObjectives.Num());
            if(Clue==1&&!bContinue){UE_LOG(LogTemp,Display,TEXT("M03_PLAY CART_SAVED"));Stage=7;Next=Now+1;return true;}
            if(Clue==3){
                if(Q->Status!=ERPGQuestStatus::Completed)return Fail(TEXT("NotAutoCompleted"));
                if(Quests.ContainsByPredicate([](const auto& X){return X.QuestId==TEXT("CH01.M04");}))return Fail(TEXT("AutoAcceptedM04"));
                if(!Flow->GetDialogueManager()->GetAvailableDialogues().ContainsByPredicate([](const auto& X){return X.Id==TEXT("CH01.M04.Offer");}))return Fail(TEXT("M04NotOffered"));
                Stage=8;Next=Now;return true;
            }
            ++Clue;Stage=5;return true;
        }
        ARPGChapterOneClue* Target=nullptr;
        for(TActorIterator<ARPGChapterOneClue> It(W);It;++It)if(It->GetTargetId()==FString::Printf(TEXT("CH01.CL%02d"),Clue)){Target=*It;break;}
        if(!Target)return Fail(TEXT("AuthoredClueMissing"));
        // Walk the open southern lane through normal CharacterMovement; never teleport or set progress.
        FVector Goal=Target->GetActorLocation()+FVector(0,60,0);
        if(Pawn->GetActorLocation().Y>-900)Goal=FVector(-2400,-1100,Pawn->GetActorLocation().Z);
        FVector Delta=Goal-Pawn->GetActorLocation(); Delta.Z=0;
        PC->SetControlRotation((Target->GetActorLocation()-Pawn->GetActorLocation()).Rotation());
        if(Delta.Size()>15){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
        if(Now<Next)return true;
        if(!Target->CanObserve(PC))return Fail(TEXT("ClueReachOrSight"));
        auto* Input=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        auto* Action=LoadObject<UInputAction>(nullptr,TEXT("/Game/Blueprints/Character/Player/Input/Actions/IA_PickUp.IA_PickUp"));
        if(!Input||!Input->GetPlayerInput()||!Action)return Fail(TEXT("InteractionInputMissing"));
        Input->GetPlayerInput()->InjectInputForAction(Action,FInputActionValue(true)); Stage=6;Next=Now+1;
        UE_LOG(LogTemp,Display,TEXT("M03_PLAY INTERACT clue=%d distance=%.1f"),Clue,FVector::Dist(Target->GetActorLocation(),Pawn->GetActorLocation()));
        return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGChapterOnePlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);
    FParse::Value(FCommandLine::Get(),TEXT("M03Screenshot="),P->Screenshot);
    P->bContinue=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterOneContinue"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
