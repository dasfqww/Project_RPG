#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossSummonComponent.h"
#include "AI/RPGBossSummonObjectiveComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTNode.h"
#include "BrainComponent.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameplayEffect.h"
#include "HAL/PlatformTime.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "Manager/TeamManager.h"
#include "RPGGameplayTags.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Tests/RPGBossAttackScenario.h"

namespace
{
	constexpr const TCHAR* GuardianClassPath = TEXT("/Game/Blueprints/Character/NPC/Gruntling/Guardian/BP_Gruntling_Guardian.BP_Gruntling_Guardian_C");

	// Explicit development stimulus, not a player skill/hit-query proof.
	void ApplyMechanicStimulus(UObject* Outer, UAbilitySystemComponent* ASC, FGameplayAttribute Attribute, float Value)
	{
		if (!IsValid(ASC) || !ASC->IsOwnerActorAuthoritative() || !FMath::IsFinite(Value)) { return; }
		UGameplayEffect* Effect = NewObject<UGameplayEffect>(Outer);
		Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
		FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Override;
		Modifier.ModifierMagnitude = FScalableFloat(Value);
		ASC->ApplyGameplayEffectToSelf(Effect, 1.f, ASC->MakeEffectContext());
	}
}

void URPGBossMapE2EProbeComponent::OnSummonMechanicEffectApplied(UAbilitySystemComponent* SourceASC,
	const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle, UAbilitySystemComponent* TargetASC)
{
	if (!bEnabled || bFinished || !bSummonMechanicScenario || !OriginalBoss.IsValid() || !SourceASC || !TargetASC) { return; }
	ARPGNonPlayerCharacter* Member = Cast<ARPGNonPlayerCharacter>(SourceASC->GetAvatarActor());
	if (!SummonMechanicMembers.Contains(Member) || !IsValid(Member) || Member->GetOwner() != OriginalBoss.Get() ||
		Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC || !Spec.GetContext().GetAbility()) { return; }
	const float* Before = PhaseCombatHealth.Find(Cast<APawn>(TargetASC->GetAvatarActor()));
	const float After = TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!Before || !FMath::IsFinite(After) || After >= *Before || OriginalBoss->BossAIComponent->GetBossState().PhaseIndex != 1) { return; }
	if (!bSummonMechanicDamage)
	{
		bSummonMechanicDamage = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_GUARDIAN_DAMAGE Before=%g After=%g AuthoredGAS=1"), *Before, After);
	}
}

