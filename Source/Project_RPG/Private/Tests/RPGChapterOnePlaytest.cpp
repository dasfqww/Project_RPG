#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Actor/Narrative/RPGNarrativeNPC.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "RPGGameplayTags.h"
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
    int Stage=0, Clue=1, Waypoint=0, SkillSlot=INDEX_NONE;
    bool bContinue=false, bApproach=false, bReleased=false, bToggleStopped=false;
    bool bApproachRetried=false, bAimSettled=false;
    FVector CastStart=FVector::ZeroVector, ApproachStart=FVector(-170,0,0);
    double Deadline=FPlatformTime::Seconds()+240, Next=0, PressedAt=0;
    bool Fail(const TCHAR* Reason) { UE_LOG(LogTemp,Error,TEXT("M03_PLAY FAIL %s stage=%d clue=%d"),Reason,Stage,Clue); FPlatformMisc::RequestExitWithStatus(false,1); return false; }
    bool Click(URPGNarrativeDialogueWidget* Panel, const FString& Choice)
    {
        TArray<UWidget*> Widgets; Panel->WidgetTree->GetAllWidgets(Widgets);
        for(auto* Widget:Widgets) if(auto* Row=Cast<URPGNarrativeActionWidget>(Widget);
            Row&&Row->GetDialogueId()==TEXT("CH01.M04.Offer")&&Row->GetChoiceId()==Choice&&Row->GetIsEnabled())
            if(auto* Button=Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton"))))
            {Button->OnClicked.Broadcast();return true;}
        return false;
    }
    FString SignalCaption(UWorld* W)
    {
        for(TActorIterator<ARPGChapterOneClue> It(W);It;++It) if(It->GetTargetId()==TEXT("CH01.CL03"))
            if(auto* Label=It->FindComponentByClass<UWidgetComponent>())
                if(auto* Widget=Label->GetUserWidgetObject())
                    if(auto* Text=Cast<UTextBlock>(Widget->WidgetTree->RootWidget))return Text->GetText().ToString();
        return {};
    }
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
        if(Stage>=9)
        {
            const auto* M04=Quests.FindByPredicate([](const auto& X){return X.QuestId==TEXT("CH01.M04");});
            if(!M04)return Fail(TEXT("M04AcceptanceMissing"));
            if(Stage==9)
            {
                if(!SignalCaption(W).Contains(TEXT("북쪽 실습장")))return true;
                FRPGQuestProgress Tracked;
                if(!Flow->GetQuestTracking()->Read(Tracked)||Tracked.QuestId!=TEXT("CH01.M04"))return Fail(TEXT("M04Tracking"));
                if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot.Replace(TEXT(".png"),TEXT("-M04-Active.png")),true,false);
                UE_LOG(LogTemp,Display,TEXT("M04_PLAY ACCEPTED Guidance=1 Tracking=1"));Stage=10;
            }
            auto* Objectives=Pawn->FindComponentByClass<URPGIntroApproachObjectiveComponent>();
            auto* Slots=Pawn->FindComponentByClass<UQuickSlotComponent>();
            if(!Objectives||!Slots)return Fail(TEXT("M04Components"));
            const auto Progress=Objectives->GetApproachProgress();
            if(Stage==10||Stage==12)
            {
                // Southern detour preserves the central cover; every position is reached by movement input.
                const FVector Points[]={FVector(1100,-1100,0),FVector(-2400,-1100,0),FVector(-2400,0,0),
                    FVector(-1500,-1100,0),FVector(-450,-1100,0),FVector(-450,0,0),FVector(1700,0,0)};
                if(Stage==12)Waypoint=6;
                const FVector Goal=Stage==10&&Waypoint==5?ApproachStart:Points[Waypoint];
                FVector Delta=Goal-Pawn->GetActorLocation();Delta.Z=0;
                if(Delta.Size()>20){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
                if(Stage==10&&Waypoint<5){++Waypoint;return true;}
                if(!Progress.bActive||!Progress.bStartReached)return true;
                if(!bAimSettled)
                {
                    PC->SetControlRotation(FRotator(0,Stage==10&&bApproachRetried?90:0,0));
                    bAimSettled=true;Next=Now+.5;return true;
                }
                if(Now<Next)return true;
                const FGameplayTag Tag=Stage==10?RPGGameplayTags::Player_Ability_Skill_AssultBlade:RPGGameplayTags::Player_Ability_Skill_WhirlWind;
                for(int Index=0;Index<Slots->GetMaxSkillSlots();++Index)
                    if(const auto* Content=Slots->GetSkillSlotContent(Index);Content&&Content->AbilityTag==Tag){SkillSlot=Index;break;}
                if(SkillSlot==INDEX_NONE)return Fail(TEXT("M04SkillNotEquipped"));
                CastStart=Pawn->GetActorLocation();
                Slots->BeginUseSkillSlot(SkillSlot);bReleased=false;PressedAt=Now;Next=Now+.2;++Stage;
                UE_LOG(LogTemp,Display,TEXT("M04_PLAY PRESS step=%d slot=%d location=%s"),Stage,SkillSlot,*Pawn->GetActorLocation().ToString());return true;
            }
            if(Stage==11||Stage==13)
            {
                if(!bReleased&&Now>=Next){Slots->EndUseSkillSlot(SkillSlot);bReleased=true;Next=Now+20;}
                if(Stage==13&&bReleased&&!bToggleStopped&&!Progress.IsComplete()&&Now-PressedAt>2)
                {
                    // WhirlWind uses Toggle: a second normal press enters its authored End section.
                    Slots->BeginUseSkillSlot(SkillSlot);bToggleStopped=true;bReleased=false;Next=Now+.2;
                    UE_LOG(LogTemp,Display,TEXT("M04_PLAY TOGGLE_OFF slot=%d"),SkillSlot);
                }
                if(Stage==11&&Progress.bApproachCompleted){Stage=12;SkillSlot=INDEX_NONE;bAimSettled=false;return true;}
                if(Stage==13&&Progress.IsComplete()){UE_LOG(LogTemp,Display,TEXT("M04_PLAY INPUT_PASS Steps=3 ActualSkills=2"));Stage=14;Next=Now;return true;}
                if(bReleased&&Now>Next)
                {
                    const float Distance=FVector::Dist2D(CastStart,Pawn->GetActorLocation());
                    UE_LOG(LogTemp,Display,TEXT("M04_PLAY END_POSITION stage=%d start=%s end=%s distance=%.1f"),Stage,*CastStart.ToString(),*Pawn->GetActorLocation().ToString(),Distance);
                    if(Stage==11&&!bApproachRetried&&Distance>=100&&Distance<1800)
                    {
                        // Calibrate the ordinary second attempt from observed montage travel, after
                        // the normal cooldown wait. It must still earn the server's spatial approval.
                        // The approach box spans Y=-400..400: straddle its southern
                        // boundary instead of starting inside it for a short dash.
                        ApproachStart=FVector(300,-400-Distance*.5f,0);bApproachRetried=true;bAimSettled=false;
                        Stage=10;Waypoint=5;SkillSlot=INDEX_NONE;return true;
                    }
                    return Fail(TEXT("M04NormalSkillEnd"));
                }
                return true;
            }
            if(Stage==14)
            {
                if(M04->Status==ERPGQuestStatus::Completed)
                {
                    if(!SignalCaption(W).Contains(TEXT("입문 1 실적 저장 확인")))return true;
                    Stage=15;Waypoint=0;return true;
                }
                if(Now>=Next){Flow->Refresh();Next=Now+2;}
            }
            if(Stage==15)
            {
                const FVector Points[]={FVector(1700,-1100,0),FVector(750,-1540,0)};
                FVector Delta=Points[Waypoint]-Pawn->GetActorLocation();Delta.Z=0;
                if(Delta.Size()>20){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
                if(Waypoint==0){++Waypoint;return true;}
                PC->SetControlRotation(FRotator(-10,-90,0));
                if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);
                UE_LOG(LogTemp,Display,TEXT("M04_PLAY COMPLETE Saved=1 ConfirmedCaption=1"));Stage=7;Next=Now+4;return true;
            }
            return true;
        }
        if(Stage==8){
            ARPGNarrativeNPC* Board=nullptr;
            for(TActorIterator<ARPGNarrativeNPC> It(W);It;++It)if(It->HasEntry(TEXT("CH01.M04.Offer"))){Board=*It;break;}
            if(!Board)return Fail(TEXT("M04BoardMissing"));
            FVector Delta=Board->GetActorLocation()+FVector(0,60,0)-Pawn->GetActorLocation();Delta.Z=0;
            if(Delta.Size()>15){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
            if(auto* Panel=Board->GetOpenDialogue(PC);Panel&&Panel->IsInViewport()){
                if(bApproach)
                {
                    const auto* M04=Quests.FindByPredicate([](const auto& X){return X.QuestId==TEXT("CH01.M04");});
                    if(!M04){if(!Click(Panel,TEXT("accept")))Click(Panel,FString());return true;}
                    Panel->CloseScreen();Stage=9;return true;
                }
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
    P->bApproach=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterOneApproach"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
