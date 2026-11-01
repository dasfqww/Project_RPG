#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "Tests/RPGBossAbilityTestFixture.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "Ability/RPGBossPersistentHazardAbility.h"
#include "AI/RPGBossPersistentHazard.h"
#include "Controller/RPGAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

#include "AbilitySystemComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/PawnUIComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AI/RPGBossDefinition.h"
#include "Attribute/RPGAttributeSet.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTestPhaseOne, "Tests.RaidBoss.Phase.One");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTestPhaseTwo, "Tests.RaidBoss.Phase.Two");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTestPhaseThree, "Tests.RaidBoss.Phase.Three");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTestTelegraph, "Tests.RaidBoss.Network.Telegraph");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossTestHazard, "Tests.RaidBoss.Network.Hazard");

ARPGBossTestPawn::ARPGBossTestPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	USphereComponent* QueryCollision = CreateDefaultSubobject<USphereComponent>(TEXT("QueryCollision"));
	SetRootComponent(QueryCollision);
	QueryCollision->InitSphereRadius(34.f);
	QueryCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	QueryCollision->SetCollisionObjectType(ECC_Pawn);
	QueryCollision->SetCollisionResponseToAllChannels(ECR_Overlap);
	ASC = CreateDefaultSubobject<URPGAbilitySystemComponent>(TEXT("ASC"));
	ASC->SetIsReplicated(true);
	ASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	Attributes = CreateDefaultSubobject<URPGAttributeSet>(TEXT("Attributes"));
	Boss = CreateDefaultSubobject<URPGBossAIComponent>(TEXT("Boss"));
	PawnUI = CreateDefaultSubobject<UPawnUIComponent>(TEXT("PawnUI"));
}

UAbilitySystemComponent* ARPGBossTestPawn::GetAbilitySystemComponent() const
{
	return ASC;
}

URPGBossDefinition* ARPGBossTestPawn::MakeDefinition(UObject* Outer)
{
	URPGBossDefinition* Result = NewObject<URPGBossDefinition>(Outer);
	FRPGBossPhaseDefinition Phase;
	Phase.PhaseTag = TAG_BossTestPhaseOne;
	Result->Phases.Add(Phase);
	Phase.PhaseTag = TAG_BossTestPhaseTwo;
	Phase.HealthThreshold = 0.7f;
	Result->Phases.Add(Phase);
	Phase.PhaseTag = TAG_BossTestPhaseThree;
	Phase.HealthThreshold = 0.3f;
	Result->Phases.Add(Phase);
	return Result;
}

void ARPGBossTestPawn::BeginPlay()
{
	Super::BeginPlay();
	ASC->InitAbilityActorInfo(this, this);
	ASC->AddAttributeSetSubobject(Attributes.Get());
	if (HasAuthority())
	{
		Boss->Definition = MakeDefinition(this);
		ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.0f);
		SetTestHealth(100.0f);
	}
}

void ARPGBossTestPawn::SetTestHealth(float Health)
{
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), Health);
}

void ARPGBossTestPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (HasAuthority() || !FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		return;
	}
	const FRPGBossState State = Boss->GetBossState();
	if (!bSawHazardRemoved)
	{
		for (TActorIterator<ARPGBossPersistentHazard> It(GetWorld()); It; ++It)
		{
			const FRPGBossHazardState AreaState = It->GetHazardState();
			if (AreaState.SourcePawn != this || AreaState.ActionTag != TAG_BossTestHazard || !It->IsPresentationActive()) { continue; }
			if (!bSawHazardState && AreaState.PulseCount >= 1 && AreaState.EncounterId == 1 && AreaState.Radius == 200.f)
			{
				const bool bDenied = !It->InitializeFromCast(nullptr, FVector::ZeroVector, 1, FGameplayEffectSpecHandle());
				bSawHazardState = bDenied;
				ObservedHazard = *It;
				UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_CLIENT_STATE Encounter=1 Radius=200 Active=1 AuthorityDenied=%d"), int32(bDenied));
			}
			if (bSawHazardState && !bSawHazardPulse && AreaState.PulseCount >= 2)
			{
				bSawHazardPulse = true;
				UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_PULSE_REPLICATED PulseCountAtLeast2=1"));
			}
			if (bSawHazardState && !bSawHazardTargetDamage)
			{
				int32 Damaged = 0;
				for (TActorIterator<ARPGBossTestPawn> Target(GetWorld()); Target; ++Target)
				{
					if (Target->Attributes->GetMaxHealth() == 10000.f && Target->Attributes->GetCurrentHealth() < 10000.f &&
						FVector::Dist(Target->GetActorLocation(), AreaState.Center) < AreaState.Radius) { ++Damaged; }
				}
				if (Damaged == 2)
				{
					bSawHazardTargetDamage = true;
					UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_TARGET_DAMAGE_REPLICATED Targets=2 Lowered=1"));
				}
			}
		}
		if (bSawHazardState && !ObservedHazard.IsValid())
		{
			bSawHazardRemoved = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_REMOVED_REPLICATED Removed=1"));
		}
	}
	if (!bSawEnrageClock && State.EncounterId == 1 && State.EncounterState == ERPGBossEncounterState::InCombat &&
		!State.bEnraged && State.EnrageServerTime > 0.0 && Attributes->GetAttack() == 1.f)
	{
		bSawEnrageClock = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_ENRAGE_CLOCK_REPLICATED DeadlinePositive=1 Attack=1"));
	}
	if (!bSawPhaseTwo && State.PhaseIndex == 1 && State.bEnraged &&
		State.EnrageServerTime == 0.0 && Attributes->GetAttack() == 2.f &&
		State.EncounterState == ERPGBossEncounterState::InCombat && Attributes->GetCurrentHealth() == 69.0f)
	{
		const bool bDenied = !Boss->SetEnraged(false) && !Boss->BeginReset() && !Boss->MarkWiped() && !Boss->StartEncounter();
		bSawPhaseTwo = bDenied && Boss->GetBossState().Revision == State.Revision;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_PHASE2 Health=69 AuthorityDenied=%d"), bSawPhaseTwo);
		if (bSawPhaseTwo) { UE_LOG(LogTemp, Display, TEXT("RAID_ENRAGE_REPLICATED Attack=2 DeadlineConsumed=1")); }
	}
	if (!bSawReset && State.EncounterState == ERPGBossEncounterState::Resetting)
	{
		bSawReset = State.PhaseIndex == INDEX_NONE && !State.PhaseTag.IsValid() && !State.bEnraged &&
			State.EnrageServerTime == 0.0 && Attributes->GetAttack() == 1.f;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_RESET Clean=%d"), bSawReset);
		if (bSawReset) { UE_LOG(LogTemp, Display, TEXT("RAID_ENRAGE_RESET_REPLICATED Attack=1 Deadline=0")); }
	}
	if (!bSawRestart && State.EncounterId == 2 && State.PhaseIndex == 0 &&
		State.EncounterState == ERPGBossEncounterState::InCombat && Attributes->GetCurrentHealth() == 100.0f)
	{
		bSawRestart = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_RESTART Encounter=2 Health=100"));
	}
	if (!bSawDead && State.EncounterState == ERPGBossEncounterState::Dead && Attributes->GetCurrentHealth() == 0.0f &&
		ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death))
	{
		bSawDead = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_DEAD Health=0"));
	}
	if (!bSawBossAttackDamage && IsPlayerControlled() && State.EncounterState == ERPGBossEncounterState::Idle &&
		Attributes->GetCurrentHealth() == 69.f)
	{
		bSawBossAttackDamage = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_ATTACK_REPLICATED Health=69"));
	}
	if (!bSawTimedStrikeDamage && IsPlayerControlled() && State.EncounterState == ERPGBossEncounterState::Idle &&
		Attributes->GetCurrentHealth() == 38.f)
	{
		bSawTimedStrikeDamage = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_REPLICATED Health=38"));
	}
#endif
}

ARPGBossNetworkTestGameMode::ARPGBossNetworkTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ARPGBossThreatProbeController::StaticClass();
	bStartPlayersAsSpectators = false;
}