void URPGBossMapE2EProbeComponent::TickSummonMechanicServer()
{
	if (!bSummonMechanicScenario || !OriginalBoss.IsValid() || !bSawCombatDamage || CombatClients.Num() != 2) { return; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	AAIController* AI = Cast<AAIController>(Boss->GetController());
	if (!ASC || !AI || !AI->GetBrainComponent()) { Fail(TEXT("SummonMechanicSourceInvalid")); return; }
	if (SummonMechanicStep == 0)
	{
		for (const auto& Participant : CombatHealth)
		{
			ARPGPlayer* Player = Cast<ARPGPlayer>(Participant.Key.Get());
			if (!Player) { Fail(TEXT("SummonMechanicParticipantLost")); return; }
			UAbilitySystemComponent* TargetASC = Player->GetAbilitySystemComponent();
			PhaseCombatHealth.Add(Player, TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()));
			PhaseEffectBindings.Emplace(TargetASC, TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this,
				&ThisClass::OnSummonMechanicEffectApplied, TargetASC));
		}
		const float Maximum = ASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute());
		if (!FMath::IsFinite(Maximum) || Maximum <= 0.f) { Fail(TEXT("SummonMechanicInvalidHealth")); return; }
		ApplyMechanicStimulus(this, ASC, URPGAttributeSet::GetCurrentHealthAttribute(), Maximum * .69f);
		SummonMechanicStep = 1;
		Deadline = FPlatformTime::Seconds() + 40.0;
	}
	const FRPGBossState State = Boss->BossAIComponent->GetBossState();
	const FRPGBossSummonObjectiveState Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
	if (SummonMechanicStep <= 3 && (State.PhaseIndex != 1 || AI->GetBrainComponent()->IsRunning() ||
		Boss->GetCharacterMovement()->MovementMode != MOVE_None || !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible)))
	{
		Fail(TEXT("SummonMechanicPhaseProtectionOrAIInvalid")); return;
	}
	const TArray<FRPGBossSummonRecord> Records = Boss->BossSummonComponent->GetSummons();
	if (SummonMechanicStep == 1)
	{
		if (Records.IsEmpty()) { return; }
		if (Records.Num() != 2) { Fail(TEXT("SummonMechanicUnexpectedWaveSize")); return; }
		for (const FRPGBossSummonRecord& Record : Records)
		{
			if (!IsValid(Record.Pawn) || Record.Pawn->GetClass()->GetPathName() != GuardianClassPath || Record.EncounterId != State.EncounterId ||
				Record.PhaseIndex != 1 || Record.Sequence != Objective.Sequence) { Fail(TEXT("SummonMechanicWrongGuardianWave")); return; }
			if (!SummonMechanicOrigins.Contains(Record.Pawn)) { SummonMechanicOrigins.Add(Record.Pawn, Record.Pawn->GetActorLocation()); }
			if (!Record.bInitialized) { return; }
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		APawn* Target = Boss->BossThreatComponent->GetCurrentTarget();
		if (!Nav || !IsValid(Target)) { return; }
		for (const FRPGBossSummonRecord& Record : Records)
		{
			AAIController* MemberAI = Cast<AAIController>(Record.Pawn->GetController());
			const ANavigationData* Data = Nav->GetNavDataForProps(Record.Pawn->GetNavAgentPropertiesRef(), Record.Pawn->GetActorLocation());
			FNavLocation Start, Goal;
			if (!MemberAI || !Data || !Nav->ProjectPointToNavigation(Record.Pawn->GetActorLocation(), Start, FVector(100.f, 100.f, 300.f), Data) ||
				!Nav->ProjectPointToNavigation(Target->GetActorLocation(), Goal, FVector(100.f, 100.f, 300.f), Data))
			{ Fail(TEXT("SummonMechanicGuardianNavigationMissing")); return; }
			const FPathFindingResult Path = Nav->FindPathSync(FPathFindingQuery(MemberAI, *Data, Start.Location, Goal.Location));
			if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial()) { Fail(TEXT("SummonMechanicGuardianPathIncomplete")); return; }
			SummonMechanicMembers.Add(Record.Pawn);
		}
		SummonMechanicSequence = Objective.Sequence;
		SummonMechanicStep = 2;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_WAVE Guardians=2 Ready=1 NavigationPaths=2 Sequence=%d"), SummonMechanicSequence);
	}
	if (SummonMechanicStep == 2)
	{
		if (!bSummonMechanicDamage && FPlatformTime::Seconds() >= SummonGuardianDiagnosticAt)
		{
			SummonGuardianDiagnosticAt = FPlatformTime::Seconds() + 5.0;
			for (const auto& MemberRef : SummonMechanicMembers)
			{
				const ARPGNonPlayerCharacter* Member = MemberRef.Get();
				if (!IsValid(Member)) { continue; }
				const auto* MemberAI = Cast<AAIController>(Member->GetController());
				const auto* Brain = MemberAI ? MemberAI->GetBrainComponent() : nullptr;
				const auto* Tree = Cast<UBehaviorTreeComponent>(Brain);
				const UBTNode* Node = Tree ? Tree->GetActiveNode() : nullptr;
				const auto* Blackboard = MemberAI ? MemberAI->GetBlackboardComponent() : nullptr;
				const AActor* Target = Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(TEXT("TargetActor"))) : nullptr;
				const auto* MemberASC = Member->GetAbilitySystemComponent();
				const auto* Mesh = Member->GetMesh();
				const FVector* Origin = SummonMechanicOrigins.Find(MemberRef);
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_GUARDIAN_DIAG Member=%s Ready=%d OwnerMatches=%d ControllerMatches=%d Team=%d BrainRunning=%d ActiveNode=%s MoveStatus=%d MoveMode=%d Target=%s Distance=%g Travel=%g MeshTick=%d AnimTickPolicy=%d RecentlyRendered=%d Montage=%s Tags=%s"),
					*Member->GetName(), int32(Member->IsNPCStartUpDataReady()), int32(Member->GetOwner() == Boss),
					int32(MemberAI && Member->GetEncounterSummonController() == MemberAI), MemberAI ? int32(MemberAI->GetGenericTeamId().GetId()) : -1,
					Brain ? int32(Brain->IsRunning()) : -1, Node ? *Node->GetNodeName() : TEXT("None"), MemberAI ? int32(MemberAI->GetMoveStatus()) : -1,
					int32(Member->GetCharacterMovement()->MovementMode), *GetNameSafe(Target), IsValid(Target) ? FVector::Dist2D(Member->GetActorLocation(), Target->GetActorLocation()) : -1.f,
					Origin ? FVector::Dist2D(*Origin, Member->GetActorLocation()) : -1.f,
					Mesh ? int32(Mesh->IsComponentTickEnabled()) : -1, Mesh ? int32(Mesh->VisibilityBasedAnimTickOption) : -1,
					int32(Member->WasRecentlyRendered()), *GetNameSafe(MemberASC ? MemberASC->GetCurrentMontage() : nullptr),
					MemberASC ? *MemberASC->GetOwnedGameplayTags().ToStringSimple() : TEXT("None"));
			}
		}
		bool bMoved = false;
		for (const auto& Entry : SummonMechanicOrigins)
		{
			if (Entry.Key.IsValid() && FVector::DistSquared2D(Entry.Value, Entry.Key->GetActorLocation()) > FMath::Square(25.f)) { bMoved = true; }
		}
		if (!bMoved || !bSummonMechanicDamage || SummonMechanicReadyClients.Num() != 2) { return; }
		ARPGNonPlayerCharacter* Member = SummonMechanicMembers[0].Get();
		if (!IsValid(Member)) { Fail(TEXT("SummonMechanicFirstGuardianLost")); return; }
		UAbilitySystemComponent* MemberASC = Member->GetAbilitySystemComponent();
		ApplyMechanicStimulus(this, MemberASC, URPGAttributeSet::GetTakeDamageAttribute(),
			MemberASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) + 1.f);
		SummonMechanicStep = 3;
	}
	if (SummonMechanicStep == 3)
	{
		if (Boss->BossSummonObjectiveComponent->GetObjectiveState().DefeatedCount != 1) { Fail(TEXT("SummonMechanicFirstKillNotCounted")); return; }
		if (SummonMechanicPartialClients.Num() != 2) { return; }
		ARPGNonPlayerCharacter* Member = SummonMechanicMembers[1].Get();
		if (!IsValid(Member)) { Fail(TEXT("SummonMechanicSecondGuardianLost")); return; }
		UAbilitySystemComponent* MemberASC = Member->GetAbilitySystemComponent();
		if (bSummonPlayerKillScenario)
		{
			// One server kill preserves the existing 1/2 replication barrier. Only
			// the final kill uses real remote input; do not claim two player kills.
			APlayerController* Attacker = nullptr;
			float Nearest = TNumericLimits<float>::Max();
			for (const auto& Client : ReadyClients)
			{
				APlayerController* PC = Client.Get();
				if (!IsValid(PC) || PC->IsLocalController() || !IsValid(PC->GetPawn())) { continue; }
				const float Distance = FVector::DistSquared2D(PC->GetPawn()->GetActorLocation(), Member->GetActorLocation());
				if (Distance < Nearest) { Nearest = Distance; Attacker = PC; }
			}
			ARPGPlayer* Player = Attacker ? Cast<ARPGPlayer>(Attacker->GetPawn()) : nullptr;
			UAbilitySystemComponent* PlayerASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
			FGameplayAbilitySpec* Attack = RPGBossAttackScenarios::FindAbility(PlayerASC, RPGBossAttackScenarios::Current());
			auto* Security = Player ? Player->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
			auto* InputProbe = Attacker ? Attacker->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
			if (!Attack || Attack->IsActive() || !Security || !InputProbe || !MemberASC ||
				MemberASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) <= 0.f)
			{ Fail(TEXT("SummonPlayerKillAttackerOrVictimInvalid")); return; }
			// In-memory low-health fixture bounds this path to one authored hit.
			// No attack power, target data, hit geometry or ability eligibility edits.
			ApplyMechanicStimulus(this, MemberASC, URPGAttributeSet::GetCurrentHealthAttribute(), 1.f);
			PlayerAttackTarget = Player;
			PlayerAttackVictim = Member;
			PlayerAttackASC = PlayerASC;
			PlayerAttackHandle = Attack->Handle;
			PlayerAttackViolationBaseline = Security->GetTotalViolationCount();
			PlayerAttackEndedHandle = PlayerASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandlePlayerAttackEnded);
			SummonPlayerActivatedHandle = PlayerASC->AbilityActivatedCallbacks.AddUObject(this, &ThisClass::OnSummonPlayerAttackActivated);
			PhaseEffectBindings.Emplace(MemberASC, MemberASC->OnGameplayEffectAppliedDelegateToSelf.AddUObject(this,
				&ThisClass::OnSummonPlayerKillEffectApplied));
			bPlayerAttackRequested = true;
			SummonMechanicStep = 4;
			InputProbe->ClientBeginPlayerAttack(Player, 1.f, Member);
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_KILL_REQUEST Skill=AssultBlade RemoteOwner=1 VictimHPFixture=1 FirstKillServerStimulus=1"));
			return;
		}
		ApplyMechanicStimulus(this, MemberASC, URPGAttributeSet::GetTakeDamageAttribute(),
			MemberASC->GetNumericAttribute(URPGAttributeSet::GetMaxHealthAttribute()) + 1.f);
		SummonMechanicStep = 4;
		return; // Completion deliberately happens outside the GAS death callback stack.
	}
	if (SummonMechanicStep == 4)
	{
		if (bSummonPlayerKillScenario)
		{
			ARPGPlayer* Player = Cast<ARPGPlayer>(PlayerAttackTarget.Get());
			const auto* Security = Player ? Player->FindComponentByClass<URPGSecurityValidationComponent>() : nullptr;
			if (!Security || Security->GetTotalViolationCount() != PlayerAttackViolationBaseline)
			{ Fail(TEXT("SummonPlayerKillSecurityChanged")); return; }
			if (!bSummonPlayerKillHit || !bPlayerAttackOwnerConfirmed || !bPlayerAttackEndedNaturally) { return; }
		}
		if (State.PhaseIndex != 2) { return; }
		if (Objective.bActive || !Records.IsEmpty() || ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible) ||
			!AI->GetBrainComponent()->IsRunning() || Boss->GetCharacterMovement()->MovementMode == MOVE_None)
		{ Fail(TEXT("SummonMechanicCombatResumeInvalid")); return; }
		for (const auto& Member : SummonMechanicMembers) { if (Member.IsValid() && !Member->IsActorBeingDestroyed()) { return; } }
		if (SummonMechanicCompleteClients.Num() != 2) { return; }
		if (bSummonPlayerKillScenario)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_KILL_COMPLETE Skill=AssultBlade EnhancedInput=1 ServerHitContext=1 NaturalEnd=1 SecurityUnchanged=1 VictimHPFixture=1 FirstKillServerStimulus=1 Clients=2"));
			CleanupPlayerAttackProbe();
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_COMPLETE GuardianNavigation=1 GuardianGAS=1 ProgressClients=2 ResumeClients=2 ServerKillStimulus=1"));
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
		CleanupCombatPhaseProbe();
		bFinished = true;
		SetComponentTickEnabled(false);
	}
}

