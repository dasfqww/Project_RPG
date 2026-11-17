#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Actor/Narrative/RPGChapterSeven.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/Button.h"
#include "Components/StaticMeshComponent.h"
#include "Blueprint/WidgetTree.h"
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
#include "RPGGameplayTags.h"
#include "UI/Narrative/RPGNarrativeDialogueWidget.h"
#include "UnrealClient.h"
namespace ChapterSevenPlaytest
{
struct FProbe
{
    FString Character,Steam,Session,Screenshot;int32 Stage=0,Phase=0;double Next=0,Until=FPlatformTime::Seconds()+600;
    bool BossFixture=false,FixtureSet=false,Early=false,Attacking=false,Captured=false;double Release=0;
    bool Fail(const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("M07_PLAY FAIL %s Stage=%d"),Why,Stage);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    bool Tick(float)
    {
        const double Now=FPlatformTime::Seconds();if(Now>Until)return Fail(TEXT("Timeout"));
        UWorld* W=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.World()&&C.World()->IsGameWorld()){W=C.World();break;}
        if(!W||!W->GetGameInstance())return true;
        auto* GI=W->GetGameInstance();auto* Web=GI->GetSubsystem<UHttpWebManager>();auto* D=GI->GetSubsystem<URPGDungeonSessionSubsystem>();
        if(!Web||!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))return Fail(TEXT("PrivateContext"));
        if(W->GetNetMode()==NM_DedicatedServer)
        {
            if(BossFixture&&!FixtureSet)for(TActorIterator<ARPGBossCharacter> It(W);It;++It)
                if(auto* A=Cast<URPGAbilitySystemComponent>(It->GetAbilitySystemComponent());A&&It->IsNPCStartUpDataReady())
                {A->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),10);A->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),10);FixtureSet=true;UE_LOG(LogTemp,Display,TEXT("M07_PLAY BOSS_HP_FIXTURE=10 Natural balance not measured"));}
            return true;
        }
        if(!D)return Fail(TEXT("ClientDungeonContext"));
        if(Stage==0){GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);Stage=1;return true;}
        if(Stage==1){if(!Web->HasAccessToken())return true;if(!D->SelectCharacter(Character)||!D->ResumeDungeonSession())return Fail(TEXT("Resume"));Stage=2;return true;}
        if(Stage==2){if(D->IsRequestInFlight())return true;auto S=D->GetCurrentDungeonSession();if(S.DungeonSessionId!=Session||S.State!=TEXT("InProgress"))return Fail(TEXT("Session"));if(!D->ConnectToDungeon(W->GetFirstPlayerController(),S.ServerAddress))return Fail(TEXT("Connect"));Stage=3;return true;}
        if(W->GetNetMode()!=NM_Client)return true;
        auto* PC=W->GetFirstPlayerController();auto* P=PC?Cast<ARPGPlayer>(PC->GetPawn()):nullptr;auto* PS=PC?PC->GetPlayerState<ARPGPlayerState>():nullptr;
        if(!P||!PS||!PS->HasAuthenticatedCharacter())return true;
        auto* Flow=PC->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>();FGuid Id;FGuid::Parse(Character,Id);
        if(Stage==3){if(!Flow->SelectCharacter(Id)||!Flow->Refresh())return true;Stage=4;return true;}
        if(Flow->IsBusy()||Flow->HasPendingChoice()||!Flow->HasCurrentSnapshot())return true;
        auto Move=[&](FVector Goal,float Distance=15.f){FVector V=Goal-P->GetActorLocation();V.Z=0;PC->SetControlRotation(V.Rotation());if(V.Size()<Distance)return true;P->AddMovementInput(V.GetSafeNormal(),1);return false;};
        auto Input=[&](){auto* I=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();auto* A=LoadObject<UInputAction>(nullptr,TEXT("/Game/Blueprints/Character/Player/Input/Actions/IA_PickUp.IA_PickUp"));if(!I||!A)return false;I->GetPlayerInput()->InjectInputForAction(A,FInputActionValue(true));return true;};
        const auto Quests=Flow->GetQuestManager()->GetQuests(Id);const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M07");});
        if(Stage==9)
        {
            if(Phase!=0&&Phase!=10&&!Captured&&Now>=Next-1)
            {
                if(Phase==2)
                {
                    bool Outside=false;
                    for(TActorIterator<ARPGChapterSevenOperation> It(W);It;++It)if(It->Step==2)
                        if(auto* Mesh=It->FindComponentByClass<UStaticMeshComponent>())Outside=Mesh->GetRelativeLocation().Equals(FVector(-2400,-1200,0),1.f);
                    if(!Outside)return Fail(TEXT("CommittedPatientNotOutsideB"));
                    UE_LOG(LogTemp,Display,TEXT("M07_PLAY EXPORTED_PATIENT_OUTSIDE_B"));
                }
                if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);Captured=true;
            }
            if(Now<Next)return true;FPlatformMisc::RequestExitWithStatus(false,0);return false;
        }
        if(Phase==10)
        {
            if(Q&&Q->CompletedObjectives.Contains(TEXT("boss"))){UE_LOG(LogTemp,Display,TEXT("M07_PLAY BOSS_SETTLED Export=%d"),Q->CompletedObjectives.Contains(TEXT("export")));Stage=9;Next=Now+2;return true;}
            ARPGBossCharacter* B=nullptr;for(TActorIterator<ARPGBossCharacter> It(W);It;++It){B=*It;break;}if(!B)return true;
            auto* A=Cast<URPGAbilitySystemComponent>(P->GetAbilitySystemComponent());if(!A)return true;
            if(Attacking&&Now>=Release){A->OnAbilityInputReleased(RPGGameplayTags::InputTag_LightAttack_GreatSword);Attacking=false;}
            if(!Move(B->GetActorLocation()-FVector(160,0,0),30))return true;
            PC->SetControlRotation((B->GetActorLocation()-P->GetActorLocation()).Rotation());
            if(Now>=Next){A->OnAbilityInputPressed(RPGGameplayTags::InputTag_LightAttack_GreatSword);Attacking=true;Release=Now+.15;Next=Now+1;Flow->Refresh();UE_LOG(LogTemp,Display,TEXT("M07_PLAY BOSS_ATTACK_INPUT"));}return true;
        }
        ARPGChapterSevenDaon* NPC=nullptr;for(TActorIterator<ARPGChapterSevenDaon> It(W);It;++It){NPC=*It;break;}if(!NPC)return Fail(TEXT("DaonMissing"));
        if(!Q)
        {
            if(!Move(NPC->GetActorLocation()-FVector(150,0,0))||Now<Next)return true;
            if(auto* Panel=NPC->GetOpenDialogue(PC))
            {TArray<UWidget*> Widgets;Panel->WidgetTree->GetAllWidgets(Widgets);for(auto* V:Widgets)if(auto* Row=Cast<URPGNarrativeActionWidget>(V);Row&&Row->GetIsEnabled()&&Row->GetDialogueId()==TEXT("CH01.M07.Offer"))if(auto* B=Cast<UButton>(Row->GetWidgetFromName(TEXT("NarrativeActionButton")))){B->OnClicked.Broadcast();Next=Now+.7;return true;}}
            else{if(!Input())return Fail(TEXT("OfferInput"));Next=Now+1;}return true;
        }
        if(auto* Panel=NPC->GetOpenDialogue(PC))Panel->CloseScreen();
        if(Stage==4){UE_LOG(LogTemp,Display,TEXT("M07_PLAY RESTORED Phase=%d Boss=%d Safe=%d Brace=%d Export=%d"),Phase,Q->CompletedObjectives.Contains(TEXT("boss")),Q->CompletedObjectives.Contains(TEXT("safe")),Q->CompletedObjectives.Contains(TEXT("brace")),Q->CompletedObjectives.Contains(TEXT("export")));Stage=5;}
        int32 Want=0;
        if(Phase==0)Want=2; // Attempt extraction before combat: must be no-op.
        else if(Q->Status==ERPGQuestStatus::Completed){UE_LOG(LogTemp,Display,TEXT("M07_PLAY EXPORT_COMPLETE"));Stage=9;Next=Now+3;return true;}
        else if(Phase==1&&Q->CompletedObjectives.Contains(TEXT("safe"))){UE_LOG(LogTemp,Display,TEXT("M07_PLAY SAFETY_SAVED Export=0"));Stage=9;Next=Now+3;return true;}
        else Want=!Q->CompletedObjectives.Contains(TEXT("safe"))?0:!Q->CompletedObjectives.Contains(TEXT("brace"))?1:2;
        ARPGChapterSevenOperation* Target=nullptr;for(TActorIterator<ARPGChapterSevenOperation> It(W);It;++It)if(It->Step==Want){Target=*It;break;}if(!Target)return Fail(TEXT("OperationMissing"));
        if(Phase==0&&Early&&Now>=Next){if(Q->CompletedObjectives.Contains(TEXT("export")))return Fail(TEXT("PrematureExport"));UE_LOG(LogTemp,Display,TEXT("M07_PLAY ACCEPTED_EARLY_EXPORT_REJECTED"));Stage=9;Next=Now+2;return true;}
        if(!Move(Target->GetActorLocation()-FVector(150,0,0))||Now<Next)return true;
        if(!Input())return Fail(TEXT("OperationInput"));Early=true;Next=Now+2;Flow->Refresh();return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGChapterSevenPlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);FParse::Value(FCommandLine::Get(),TEXT("M07Phase="),P->Phase);FParse::Value(FCommandLine::Get(),TEXT("M07Screenshot="),P->Screenshot);P->BossFixture=FParse::Param(FCommandLine::Get(),TEXT("RPGChapterSevenBossFixture"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
