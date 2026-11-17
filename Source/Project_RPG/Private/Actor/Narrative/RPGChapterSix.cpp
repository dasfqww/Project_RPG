#include "Actor/Narrative/RPGChapterSix.h"
#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "Narrative/RPGQuestManager.h"
#include "Player/RPGPlayerState.h"
#include "Progression/RPGCharacterProgressionComponent.h"
#include "Progression/RPGIntroRescue.h"
namespace
{
    bool HasPriorRescue(const APawn* P)
    {
        const auto* C=P?P->FindComponentByClass<URPGCharacterProgressionComponent>():nullptr;
        return C&&C->GetProgressionView().State==ERPGProgressionRestoreState::Ready&&(C->GetProgressionView().Milestones&8)!=0;
    }
    void ShowLabel(UWidgetComponent& Label,TObjectPtr<URPGChapterOneClueLabel>& Widget,APlayerController& PC,const FText& Text)
    {
        if(!Widget||Widget->GetOwningPlayer()!=&PC){Widget=CreateWidget<URPGChapterOneClueLabel>(&PC);Label.SetOwnerPlayer(PC.GetLocalPlayer());Label.SetWidget(Widget);}
        if(Widget)Widget->SetCaption(Text);
    }
}
ARPGChapterSixPatient::ARPGChapterSixPatient()
{
    TargetId=TEXT("CH01.M06.Patient");DisplayName=NSLOCTEXT("M06","Patient","구조 대상");
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=true;PrimaryActorTick.TickInterval=.25f;
    Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("PatientPersonalLabel"));Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,120));Label->SetWidgetSpace(EWidgetSpace::Screen);Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterSixPatient::BeginPlay(){Super::BeginPlay();if(Nameplate)Nameplate->SetVisibility(false);}
bool ARPGChapterSixPatient::CanObserve(APlayerController* PC) const
{
    if(!Super::CanObserve(PC))return false;
    const APawn* P=PC?PC->GetPawn():nullptr;const auto* C=P?P->FindComponentByClass<URPGIntroRescueObjectives>():nullptr;
    const auto* M=GetWorld()?GetWorld()->GetAuthGameMode<ARPGIntroRescueGameMode>():nullptr;
    if(!C||!C->IsReady()||!M||HasPriorRescue(P)!=bRecallPoint)return false;
    if(bRecallPoint)return true; // Verified own prior clear; this observation is new personal story only.
    return M->GetSurvivor()&&M->GetSurvivor()->Living()&&M->GetPhase()!=ERPGRescuePhase::Failed;
}
void ARPGChapterSixPatient::Tick(float Delta)
{
    Super::Tick(Delta);if(GetNetMode()==NM_DedicatedServer)return;
    auto* PC=GetWorld()->GetFirstPlayerController();const APawn* P=PC?PC->GetPawn():nullptr;
    if(!PC||!PC->GetLocalPlayer()||!P){Label->SetVisibility(false);return;}
    const bool Relevant=HasPriorRescue(P)==bRecallPoint;
    Label->SetVisibility(Relevant&&FVector::DistSquared(GetActorLocation(),P->GetActorLocation())<FMath::Square(800.f));
    if(Appearance)Appearance->SetVisibility(Relevant);
    // Local focus follows personal presentation; the server independently validates its own state.
    InteractionBounds->SetCollisionEnabled(Relevant?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    ShowLabel(*Label,PersonalLabel,*PC,bRecallPoint
        ?NSLOCTEXT("M06","Recall","[대피 현장 재방문] 부상자들\n과거 전투 실적과 별개로 현장을 확인하세요.\n새 실습 완료·보상을 만들지 않습니다.")
        :NSLOCTEXT("M06","Fresh","[구조 대상] 부상자들\n다온의 안내 후 가까이에서 상태 확인\n보호·내부 탈출 성공은 별도입니다."));
}
ARPGChapterSixDaon::ARPGChapterSixDaon()
{
    DisplayName=NSLOCTEXT("M06","Daon","다온");DialogueEntryIds={TEXT("CH01.M06.Offer"),TEXT("CH01.D06"),TEXT("CH01.D07.Preview")};
    Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("RescuePersonalLabel"));Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,150));Label->SetWidgetSpace(EWidgetSpace::Screen);Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterSixDaon::BeginPlay(){Super::BeginPlay();if(Nameplate)Nameplate->SetVisibility(false);}
void ARPGChapterSixDaon::Tick(float Delta)
{
    Super::Tick(Delta);if(GetNetMode()==NM_DedicatedServer)return;
    auto* PC=GetWorld()->GetFirstPlayerController();const APawn* P=PC?PC->GetPawn():nullptr;
    if(!PC||!PC->GetLocalPlayer()||!P){Label->SetVisibility(false);return;}
    Label->SetVisibility(FVector::DistSquared(GetActorLocation(),P->GetActorLocation())<FMath::Square(900.f));
    auto* F=PC->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>();const auto* S=PC->GetPlayerState<ARPGPlayerState>();FGuid Id;
    FText Text=NSLOCTEXT("M06","Offer","다온 · 함께 열어야 하는 길\n대화해 수락·구조 순서 확인");
    if(F&&S&&FGuid::Parse(S->GetBackendCharacterId(),Id)&&F->GetSelectedCharacterId()==Id&&F->HasCurrentSnapshot())
    {
        const auto Quests=F->GetQuestManager()->GetQuests(Id);
        if(const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M06");}))
        {
            if(Q->Status==ERPGQuestStatus::Completed)Text=NSLOCTEXT("M06","Saved","[완료 저장] 함께 열어야 하는 길\n다음: 주갱도와 돌아갈 길 확보\n광산 밖 최종 반출은 아직입니다.");
            else
            {
                if(!Q->CompletedObjectives.Contains(TEXT("briefing")))Text=NSLOCTEXT("M06","Briefing","[수락 저장] 다온\n대화해 구조 순서 확인");
                else if(!Q->CompletedObjectives.Contains(TEXT("survivors")))Text=HasPriorRescue(P)?NSLOCTEXT("M06","RecallHint","[안내 저장] 대피 현장 확인\n북동쪽 대피실의 부상자를 확인하세요.\n이미 완료한 전투를 다시 요구하지 않습니다."):NSLOCTEXT("M06","PatientHint","[안내 저장] 부상자 확인\n보호 대상 옆에서 상태를 확인하세요.");
                else Text=NSLOCTEXT("M06","CombatPending","[부상자 확인 저장]\n기여·45초 보호·생존 탈출과 정산을 확인하세요.");
                if(TrackingOffered!=Id){TrackingOffered=Id;FRPGQuestProgress Current;if(auto T=F->GetQuestTracking();T&&!T->Read(Current))T->Track(Id,TEXT("CH01.M06"));}
                const double Now=FPlatformTime::Seconds();if(Now>=NextRefresh&&!F->IsBusy()&&!F->HasPendingChoice()){NextRefresh=Now+2;F->Refresh();}
            }
        }
    }
    ShowLabel(*Label,PersonalLabel,*PC,Text);
}