void URPGBossMapE2EProbeComponent::OnSummonPlayerAttackActivated(UGameplayAbility* Ability)
{
	if (!bEnabled || bFinished || !bSummonPlayerKillScenario || !Ability || !PlayerAttackASC.IsValid() ||
		Ability->GetCurrentAbilitySpecHandle() != PlayerAttackHandle || SummonPlayerTargetDataHandle.IsValid()) { return; }
	// PreActivate notifies observers before the skill installs its target-data
	// consumer. This listener only reads; it must never consume or rewrite data.
	SummonPlayerPredictionKey = Ability->GetCurrentActivationInfo().GetActivationPredictionKey();
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_TARGET_WATCH PredictionKey=%s"), *SummonPlayerPredictionKey.ToString());
	const TWeakObjectPtr<UGameplayAbility> ObservedAbility = Ability;
	SummonPlayerTargetDataHandle = PlayerAttackASC->AbilityTargetDataSetDelegate(PlayerAttackHandle, SummonPlayerPredictionKey).AddWeakLambda(this,
		[this, ObservedAbility](const FGameplayAbilityTargetDataHandle& Data, FGameplayTag)
		{
			if (ObservedAbility.IsValid()) RPGBossAttackScenarios::ObserveSubmittedTarget(ObservedAbility.Get(), Data, PlayerAttackVictim.Get());
		});
}

