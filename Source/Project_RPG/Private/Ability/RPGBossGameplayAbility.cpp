#include "Ability/RPGBossGameplayAbility.h"
#include "Ability/RPGBossPatternCooldownEffect.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "AI/RPGBossThreatComponent.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "FunctionLibrary/RPGAbilityFunctionLibrary.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

URPGBossGameplayAbility::URPGBossGameplayAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
	bRetriggerInstancedAbility = false;
	AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnTriggered;
	ActivationPolicy = ERPGGladiatorAbilityActivationPolicy::Manual;
}

bool URPGBossGameplayAbility::IsPatternCooldownConfigurationValid() const
{
	// Do not silently replace an independently authored cooldown GE.
	return FMath::IsFinite(PatternCooldownSeconds) && PatternCooldownSeconds >= 0.f && PatternCooldownSeconds <= 600.f &&
		(PatternCooldownSeconds == 0.f || (PatternCooldownTag.IsValid() && !CooldownGameplayEffectClass));
}

const FGameplayTagContainer* URPGBossGameplayAbility::GetCooldownTags() const
{
	if (PatternCooldownSeconds == 0.f) { return Super::GetCooldownTags(); }
	CachedPatternCooldownTags.Reset();
	if (PatternCooldownTag.IsValid()) { CachedPatternCooldownTags.AddTag(PatternCooldownTag); }
	return &CachedPatternCooldownTags;
}

bool URPGBossGameplayAbility::CommitCheck(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, FGameplayTagContainer* OptionalRelevantTags)
{
	return IsPatternCooldownConfigurationValid() && (PatternCooldownSeconds == 0.f || IsBossExecutionValid()) &&
		Super::CommitCheck(Handle, ActorInfo, ActivationInfo, OptionalRelevantTags);
}

void URPGBossGameplayAbility::ApplyCooldown(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (PatternCooldownSeconds == 0.f)
	{
		Super::ApplyCooldown(Handle, ActorInfo, ActivationInfo);
		return;
	}
	if (!IsPatternCooldownConfigurationValid() || !IsBossExecutionValid()) { return; }
	const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		URPGBossPatternCooldownEffect::StaticClass(), GetAbilityLevel(Handle, ActorInfo));
	if (!Spec.IsValid()) { return; }
	Spec.Data->SetDuration(PatternCooldownSeconds, true);
	Spec.Data->DynamicGrantedTags.AddTag(PatternCooldownTag);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	// GAS owns expiry. Cancellation/phase transition/reset does not refund a committed cooldown.
}

bool URPGBossGameplayAbility::PassesActivationTargetConditions(const APawn* Source, const APawn* Target, bool bRequiresTarget) const
{
	if (!IsValid(Source) || !FMath::IsFinite(MinimumActivationDistance) || !FMath::IsFinite(MaximumActivationDistance) ||
		MinimumActivationDistance < 0.f || MaximumActivationDistance < 0.f ||
		MinimumActivationDistance > 1000000.f || MaximumActivationDistance > 1000000.f ||
		(MaximumActivationDistance > 0.f && MinimumActivationDistance > MaximumActivationDistance)) { return false; }
	if (!bRequiresTarget) { return true; }
	if (!IsValid(Target)) { return false; }
	if (MinimumActivationDistance > 0.f || MaximumActivationDistance > 0.f)
	{
		const FVector SourceLocation = Source->GetActorLocation();
		const FVector TargetLocation = Target->GetActorLocation();
		if (SourceLocation.ContainsNaN() || TargetLocation.ContainsNaN()) { return false; }
		const double DistanceSquared = FVector::DistSquared2D(SourceLocation, TargetLocation);
		if (!FMath::IsFinite(DistanceSquared) || DistanceSquared < FMath::Square(double(MinimumActivationDistance)) ||
			(MaximumActivationDistance > 0.f && DistanceSquared > FMath::Square(double(MaximumActivationDistance)))) { return false; }
	}
	const AController* Controller = Source->GetController();
	return !bRequireActivationLineOfSight || (IsValid(Controller) && Controller->LineOfSightTo(Target));
}

