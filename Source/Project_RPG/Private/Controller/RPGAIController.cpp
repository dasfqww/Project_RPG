// Fill out your copyright notice in the Description page of Project Settings.


#include "Controller/RPGAIController.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "AI/RPGBossThreatComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "RPGGameplayTags.h"
#include "Perception/AISense_Sight.h"

#include "RPGDebugHelper.h"

ARPGAIController::ARPGAIController(const FObjectInitializer& ObjectInitializer)
	:Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>("PathFollowingComponent"))
{
	

	AISenseConfig_Sight = CreateDefaultSubobject<UAISenseConfig_Sight>("NPCSenseConfig_Sight");
	AISenseConfig_Sight->DetectionByAffiliation.bDetectEnemies = true;
	AISenseConfig_Sight->DetectionByAffiliation.bDetectFriendlies = false;
	AISenseConfig_Sight->DetectionByAffiliation.bDetectNeutrals = false;
	AISenseConfig_Sight->SightRadius = 5000.f;
	AISenseConfig_Sight->LoseSightRadius = 0.f;
	AISenseConfig_Sight->PeripheralVisionAngleDegrees = 360.f;

	NPCPerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("NPCPerceptionComponent"));
	NPCPerceptionComponent->ConfigureSense(*AISenseConfig_Sight);
	NPCPerceptionComponent->SetDominantSense(UAISenseConfig_Sight::StaticClass());
	NPCPerceptionComponent->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ThisClass::OnNPCPerceptionUpdated);

	SetGenericTeamId(FGenericTeamId(1));

}

ETeamAttitude::Type ARPGAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	const APawn* PawnToCheck = Cast<const APawn>(&Other);
	if (!PawnToCheck || !IsValid(PawnToCheck->GetController()))
	{
		return ETeamAttitude::Neutral;
	}

	const IGenericTeamAgentInterface* OtherTeamAgent = Cast<const IGenericTeamAgentInterface>(PawnToCheck->GetController());

	if (OtherTeamAgent&&OtherTeamAgent->GetGenericTeamId()<GetGenericTeamId())
	{
		return ETeamAttitude::Hostile;
	}

	return ETeamAttitude::Friendly;
}

FPathFollowingRequestResult ARPGAIController::MoveTo(const FAIMoveRequest& Request, FNavPathSharedPtr* OutPath)
{
	if (RPGCombatControl::IsActionBlocked(GetPawn())) return FPathFollowingRequestResult();
	return Super::MoveTo(Request, OutPath);
}

void ARPGAIController::BeginPlay()
{
	Super::BeginPlay();

	if (UCrowdFollowingComponent* CrowdComp = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		CrowdComp->SetCrowdSimulationState
			(bEnableDetourCrowdAvoidance ? ECrowdSimulationState::Enabled : ECrowdSimulationState::Disabled);
	

		switch (DetourCrowdAvoidanceQuality)
		{
		case 1:CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Low);
			break;
		
		case 2:CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Medium);
			break;
		
		case 3:CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
			break;
		
		case 4:CrowdComp->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::High);
			break;
		default:
			break;
		}

		CrowdComp->SetAvoidanceGroup(1);
		CrowdComp->SetGroupsToAvoid(1);
		CrowdComp->SetCrowdCollisionQueryRange(CollisionQueryRange);
	}
}

void ARPGAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) { RefreshEncounterSummonTarget(); }
}

bool ARPGAIController::RefreshEncounterSummonTarget()
{
	ARPGNonPlayerCharacter* NPC = Cast<ARPGNonPlayerCharacter>(GetPawn());
	if (!HasAuthority() || !IsValid(NPC) || NPC->GetEncounterSummonController() != this ||
		NPC->FindComponentByClass<URPGBossThreatComponent>()) { return false; }

	UBlackboardComponent* BlackboardComponent = GetBlackboardComponent();
	static const FName TargetKey(TEXT("TargetActor"));
	const FBlackboard::FKey Key = BlackboardComponent ? BlackboardComponent->GetKeyID(TargetKey) : FBlackboard::InvalidKey;
	if (Key == FBlackboard::InvalidKey || BlackboardComponent->GetKeyType(Key) != UBlackboardKeyType_Object::StaticClass()) { return true; }

	APawn* Source = Cast<APawn>(NPC->GetOwner());
	AAIController* SourceAI = IsValid(Source) ? Cast<AAIController>(Source->GetController()) : nullptr;
	const URPGBossThreatComponent* Threat = IsValid(Source) ? Source->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	const UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	const float Health = ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
	APawn* Current = Cast<APawn>(BlackboardComponent->GetValueAsObject(TargetKey));
	APawn* Next = nullptr;
	if (NPC->IsNPCStartUpDataReady() && !NPC->IsActorBeingDestroyed() && FMath::IsFinite(Health) && Health > 0.f &&
		ASC && !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) &&
		IsValid(Source) && !Source->IsActorBeingDestroyed() && NPC->GetInstigator() == Source && IsValid(SourceAI) && Threat &&
		GetGenericTeamId() != FGenericTeamId::NoTeam && SourceAI->GetGenericTeamId() == GetGenericTeamId())
	{
		// Keep an independently acquired participant while eligible. The boss ledger
		// already rejects death, disconnect, controller replacement and arena exit.
		Next = Threat->GetThreatForTarget(Current) > 0.0 ? Current : Threat->GetCurrentTarget();
		if (Next && GetTeamAttitudeTowards(*Next) != ETeamAttitude::Hostile) { Next = nullptr; }
	}
	if (BlackboardComponent->GetValueAsObject(TargetKey) != Next)
	{
		StopMovement();
		// Aborting a MoveTo may run authored callbacks, including encounter reset.
		if (!IsValid(NPC) || GetPawn() != NPC || NPC->GetEncounterSummonController() != this ||
			!IsValid(BlackboardComponent) || GetBlackboardComponent() != BlackboardComponent) { return true; }
		ClearFocus(EAIFocusPriority::Gameplay);
		if (!NPC->IsNPCStartUpDataReady() || NPC->GetOwner() != Source ||
			(Next && (!IsValid(Threat) || Threat->GetThreatForTarget(Next) <= 0.0))) { Next = nullptr; }
		BlackboardComponent->SetValueAsObject(TargetKey, Next);
	}
	return true;
}

void ARPGAIController::OnNPCPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority()) { return; }
	if (RefreshEncounterSummonTarget()) { return; }
	if (APawn* ControlledPawn = GetPawn())
	{
		if (URPGBossThreatComponent* Threat = ControlledPawn->FindComponentByClass<URPGBossThreatComponent>())
		{
			if (Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
			{
				Threat->NotifySightTarget(Cast<APawn>(Actor), Stimulus.WasSuccessfullySensed());
			}
			return; // Boss targets have a single writer; preserve the legacy path for other NPCs.
		}
	}
	if (UBlackboardComponent* BlackboardComponent = GetBlackboardComponent())
	{
		if (!BlackboardComponent->GetValueAsObject(FName("TargetActor")))
		{
			if (Stimulus.WasSuccessfullySensed() && Actor)
			{
				BlackboardComponent->SetValueAsObject(FName("TargetActor"), Actor);
			}
		}
	}
}