void URPGBossMapE2EProbeComponent::CleanupSummonPlayerTargetObservation()
{
	if (PlayerAttackASC.IsValid())
	{
		PlayerAttackASC->AbilityActivatedCallbacks.Remove(SummonPlayerActivatedHandle);
		if (SummonPlayerTargetDataHandle.IsValid())
			PlayerAttackASC->AbilityTargetDataSetDelegate(PlayerAttackHandle, SummonPlayerPredictionKey).Remove(SummonPlayerTargetDataHandle);
	}
	SummonPlayerActivatedHandle.Reset();
	SummonPlayerTargetDataHandle.Reset();
}

void URPGBossMapE2EProbeComponent::OnSummonPlayerKillEffectApplied(UAbilitySystemComponent* SourceASC,
	const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle Handle)
{
	if (!bEnabled || bFinished || !bSummonPlayerKillScenario || SummonMechanicStep != 4 ||
		!Spec.SetByCallerTagMagnitudes.Contains(RPGGameplayTags::Shared_SetByCaller_BaseDamage)) { return; }
	const FRPGBossAttackScenario& Scenario = RPGBossAttackScenarios::Current();
	const FGameplayAbilitySpec* Attack = PlayerAttackASC.IsValid() ? PlayerAttackASC->FindAbilitySpecFromHandle(PlayerAttackHandle) : nullptr;
	const UGameplayAbility* Ability = Attack ? Attack->GetPrimaryInstance() : nullptr;
	ARPGNonPlayerCharacter* Member = Cast<ARPGNonPlayerCharacter>(PlayerAttackVictim.Get());
	if (!IsValid(Member) || !OriginalBoss.IsValid() || Member->GetOwner() != OriginalBoss.Get() ||
		SourceASC != PlayerAttackASC.Get() || !Attack || !Attack->IsActive() || !Ability || Ability->GetClass()->GetPathName() != Scenario.AbilityPath ||
		Spec.GetContext().GetInstigatorAbilitySystemComponent() != SourceASC || !Scenario.HitEvidence ||
		!Scenario.HitEvidence->ContainsCommittedServerHit(Spec, Member, Ability))
	{ Fail(TEXT("SummonPlayerKillUnprovenDamageSource")); return; }
	const float Health = Member->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute());
	if (!FMath::IsFinite(Health) || Health != 0.f) { Fail(TEXT("SummonPlayerKillNotLethal")); return; }
	bSummonPlayerKillHit = true;
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_KILL_HIT Skill=AssultBlade AuthoredAbilityActive=1 ServerHitContext=1 HPZero=1"));
}