void ARPGBossNetworkTestGameMode::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (BeginNetworkSummon()) { return; }
	if (BeginDeferredDashJoin()) { return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		BossPawn = GetWorld()->SpawnActor<ARPGBossTestPawn>();
		// Test-only short timer; no production raid definition or balance is modified.
		BossPawn->Boss->Definition->EnrageSeconds = 8.f;
		BossPawn->Boss->Definition->EnragedAttackMultiplier = 2.f;
		ARPGAIController* DamageBossController = GetWorld()->SpawnActor<ARPGAIController>();
		DamageBossController->Possess(BossPawn);
		DamageBossController->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		NewObject<URPGBossThreatComponent>(BossPawn)->RegisterComponent();
		StartupBoss = GetWorld()->SpawnActor<ARPGBossStartupTestCharacter>();
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_READY"));
	}
#endif
}

bool ARPGBossNetworkTestGameMode::StartNetworkHazard()
{
	if (HazardSource) { return true; }
	// Independent test source/targets cannot perturb the existing player HP checkpoints.
	HazardSource = GetWorld()->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* AI = GetWorld()->SpawnActor<ARPGAIController>();
	AI->Possess(HazardSource);
	AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	HazardSource->SetActorLocation(FVector(3000.f, 0.f, 0.f));
	HazardSource->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	URPGBossThreatComponent* Targets = NewObject<URPGBossThreatComponent>(HazardSource);
	Targets->RegisterComponent();
	auto SpawnTarget = [&](FVector Location)
	{
		ARPGBossTestPawn* Pawn = GetWorld()->SpawnActor<ARPGBossTestPawn>();
		AAIController* TargetAI = GetWorld()->SpawnActor<AAIController>();
		TargetAI->SetGenericTeamId(FGenericTeamId(0)); TargetAI->Possess(Pawn);
		Pawn->SetActorLocation(Location);
		Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 10000.f);
		Pawn->SetTestHealth(10000.f);
		return Pawn;
	};
	HazardFirst = SpawnTarget(FVector(3060.f, 0.f, 0.f));
	HazardSecond = SpawnTarget(FVector(3130.f, 0.f, 0.f));
	if (!HazardSource->Boss->StartEncounter() || !Targets->AddThreat(HazardFirst, 10.f, 1) || !Targets->AddThreat(HazardSecond, 1.f, 1)) { return false; }
	HazardCastHandle = HazardSource->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossPersistentHazardAbility::StaticClass(), 1));
	URPGBossPersistentHazardAbility* Cast = CastChecked<URPGBossPersistentHazardAbility>(HazardSource->ASC->FindAbilitySpecFromHandle(HazardCastHandle)->GetPrimaryInstance());
	Cast->ActionTag = TAG_BossTestHazard;
	Cast->WindupSeconds = .1f; Cast->DurationSeconds = 120.f; Cast->PulseInterval = 1.f;
	Cast->BaseDamage.Value = 10.f; Cast->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
	Cast->SecurityProfile.MaximumHitsPerActivation = 128;
	return HazardSource->ASC->TryActivateAbility(HazardCastHandle);
}

void ARPGBossNetworkTestGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkSummon"))) { TickNetworkSummon(); return; }
	TickNetworkDash();
	if (!BossPawn)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (HazardSource && !bReportedHazardStarted)
	{
		const FGameplayAbilitySpec* CastSpec = HazardSource->ASC->FindAbilitySpecFromHandle(HazardCastHandle);
		for (TActorIterator<ARPGBossPersistentHazard> It(GetWorld()); It; ++It)
		{
			if (It->GetHazardState().SourcePawn == HazardSource && It->GetHazardState().PulseCount >= 1 &&
				CastSpec && !CastSpec->IsActive() &&
				HazardFirst->Attributes->GetCurrentHealth() < 10000.f && HazardSecond->Attributes->GetCurrentHealth() < 10000.f)
			{
				NetworkHazard = *It;
				bReportedHazardStarted = true;
				UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_SERVER_STARTED Persistent=1 Targets=2 CastEnded=1"));
			}
		}
	}
	if (AbilityProbe && AbilityProbe->Activated == 1 && !bReportedAbilityStart)
	{
		bReportedAbilityStart = true;
		UE_LOG(LogTemp, Display, TEXT("RAID_ABILITY_STARTED ServerOnly=1"));
	}
	// A listen host joins before remote clients finish their initial load.
	// Start the bounded windup only once the first remote participant joins,
	// just as on a dedicated server, so cold loading cannot consume its window.
	const int32 FirstRemotePlayerCount = GetNetMode() == NM_ListenServer ? 2 : 1;
	if (Stage == 0 && GetNumPlayers() + GetNumSpectators() >= FirstRemotePlayerCount)
	{
		if (BossPawn->Boss->StartEncounter())
		{
			DamageSource = ProbePlayers.Num() && ProbePlayers[0].IsValid() ? ProbePlayers[0]->GetPawn() : nullptr;
			if (DamageSource.IsValid())
			{
				PendingDamageSpec = MakeBossTestDamageSpec(CastChecked<ARPGBossTestPawn>(DamageSource.Get())->ASC, 31.f);
				DamageEncounterId = BossPawn->Boss->GetBossState().EncounterId;
			}
			Stage = 1;
			StageTime = Now;
		}
	}
	else if (Stage == 1 && Now - StageTime > 2.0)
	{
		URPGBossThreatComponent* DamageThreat = BossPawn->FindComponentByClass<URPGBossThreatComponent>();
		float HealthDamage; bool bThreatAdded;
		const FHitResult Hit(BossPawn, nullptr, BossPawn->GetActorLocation(), FVector::UpVector);
		if (BossPawn->Attributes->GetCurrentHealth() == 100.f &&
			DamageThreat->ApplyDamageFromServerHit(DamageSource.Get(), Hit, PendingDamageSpec, DamageEncounterId,
			FRPGSkillSecurityProfile(), HealthDamage, bThreatAdded) && HealthDamage == 31.f && bThreatAdded &&
			DamageThreat->GetThreatForTarget(DamageSource.Get()) == 31.0 && BossPawn->Attributes->GetCurrentHealth() == 69.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_DAMAGE_APPLIED Health=69 Threat=31"));
		}
		if (!BossPawn->Boss->GetBossState().bEnraged || BossPawn->Boss->GetBossState().EnrageServerTime != 0.0 ||
			BossPawn->Attributes->GetAttack() != 2.f) { return; }
		UE_LOG(LogTemp, Display, TEXT("RAID_ENRAGE_AUTO_SERVER Attack=2 DeadlineConsumed=1"));
		if (!StartNetworkHazard()) { UE_LOG(LogTemp, Error, TEXT("RAID_HAZARD_FAIL StartDenied")); return; }
		TelegraphSequence = StartupBoss->BossAIComponent->BeginTelegraph(TAG_BossTestTelegraph,
			StartupBoss->GetActorLocation() + FVector(100.f, 0.f, 0.f), 120.f);
		if (TelegraphSequence != 0)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TELEGRAPH_STARTED Encounter=%d Sequence=%d"),
				StartupBoss->BossAIComponent->GetTelegraphState().EncounterId, TelegraphSequence);
		}
		Stage = 2;
		StageTime = Now;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_LATE_JOIN_READY"));
	}
	else if (Stage == 2 && GetNumPlayers() + GetNumSpectators() >= (GetNetMode() == NM_ListenServer ? 3 : 2))
	{
		if (LateJoinReadyTime < 0.0) { LateJoinReadyTime = Now; return; }
		if (Now - LateJoinReadyTime < 2.0) { return; }
		if (StartupBoss->BossAIComponent->EndTelegraph(
			StartupBoss->BossAIComponent->GetBossState().EncounterId, TelegraphSequence))
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TELEGRAPH_ENDED Sequence=%d"), TelegraphSequence);
		}
		URPGBossThreatComponent* Threat = StartupBoss->BossThreatComponent;
		const int32 EncounterId = StartupBoss->BossAIComponent->GetBossState().EncounterId;
		for (const TWeakObjectPtr<APlayerController>& Player : ProbePlayers)
		{
			if (!Player.IsValid() || Player->IsLocalController()) { continue; }
			APawn* PlayerPawn = Player->GetPawn();
			if (!PlayerPawn) { continue; }
			if (!ThreatVictim.IsValid())
			{
				ThreatVictim = Player;
				PlayerPawn->SetActorLocation(
					StartupBoss->GetActorLocation() + FVector(100.f, 0.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				Threat->AddThreat(PlayerPawn, 100.f, EncounterId);
			}
			else
			{
				ThreatFallback = PlayerPawn;
				PlayerPawn->SetActorLocation(
					StartupBoss->GetActorLocation() + FVector(0.f, 150.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				Threat->AddThreat(PlayerPawn, 10.f, EncounterId);
			}
		}
		if (ThreatVictim.IsValid() && ThreatFallback.IsValid() && Threat->GetCurrentTarget() == ThreatVictim->GetPawn())
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_THREAT_SELECTED Priority=1"));
			AttackProbe = NewObject<URPGBossAttackTestProbe>(this);
			StartupBoss->GetAbilitySystemComponent()->GiveAbility(FGameplayAbilitySpec(
				URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, AttackProbe));
			StartBossAbilityTestTree(CastChecked<AAIController>(StartupBoss->GetController()), AttackProbe,
				120.f, URPGBossAttackTestAbility::StaticClass());
			Stage = 20;
		}
		StageTime = Now;
	}
	else if (Stage == 20 && AttackProbe && AttackProbe->FinishedTasks == 1)
	{
		if (AttackProbe->LastResult == EBTNodeResult::Succeeded && AttackProbe->bCommitted &&
			AttackProbe->bTelegraphStarted && AttackProbe->bTelegraphCleared &&
			AttackProbe->bFirstHitApplied && !AttackProbe->bDuplicateHitApplied &&
			AttackProbe->HitActor == ThreatVictim->GetPawn() &&
			CastChecked<ARPGBossTestPawn>(ThreatVictim->GetPawn())->Attributes->GetCurrentHealth() == 69.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_ATTACK_DAMAGE Health=69 ServerOnly=1 DuplicateBlocked=1"));
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("RAID_BOSS_ATTACK_FAILED Result=%d Committed=%d FirstHit=%d DuplicateHit=%d TargetMatch=%d Health=%.1f"),
				static_cast<int32>(AttackProbe->LastResult), AttackProbe->bCommitted,
				AttackProbe->bFirstHitApplied, AttackProbe->bDuplicateHitApplied,
				AttackProbe->HitActor == ThreatVictim->GetPawn(),
				CastChecked<ARPGBossTestPawn>(ThreatVictim->GetPawn())->Attributes->GetCurrentHealth());
		}
		TimedStrikeProbe = NewObject<URPGBossAbilityTestProbe>(this);
		const FGameplayAbilitySpecHandle TimedHandle = StartupBoss->GetAbilitySystemComponent()->GiveAbility(FGameplayAbilitySpec(
			URPGBossTelegraphedStrikeAbility::StaticClass(), 1, INDEX_NONE, TimedStrikeProbe));
		TimedStrikeAbility = CastChecked<URPGBossTelegraphedStrikeAbility>(
			StartupBoss->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(TimedHandle)->GetPrimaryInstance());
		TimedStrikeAbility->ActionTag = TAG_BossTestTelegraph;
		TimedStrikeAbility->WindupSeconds = 2.f;
		TimedStrikeAbility->Radius = 75.f;
		TimedStrikeAbility->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
		TimedStrikeAbility->BaseDamage.Value = 31.f;
		StartBossAbilityTestTree(CastChecked<AAIController>(StartupBoss->GetController()), TimedStrikeProbe,
			120.f, URPGBossTelegraphedStrikeAbility::StaticClass());
		Stage = 21;
		StageTime = Now;
	}
	else if (Stage == 21 && TimedStrikeProbe)
	{
		const float TargetHealth = CastChecked<ARPGBossTestPawn>(ThreatVictim->GetPawn())->Attributes->GetCurrentHealth();
		if (!bReportedTimedStrikeWindup && TimedStrikeAbility->IsActive() &&
			StartupBoss->BossAIComponent->GetTelegraphState().bActive && TargetHealth == 69.f)
		{
			bReportedTimedStrikeWindup = true;
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_WINDUP Health=69"));
		}
		if (TimedStrikeProbe->FinishedTasks != 1) { return; }
		if (bReportedTimedStrikeWindup && TimedStrikeProbe->LastResult == EBTNodeResult::Succeeded &&
			!StartupBoss->BossAIComponent->GetTelegraphState().bActive && TargetHealth == 38.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_DAMAGE Health=38 ServerOnly=1"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_BOSS_TIMED_STRIKE_FAILED Windup=%d Result=%d Health=%.1f"),
				bReportedTimedStrikeWindup, static_cast<int32>(TimedStrikeProbe->LastResult), TargetHealth);
		}
		StartBossAbilityTestTree(CastChecked<AAIController>(StartupBoss->GetController()), TimedStrikeProbe,
			120.f, URPGBossTelegraphedStrikeAbility::StaticClass());
		Stage = 22;
		StageTime = Now;
	}
	else if (Stage == 22 && TimedStrikeProbe)
	{
		ARPGBossTestPawn* Victim = CastChecked<ARPGBossTestPawn>(ThreatVictim->GetPawn());
		if (!bMovedTimedStrikeTarget && TimedStrikeAbility->IsActive() &&
			StartupBoss->BossAIComponent->GetTelegraphState().bActive)
		{
			Victim->SetActorLocation(StartupBoss->GetActorLocation() + FVector(500.f, 0.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			bMovedTimedStrikeTarget = true;
		}
		if (TimedStrikeProbe->FinishedTasks != 2) { return; }
		if (bMovedTimedStrikeTarget && TimedStrikeProbe->LastResult == EBTNodeResult::Succeeded &&
			Victim->Attributes->GetCurrentHealth() == 38.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_DODGED Health=38"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_BOSS_TIMED_STRIKE_DODGE_FAILED Health=%.1f"),
				Victim->Attributes->GetCurrentHealth());
		}
		Victim->SetActorLocation(StartupBoss->GetActorLocation() + FVector(100.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		StartBossAbilityTestTree(CastChecked<AAIController>(StartupBoss->GetController()), TimedStrikeProbe,
			120.f, URPGBossTelegraphedStrikeAbility::StaticClass());
		Stage = 23;
		StageTime = Now;
	}
	else if (Stage == 23 && TimedStrikeProbe && TimedStrikeAbility->IsActive() &&
		StartupBoss->BossAIComponent->GetTelegraphState().bActive)
	{
		CastChecked<UBehaviorTreeComponent>(CastChecked<AAIController>(StartupBoss->GetController())->BrainComponent)
			->StopTree(EBTStopMode::Safe);
		Stage = 24;
		StageTime = Now;
	}
	else if (Stage == 24 && Now - StageTime > 2.5)
	{
		const float TargetHealth = CastChecked<ARPGBossTestPawn>(ThreatVictim->GetPawn())->Attributes->GetCurrentHealth();
		if (!TimedStrikeAbility->IsActive() && !StartupBoss->BossAIComponent->GetTelegraphState().bActive &&
			TargetHealth == 38.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_BOSS_TIMED_STRIKE_CANCELLED Health=38 Telegraph=0"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_BOSS_TIMED_STRIKE_CANCEL_FAILED Active=%d Health=%.1f"),
				TimedStrikeAbility->IsActive(), TargetHealth);
		}
		AbilityProbe = NewObject<URPGBossAbilityTestProbe>(this);
		StartupBoss->GetAbilitySystemComponent()->GiveAbility(FGameplayAbilitySpec(
			URPGBossTestAbility::StaticClass(), 1, INDEX_NONE, AbilityProbe));
		StartBossAbilityTestTree(CastChecked<AAIController>(StartupBoss->GetController()), AbilityProbe);
		Stage = 3;
		StageTime = Now;
	}
	else if (Stage == 3 && Now - StageTime > 5.0)
	{
		if (!NetworkHazard.IsValid() || !NetworkHazard->IsPresentationActive())
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_HAZARD_FAIL ExpiredBeforeReset")); return;
		}
		HazardFirstResetHealth = HazardFirst->Attributes->GetCurrentHealth();
		HazardSecondResetHealth = HazardSecond->Attributes->GetCurrentHealth();
		HazardSource->Boss->BeginReset();
		if (NetworkHazard.IsValid() && !NetworkHazard->IsActorBeingDestroyed())
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_HAZARD_FAIL ResetDidNotRemoveArea")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_RESET_SERVER Removed=1"));
		BossPawn->Boss->MarkWiped();
		BossPawn->Boss->BeginReset();
		Stage = 4;
		StageTime = Now;
	}
	else if (Stage == 4 && Now - StageTime > 3.0)
	{
		if (HazardFirst->Attributes->GetCurrentHealth() != HazardFirstResetHealth || HazardSecond->Attributes->GetCurrentHealth() != HazardSecondResetHealth)
		{
			UE_LOG(LogTemp, Error, TEXT("RAID_HAZARD_FAIL DamageAfterReset")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAID_HAZARD_RESET_QUIET Targets=2 HealthUnchanged=1"));
		BossPawn->SetTestHealth(100.0f);
		BossPawn->Boss->CompleteReset();
		BossPawn->Boss->StartEncounter();
		float StaleDamage; bool bStaleThreat;
		const FHitResult Hit(BossPawn, nullptr, BossPawn->GetActorLocation(), FVector::UpVector);
		if (!BossPawn->FindComponentByClass<URPGBossThreatComponent>()->ApplyDamageFromServerHit(
			DamageSource.Get(), Hit, PendingDamageSpec, DamageEncounterId, FRPGSkillSecurityProfile(), StaleDamage, bStaleThreat) &&
			StaleDamage == 0.f && !bStaleThreat && BossPawn->Attributes->GetCurrentHealth() == 100.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_DAMAGE_STALE_REJECTED Encounter=2 Health=100"));
		}
		Stage = 5;
		StageTime = Now;
	}
	else if (Stage == 5 && Now - StageTime > 3.0)
	{
		float HealthDamage; bool bThreatAdded;
		const FHitResult Hit(BossPawn, nullptr, BossPawn->GetActorLocation(), FVector::UpVector);
		if (DamageSource.IsValid() && BossPawn->FindComponentByClass<URPGBossThreatComponent>()->ApplyDamageFromServerHit(
			DamageSource.Get(), Hit, MakeBossTestDamageSpec(CastChecked<ARPGBossTestPawn>(DamageSource.Get())->ASC, 1000.f),
			BossPawn->Boss->GetBossState().EncounterId, FRPGSkillSecurityProfile(), HealthDamage, bThreatAdded) &&
			HealthDamage == 100.f && !bThreatAdded && BossPawn->Attributes->GetCurrentHealth() == 0.f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_DAMAGE_LETHAL Health=0 ThreatCleared=1"));
		}
		Stage = 6;
		UE_LOG(LogTemp, Display, TEXT("RAID_PROBE_SERVER_COMPLETE"));
	}
	else if (Stage == 6 && bThreatVictimLeft)
	{
		URPGBossThreatComponent* Threat = StartupBoss->BossThreatComponent;
		Threat->RefreshTargets();
		if (ThreatFallback.IsValid() && Threat->GetCurrentTarget() == ThreatFallback.Get() && AbilityProbe &&
			AbilityProbe->Cancelled == 1 && AbilityProbe->FinishedTasks == 1 && AbilityProbe->LastResult == EBTNodeResult::Failed)
		{
			UE_LOG(LogTemp, Display, TEXT("RAID_ABILITY_DISCONNECT Cancelled=1 TaskFailed=1"));
			UE_LOG(LogTemp, Display, TEXT("RAID_THREAT_DISCONNECT Fallback=1"));
			StartupBoss->BossAIComponent->BeginReset();
			if (!Threat->GetCurrentTarget() && Threat->GetThreatForTarget(ThreatFallback.Get()) == 0.0)
			{
				UE_LOG(LogTemp, Display, TEXT("RAID_THREAT_RESET Clean=1"));
			}
			Stage = 7;
		}
	}
#endif
}

void ARPGBossNetworkTestGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
#if !UE_BUILD_SHIPPING
	FString DeferredAddress;
	if (FParse::Value(FCommandLine::Get(), TEXT("RPGBossSummonDeferredJoin="), DeferredAddress)) { return; }
	if (FParse::Value(FCommandLine::Get(), TEXT("RPGBossDashDeferredJoin="), DeferredAddress)) { return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("RPGBossNetworkTest")))
	{
		NewPlayer->Possess(GetWorld()->SpawnActor<ARPGBossTestPawn>());
		CastChecked<ARPGBossTestPawn>(NewPlayer->GetPawn())->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
		ProbePlayers.Add(NewPlayer);
		RecordDashLateJoin();
		RecordSummonLateJoin();
	}
#endif
}

void ARPGBossNetworkTestGameMode::Logout(AController* Exiting)
{
	if (ThreatVictim == TWeakObjectPtr<APlayerController>(Cast<APlayerController>(Exiting))) { bThreatVictimLeft = true; }
	Super::Logout(Exiting);
}