bool URPGBossGameplayAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	APawn* Pawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!IsPatternCooldownConfigurationValid() || !IsValid(Pawn) || !Pawn->HasAuthority() || !IsValid(Pawn->GetController()) || !ASC ||
		ASC->GetAvatarActor() != Pawn || InstancingPolicy != EGameplayAbilityInstancingPolicy::InstancedPerActor ||
		NetExecutionPolicy != EGameplayAbilityNetExecutionPolicy::ServerOnly ||
		NetSecurityPolicy != EGameplayAbilityNetSecurityPolicy::ServerOnly ||
		AbilityActivationPolicy != ERPGAbilityActivationPolicy::OnTriggered ||
		ActivationPolicy != ERPGGladiatorAbilityActivationPolicy::Manual || ExecutionSerial == MAX_uint64)
	{
		return false;
	}
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
	const URPGBossAIComponent* State = Pawn->FindComponentByClass<URPGBossAIComponent>();
	const URPGBossThreatComponent* Targets = Pawn->FindComponentByClass<URPGBossThreatComponent>();
	const bool bRequiresTarget = RequiresExecutionTarget();
	const APawn* Target = Targets && bRequiresTarget ? Targets->GetCurrentTarget() : nullptr;
	return Spec && !Spec->IsActive() && State && Targets &&
		State->GetBossState().EncounterState == ERPGBossEncounterState::InCombat &&
		PassesActivationTargetConditions(Pawn, Target, bRequiresTarget) &&
		Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
}

void URPGBossGameplayAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	++ExecutionSerial;
	BossPawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
	Boss = BossPawn.IsValid() ? BossPawn->FindComponentByClass<URPGBossAIComponent>() : nullptr;
	Threat = BossPawn.IsValid() ? BossPawn->FindComponentByClass<URPGBossThreatComponent>() : nullptr;
	BossController = BossPawn.IsValid() ? BossPawn->GetController() : nullptr;
	bRequiresTargetThisExecution = RequiresExecutionTarget();
	ExecutionTarget = bRequiresTargetThisExecution && Threat.IsValid() ? Threat->GetCurrentTarget() : nullptr;
	ExecutionTargetController = ExecutionTarget.IsValid() ? ExecutionTarget->GetController() : nullptr;
	const FRPGBossState State = Boss.IsValid() ? Boss->GetBossState() : FRPGBossState();
	EncounterId = State.EncounterId;
	PhaseIndex = State.PhaseIndex;
	bContextOpen = true;
	bDamageAppliedThisExecution = false;
	bAreaDamageResolvedThisExecution = false;
	TelegraphEncounterId = 0;
	TelegraphSequence = 0;
	// Recheck the captured target before Blueprint activation, costs or windup.
	// Do not turn start-range rules into continuous cancellation rules during a dash.
	if (!IsBossExecutionValid() || !PassesActivationTargetConditions(BossPawn.Get(), ExecutionTarget.Get(), bRequiresTargetThisExecution))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	Boss->OnBossStateChanged.AddUniqueDynamic(this, &ThisClass::OnBossStateChanged);
	// Captured context must be available before Blueprint ActivateAbility runs.
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

int32 URPGBossGameplayAbility::StartBossTelegraph(FGameplayTag ActionTag, FVector AimLocation, float WindupSeconds)
{
	if (TelegraphSequence != 0 || !IsBossExecutionValid()) { return 0; }
	const uint64 StartedSerial = ExecutionSerial;
	const int32 StartedEncounter = EncounterId;
	const TWeakObjectPtr<URPGBossAIComponent> StartedBoss = Boss;
	const int32 Sequence = StartedBoss->BeginTelegraph(ActionTag, AimLocation, WindupSeconds);
	if (Sequence != 0)
	{
		// A presentation listener may cancel the ability while BeginTelegraph broadcasts.
		if (!bContextOpen || !IsActive() || ExecutionSerial != StartedSerial ||
			EncounterId != StartedEncounter || !IsBossExecutionValid())
		{
			if (StartedBoss.IsValid()) { StartedBoss->EndTelegraph(StartedEncounter, Sequence); }
			return 0;
		}
		TelegraphEncounterId = EncounterId;
		TelegraphSequence = Sequence;
	}
	return Sequence;
}

bool URPGBossGameplayAbility::FinishBossTelegraph()
{
	if (!Boss.IsValid() || TelegraphSequence == 0) { return false; }
	const int32 ExpectedEncounter = TelegraphEncounterId;
	const int32 ExpectedSequence = TelegraphSequence;
	TelegraphEncounterId = 0;
	TelegraphSequence = 0;
	return Boss->EndTelegraph(ExpectedEncounter, ExpectedSequence);
}