void URPGBossMapE2EProbeComponent::ConfirmSummonPlayerAttack(int32 Checkpoint)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
	auto* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || PC->IsLocalController() || !Driver || !Driver->bEnabled || Driver->bFinished || !Driver->bSummonPlayerKillScenario) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	if (Checkpoint == 0 && Driver->ServerStage <= 1 && Player &&
		RPGBossAttackScenarios::FindAbility(Player->GetAbilitySystemComponent(), RPGBossAttackScenarios::Current()))
	{ Driver->PlayerAttackReadyClients.Add(PC); }
	else if (Checkpoint == 2 && Driver->ServerStage == 2 && Driver->SummonMechanicStep == 4 &&
		Driver->ReadyClients.Contains(PC) && Player && Player == Driver->PlayerAttackTarget.Get() && Driver->bSummonPlayerKillHit)
	{ Driver->bPlayerAttackOwnerConfirmed = true; }
}

void URPGBossMapE2EProbeComponent::TickSummonMechanicClient()
{
	if (!bSummonMechanicScenario || !OriginalBoss.IsValid() || ClientPhase != 3) { return; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	const FRPGBossState State = Boss->BossAIComponent->GetBossState();
	const FRPGBossSummonObjectiveState Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
	if (State.PhaseIndex == 0)
	{
		for (TActorIterator<ARPGPlayer> It(GetWorld()); It; ++It)
		{
			PhaseCombatHealth.Add(*It, It->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()));
		}
		return;
	}
	for (const auto& Entry : PhaseCombatHealth)
	{
		const ARPGPlayer* Player = Cast<ARPGPlayer>(Entry.Key.Get());
		if (Player && Player->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) < Entry.Value)
		{ bSummonMechanicClientDamage = true; }
	}
	// Replication between boss state, objective and member actors is not ordered.
	// Only acknowledge a coherent snapshot; retry the RPC until the server advances.
	if (State.PhaseIndex == 1 && Objective.bActive && Objective.EncounterId == State.EncounterId && Objective.PhaseIndex == 1 && Objective.RequiredCount == 2)
	{
		if (Objective.DefeatedCount == 0 && bSummonMechanicClientDamage)
		{
			const TArray<FRPGBossSummonRecord> Records = Boss->BossSummonComponent->GetSummons();
			if (Records.Num() != 2) { return; }
			const auto* Teams = GetWorld()->GetSubsystem<UTeamManager>();
			const auto* PC = Cast<APlayerController>(GetOwner());
			const APawn* LocalPawn = PC ? PC->GetPawn() : nullptr;
			if (!Teams || !IsValid(LocalPawn)) { return; }
			for (const FRPGBossSummonRecord& Record : Records)
			{
				if (!IsValid(Record.Pawn) || Record.Pawn->GetOwner() != Boss || Record.Pawn->GetClass()->GetPathName() != GuardianClassPath ||
					Record.Sequence != Objective.Sequence || Record.EncounterId != State.EncounterId || Record.PhaseIndex != 1) { return; }
				if (Teams->CompareTeams(LocalPawn, Record.Pawn) != ERPGTeamComparison::DifferentTeams) { return; }
			}
			SummonMechanicSequence = Objective.Sequence;
			if (SummonMechanicClientStep == 0)
			{
				SummonMechanicClientStep = 1;
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_CLIENT_TEAMS_READY Guardians=2 HostileToLocalPawn=1"));
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_WAVE Guardians=2 Objective=2 ParticipantHPDecreased=1"));
			}
			ServerConfirmCombatPhase(10, SummonMechanicSequence);
		}
		else if (Objective.DefeatedCount == 1 && SummonMechanicClientStep >= 1 && Objective.Sequence == SummonMechanicSequence)
		{
			if (SummonMechanicClientStep == 1)
			{
				SummonMechanicClientStep = 2;
				UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_PARTIAL Defeated=1 Required=2 Phase=1"));
			}
			ServerConfirmCombatPhase(11, SummonMechanicSequence);
		}
	}
	else if (State.PhaseIndex == 2 && !Objective.bActive && Objective.EncounterId == State.EncounterId && Objective.PhaseIndex == 2 &&
		Boss->BossSummonComponent->GetSummons().IsEmpty() && SummonMechanicClientStep >= 2)
	{
		if (SummonMechanicClientStep == 2)
		{
			SummonMechanicClientStep = 3;
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_MECHANIC_CLIENT_COMPLETE Phase=2 ObjectiveInactive=1 WaveCleared=1"));
		}
		ServerConfirmCombatPhase(12, SummonMechanicSequence);
	}
}

