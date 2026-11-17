#include "Actor/Narrative/RPGChapterOneDaon.h"
#include "Actor/Narrative/RPGChapterOneClue.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Narrative/RPGNarrativeCoordinator.h"
#include "Narrative/RPGNarrativeInvestigationComponent.h"
#include "Narrative/RPGNarrativeQuestTracking.h"
#include "Narrative/RPGQuestManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"

ARPGChapterOneDiscovery::ARPGChapterOneDiscovery()
{
    TargetId=TEXT("CH01.Daon");bAlwaysRelevant=true;
    InteractionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterOneDiscovery::BeginPlay()
{
    Super::BeginPlay();SetActorEnableCollision(false);
    if(Nameplate)Nameplate->SetVisibility(false);
    if(Appearance)Appearance->SetVisibility(false);
}
ARPGChapterOneDaon::ARPGChapterOneDaon()
{
    DisplayName=NSLOCTEXT("ChapterOne","Daon","다온");
    DialogueEntryIds={TEXT("CH01.M05.Offer"),TEXT("CH01.D05"),TEXT("CH01.D06.Preview"),TEXT("CH01.M06.Offer"),TEXT("CH01.D06"),TEXT("CH01.D07.Preview")};
    Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("DaonPersonalLabel"));Label->SetupAttachment(RootComponent);
    Label->SetRelativeLocation(FVector(0,0,150));Label->SetWidgetSpace(EWidgetSpace::Screen);
    Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void ARPGChapterOneDaon::BeginPlay()
{
    Super::BeginPlay();if(Nameplate)Nameplate->SetVisibility(false);
    if(HasAuthority())
    {
        FActorSpawnParameters P;P.Owner=this;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Discovery=GetWorld()->SpawnActor<ARPGChapterOneDiscovery>(GetActorLocation(),GetActorRotation(),P);
        if(Discovery)Discovery->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
    }
}
void ARPGChapterOneDaon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ARPGChapterOneDaon,Discovery);
}
void ARPGChapterOneDaon::EndPlay(const EEndPlayReason::Type Reason)
{
    if(HasAuthority()&&IsValid(Discovery))Discovery->Destroy();
    Discovery=nullptr;Super::EndPlay(Reason);
}
void ARPGChapterOneDaon::Interact(APlayerController* PC)
{
    if(!CanInteractWith(PC))return;
    auto* Flow=PC->GetLocalPlayer()->GetSubsystem<URPGNarrativeCoordinator>();auto* State=PC->GetPlayerState<ARPGPlayerState>();FGuid Id;
    if(Flow&&State&&FGuid::Parse(State->GetBackendCharacterId(),Id)&&Flow->GetSelectedCharacterId()==Id&&Flow->HasCurrentSnapshot())
    {
        const auto Quests=Flow->GetQuestManager()->GetQuests(Id);
        const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M05");});
        if(Q&&Q->Status==ERPGQuestStatus::Active&&!Q->CompletedObjectives.Contains(TEXT("discovery")))
        {
            if(auto* C=PC->FindComponentByClass<URPGNarrativeInvestigationComponent>();C&&Discovery)C->Investigate(Discovery);
            return;
        }
    }
    Super::Interact(PC);
}
void ARPGChapterOneDaon::Tick(float Delta)
{
    Super::Tick(Delta);if(GetNetMode()==NM_DedicatedServer)return;
    auto* PC=GetWorld()->GetFirstPlayerController();auto* Local=PC&&PC->IsLocalController()?PC->GetLocalPlayer():nullptr;
    const APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(!Local||!Pawn){Label->SetVisibility(false);return;}
    Label->SetVisibility(FVector::DistSquared(GetActorLocation(),Pawn->GetActorLocation())<FMath::Square(700.f));
    if(!PersonalLabel||PersonalLabel->GetOwningPlayer()!=PC)
    {PersonalLabel=CreateWidget<URPGChapterOneClueLabel>(PC);Label->SetOwnerPlayer(Local);Label->SetWidget(PersonalLabel);}
    if(!PersonalLabel)return;
    FText Caption=NSLOCTEXT("ChapterOne","FindDaon","다온\n가까이에서 응답을 남긴 사람 확인");
    auto* Flow=Local->GetSubsystem<URPGNarrativeCoordinator>();auto* State=PC->GetPlayerState<ARPGPlayerState>();FGuid Id;
    if(Flow&&State&&FGuid::Parse(State->GetBackendCharacterId(),Id)&&Flow->GetSelectedCharacterId()==Id&&Flow->HasCurrentSnapshot())
    {
        const auto Quests=Flow->GetQuestManager()->GetQuests(Id);
        if(const auto* Q=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M05");}))
        {
            if(Q->Status==ERPGQuestStatus::Completed)
            {
                const auto* Rescue=Quests.FindByPredicate([](const auto& V){return V.QuestId==TEXT("CH01.M06");});
                Caption=Rescue&&Rescue->Status==ERPGQuestStatus::Completed
                    ?NSLOCTEXT("ChapterOne","DaonM06Saved","[완료 저장] 함께 열어야 하는 길\n다음: 주갱도와 돌아갈 길 확보")
                    :Rescue?NSLOCTEXT("ChapterOne","DaonM06Active","[진행] 함께 열어야 하는 길\n입문 3에서 부상자 확인·보호·내부 대피 진행")
                    :NSLOCTEXT("ChapterOne","DaonM06Offer","[다음 의뢰] 함께 열어야 하는 길\n다온과 대화해 구조 순서를 확인하세요.");
            }
            else
            {
                if(Q->CompletedObjectives.Contains(TEXT("discovery")))Caption=NSLOCTEXT("ChapterOne","DaonFound","[발견 저장] 다온\n가까이에서 대화해 봉쇄 이유와 구조 요청 확인");
                if(Q->CompletedObjectives.Contains(TEXT("testimony")))Caption=NSLOCTEXT("ChapterOne","DaonHeard","[증언 확인] 생존자의 증언\n공격 기회 실습과 정산 저장을 확인하세요.");
                if(TrackingOffered!=Id)
                {
                    TrackingOffered=Id;FRPGQuestProgress Tracked;auto Tracking=Flow->GetQuestTracking();
                    if(Tracking&&!Tracking->Read(Tracked))Tracking->Track(Id,TEXT("CH01.M05"));
                }
                const double Now=FPlatformTime::Seconds();
                if(Now>=NextRefresh&&!Flow->IsBusy()&&!Flow->HasPendingChoice())
                {NextRefresh=Now+2;Flow->Refresh();}
            }
        }
    }
    PersonalLabel->SetCaption(Caption);
}