bool URPGBossGameplayAbility::ExecuteTargetedServerDamage(const FTransform& QueryTransform,
	const FRPGHitQueryProfile& HitProfile, TSubclassOf<UGameplayEffect> DamageEffectClass,
	const FScalableFloat& BaseDamage, const FRPGSkillSecurityProfile& SecurityProfile,
	FRPGHitQueryResult& OutHit)
{
	OutHit = FRPGHitQueryResult();
	FString ProfileError;
	if (bApplyingDamage || bDamageAppliedThisExecution || bAreaDamageResolvedThisExecution || !IsBossExecutionValid() || !DamageEffectClass ||
		QueryTransform.ContainsNaN() || HitProfile.Filter.TeamRule != ERPGHitQueryTeamRule::HostileOnly ||
		HitProfile.Filter.bIncludeSourceActor || !SecurityProfile.bRequireAuthorityHitQuery ||
		!SecurityProfile.IsValid(&ProfileError))
	{
		return false;
	}
	UWorld* World = GetWorld();
	URPGHitQuerySubsystem* HitQueries = World ? World->GetSubsystem<URPGHitQuerySubsystem>() : nullptr;
	APawn* Target = GetBossExecutionTarget();
	if (!HitQueries || !Target) { return false; }

	FRPGHitQueryContext Context;
	Context.SourceActor = BossPawn.Get();
	Context.QueryTransform = QueryTransform;
	Context.Profile = HitProfile;
	Context.Profile.Filter.MaxResults = HitProfile.Filter.MaxResults > 0
		? FMath::Min(HitProfile.Filter.MaxResults, SecurityProfile.MaximumTargetsPerQuery)
		: SecurityProfile.MaximumTargetsPerQuery;
	TArray<FRPGHitQueryResult> Results;
	if (!HitQueries->ExecuteHitQuery(Context, Results)) { return false; }
	const FRPGHitQueryResult* TargetHit = Results.FindByPredicate(
		[Target](const FRPGHitQueryResult& Result) { return Result.TargetActor == Target; });
	if (!TargetHit || TargetHit->HitResult.GetActor() != Target || !IsBossExecutionValid()) { return false; }

	const FGameplayEffectSpecHandle Spec = MakeEnemyDamageEffectSpecHandle(DamageEffectClass, BaseDamage);
	if (!Spec.IsValid() || !Spec.Data->Def ||
		Spec.Data->Def->DurationPolicy != EGameplayEffectDurationType::Instant ||
		Spec.Data->GetDuration() != UGameplayEffect::INSTANT_APPLICATION || Spec.Data->GetPeriod() != 0.f)
	{
		return false;
	}
	// GE application invokes synchronous callbacks, which may re-enter or restart this ability.
	TGuardValue<bool> ApplyingGuard(bApplyingDamage, true);
	const uint64 ApplyingSerial = ExecutionSerial;
	if (!URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(
		BossPawn.Get(), TargetHit->HitResult, Spec, SecurityProfile))
	{
		return false;
	}
	if (ExecutionSerial == ApplyingSerial) { bDamageAppliedThisExecution = true; }
	OutHit = *TargetHit;
	return true;
}

