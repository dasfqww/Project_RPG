#include "Actor/Narrative/RPGChapterSeven.h"
#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "Narrative/RPGQuestManager.h"
#include "Player/RPGPlayerState.h"

FString ARPGChapterSevenOperation::Objective() const
{
    return Step==0?TEXT("safe"):Step==1?TEXT("brace"):Step==2?TEXT("export"):FString();
}
ARPGChapterSevenOperation::ARPGChapterSevenOperation()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=true;PrimaryActorTick.TickInterval=.25f;
    Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("OperationLabel"));Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,130));Label->SetWidgetSpace(EWidgetSpace::Screen);
    Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterSevenOperation::BeginPlay()
{
    TargetId=TEXT("CH01.M07.")+Objective();Super::BeginPlay();if(Nameplate)Nameplate->SetVisibility(false);
}
bool ARPGChapterSevenOperation::CanObserve(APlayerController* PC) const
{
    // No scene/view acknowledgement is submitted. Only this placed operation can
    // enter the existing distance/LOS/living/admission/generation approval path.
    return Step>=0&&Step<=2&&Super::CanObserve(PC);
}
void ARPGChapterSevenOperation::Tick(float Delta)
{
    Super::Tick(Delta);if(GetNetMode()==NM_DedicatedServer)return;
    auto* PC=GetWorld()->GetFirstPlayerController();auto* Local=PC?PC->GetLocalPlayer():nullptr;
    const APawn* Pawn=PC?PC->GetPawn():nullptr;if(!Local||!Pawn){Label->SetVisibility(false);return;}
    Label->SetVisibility(FVector::DistSquared(Pawn->GetActorLocation(),GetActorLocation())<FMath::Square(1100.f));
    if(!PersonalLabel){PersonalLabel=CreateWidget<URPGChapterOneClueLabel>(PC);Label->SetOwnerPlayer(Local);Label->SetWidget(PersonalLabel);}
    if(!PersonalLabel)return;
    FText Caption=NSLOCTEXT("M07","Accept","다온과 대화해 돌아갈 길을 수락하세요.\n일반 보스 성공 정산 후 반출을 진행합니다.");
    auto* F=Local->GetSubsystem<URPGNarrativeCoordinator>();const auto* PS=PC->GetPlayerState<ARPGPlayerState>();FGuid Id;
    if(F&&PS&&FGuid::Parse(PS->GetBackendCharacterId(),Id)&&F->GetSelectedCharacterId()==Id&&F->HasCurrentSnapshot())
    {
        const auto Quests=F->GetQuestManager()->GetQuests(Id);
        if(const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M07");}))
        {
            const bool Done=Q->CompletedObjectives.Contains(Objective());
            Caption=Q->Status==ERPGQuestStatus::Completed
                ?NSLOCTEXT("M07","Complete","[완료 저장] 돌아갈 길\n환자들을 광산 밖으로 반출했습니다.\n다음: 미라에게 귀환 보고 (M08 후속)")
                :!Q->CompletedObjectives.Contains(TEXT("boss"))
                ?NSLOCTEXT("M07","Boss","먼저 로비에서 첫 보스 Normal 입장\n전투 성공·정산 뒤 이 현장으로 돌아오세요.\n장면 확인은 구조 성공이 아닙니다.")
                :Done?NSLOCTEXT("M07","Saved","[반출 단계 저장] 다음 현장으로 이동\n재접속해도 이 단계와 전투 실적은 보존됩니다.")
                :Step==0?NSLOCTEXT("M07","Safe","01 · C 주갱도\n가까이에서 안전 확인\n전투 정산과 구조 결과는 별도입니다.")
                :Step==1?NSLOCTEXT("M07","Brace","02 · B 봉쇄 버팀대\nC 안전 확인 후 가까이에서 해체")
                :NSLOCTEXT("M07","Export","03 · D 대피실 부상자\nB 개방 후 함께 광산 밖으로 반출");
            if(Appearance)
            {
                // Personal story staging follows the committed server snapshot,
                // including a new process. It cannot write progress or rewards.
                if(Step==1)Appearance->SetVisibility(!Done);
                if(Step==2)Appearance->SetRelativeLocation(Done?FVector(-2400,-1200,0):FVector::ZeroVector);
            }
            FRPGQuestProgress Tracked;if(Q->Status==ERPGQuestStatus::Active)
                if(auto T=F->GetQuestTracking();T&&!T->Read(Tracked))T->Track(Id,TEXT("CH01.M07"));
            const double Now=FPlatformTime::Seconds();
            if(Q->Status==ERPGQuestStatus::Active&&Now>=NextRefresh&&!F->IsBusy()&&!F->HasPendingChoice()){NextRefresh=Now+2;F->Refresh();}
        }
    }
    PersonalLabel->SetCaption(Caption);
}
ARPGChapterSevenDaon::ARPGChapterSevenDaon()
{
    DisplayName=NSLOCTEXT("M07","Daon","다온 · 돌아갈 길");
    DialogueEntryIds={TEXT("CH01.M07.Offer"),TEXT("CH01.D07")};
}
