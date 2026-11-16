#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Progression/RPGIntroOpportunity.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Manager/HttpWebManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Online/RPGDungeonSessionSubsystem.h"
#include "Online/RPGSteamAuthSubsystem.h"
#include "Player/RPGPlayerState.h"
#include "RPGGameplayTags.h"
#include "UnrealClient.h"

namespace IntroOpportunityPlaytest
{
struct FProbe
{
    FString Character,Steam,Session,Screenshot;
    bool bContinue=false,bPressed=false,bWestApproach=false;
    int32 Stage=0; uint32 FirstRound=0;
    double Until=FPlatformTime::Seconds()+240,Next=0,Release=0;
    bool Fail(const TCHAR* Why) { UE_LOG(LogTemp,Error,TEXT("INTRO2_PLAY FAIL %s Stage=%d"),Why,Stage);FPlatformMisc::RequestExitWithStatus(false,1);return false; }
    void Attack(URPGAbilitySystemComponent& ASC,double Time)
    {
        ASC.OnAbilityInputPressed(RPGGameplayTags::InputTag_LightAttack_GreatSword);bPressed=true;bWestApproach=false;Release=Time+.15;
        UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY ATTACK Stage=%d"),Stage);
        if(const auto* Pawn=ASC.GetAvatarActor())UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY POSITION %s Yaw=%.1f"),*Pawn->GetActorLocation().ToString(),Pawn->GetActorRotation().Yaw);
    }
    bool Tick(float)
    {
        if(FPlatformTime::Seconds()>Until)return Fail(TEXT("Timeout"));
        UWorld* W=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.World()&&C.World()->IsGameWorld()){W=C.World();break;}
        if(!W||!W->GetGameInstance()||W->GetNetMode()==NM_DedicatedServer)return true;
        auto* GI=W->GetGameInstance();auto* Web=GI->GetSubsystem<UHttpWebManager>();auto* Flow=GI->GetSubsystem<URPGDungeonSessionSubsystem>();
        if(!Web||!Flow||!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))return Fail(TEXT("PrivateContext"));
        if(Stage==0){GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);Stage=1;return true;}
        if(Stage==1){if(!Web->HasAccessToken())return true;if(!Flow->SelectCharacter(Character)||!Flow->ResumeDungeonSession())return Fail(TEXT("Resume"));Stage=2;return true;}
        if(Stage==2){if(Flow->IsRequestInFlight())return true;const auto S=Flow->GetCurrentDungeonSession();if(S.DungeonSessionId!=Session||S.State!=TEXT("InProgress"))return Fail(TEXT("Session"));if(!Flow->ConnectToDungeon(W->GetFirstPlayerController(),S.ServerAddress))return Fail(TEXT("Connect"));Stage=3;return true;}
        if(Stage>=12)
        {
            const double Now=FPlatformTime::Seconds();if(Now<Next)return true;
            if(Stage==13){FPlatformMisc::RequestExitWithStatus(false,0);return false;}
            if(Flow->IsRequestInFlight())return true;
            const auto S=Flow->GetCurrentDungeonSession();
            if(S.DungeonSessionId!=Session)return Fail(TEXT("SettlementScope"));
            if(S.State==TEXT("Cleared"))
            {
                UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY SETTLED State=Cleared"));
                if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);
                Stage=13;Next=Now+1;return true;
            }
            if(S.State!=TEXT("InProgress")&&S.State!=TEXT("SettlementPending"))return Fail(TEXT("SettlementTerminal"));
            Flow->RefreshDungeonSession();Next=Now+.5;return true;
        }
        if(W->GetNetMode()!=NM_Client)return true;
        auto* PC=W->GetFirstPlayerController();auto* Pawn=PC?Cast<ARPGPlayer>(PC->GetPawn()):nullptr;
        auto* State=PC?PC->GetPlayerState<ARPGPlayerState>():nullptr;
        if(!Pawn||!State||!State->HasAuthenticatedCharacter()||State->GetBackendCharacterId()!=Character)return true;
        auto* C=Pawn->FindComponentByClass<URPGIntroOpportunityObjectiveComponent>();auto* ASC=Pawn->GetRPGAbilitySystemComponent();
        if(!C||!ASC)return Fail(TEXT("Components"));const auto P=C->GetProgress();if(!P.bActive)return true;
        ARPGIntroOpportunityTarget* Target=nullptr;for(TActorIterator<ARPGIntroOpportunityTarget> It(W);It;++It){Target=*It;break;}if(!Target)return true;
        const double Time=W->GetTimeSeconds();
        if(bPressed&&Time>=Release){ASC->OnAbilityInputReleased(RPGGameplayTags::InputTag_LightAttack_GreatSword);bPressed=false;}
        if(Stage==6||Stage==10)
        {
            // Normal basic attacks can move the Pawn. Re-approach between rounds rather than assuming a fixed pose.
            // Finish moving east so the movement-oriented character faces the target, even after backing away.
            FVector Delta=Target->GetActorLocation()-FVector(bWestApproach?145:500,0,0)-Pawn->GetActorLocation();Delta.Z=0;
            PC->SetControlRotation(FRotator(0,0,0));
            if(Delta.Size()>10){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
            if(!bWestApproach){bWestApproach=true;return true;}
        }
        if(Stage==3){if(P.Completed!=0)return Fail(TEXT("ReconnectRetainedCount"));UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY RESET Count=0 Continue=%d"),bContinue);Stage=4;}
        if(Stage==4)
        {
            FVector Goal=Target->GetActorLocation()-FVector(145,0,0);FVector Delta=Goal-Pawn->GetActorLocation();Delta.Z=0;
            PC->SetControlRotation(FRotator(0,0,0));if(Delta.Size()>10){Pawn->AddMovementInput(Delta.GetSafeNormal(),1);return true;}
            if(Target->GetPhase()!=ERPGOpportunityPhase::Recovery||Target->GetSecondsRemaining()<2.5)return true;
            Attack(*ASC,Time);Stage=5;Next=Time+1;return true;
        }
        if(Time<Next)return true;
        if(Stage==5){if(P.Completed!=0)return Fail(TEXT("ClosedWindowCredited"));UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY CLOSED Count=0"));Stage=6;}
        if(Stage==6||Stage==10)
        {
            if(Target->GetPhase()!=ERPGOpportunityPhase::Open||Target->GetSecondsRemaining()<2.5||Target->GetRound()==FirstRound)return true;
            if(Stage==6)FirstRound=Target->GetRound();Attack(*ASC,Time);Stage=Stage==6?7:11;Next=Time+.8;return true;
        }
        if(Stage==7)
        {
            if(P.Completed==0&&Target->GetPhase()==ERPGOpportunityPhase::Open)return true;
            if(P.Completed!=1)return Fail(TEXT("FirstHitMissing"));
            if(Target->GetRound()!=FirstRound||Target->GetPhase()!=ERPGOpportunityPhase::Open)return Fail(TEXT("RepeatWindowLost"));
            Attack(*ASC,Time);Stage=8;Next=Time+.8;return true;
        }
        if(Stage==8)
        {
            if(P.Completed!=1)return Fail(TEXT("SameRoundCreditedTwice"));
            if(Target->GetPhase()==ERPGOpportunityPhase::Open)return true;
            UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY SAME_ROUND Count=1 Round=%u"),FirstRound);
            if(!bContinue){UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY ONE_SAVED"));FPlatformMisc::RequestExitWithStatus(false,0);return false;}
            Stage=10;return true;
        }
        if(Stage==11)
        {
            if(P.Completed!=2)
            {
                if(P.Completed!=1)return Fail(TEXT("SecondHitScopeChanged"));
                if(Target->GetPhase()==ERPGOpportunityPhase::Open)return true;
                UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY MISSED ReapproachNextWindow=1"));Stage=10;Next=0;return true;
            }
            UE_LOG(LogTemp,Display,TEXT("INTRO2_PLAY COMPLETE Count=2 DistinctRounds=1"));
            Stage=12;Next=FPlatformTime::Seconds()+.5;return true;
        }
        return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGIntroOpportunityPlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);
    FParse::Value(FCommandLine::Get(),TEXT("Intro2Screenshot="),P->Screenshot);P->bContinue=FParse::Param(FCommandLine::Get(),TEXT("RPGIntroOpportunityContinue"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
