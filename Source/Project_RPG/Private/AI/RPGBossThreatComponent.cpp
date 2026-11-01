#include "AI/RPGBossThreatComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AIController.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameplayEffect.h"
#include "Interface/PawnUIInterface.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"

namespace RPGBossThreat
{
	constexpr double MaxScore = 1.e12;
}

URPGBossThreatComponent::URPGBossThreatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.2f;
	SetIsReplicatedByDefault(false);
}

void URPGBossThreatComponent::BeginPlay()
{
	Super::BeginPlay();
	SetComponentTickEnabled(IsServer());
	if (IsServer())
	{
		Boss = GetOwner()->FindComponentByClass<URPGBossAIComponent>();
		if (Boss.IsValid())
		{
			Boss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged);
		}
		RefreshTargets();
	}
}

bool URPGBossThreatComponent::IsServer() const
{
	return !bEndingPlay && IsValid(GetOwner()) && GetOwner()->HasAuthority();
}

bool URPGBossThreatComponent::IsCombatActive() const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	return IsServer() && OwnerPawn && IsValid(OwnerPawn->GetController()) && Boss.IsValid() &&
		Boss->GetBossState().EncounterState == ERPGBossEncounterState::InCombat;
}

bool URPGBossThreatComponent::IsEligible(APawn* Target) const
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(Target) || IsDungeonGameplayRevoked(Target) || Target == OwnerPawn || !OwnerPawn || Target->GetWorld() != GetWorld() ||
		!Target->HasActorBegunPlay() || !IsValid(Target->GetController()) || !IsValid(OwnerPawn->GetController()) ||
		Target->GetActorLocation().ContainsNaN() || OwnerPawn->GetActorLocation().ContainsNaN() ||
		!FMath::IsFinite(MaxTargetDistance) || MaxTargetDistance <= 0.0f)
	{
		return false;
	}
	const double Radius = FMath::Min<double>(MaxTargetDistance, 1000000.0);
	if (FVector::DistSquared(Target->GetActorLocation(), OwnerPawn->GetActorLocation()) > Radius * Radius)
	{
		return false;
	}
	const IGenericTeamAgentInterface* OwnerTeam = Cast<IGenericTeamAgentInterface>(OwnerPawn->GetController());
	const IGenericTeamAgentInterface* TargetTeam = Cast<IGenericTeamAgentInterface>(Target->GetController());
	if (!OwnerTeam || !TargetTeam || OwnerTeam->GetGenericTeamId() == FGenericTeamId::NoTeam ||
		TargetTeam->GetGenericTeamId() == FGenericTeamId::NoTeam ||
		OwnerTeam->GetTeamAttitudeTowards(*Target) != ETeamAttitude::Hostile)
	{
		return false;
	}
	if (const APlayerState* PlayerState = Target->GetPlayerState())
	{
		if (PlayerState->IsInactive() || PlayerState->IsSpectator() || PlayerState->IsOnlyASpectator())
		{
			return false;
		}
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target);
	if (!IsValid(ASC) || ASC->GetAvatarActor() != Target ||
		!ASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) ||
		ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
	{
		return false;
	}
	const float Health = ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	return FMath::IsFinite(Health) && Health > 0.0f;
}

URPGBossThreatComponent::FThreatEntry* URPGBossThreatComponent::AdmitTarget(APawn* Target)
{
	if (FThreatEntry* Existing = Entries.Find(Target))
	{
		if (Existing->Controller.Get() == Target->GetController()) { return Existing; }
		Entries.Remove(Target);
	}
	if (Entries.Num() >= FMath::Clamp(MaxTrackedTargets, 1, 512)) { return nullptr; }
	FThreatEntry& Entry = Entries.Add(Target);
	Entry.Controller = Target->GetController();
	Entry.Order = NextOrder++;
	return &Entry;
}

bool URPGBossThreatComponent::AddThreat(APawn* Target, float Amount, int32 ExpectedEncounterId)
{
	if (bUpdating || !IsCombatActive() || Boss->GetBossState().EncounterId != ExpectedEncounterId ||
		!FMath::IsFinite(Amount) || Amount <= 0.0f || !IsEligible(Target))
	{
		return false;
	}
	TGuardValue<bool> Guard(bUpdating, true);
	FThreatEntry* Entry = AdmitTarget(Target);
	if (!Entry) { return false; }
	Entry->Score = FMath::Min(Entry->Score + static_cast<double>(Amount), RPGBossThreat::MaxScore);
	SelectTarget();
	return true;
}

