#include "Progression/RPGIntroOpportunity.h"
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayEffectExtension.h"
#include "Manager/HttpWebManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/RPGPlayerState.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"

URPGIntroOpportunityObjectiveComponent::URPGIntroOpportunityObjectiveComponent()
{
    SetIsReplicatedByDefault(true); PrimaryComponentTick.bCanEverTick = true; PrimaryComponentTick.TickInterval = .1f;
}
void URPGIntroOpportunityObjectiveComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION_NOTIFY(URPGIntroOpportunityObjectiveComponent, Progress, COND_OwnerOnly, REPNOTIFY_Always);
}
bool URPGIntroOpportunityObjectiveComponent::Eligible() const
{
    const auto* Pawn = Cast<ARPGPlayer>(GetOwner());
    const auto* State = Pawn ? Pawn->GetPlayerState<ARPGPlayerState>() : nullptr;
    const auto* PC = Pawn ? Pawn->GetController() : nullptr;
    const auto* Skills = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
    const auto* Class = Skills ? Skills->GetPlayerClassDefinition() : nullptr;
    const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGIntroOpportunityGameMode>() : nullptr;
    const auto* Web = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UHttpWebManager>() : nullptr;
    return IsValid(Pawn) && !Pawn->IsActorBeingDestroyed() && Pawn->HasAuthority() && GetWorld()
        && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() == NM_DedicatedServer
        && Mode && Mode->IsBackendDungeonDifficultyReady() && Mode->GetConfirmedDungeonContentId() == TEXT("Progression.Intro2")
        && Mode->GetCurrentGameDifficulty() == ERPGGameDifficulty::Normal
        && IsValid(State) && !State->IsInactive() && State->HasAuthenticatedCharacter() && State->GetBackendConnectionGeneration() > 0
        && IsValid(PC) && PC->GetPawn() == Pawn && PC->PlayerState == State && State->GetOwner() == PC
        && !IsDungeonGameplayRevoked(PC) && Skills && Skills->GetSkillLoadState() == ERPGPlayerSkillLoadState::Ready
        && !Skills->IsConfigurationMutable() && Class && Class->ClassId == FName(TEXT("GreatSwordWarrior"))
        && Web && Web->IsConfiguredForDungeonServer() && Web->GetConfiguredDungeonSessionId() == State->GetBackendDungeonSessionId()
        && !Mode->IsDungeonPartyMemberRevoked(State->GetBackendDungeonSessionId(), State->GetBackendCharacterId(), State->GetAuthenticatedSteamId())
        && FRPGApprovedHitContext(GetOwner(), GetOwner()).IsCurrent(GetOwner(), GetOwner());
}
void URPGIntroOpportunityObjectiveComponent::RefreshScope()
{
    auto* Pawn = Cast<ARPGPlayer>(GetOwner());
    auto* State = Pawn ? Pawn->GetPlayerState<ARPGPlayerState>() : nullptr;
    const auto* Skills = Pawn ? Pawn->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
    const bool Ready = Eligible();
    const bool Same = Ready && Progress.bActive && State == ObservedState.Get() && Pawn->GetController() == ObservedController.Get()
        && State->GetBackendDungeonSessionId() == Session && State->GetBackendCharacterId() == Character
        && State->GetAuthenticatedSteamId() == Account && State->GetBackendConnectionGeneration() == Connection
        && Skills->GetSkillRestoreGeneration() == Restore && Lifetime.IsCurrent(Pawn, Pawn);
    if (Same) return;
    const bool WasActive = Progress.bActive;
    Progress = {}; LastTarget.Reset(); LastRound = 0; ObservedState.Reset(); ObservedController.Reset();
    if (Ready)
    {
        Progress.bActive = true; ObservedState = State; ObservedController = Pawn->GetController();
        Session = State->GetBackendDungeonSessionId(); Character = State->GetBackendCharacterId(); Account = State->GetAuthenticatedSteamId();
        Connection = State->GetBackendConnectionGeneration(); Restore = Skills->GetSkillRestoreGeneration(); Lifetime = FRPGApprovedHitContext(Pawn, Pawn);
    }
    if (WasActive || Ready) Publish();
}
void URPGIntroOpportunityObjectiveComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    if (GetOwner()->HasAuthority()) RefreshScope();
    else if (auto* Pawn = Cast<APawn>(GetOwner()); Pawn && Pawn->IsLocallyControlled() && !Guide)
        if (auto* PC = Cast<APlayerController>(Pawn->GetController()); PC && PC->GetLocalPlayer())
        {
            Guide = CreateWidget<URPGIntroOpportunityGuideWidget>(PC); if (Guide) Guide->AddToViewport(65);
        }
}
bool URPGIntroOpportunityObjectiveComponent::Credit(ARPGIntroOpportunityTarget& Target, const uint32 Round)
{
    RefreshScope();
    const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGIntroOpportunityGameMode>() : nullptr;
    if (!Progress.bActive || Progress.IsComplete() || !Mode || Mode->GetTarget() != &Target || !Target.IsWindowOpen()
        || !Round || Round != Target.GetRound()) return false;
    if (LastTarget.Get() == &Target && LastRound == Round)
    {
        UE_LOG(LogTemp, Display, TEXT("INTRO2_REJECTED SameRound Character=%s Round=%u"), *Character, Round);
        return false;
    }
    LastTarget = &Target; LastRound = Round; ++Progress.Completed;
    UE_LOG(LogTemp, Display, TEXT("INTRO2_APPROVED Character=%s Round=%u Count=%d"), *Character, Round, Progress.Completed);
    Publish(); return true;
}
void URPGIntroOpportunityObjectiveComponent::Publish() { GetOwner()->ForceNetUpdate(); OnProgressChanged.Broadcast(Progress); }
void URPGIntroOpportunityObjectiveComponent::OnRep_Progress() { OnProgressChanged.Broadcast(Progress); }
void URPGIntroOpportunityObjectiveComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (Guide) Guide->RemoveFromParent(); Guide = nullptr; Super::EndPlay(Reason);
}
ARPGIntroOpportunityPlayer::ARPGIntroOpportunityPlayer()
{
    CreateDefaultSubobject<URPGIntroOpportunityObjectiveComponent>(TEXT("OpportunityObjectives"));
}
ARPGIntroOpportunityTarget::ARPGIntroOpportunityTarget()
{
    PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.bStartWithTickEnabled = true; PrimaryActorTick.TickInterval = .1f;
    AIControllerClass = AAIController::StaticClass(); AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
}
void ARPGIntroOpportunityTarget::HandleNPCStartUpDataReady()
{
    Super::HandleNPCStartUpDataReady();
    if (HasAuthority() && GetRPGAbilitySystemComponent())
    {
        GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100000.f);
        GetRPGAbilitySystemComponent()->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100000.f);
        GetCharacterMovement()->DisableMovement(); SuppressDeathItemDrops(); SetActorTickEnabled(true);
    }
}
void ARPGIntroOpportunityTarget::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARPGIntroOpportunityTarget, Phase); DOREPLIFETIME(ARPGIntroOpportunityTarget, Round); DOREPLIFETIME(ARPGIntroOpportunityTarget, Deadline);
}
void ARPGIntroOpportunityTarget::Tick(float Delta)
{
    Super::Tick(Delta);
    const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGIntroOpportunityGameMode>() : nullptr;
    const auto* Stats = GetRPGAbilitySystemComponent() ? GetRPGAbilitySystemComponent()->GetSet<URPGAttributeSet>() : nullptr;
    if (!HasAuthority() || !Mode || Mode->GetTarget() != this || !Mode->IsBackendDungeonDifficultyReady()
        || !IsNPCStartUpDataReady() || !Stats || Stats->GetCurrentHealth() <= 0.f) return;
    const double Now = GetWorld()->GetTimeSeconds(); if (Now < Deadline) return;
    if (Phase == ERPGOpportunityPhase::Waiting || Phase == ERPGOpportunityPhase::Recovery)
    {
        if (Round == MAX_uint32) { Phase = ERPGOpportunityPhase::Waiting; SetActorTickEnabled(false); return; }
        ++Round; Phase = ERPGOpportunityPhase::Telegraph; Deadline = Now + 2;
    }
    else if (Phase == ERPGOpportunityPhase::Telegraph) { Phase = ERPGOpportunityPhase::Open; Deadline = Now + 3; }
    else { Phase = ERPGOpportunityPhase::Recovery; Deadline = Now + 3; }
    UE_LOG(LogTemp, Display, TEXT("INTRO2_PHASE Round=%u Phase=%d"), Round, static_cast<int32>(Phase));
    ForceNetUpdate();
}
bool ARPGIntroOpportunityTarget::IsWindowOpen() const
{
    const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ARPGIntroOpportunityGameMode>() : nullptr;
    const auto* Stats = GetRPGAbilitySystemComponent() ? GetRPGAbilitySystemComponent()->GetSet<URPGAttributeSet>() : nullptr;
    return HasAuthority() && GetWorld() && GetWorld()->GetNetMode() == NM_DedicatedServer && !IsActorBeingDestroyed()
        && Mode && Mode->GetTarget() == this && Mode->GetConfirmedDungeonContentId() == TEXT("Progression.Intro2")
        && Phase == ERPGOpportunityPhase::Open && Round > 0 && GetWorld()->GetTimeSeconds() < Deadline
        && Stats && Stats->GetCurrentHealth() > 0
        && !GetRPGAbilitySystemComponent()->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) && !IsDungeonGameplayRevoked(this);
}
double ARPGIntroOpportunityTarget::GetSecondsRemaining() const
{
    // Presentation only. Credit always evaluates the server's own deadline.
    const auto* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
    return State ? FMath::Max(0., Deadline - State->GetServerWorldTimeSeconds()) : 0.;
}
void ARPGIntroOpportunityTarget::ObserveDamage(const FGameplayEffectModCallbackData& Data, float Before, float After)
{
    auto* SourceASC = Data.EffectSpec.GetContext().GetOriginalInstigatorAbilitySystemComponent();
    auto* Source = SourceASC ? Cast<ARPGPlayer>(SourceASC->GetAvatarActor()) : nullptr;
    if (!HasAuthority() || GetRPGAbilitySystemComponent() != &Data.Target || Data.Target.GetAvatarActor() != this
        || Data.EvaluatedData.Attribute != URPGAttributeSet::GetTakeDamageAttribute() || !Source
        || !FMath::IsFinite(Before) || !FMath::IsFinite(After) || Before <= 0 || After < 0 || After >= Before
        || !URPGCombatFunctionLibrary::IsTargetPawnHostile(Source, this)) return;
    if (!IsWindowOpen()) { UE_LOG(LogTemp, Display, TEXT("INTRO2_REJECTED Closed Round=%u"), Round); return; }
    if (auto* Objectives = Source->FindComponentByClass<URPGIntroOpportunityObjectiveComponent>()) Objectives->Credit(*this, Round);
}
void ARPGIntroOpportunityGameMode::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && IsRunningDedicatedServer() && TargetClass)
    {
        const FTransform Transform(FRotator(0,180,0), FVector(500,0,100));
        Target = GetWorld()->SpawnActorDeferred<ARPGIntroOpportunityTarget>(TargetClass, Transform);
        if (Target) { Target->ConfigureEncounterSummon(1); Target->FinishSpawning(Transform); }
    }
}
const TCHAR* ARPGIntroOpportunityGameMode::GetPracticeContentId() const { return TEXT("Progression.Intro2"); }
UActorComponent* ARPGIntroOpportunityGameMode::FindPracticeObjectives(ARPGPlayer* Pawn) const { return Pawn ? Pawn->FindComponentByClass<URPGIntroOpportunityObjectiveComponent>() : nullptr; }
ARPGTrainingGameMode::FPracticeStatus ARPGIntroOpportunityGameMode::ReadPracticeStatus(const UActorComponent* Objectives) const
{
    const auto* Value = Cast<URPGIntroOpportunityObjectiveComponent>(Objectives); if (!Value) return {};
    const auto P = Value->GetProgress(); return {P.bActive, P.bActive && P.Completed == 0, P.IsComplete()};
}
void ARPGIntroOpportunityGameMode::BindPracticeObjectives(UActorComponent* Objectives) { if (auto* Value = Cast<URPGIntroOpportunityObjectiveComponent>(Objectives)) Value->OnProgressChanged.AddUniqueDynamic(this, &ThisClass::Changed); }
void ARPGIntroOpportunityGameMode::UnbindPracticeObjectives(UActorComponent* Objectives) { if (auto* Value = Cast<URPGIntroOpportunityObjectiveComponent>(Objectives)) Value->OnProgressChanged.RemoveDynamic(this, &ThisClass::Changed); }
void ARPGIntroOpportunityGameMode::Changed(const FRPGIntroOpportunityProgress&) { NotifyPracticeProgressChanged(); }
const TCHAR* URPGIntroOpportunityGuideWidget::GetPracticeContentId() const { return TEXT("Progression.Intro2"); }
bool URPGIntroOpportunityGuideWidget::HasActiveProgress(const APawn* Pawn) const
{
    const auto* Value = Pawn ? Pawn->FindComponentByClass<URPGIntroOpportunityObjectiveComponent>() : nullptr;
    return Value && Value->IsRegistered() && Value->GetProgress().bActive;
}
void URPGIntroOpportunityGuideWidget::ShowCurrentProgress(const ARPGPlayerController& Controller)
{
    const auto* Pawn = GetDisplayedPawn(); const auto* Value = Pawn ? Pawn->FindComponentByClass<URPGIntroOpportunityObjectiveComponent>() : nullptr;
    if (!Value || !HasActiveProgress(Pawn)) { ClearPresentation(); return; }
    if (!Card || !Title || !Hint || Goals.Num() != 2) return;
    const auto P = Value->GetProgress();
    Title->SetText(FText::Format(NSLOCTEXT("Intro2", "Title", "입문 2 · 공격 기회 {0}/2"), FText::AsNumber(P.Completed)));
    for (int32 Index=0; Index<2; ++Index) Goals[Index]->SetText(FText::Format(P.Completed > Index
        ? NSLOCTEXT("Intro2", "Done", "[완료] 서로 다른 기회에 적중 {0}") : NSLOCTEXT("Intro2", "Pending", "[대기] 서로 다른 기회에 적중 {0}"), FText::AsNumber(Index+1)));
    const ARPGIntroOpportunityTarget* Target = nullptr;
    for (TActorIterator<ARPGIntroOpportunityTarget> It(GetWorld()); It; ++It) { Target = *It; break; }
    Hint->SetText(P.IsComplete() ? NSLOCTEXT("Intro2", "Complete", "개인 실습 완료! 파티 완료와 정산 저장을 확인하세요.")
        : Target && Target->GetPhase() == ERPGOpportunityPhase::Open
            ? NSLOCTEXT("Intro2", "Open", "[공격 기회] 지금 가까이에서 공격하세요. 같은 기회에는 한 번만 인정합니다.")
            : NSLOCTEXT("Intro2", "Wait", "[예고·대기] 대상에게 접근해 공격 기회가 열릴 때를 기다리세요. 창 밖 적중은 인정하지 않습니다."));
    Card->SetVisibility(ESlateVisibility::HitTestInvisible); RefreshCompletionActions(P.IsComplete());
}