int32 URPGBossGameplayAbility::ExecuteAreaServerDamage(const FTransform& QueryTransform,
	const FRPGHitQueryProfile& HitProfile, TSubclassOf<UGameplayEffect> DamageEffectClass,
	const FScalableFloat& BaseDamage, const FRPGSkillSecurityProfile& SecurityProfile,
	TArray<FRPGHitQueryResult>& OutHits)
{
	OutHits.Reset();
	FString ProfileError;
	if (bApplyingDamage || bDamageAppliedThisExecution || bAreaDamageResolvedThisExecution || !IsBossExecutionValid() ||
		!DamageEffectClass || QueryTransform.ContainsNaN() ||
		HitProfile.Filter.TeamRule != ERPGHitQueryTeamRule::HostileOnly || HitProfile.Filter.bIncludeSourceActor ||
		!SecurityProfile.bRequireAuthorityHitQuery || !SecurityProfile.IsValid(&ProfileError)) { return 0; }
	UWorld* World = GetWorld();
	URPGHitQuerySubsystem* HitQueries = World ? World->GetSubsystem<URPGHitQuerySubsystem>() : nullptr;
	if (!HitQueries) { return 0; }
	const uint64 ApplyingSerial = ExecutionSerial;
	const FGameplayEffectSpecHandle Template = MakeEnemyDamageEffectSpecHandle(DamageEffectClass, BaseDamage);
	if (!Template.IsValid() || !Template.Data->Def ||
		Template.Data->Def->DurationPolicy != EGameplayEffectDurationType::Instant ||
		Template.Data->GetDuration() != UGameplayEffect::INSTANT_APPLICATION || Template.Data->GetPeriod() != 0.f ||
		ExecutionSerial != ApplyingSerial || !IsBossExecutionValid()) { return 0; }
	FRPGHitQueryContext Context;
	Context.SourceActor = BossPawn.Get();
	Context.QueryTransform = QueryTransform;
	Context.Profile = HitProfile;
	Context.Profile.Filter.MaxResults = HitProfile.Filter.MaxResults > 0
		? FMath::Min(HitProfile.Filter.MaxResults, SecurityProfile.MaximumTargetsPerQuery)
		: SecurityProfile.MaximumTargetsPerQuery;
	bAreaDamageResolvedThisExecution = true;
	TArray<FRPGHitQueryResult> Results;
	if (!HitQueries->ExecuteHitQuery(Context, Results)) { return 0; }
	if (ExecutionSerial != ApplyingSerial || !IsBossExecutionValid()) { return 0; }
	// A hit callback can change another participant's controller or remove their threat.
	// Capture identities before applying any effects, then recheck eligibility before each hit.
	TArray<TWeakObjectPtr<AController>> Controllers;
	for (const FRPGHitQueryResult& Hit : Results)
	{
		APawn* Participant = Cast<APawn>(Hit.TargetActor);
		Controllers.Add(Participant && Threat->GetThreatForTarget(Participant) > 0.0
			? Participant->GetController() : nullptr);
	}
	TGuardValue<bool> ApplyingGuard(bApplyingDamage, true);
	TSet<TWeakObjectPtr<APawn>> Visited;
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		if (ExecutionSerial != ApplyingSerial || !IsActive() || !IsBossEncounterExecutionValid()) { break; }
		const FRPGHitQueryResult& Hit = Results[Index];
		APawn* Participant = Cast<APawn>(Hit.TargetActor);
		if (!IsValid(Participant) || Hit.HitResult.GetActor() != Participant || !Controllers[Index].IsValid() ||
			Participant->GetController() != Controllers[Index].Get() || Visited.Contains(Participant) ||
			Threat->GetThreatForTarget(Participant) <= 0.0) { continue; }
		Visited.Add(Participant);
		const FGameplayEffectSpecHandle Spec(new FGameplayEffectSpec(*Template.Data));
		if (URPGAbilityFunctionLibrary::ApplyGameplayEffectSpecHandleToServerHit(BossPawn.Get(), Hit.HitResult, Spec, SecurityProfile))
		{
			if (ExecutionSerial == ApplyingSerial) { bDamageAppliedThisExecution = true; }
			OutHits.Add(Hit);
		}
	}
	return OutHits.Num();
}

bool URPGBossGameplayAbility::IsBossEncounterExecutionValid() const
{
	if (!bContextOpen || !BossPawn.IsValid() || !BossPawn->HasAuthority() || !BossController.IsValid() ||
		BossPawn->GetController() != BossController.Get() || !Boss.IsValid() || !Threat.IsValid() ||
		!CurrentActorInfo || CurrentActorInfo->AvatarActor.Get() != BossPawn.Get())
	{
		return false;
	}
	const FRPGBossState State = Boss->GetBossState();
	return State.EncounterState == ERPGBossEncounterState::InCombat && State.EncounterId == EncounterId &&
		State.PhaseIndex == PhaseIndex;
}

bool URPGBossGameplayAbility::IsBossExecutionValid() const
{
	if (!IsBossEncounterExecutionValid()) { return false; }
	if (!bRequiresTargetThisExecution) { return true; }
	return ExecutionTarget.IsValid() && ExecutionTargetController.IsValid() &&
		ExecutionTarget->GetController() == ExecutionTargetController.Get() && Threat->GetCurrentTarget() == ExecutionTarget.Get();
}

APawn* URPGBossGameplayAbility::GetBossExecutionTarget() const
{
	return IsBossExecutionValid() ? ExecutionTarget.Get() : nullptr;
}

void URPGBossGameplayAbility::OnBossStateChanged(const FRPGBossState& Previous, const FRPGBossState& NewState)
{
	if (IsActive() && !IsBossExecutionValid())
	{
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
	}
}

void URPGBossGameplayAbility::EndAbility(FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	FinishBossTelegraph();
	bContextOpen = false;
	if (Boss.IsValid()) { Boss->OnBossStateChanged.RemoveDynamic(this, &ThisClass::OnBossStateChanged); }
	// No cleanup after Super: end observers may immediately start a new execution on this instance.
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