int32 URPGBossThreatComponent::ForceTarget(APawn* Target, float DurationSeconds, int32 ExpectedEncounterId)
{
	if (bUpdating || !IsCombatActive() || Boss->GetBossState().EncounterId != ExpectedEncounterId ||
		LastLockToken == MAX_int32 || !FMath::IsFinite(DurationSeconds) || DurationSeconds < .01f || DurationSeconds > 120.f ||
		!IsEligible(Target)) { return 0; }
	const double Deadline = GetWorld()->GetTimeSeconds() + static_cast<double>(DurationSeconds);
	if (!FMath::IsFinite(Deadline)) { return 0; }
	TGuardValue<bool> Guard(bUpdating, true);
	FThreatEntry* Entry = AdmitTarget(Target);
	if (!Entry) { return 0; }
	// Admission needs a positive participation score, not a permanent top-threat bonus.
	Entry->Score = FMath::Max(Entry->Score, 1.0);
	ClearForcedTargetState();
	ForcedTarget = Target;
	ForcedTargetController = Target->GetController();
	ForcedSourceController = CastChecked<APawn>(GetOwner())->GetController();
	ForcedEncounterId = ExpectedEncounterId;
	ForcedPhaseIndex = Boss->GetBossState().PhaseIndex;
	ForcedTargetDeadline = Deadline;
	const int32 Token = ActiveLockToken = ++LastLockToken;
	GetWorld()->GetTimerManager().SetTimer(ForcedTargetTimeout,
		FTimerDelegate::CreateWeakLambda(this, [this, ExpectedEncounterId, Token]()
		{
			ReleaseForcedTarget(ExpectedEncounterId, Token);
		}), DurationSeconds, false);
	SelectTarget();
	// A Blackboard observer may synchronously reset the encounter or replace ownership.
	return ActiveLockToken == Token && GetForcedTarget() == Target ? Token : 0;
}

bool URPGBossThreatComponent::ReleaseForcedTarget(int32 ExpectedEncounterId, int32 ExpectedLockToken)
{
	if (bUpdating || !IsServer() || ExpectedLockToken <= 0 || ActiveLockToken != ExpectedLockToken ||
		ForcedEncounterId != ExpectedEncounterId || !Boss.IsValid() ||
		Boss->GetBossState().EncounterId != ExpectedEncounterId) { return false; }
	TGuardValue<bool> Guard(bUpdating, true);
	ClearForcedTargetState();
	SelectTarget();
	return true;
}

APawn* URPGBossThreatComponent::GetForcedTarget() const
{
	if (ActiveLockToken == 0 || !IsCombatActive() || !GetWorld() ||
		Boss->GetBossState().EncounterId != ForcedEncounterId || Boss->GetBossState().PhaseIndex != ForcedPhaseIndex ||
		!ForcedSourceController.IsValid() || CastChecked<APawn>(GetOwner())->GetController() != ForcedSourceController.Get() ||
		!FMath::IsFinite(ForcedTargetDeadline) || GetWorld()->GetTimeSeconds() >= ForcedTargetDeadline) { return nullptr; }
	APawn* Target = ForcedTarget.Get();
	const FThreatEntry* Entry = Entries.Find(Target);
	return IsEligible(Target) && ForcedTargetController.IsValid() && Target->GetController() == ForcedTargetController.Get() &&
		Entry && Entry->Controller == ForcedTargetController && Entry->Score > 0.0 ? Target : nullptr;
}

void URPGBossThreatComponent::ClearForcedTargetState()
{
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ForcedTargetTimeout); }
	ForcedTarget.Reset();
	ForcedTargetController.Reset();
	ForcedSourceController.Reset();
	ForcedTargetDeadline = 0.0;
	ForcedEncounterId = 0;
	ForcedPhaseIndex = INDEX_NONE;
	ActiveLockToken = 0;
	// Keep the lifetime token counter and forced-selection flag until normal reselection.
}

