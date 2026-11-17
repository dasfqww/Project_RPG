#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Progression/RPGIntroRescue.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/QuickSlotComponent.h"
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
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "UnrealClient.h"

// Input/observation only. No goal setters, teleports, health writes or settlement calls.
namespace IntroRescuePlaytest
{
struct FProbe
{
    FString Character,Steam,Session,Screenshot;
    int32 Stage=0,ChargeSlot=INDEX_NONE;uint32 FailedAttempt=0;
    bool bContinue=false,bFailureSeen=false,bWest=false,bCharging=false,bAttack=false,bPreparked=false,bLeft=false;
    float LastStagger=-1;double Deadline=FPlatformTime::Seconds()+600,Next=0,Pressed=0,Release=0;
    bool Fail(const TCHAR* Why){UE_LOG(LogTemp,Error,TEXT("INTRO3_PLAY FAIL %s Stage=%d"),Why,Stage);FPlatformMisc::RequestExitWithStatus(false,1);return false;}
    bool Move(APawn& Pawn,APlayerController& PC,FVector Goal)
    {FVector D=Goal-Pawn.GetActorLocation();D.Z=0;PC.SetControlRotation(FRotator::ZeroRotator);if(D.Size()<12)return true;Pawn.AddMovementInput(D.GetSafeNormal(),1);return false;}
    bool Approach(APawn& P,APlayerController& PC,const AActor& T)
    {if(!Move(P,PC,T.GetActorLocation()-FVector(bWest?145:500,0,0)))return false;if(!bWest){bWest=true;return false;}return true;}
    bool Tick(float)
    {
        if(FPlatformTime::Seconds()>Deadline)return Fail(TEXT("Timeout"));
        UWorld* W=nullptr;for(const auto& C:GEngine->GetWorldContexts())if(C.World()&&C.World()->IsGameWorld()){W=C.World();break;}
        if(!W||!W->GetGameInstance()||W->GetNetMode()==NM_DedicatedServer)return true;
        auto* GI=W->GetGameInstance();auto* Web=GI->GetSubsystem<UHttpWebManager>();auto* Flow=GI->GetSubsystem<URPGDungeonSessionSubsystem>();
        if(!Web||!Flow||!Web->GetApiUrl().StartsWith(TEXT("http://127.0.0.1:")))return Fail(TEXT("PrivateContext"));
        if(Stage==0){GI->GetSubsystem<URPGSteamAuthSubsystem>()->LoginToDevelopmentBackend(Steam);Stage=1;return true;}
        if(Stage==1){if(!Web->HasAccessToken())return true;if(!Flow->SelectCharacter(Character)||!Flow->ResumeDungeonSession())return Fail(TEXT("Resume"));Stage=2;return true;}
        if(Stage==2){if(Flow->IsRequestInFlight())return true;const auto S=Flow->GetCurrentDungeonSession();if(S.DungeonSessionId!=Session||S.State!=TEXT("InProgress"))return Fail(TEXT("Session"));if(!Flow->ConnectToDungeon(W->GetFirstPlayerController(),S.ServerAddress))return Fail(TEXT("Connect"));Stage=3;return true;}
        const double Real=FPlatformTime::Seconds();
        if(Stage>=10)
        {
            if(Real<Next)return true;if(Stage==11){FPlatformMisc::RequestExitWithStatus(false,0);return false;}
            if(Flow->IsRequestInFlight())return true;const auto S=Flow->GetCurrentDungeonSession();if(S.DungeonSessionId!=Session)return Fail(TEXT("SettlementScope"));
            if(S.State==TEXT("Cleared")){UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY SETTLED State=Cleared"));if(!Screenshot.IsEmpty())FScreenshotRequest::RequestScreenshot(Screenshot,true,false);Stage=11;Next=Real+1;return true;}
            if(S.State!=TEXT("InProgress")&&S.State!=TEXT("SettlementPending"))return Fail(TEXT("SettlementTerminal"));Flow->RefreshDungeonSession();Next=Real+.5;return true;
        }
        if(W->GetNetMode()!=NM_Client)return true;auto* PC=W->GetFirstPlayerController();auto* Pawn=PC?Cast<ARPGPlayer>(PC->GetPawn()):nullptr;
        auto* State=PC?PC->GetPlayerState<ARPGPlayerState>():nullptr;if(!Pawn||!State||!State->HasAuthenticatedCharacter()||State->GetBackendCharacterId()!=Character)return true;
        auto* C=Pawn->FindComponentByClass<URPGIntroRescueObjectives>();auto* ASC=Pawn->GetRPGAbilitySystemComponent();auto* Slots=Pawn->FindComponentByClass<UQuickSlotComponent>();
        if(!C||!ASC||!Slots)return Fail(TEXT("Components"));const auto P=C->GetProgress();if(!P.bActive||!Slots->HasInitializedSkillSlots())return true;
        const double Time=W->GetTimeSeconds();
        if(Stage==3)
        {if(P.Stagger!=0||P.bContribution||P.bDefense||P.bEscaped)return Fail(TEXT("ReconnectRetainedBundle"));UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY RESET Stagger=0 Continue=%d"),bContinue);Stage=4;}
        if(P.Stagger!=LastStagger){LastStagger=P.Stagger;UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY METER Attempt=%u Stagger=%.0f Phase=%d"),P.Attempt,P.Stagger,int32(P.Phase));}
        if(bAttack&&Time>=Release){ASC->OnAbilityInputReleased(RPGGameplayTags::InputTag_LightAttack_GreatSword);bAttack=false;}
        if(P.Phase==ERPGRescuePhase::Failed)
        {if(!bContinue)return Fail(TEXT("PartialAttemptFailed"));bFailureSeen=true;FailedAttempt=P.Attempt;Stage=5;bWest=false;return true;}
        if(Stage==5)
        {if(P.Attempt<=FailedAttempt)return true;if(P.Stagger!=0||P.bContribution||P.bDefense||P.bEscaped)return Fail(TEXT("RetryRetainedBundle"));UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY FAILURE_RESET Attempt=%u Stagger=0"),P.Attempt);Stage=6;}
        if(P.Phase==ERPGRescuePhase::Contribution)
        {
            if(!bContinue&&P.Stagger>0){UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY PARTIAL Stagger=%.0f"),P.Stagger);FPlatformMisc::RequestExitWithStatus(false,0);return false;}
            ARPGIntroRescuePawn* Target=nullptr;for(TActorIterator<ARPGIntroRescuePawn> It(W);It;++It)if(It->GetPracticeRole()==ERPGRescueRole::Contribution){Target=*It;break;}if(!Target)return true;
            if(bCharging)
            {
                for(const auto& Spec:ASC->GetActivatableAbilities())
                    if(const auto* Skill=Cast<URPGGameplayAbility_SkillContainer>(Spec.GetPrimaryInstance());Skill&&Skill->IsActive()&&Skill->GetActiveSkillSpec().SkillTag==RPGGameplayTags::Player_Ability_Skill_Charge)
                    {const auto* Config=Skill->GetActiveSkillSpec().ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();const auto* Policy=Cast<URPGSkillExecutionPolicy_Charge>(Skill->GetActiveExecutionPolicy());
                     if(Config&&Policy&&Policy->GetCurrentChargeLevel()>=Config->MaxChargeLevel){Slots->EndUseSkillSlot(ChargeSlot);bCharging=false;bWest=false;Next=Time+3;UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY CHARGE ReleasedAtMaximum=1"));return true;}}
                if(Time-Pressed>15){Slots->EndUseSkillSlot(ChargeSlot);bCharging=false;Next=Time+2;}return true;
            }
            if(Time<Next||!Approach(*Pawn,*PC,*Target))return true;
            ChargeSlot=INDEX_NONE;for(int32 I=0;I<8;++I)if(const auto* V=Slots->GetSkillSlotContent(I);V&&V->AbilityTag==RPGGameplayTags::Player_Ability_Skill_Charge){ChargeSlot=I;break;}
            if(ChargeSlot==INDEX_NONE)return Fail(TEXT("ChargeSlot"));Slots->BeginUseSkillSlot(ChargeSlot);Pressed=Time;bCharging=true;return true;
        }
        if(P.Phase==ERPGRescuePhase::Defense)
        {
            if(!bFailureSeen)return true; // First full attempt intentionally leaves real threats alive.
            ARPGIntroRescuePawn* Enemy=nullptr;for(TActorIterator<ARPGIntroRescuePawn> It(W);It;++It)
                if(It->GetPracticeRole()==ERPGRescueRole::Threat&&It->Health()>0&&!It->IsActorBeingDestroyed()){Enemy=*It;break;}
            if(Enemy)
            {if(Time<Next)return true;if(!Approach(*Pawn,*PC,*Enemy))return true;UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY DEFENSE_ATTACK Health=%.0f Distance=%.0f Remaining=%.1f"),Enemy->Health(),FVector::Dist2D(Enemy->GetActorLocation(),Pawn->GetActorLocation()),P.Remaining);ASC->OnAbilityInputPressed(RPGGameplayTags::InputTag_LightAttack_GreatSword);bAttack=true;Release=Time+.15;Next=Time+.9;bWest=false;return true;}
            if(P.Remaining>22){bWest=false;Move(*Pawn,*PC,FVector(700,0,100));return true;}
            if(Move(*Pawn,*PC,FVector(2000,1400,100)))bPreparked=true;
            return true;
        }
        if(P.Phase==ERPGRescuePhase::Escape)
        {
            if(!bFailureSeen||!bPreparked)return Fail(TEXT("MissingFailureOrPrepark"));
            if(P.bEscaped&&!bLeft)return Fail(TEXT("PreparkCredited"));
            if(!bLeft){if(!Move(*Pawn,*PC,FVector(2000,1000,100)))return true;bLeft=true;UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY PREPARK_REJECTED Reenter=1"));}
            Move(*Pawn,*PC,FVector(2000,1400,100));return true;
        }
        if(P.IsComplete())
        {if(!bFailureSeen||!bLeft)return Fail(TEXT("IncompleteProof"));UE_LOG(LogTemp,Display,TEXT("INTRO3_PLAY COMPLETE SameAttempt=1 Stagger100=1 Defense45=1 Waves2=1 LivingEscape=1"));Stage=10;Next=Real+.5;return true;}
        return true;
    }
};
static const FDelegateHandle Registration=FCoreDelegates::GetOnPostEngineInit().AddLambda([]{
    if(!FParse::Param(FCommandLine::Get(),TEXT("RPGIntroRescuePlaytest"))||!FParse::Param(FCommandLine::Get(),TEXT("nosteam")))return;
    auto P=MakeShared<FProbe>();FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingCharacter="),P->Character);
    FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSteam="),P->Steam);FParse::Value(FCommandLine::Get(),TEXT("RPGTrainingSession="),P->Session);
    FParse::Value(FCommandLine::Get(),TEXT("Intro3Screenshot="),P->Screenshot);P->bContinue=FParse::Param(FCommandLine::Get(),TEXT("RPGIntroRescueContinue"));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P](float D){return P->Tick(D);}),.025f);
});
}
#endif