void URPGBossMapE2EProbeComponent::ConfirmSummonMechanic(int32 Checkpoint, int32 Sequence)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	AGameModeBase* Mode = GetWorld()->GetAuthGameMode();
	URPGBossMapE2EProbeComponent* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || PC->IsLocalController() || !Driver || !Driver->bSummonMechanicScenario || Driver->bFinished ||
		Driver->ServerStage != 2 || !Driver->ReadyClients.Contains(PC) || !Driver->OriginalBoss.IsValid() ||
		Sequence <= 0 || Sequence != Driver->SummonMechanicSequence) { return; }
	const ARPGBossCharacter* Boss = Driver->OriginalBoss.Get();
	const FRPGBossState State = Boss->BossAIComponent->GetBossState();
	const FRPGBossSummonObjectiveState Objective = Boss->BossSummonObjectiveComponent->GetObjectiveState();
	if (Objective.EncounterId != State.EncounterId || Objective.PhaseIndex != State.PhaseIndex) { return; }
	if (Checkpoint == 10 && Driver->SummonMechanicStep == 2 && Driver->bSummonMechanicDamage && State.PhaseIndex == 1 &&
		Objective.bActive && Objective.Sequence == Sequence && Objective.RequiredCount == 2 && Objective.DefeatedCount == 0)
	{ Driver->SummonMechanicReadyClients.Add(PC); }
	else if (Checkpoint == 11 && Driver->SummonMechanicStep == 3 && State.PhaseIndex == 1 && Objective.bActive &&
		Objective.Sequence == Sequence && Objective.DefeatedCount == 1 && Driver->SummonMechanicReadyClients.Contains(PC))
	{ Driver->SummonMechanicPartialClients.Add(PC); }
	else if (Checkpoint == 12 && Driver->SummonMechanicStep == 4 && State.PhaseIndex == 2 && !Objective.bActive &&
		Boss->BossSummonComponent->GetSummons().IsEmpty() && Driver->SummonMechanicPartialClients.Contains(PC))
	{ Driver->SummonMechanicCompleteClients.Add(PC); }
}