bool URPGBossThreatComponent::ApplyDamageFromServerHit(APawn* Source, const FHitResult& ServerHit,
	const FGameplayEffectSpecHandle& DamageSpec, int32 ExpectedEncounterId,
	const FRPGSkillSecurityProfile& SecurityProfile, float& OutHealthDamage, bool& bOutThreatAdded)
{
	OutHealthDamage = 0.f;
	bOutThreatAdded = false;
	if (bApplyingDamage || bUpdating || !IsCombatActive() ||
		Boss->GetBossState().EncounterId != ExpectedEncounterId || !IsEligible(Source) ||
		!Source->HasAuthority() || ServerHit.GetActor() != GetOwner() || !DamageSpec.IsValid())
	{
		return false;
	}
	const FGameplayEffectSpec& Spec = *DamageSpec.Data;
	// Do not let an untagged spec bypass the shared server damage validator.
	const float BaseDamage = Spec.GetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, false, -1.f);
	if (!Spec.Def || Spec.Def->DurationPolicy != EGameplayEffectDurationType::Instant ||
		Spec.GetDuration() != UGameplayEffect::INSTANT_APPLICATION || Spec.GetPeriod() != 0.f ||
		!FMath::IsFinite(BaseDamage) || BaseDamage <= 0.f)
	{
		return false;
	}
	URPGAbilitySystemComponent* SourceASC = URPGAbilityFunctionLibrary::NativeGetWarriorASCFromActor(Source);
	URPGAbilitySystemComponent* TargetASC = URPGAbilityFunctionLibrary::NativeGetWarriorASCFromActor(GetOwner());
	const IPawnUIInterface* TargetUI = Cast<IPawnUIInterface>(GetOwner());
	if (!SourceASC || !TargetASC || SourceASC == TargetASC || TargetASC->GetAvatarActor() != GetOwner() ||
		!TargetUI || !TargetUI->GetPawnUIComponent() ||
		Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC ||
		!TargetASC->HasAttributeSetForAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) ||
		!TargetASC->HasAttributeSetForAttribute(URPGAttributeSet::GetMaxHealthAttribute()) ||
		TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
	{
		return false;
	}
	const float Before = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	const float MaxHealth = TargetASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
	if (!FMath::IsFinite(Before) || !FMath::IsFinite(MaxHealth) || Before <= 0.f || MaxHealth <= 0.f || Before > MaxHealth)
	{
		return false;
	}
	TGuardValue<bool> Guard(bApplyingDamage, true);
	const TWeakObjectPtr<AController> SourceController = Source->GetController();
	const TWeakObjectPtr<AController> OwnerController = CastChecked<APawn>(GetOwner())->GetController();
	if (!URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(Source, ServerHit, DamageSpec, SecurityProfile))
	{
		return false;
	}
	// Instant GE execution may synchronously change phase, kill the boss or invoke encounter cleanup.
	const float After = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (FMath::IsFinite(After)) { OutHealthDamage = FMath::Clamp(Before - After, 0.f, Before); }
	if (IsValid(Source) && SourceController.IsValid() && Source->GetController() == SourceController.Get() &&
		IsValid(GetOwner()) && OwnerController.IsValid() && CastChecked<APawn>(GetOwner())->GetController() == OwnerController.Get())
	{
		bOutThreatAdded = AddThreat(Source, OutHealthDamage, ExpectedEncounterId);
	}
	return true;
}

APawn* URPGBossThreatComponent::GetCurrentTarget() const
{
	APawn* Target = CurrentTarget.Get();
	if (bCurrentTargetForced && (!Target || Target != GetForcedTarget())) { return nullptr; }
	const FThreatEntry* Entry = Entries.Find(Target);
	return IsCombatActive() && IsEligible(Target) && Entry && Entry->Controller.Get() == Target->GetController() ? Target : nullptr;
}

double URPGBossThreatComponent::GetThreatForTarget(APawn* Target) const
{
	const FThreatEntry* Entry = Entries.Find(Target);
	return IsCombatActive() && IsEligible(Target) && Entry && Entry->Controller.Get() == Target->GetController() ? Entry->Score : 0.0;
}

void URPGBossThreatComponent::NotifySightTarget(APawn* Target, bool bSensed)
{
	if (bUpdating || !IsCombatActive() || !Target) { return; }
	TGuardValue<bool> Guard(bUpdating, true);
	if (!bSensed)
	{
		Entries.Remove(Target);
		PerceivedTargets.Remove(Target);
	}
	else if (IsEligible(Target) && FMath::IsFinite(InitialSightThreat) && InitialSightThreat > 0.0f)
	{
		PerceivedTargets.Add(Target);
		if (FThreatEntry* Entry = AdmitTarget(Target))
		{
			Entry->Score = FMath::Max(Entry->Score, FMath::Min<double>(InitialSightThreat, RPGBossThreat::MaxScore));
		}
	}
	SelectTarget();
}

void URPGBossThreatComponent::RefreshTargets()
{
	if (!IsServer() || bUpdating) { return; }
	if (!IsCombatActive()) { ClearThreat(); return; }
	TGuardValue<bool> Guard(bUpdating, true);
	// Rescan catches targets already visible when an encounter begins and missed sight callbacks.
	AAIController* Controller = Cast<AAIController>(CastChecked<APawn>(GetOwner())->GetController());
	UAIPerceptionComponent* Perception = Controller ? Controller->GetPerceptionComponent() : nullptr;
	if (Perception)
	{
		TArray<AActor*> SeenActors;
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SeenActors);
		SeenActors.RemoveAll([](AActor* Actor) { return !IsValid(Actor); });
		SeenActors.Sort([](const AActor& Left, const AActor& Right) { return Left.GetUniqueID() < Right.GetUniqueID(); });
		TSet<TWeakObjectPtr<APawn>> SeenTargets;
		for (AActor* Actor : SeenActors)
		{
			APawn* Target = Cast<APawn>(Actor);
			if (IsEligible(Target))
			{
				SeenTargets.Add(Target);
				if (FMath::IsFinite(InitialSightThreat) && InitialSightThreat > 0.0f)
				{
					if (FThreatEntry* Entry = AdmitTarget(Target))
					{
						Entry->Score = FMath::Max(Entry->Score, FMath::Min<double>(InitialSightThreat, RPGBossThreat::MaxScore));
					}
				}
			}
		}
		for (const TWeakObjectPtr<APawn>& Previous : PerceivedTargets)
		{
			if (!SeenTargets.Contains(Previous))
			{
				Entries.Remove(Previous);
			}
		}
		PerceivedTargets = MoveTemp(SeenTargets);
	}
	SelectTarget();
}

