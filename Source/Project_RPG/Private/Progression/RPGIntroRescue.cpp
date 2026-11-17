#include "Progression/RPGIntroRescue.h"
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGCombatContributionTypes.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffect.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Security/RPGSecurityTypes.h"

namespace
{
    bool InsideSafety(const FVector& P){return FVector::DistSquared2D(P,FVector(2000,1400,100))<FMath::Square(180.)&&FMath::Abs(P.Z-100)<150;}
    FString Scope(const ARPGPlayer& P)
    {
        const auto* S=P.GetPlayerState<ARPGPlayerState>();const auto* K=P.FindComponentByClass<URPGPlayerSkillComponent>();
        return S&&K?FString::Printf(TEXT("%u/%u/%s/%s/%s/%lld/%u"),P.GetUniqueID(),P.GetController()->GetUniqueID(),*S->GetBackendDungeonSessionId(),*S->GetBackendCharacterId(),*S->GetAuthenticatedSteamId(),S->GetBackendConnectionGeneration(),K->GetSkillRestoreGeneration()):FString();
    }
}
URPGIntroRescueObjectives::URPGIntroRescueObjectives()
{SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickInterval=.1f;}
void URPGIntroRescueObjectives::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME_CONDITION_NOTIFY(URPGIntroRescueObjectives,Progress,COND_OwnerOnly,REPNOTIFY_Always);}
bool URPGIntroRescueObjectives::IsReady() const
{
    const auto* P=Cast<ARPGPlayer>(GetOwner());const auto* S=P?P->GetPlayerState<ARPGPlayerState>():nullptr;const auto* PC=P?P->GetController():nullptr;
    const auto* K=P?P->FindComponentByClass<URPGPlayerSkillComponent>():nullptr;const auto* C=K?K->GetPlayerClassDefinition():nullptr;
    const auto* M=GetWorld()?GetWorld()->GetAuthGameMode<ARPGIntroRescueGameMode>():nullptr;
    const auto* W=GetWorld()&&GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UHttpWebManager>():nullptr;
    return P&&P->HasAuthority()&&!P->IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&GetWorld()->GetNetMode()==NM_DedicatedServer
        &&M&&M->IsBackendDungeonDifficultyReady()&&M->GetConfirmedDungeonContentId()==TEXT("Progression.Intro3")&&M->GetCurrentGameDifficulty()==ERPGGameDifficulty::Normal
        &&IsValid(S)&&!S->IsInactive()&&S->HasAuthenticatedCharacter()&&S->GetBackendConnectionGeneration()>0&&IsValid(PC)&&PC->GetPawn()==P&&PC->PlayerState==S&&S->GetOwner()==PC
        &&M->HasConfirmedDungeonAdmission(S)&&!IsDungeonGameplayRevoked(PC)&&K&&K->GetSkillLoadState()==ERPGPlayerSkillLoadState::Ready&&!K->IsConfigurationMutable()
        &&C&&C->ClassId==FName(TEXT("GreatSwordWarrior"))&&W&&W->IsConfiguredForDungeonServer()&&W->GetConfiguredDungeonSessionId()==S->GetBackendDungeonSessionId()
        &&!M->IsDungeonPartyMemberRevoked(S->GetBackendDungeonSessionId(),S->GetBackendCharacterId(),S->GetAuthenticatedSteamId())&&FRPGApprovedHitContext(GetOwner(),GetOwner()).IsCurrent(GetOwner(),GetOwner());
}
void URPGIntroRescueObjectives::Update(const FRPGIntroRescueProgress& Value)
{Progress=Value;GetOwner()->ForceNetUpdate();OnChanged.Broadcast(Progress);}
void URPGIntroRescueObjectives::OnRep_Progress(){OnChanged.Broadcast(Progress);}
void URPGIntroRescueObjectives::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);
    if(!GetOwner()->HasAuthority())if(auto* P=Cast<APawn>(GetOwner());P&&P->IsLocallyControlled()&&!Guide)
        if(auto* PC=Cast<APlayerController>(P->GetController());PC&&PC->GetLocalPlayer())
        {Guide=CreateWidget<URPGIntroRescueGuide>(PC);if(Guide)Guide->AddToViewport(65);}
}
void URPGIntroRescueObjectives::EndPlay(const EEndPlayReason::Type Reason)
{if(Guide)Guide->RemoveFromParent();Guide=nullptr;Super::EndPlay(Reason);}
ARPGIntroRescuePlayer::ARPGIntroRescuePlayer(){CreateDefaultSubobject<URPGIntroRescueObjectives>(TEXT("RescueObjectives"));}
ARPGIntroRescuePawn::ARPGIntroRescuePawn()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=true;PrimaryActorTick.TickInterval=.1f;AIControllerClass=AAIController::StaticClass();AutoPossessAI=EAutoPossessAI::PlacedInWorldOrSpawned;}
void ARPGIntroRescuePawn::Configure(ERPGRescueRole Value){PracticeRole=Value;ConfigureEncounterSummon(Value==ERPGRescueRole::Survivor?0:1);}
void ARPGIntroRescuePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{Super::GetLifetimeReplicatedProps(OutLifetimeProps);DOREPLIFETIME(ARPGIntroRescuePawn,PracticeRole);}
float ARPGIntroRescuePawn::Health() const{return GetRPGAttributeSet()?GetRPGAttributeSet()->GetCurrentHealth():0;}
bool ARPGIntroRescuePawn::Living() const{return IsNPCStartUpDataReady()&&!IsActorBeingDestroyed()&&FMath::IsFinite(Health())&&Health()>0&&!GetRPGAbilitySystemComponent()->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death);}
void ARPGIntroRescuePawn::HandleNPCStartUpDataReady()
{
    Super::HandleNPCStartUpDataReady();if(!HasAuthority())return;
    const float HP=PracticeRole==ERPGRescueRole::Contribution?100000.f:PracticeRole==ERPGRescueRole::Survivor?100.f:30.f;
    auto* ASC=GetRPGAbilitySystemComponent();ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),HP);ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),HP);
    GetCharacterMovement()->DisableMovement();SuppressDeathItemDrops();SetActorTickEnabled(true);NextAttack=GetWorld()->GetTimeSeconds()+3;
    if(PracticeRole==ERPGRescueRole::Contribution)
    {FRPGCombatContributionReceiverProfile P;P.bEnabled=true;P.StaggerThreshold=100;P.PartBreakThreshold=0;FindComponentByClass<URPGCombatContributionComponent>()->ConfigureForNewAttempt(P);}
}
void ARPGIntroRescuePawn::Tick(float Delta)
{
    Super::Tick(Delta);if(!HasAuthority()||PracticeRole!=ERPGRescueRole::Threat||!Living())return;
    auto* M=GetWorld()->GetAuthGameMode<ARPGIntroRescueGameMode>();if(!M||M->GetPhase()!=ERPGRescuePhase::Defense||!M->IsCurrentThreat(this))return;
    auto* Target=M->GetSurvivor();const double Now=GetWorld()->GetTimeSeconds();if(!Target||!Target->Living()||Now<NextAttack)return;NextAttack=Now+3;
    auto* ASC=GetRPGAbilitySystemComponent();auto Context=ASC->MakeEffectContext();Context.AddSourceObject(this);Context.AddInstigator(this,this);
    auto Effect=LoadClass<UGameplayEffect>(nullptr,TEXT("/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage.GE_Shared_ApplyDamage_C"));
    if(!Effect)return;auto Spec=ASC->MakeOutgoingSpec(Effect,1,Context);if(!Spec.IsValid())return;
    Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage,12.f);
    FHitResult Hit(Target,nullptr,Target->GetActorLocation(),(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal());
    const FRPGSkillSecurityProfile Profile;const float Before=Target->Health();
    const bool Applied=URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(this,Hit,Spec,Profile);
    UE_LOG(LogTemp,Display,TEXT("INTRO3_THREAT Attack=%d Health=%.1f->%.1f"),Applied,Before,Target->Health());
}
ARPGIntroRescueGameMode::ARPGIntroRescueGameMode()
{PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=true;PrimaryActorTick.TickInterval=.1f;}
void ARPGIntroRescueGameMode::BeginPlay(){Super::BeginPlay();}
ARPGIntroRescuePawn* ARPGIntroRescueGameMode::SpawnPractice(ERPGRescueRole PracticeRole,FVector Location)
{
    if(!PracticePawnClass)return nullptr;const FTransform T(FRotator(0,180,0),Location);
    auto* P=GetWorld()->SpawnActorDeferred<ARPGIntroRescuePawn>(PracticePawnClass,T,this);if(P){P->Configure(PracticeRole);P->FinishSpawning(T);}return P;
}
void ARPGIntroRescueGameMode::ClearAttempt()
{
    for(auto& P:Participants)if(auto* C=P.Objectives.Get())C->Update({});Participants.Reset();
    if(IsValid(ContributionTarget)){ContributionTarget->RetireEncounterSummon();ContributionTarget->Destroy();}ContributionTarget=nullptr;
    if(IsValid(Survivor)){Survivor->RetireEncounterSummon();Survivor->Destroy();}Survivor=nullptr;
    for(const auto& T:Threats)if(IsValid(T)){T->RetireEncounterSummon();T->Destroy();}Threats.Reset();Phase=ERPGRescuePhase::Waiting;bSecondWave=false;
}
void ARPGIntroRescueGameMode::FailAttempt()
{
    Phase=ERPGRescuePhase::Failed;Deadline=GetWorld()->GetTimeSeconds()+3;
    for(auto& P:Participants)if(auto* C=P.Objectives.Get()){auto V=C->GetProgress();V.Phase=Phase;V.bContribution=V.bDefense=V.bEscaped=false;C->Update(V);}
    UE_LOG(LogTemp,Display,TEXT("INTRO3_FAILED Attempt=%u"),Attempt);
}
void ARPGIntroRescueGameMode::SpawnWave(int32 Number)
{
    for(int32 I=0;I<2;++I)if(auto* P=SpawnPractice(ERPGRescueRole::Threat,FVector(1000+Number*120,I?260:-260,100)))Threats.Add(P);
    UE_LOG(LogTemp,Display,TEXT("INTRO3_WAVE Number=%d Attempt=%u"),Number,Attempt);
}
bool ARPGIntroRescueGameMode::IsCurrentThreat(const ARPGIntroRescuePawn* P) const{return Threats.Contains(P);}
void ARPGIntroRescueGameMode::Tick(float Delta)
{
    Super::Tick(Delta);if(bEnding||!HasAuthority()||!IsRunningDedicatedServer())return;const double Now=GetWorld()->GetTimeSeconds();
    if(Phase==ERPGRescuePhase::Failed){if(Now>=Deadline)ClearAttempt();return;}
    TArray<FParticipant> Current;
    bool bReady=IsBackendDungeonDifficultyReady()&&GetConfirmedDungeonContentId()==TEXT("Progression.Intro3");
    for(auto It=GetWorld()->GetPlayerControllerIterator();It;++It)if(auto* PC=It->Get();IsValid(PC)&&!PC->IsActorBeingDestroyed())
    {
        auto* P=Cast<ARPGPlayer>(PC->GetPawn());auto* C=P?P->FindComponentByClass<URPGIntroRescueObjectives>():nullptr;
        if(!C||!C->IsReady()){bReady=false;continue;}FParticipant V;V.Pawn=P;V.Objectives=C;V.Scope=Scope(*P);V.Lifetime=FRPGApprovedHitContext(P,P);Current.Add(V);
    }
    if(!bReady||Current.IsEmpty()||Current.Num()>4){if(!Participants.IsEmpty())FailAttempt();return;}
    bool bSame=Current.Num()==Participants.Num();
    for(const auto& P:Participants)bSame&=P.Pawn.IsValid()&&P.Lifetime.IsCurrent(P.Pawn.Get(),P.Pawn.Get())&&Current.ContainsByPredicate([&](const auto& V){return V.Pawn==P.Pawn&&V.Scope==P.Scope;});
    if(!bSame&&!Participants.IsEmpty()){FailAttempt();return;}
    if(Participants.IsEmpty())
    {
        if(Attempt==MAX_uint32)return;ClearAttempt();Participants=MoveTemp(Current);++Attempt;Phase=ERPGRescuePhase::Contribution;
        ContributionTarget=SpawnPractice(ERPGRescueRole::Contribution,FVector(400,0,100));Survivor=SpawnPractice(ERPGRescueRole::Survivor,FVector(1000,0,100));
        UE_LOG(LogTemp,Display,TEXT("INTRO3_STARTED Attempt=%u Members=%d"),Attempt,Participants.Num());
    }
    if(!ContributionTarget||!Survivor){FailAttempt();return;}
    // Startup is asynchronous; do not treat initial zero attributes as a death.
    if(!ContributionTarget->IsNPCStartUpDataReady()||!Survivor->IsNPCStartUpDataReady())return;
    if(!ContributionTarget->Living()||!Survivor->Living()){FailAttempt();return;}
    const auto Meter=ContributionTarget->FindComponentByClass<URPGCombatContributionComponent>()->GetContributionState();
    if(Phase==ERPGRescuePhase::Contribution&&Meter.bStaggerComplete&&Meter.Stagger>=100&&Meter.StaggerThreshold==100&&Meter.PartBreakThreshold==0)
    {Phase=ERPGRescuePhase::Defense;DefenseStart=Now;Deadline=Now+45;SpawnWave(1);}
    if(Phase==ERPGRescuePhase::Defense)
    {
        // The two waves share a narrow approach. Retire only genuinely dead
        // practice threats so their corpse capsules cannot block the next wave.
        for(const auto& T:Threats)if(IsValid(T)&&T->IsNPCStartUpDataReady()&&FMath::IsFinite(T->Health())&&T->Health()<=0)
        {T->RetireEncounterSummon();T->Destroy();}
        if(!bSecondWave&&Now-DefenseStart>=22.5){bSecondWave=true;SpawnWave(2);}
        if(Now>=Deadline)
        {
            if(!bSecondWave||Threats.Num()!=4||Threats.ContainsByPredicate([](const auto& P){return IsValid(P)&&(!P->IsNPCStartUpDataReady()||P->Living());})){FailAttempt();return;}
            Phase=ERPGRescuePhase::Escape;Deadline=Now+25;
            for(auto& P:Participants)P.bWasInside=InsideSafety(P.Pawn->GetActorLocation());
            UE_LOG(LogTemp,Display,TEXT("INTRO3_DEFENDED Attempt=%u"),Attempt);
        }
    }
    if(Phase==ERPGRescuePhase::Escape&&Now>=Deadline){FailAttempt();return;}
    if(Phase==ERPGRescuePhase::Escape||Phase==ERPGRescuePhase::Complete)
    {
        bool All=true;
        for(auto& P:Participants)
        {const bool Inside=InsideSafety(P.Pawn->GetActorLocation());if(Inside&&!P.bWasInside)P.bEntered=true;P.bWasInside=Inside;All&=Inside&&P.bEntered;}
        if(Phase==ERPGRescuePhase::Complete&&!All){FailAttempt();return;}
        if(All&&Phase!=ERPGRescuePhase::Complete){Phase=ERPGRescuePhase::Complete;UE_LOG(LogTemp,Display,TEXT("INTRO3_ESCAPED Attempt=%u"),Attempt);}
    }
    for(auto& P:Participants)if(auto* C=P.Objectives.Get())
    {
        FRPGIntroRescueProgress V;V.bActive=true;V.Attempt=Attempt;V.Phase=Phase;V.Stagger=Meter.Stagger;V.SurvivorHealth=Survivor->Health();
        V.bContribution=Phase!=ERPGRescuePhase::Contribution;V.bDefense=Phase==ERPGRescuePhase::Escape||Phase==ERPGRescuePhase::Complete;
        V.bEscaped=V.bDefense&&P.bEntered&&P.bWasInside;V.Remaining=(Phase==ERPGRescuePhase::Defense||Phase==ERPGRescuePhase::Escape)?FMath::Max(0.,Deadline-Now):0;C->Update(V);
    }
}
void ARPGIntroRescueGameMode::EndPlay(const EEndPlayReason::Type Reason){bEnding=true;ClearAttempt();Super::EndPlay(Reason);}
const TCHAR* ARPGIntroRescueGameMode::GetPracticeContentId() const{return TEXT("Progression.Intro3");}
UActorComponent* ARPGIntroRescueGameMode::FindPracticeObjectives(ARPGPlayer* P) const{return P?P->FindComponentByClass<URPGIntroRescueObjectives>():nullptr;}
ARPGTrainingGameMode::FPracticeStatus ARPGIntroRescueGameMode::ReadPracticeStatus(const UActorComponent* C) const
{
    const auto* O=Cast<URPGIntroRescueObjectives>(C);if(!O)return {};const auto P=O->GetProgress();
    // The base mode may poll between movement and this mode's next tick.
    // Recheck the live escape position before allowing its fresh membership gate.
    const bool Complete=P.IsComplete()&&Phase==ERPGRescuePhase::Complete&&O->IsReady()
        &&P.Attempt==Attempt&&InsideSafety(O->GetOwner()->GetActorLocation());
    return {P.bActive,P.bActive&&P.Phase==ERPGRescuePhase::Contribution&&P.Stagger==0&&!P.bContribution&&!P.bDefense&&!P.bEscaped,Complete};
}
void ARPGIntroRescueGameMode::BindPracticeObjectives(UActorComponent* C){if(auto* O=Cast<URPGIntroRescueObjectives>(C))O->OnChanged.AddUniqueDynamic(this,&ThisClass::Changed);}
void ARPGIntroRescueGameMode::UnbindPracticeObjectives(UActorComponent* C){if(auto* O=Cast<URPGIntroRescueObjectives>(C))O->OnChanged.RemoveDynamic(this,&ThisClass::Changed);}
void ARPGIntroRescueGameMode::Changed(const FRPGIntroRescueProgress&){NotifyPracticeProgressChanged();}
const TCHAR* URPGIntroRescueGuide::GetPracticeContentId() const{return TEXT("Progression.Intro3");}
bool URPGIntroRescueGuide::HasActiveProgress(const APawn* P) const{const auto* C=P?P->FindComponentByClass<URPGIntroRescueObjectives>():nullptr;return C&&C->GetProgress().bActive;}
void URPGIntroRescueGuide::ShowCurrentProgress(const ARPGPlayerController&)
{
    const auto* P=GetDisplayedPawn();const auto* C=P?P->FindComponentByClass<URPGIntroRescueObjectives>():nullptr;
    if(!C||!HasActiveProgress(P)){ClearPresentation();return;}if(!Card||!Title||!Hint||Goals.Num()!=3)return;const auto V=C->GetProgress();
    Title->SetText(FText::Format(NSLOCTEXT("Intro3","Title","입문 3 · 함께 지키는 길 (시도 {0})"),FText::AsNumber(V.Attempt)));
    Goals[0]->SetText(FText::Format(NSLOCTEXT("Intro3","Stagger","{0} 무력화 기여 {1}/100 · 파괴와 별개"),FText::FromString(V.bContribution?TEXT("[완료]"):TEXT("[진행]")),FText::AsNumber(V.Stagger)));
    Goals[1]->SetText(FText::FromString(V.bDefense?TEXT("[완료] 생존자 보호 · 45초/두 웨이브"):V.Phase==ERPGRescuePhase::Defense?TEXT("[진행] 생존자 보호 · 45초/두 웨이브"):TEXT("[대기] 생존자 보호 · 45초/두 웨이브")));
    Goals[2]->SetText(FText::FromString(V.bEscaped?TEXT("[완료] 같은 시도 생존 탈출"):TEXT("[대기] 방어 후 북동쪽 안전지점으로 이동")));
    if(V.Phase==ERPGRescuePhase::Failed)Hint->SetText(NSLOCTEXT("Intro3","Failed","보호 대상 또는 참가 상태를 잃었습니다. 같은 시도 묶음을 처음부터 다시 진행합니다."));
    else if(V.Phase==ERPGRescuePhase::Defense||V.Phase==ERPGRescuePhase::Escape)Hint->SetText(FText::Format(NSLOCTEXT("Intro3","Timer","남은 시간 {0}초 · 보호 대상 체력 {1}\n적을 제거한 뒤에도 방어 종료까지 지키세요. 안전지점 선점은 탈출로 인정하지 않습니다."),FText::AsNumber(FMath::CeilToInt(V.Remaining)),FText::AsNumber(V.SurvivorHealth)));
    else if(V.IsComplete())Hint->SetText(NSLOCTEXT("Intro3","Complete","같은 시도의 세 목표를 마쳤습니다. 안전지점에서 서버 정산 확정을 기다려 주세요."));
    else Hint->SetText(NSLOCTEXT("Intro3","Contribution","앞의 표적에 차지·도약·회전 종료 타격을 적중해 무력화를 누적하세요. 피해량이나 부위 파괴로 대신하지 않습니다."));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible);RefreshCompletionActions(V.IsComplete());
}