void URPGBossThreatComponent::SelectTarget()
{
	APawn* Best = nullptr;
	const FThreatEntry* BestEntry = nullptr;
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		APawn* Target = It.Key().Get();
		if (!IsEligible(Target) || It.Value().Controller.Get() != Target->GetController())
		{
			PerceivedTargets.Remove(It.Key());
			It.RemoveCurrent();
			continue;
		}
		const FThreatEntry& Entry = It.Value();
		if (!BestEntry || Entry.Score > BestEntry->Score || (Entry.Score == BestEntry->Score && Entry.Order < BestEntry->Order))
		{
			Best = Target;
			BestEntry = &Entry;
		}
	}
	APawn* Override = GetForcedTarget();
	if (Override)
	{
		Best = Override;
	}
	else
	{
		if (ActiveLockToken != 0) { ClearForcedTargetState(); }
		// An expired taunt must not retain its target through normal switch hysteresis.
		if (!bCurrentTargetForced)
		{
			if (APawn* Current = GetCurrentTarget())
			{
				const double Multiplier = FMath::IsFinite(TargetSwitchMultiplier) ? FMath::Clamp<double>(TargetSwitchMultiplier, 1.0, 10.0) : 1.1;
				if (BestEntry && BestEntry->Score <= Entries.FindChecked(Current).Score * Multiplier) { Best = Current; }
			}
		}
	}
	bCurrentTargetForced = Override != nullptr;
	CurrentTarget = IsCombatActive() ? Best : nullptr;
	WriteBlackboardTarget();
}

void URPGBossThreatComponent::WriteBlackboardTarget()
{
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AAIController* Controller = OwnerPawn ? Cast<AAIController>(OwnerPawn->GetController()) : nullptr;
	auto Write = [this](AAIController* AI, APawn* Target)
	{
		UBlackboardComponent* Blackboard = AI ? AI->GetBlackboardComponent() : nullptr;
		if (Blackboard && !TargetActorKey.IsNone())
		{
			const FBlackboard::FKey KeyId = Blackboard->GetKeyID(TargetActorKey);
			if (KeyId != FBlackboard::InvalidKey && Blackboard->GetKeyType(KeyId) == UBlackboardKeyType_Object::StaticClass())
			{
				Blackboard->SetValueAsObject(TargetActorKey, Target);
			}
		}
	};
	TWeakObjectPtr<AAIController> PreviousController = LastBlackboardController;
	LastBlackboardController = Controller;
	if (PreviousController.Get() != Controller) { Write(PreviousController.Get(), nullptr); }
	// An observer clearing the old Blackboard can unpossess/destroy the owner.
	if (!IsServer() || !OwnerPawn || OwnerPawn->GetController() != Controller) { return; }
	Write(Controller, GetCurrentTarget());
}

bool URPGBossThreatComponent::ClearThreat()
{
	if (!IsServer()) { return false; }
	TGuardValue<bool> Guard(bUpdating, true);
	ClearForcedTargetState();
	bCurrentTargetForced = false;
	Entries.Reset();
	PerceivedTargets.Reset();
	CurrentTarget.Reset();
	NextOrder = 0;
	WriteBlackboardTarget();
	return true;
}

void URPGBossThreatComponent::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState)
{
	if (NewState.EncounterId != Previous.EncounterId || NewState.EncounterState != ERPGBossEncounterState::InCombat)
	{
		ClearThreat();
	}
	else if (NewState.PhaseIndex != Previous.PhaseIndex && ActiveLockToken != 0)
	{
		ReleaseForcedTarget(NewState.EncounterId, ActiveLockToken);
	}
}

void URPGBossThreatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshTargets();
}

void URPGBossThreatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearThreat();
	ClearForcedTargetState();
	bEndingPlay = true;
	if (Boss.IsValid()) { Boss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged); }
	Super::EndPlay(EndPlayReason);
}
