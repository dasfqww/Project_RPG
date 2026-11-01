#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDamageExecutionGuard.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossDamageTest, "ProjectRPG.Boss.Damage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossDamageTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues WorldInitialization;
	WorldInitialization.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &WorldInitialization);
	if (!TestNotNull(TEXT("Damage world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	ARPGBossTestPawn* Source = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* SourceController = World->SpawnActor<AAIController>();
	SourceController->SetGenericTeamId(FGenericTeamId(0));
	SourceController->Possess(Source);
	Source->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	const FHitResult Hit(Boss, nullptr, Boss->GetActorLocation(), FVector::UpVector);
	const FGameplayEffectSpecHandle Spec = MakeBossTestDamageSpec(Source->ASC, 31.f);
	FGameplayEffectSpecHandle SharedSpec;
	const TSubclassOf<UGameplayEffect> SharedDamageClass = LoadClass<UGameplayEffect>(nullptr,
		TEXT("/Game/Blueprints/Character/Shared/GameplayEffect/GE_Shared_ApplyDamage.GE_Shared_ApplyDamage_C"));
	if (TestNotNull(TEXT("Authored shared damage GE"), SharedDamageClass.Get()))
	{
		SharedSpec = Source->ASC->MakeOutgoingSpec(
			SharedDamageClass, 1.f, Source->ASC->MakeEffectContext());
		if (TestTrue(TEXT("Authored shared damage spec"), SharedSpec.IsValid()))
		{
			SharedSpec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 31.f);
			TestTrue(TEXT("Native target tag requirement component supports guarded damage"),
				FRPGDamageExecutionGuard::Supports(*SharedSpec.Data, *Source->ASC));
			Boss->ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
			TestFalse(TEXT("Authored shared GE still rejects invulnerable targets in GAS"),
				Source->ASC->ApplyGameplayEffectSpecToTarget(*SharedSpec.Data, Boss->ASC).WasSuccessfullyApplied());
			TestEqual(TEXT("Rejected authored GE leaves HP unchanged"), Boss->Attributes->GetCurrentHealth(), 100.f);
			Boss->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		}
	}
	FRPGSkillSecurityProfile Profile;
	float HealthDamage = -1.f;
	bool bThreatAdded = true;
	auto Apply = [&](const FGameplayEffectSpecHandle& InSpec, int32 Epoch = 1)
	{
		return Threat->ApplyDamageFromServerHit(Source, Hit, InSpec, Epoch, Profile, HealthDamage, bThreatAdded);
	};
	auto Unchanged = [&]()
	{
		TestEqual(TEXT("Rejected hit cannot change HP"), Boss->Attributes->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Rejected hit cannot add threat"), Threat->GetThreatForTarget(Source), 0.0);
		TestEqual(TEXT("Failure clears damage output"), HealthDamage, 0.f);
		TestFalse(TEXT("Failure clears threat output"), bThreatAdded);
	};
	TestFalse(TEXT("Idle damage rejected"), Apply(Spec)); Unchanged();
	Boss->Boss->StartEncounter();
	if (SharedSpec.IsValid())
	{
		Boss->ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		TestFalse(TEXT("Guarded authored damage preserves GAS target tag rejection"), Apply(SharedSpec));
		Unchanged();
		Boss->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		// Use one point so this contract probe cannot advance the encounter phase
		// before the existing phase-transition and duplicate-hit regressions.
		SharedSpec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 1.f);
		TestTrue(TEXT("Guarded authored damage accepts the native tag requirement component"), Apply(SharedSpec));
		TestEqual(TEXT("Authored damage has one approved HP change"), Boss->Attributes->GetCurrentHealth(), 99.f);
		TestEqual(TEXT("Authored damage reports actual HP loss"), HealthDamage, 1.f);
		TestTrue(TEXT("Authored damage contributes threat"), bThreatAdded);
		TestEqual(TEXT("Authored threat follows HP loss"), Threat->GetThreatForTarget(Source), 1.0);
		TestEqual(TEXT("Contract probe does not advance the phase"), Boss->Boss->GetBossState().PhaseIndex, 0);
		Boss->SetTestHealth(100.f);
		Threat->ClearThreat();
	}
	TestTrue(TEXT("Attack target admitted"), Threat->AddThreat(Source, 10.f, 1));
	ARPGBossTestPawn* Decoy = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* DecoyController = World->SpawnActor<AAIController>();
	DecoyController->SetGenericTeamId(FGenericTeamId(0));
	DecoyController->Possess(Decoy);
	// Spawned query primitives enter the physics broadphase on the next world tick.
	World->Tick(LEVELTICK_All, .02f);
	URPGBossAttackTestProbe* AttackProbe = NewObject<URPGBossAttackTestProbe>(Boss);
	FRPGHitQueryContext QueryContext;
	QueryContext.SourceActor = Boss;
	QueryContext.QueryTransform = Boss->GetActorTransform();
	QueryContext.Profile = AttackProbe->HitProfile;
	TArray<FRPGHitQueryResult> QueryResults;
	URPGHitQuerySubsystem* HitQueries = World->GetSubsystem<URPGHitQuerySubsystem>();
	TestNotNull(TEXT("Shared hit query subsystem"), HitQueries);
	USphereComponent* SourceCollision = Source->FindComponentByClass<USphereComponent>();
	TestNotNull(TEXT("Target query collision"), SourceCollision);
	TestTrue(TEXT("Target collision is registered"), SourceCollision && SourceCollision->IsRegistered());
	TestTrue(TEXT("Target collision has physics state"), SourceCollision && SourceCollision->IsPhysicsStateCreated());
	TestTrue(TEXT("Target passes team and tag filter"), HitQueries->IsTargetEligible(
		Boss, Source, Boss->GetActorLocation(), AttackProbe->HitProfile.Filter));
	SourceController->SetGenericTeamId(FGenericTeamId(1));
	TestFalse(TEXT("Shared query excludes a friendly controller team"), HitQueries->IsTargetEligible(
		Boss, Source, Boss->GetActorLocation(), AttackProbe->HitProfile.Filter));
	SourceController->SetGenericTeamId(FGenericTeamId(0));
	TArray<FOverlapResult> RawOverlaps;
	FCollisionObjectQueryParams PawnObjects;
	PawnObjects.AddObjectTypesToQuery(ECC_Pawn);
	TestTrue(TEXT("Raw world overlap finds pawn collision"), World->OverlapMultiByObjectType(
		RawOverlaps, Boss->GetActorLocation(), FQuat::Identity, PawnObjects,
		FCollisionShape::MakeSphere(200.f)));
	TestTrue(TEXT("Shared hit query finds candidates"), HitQueries->ExecuteHitQuery(QueryContext, QueryResults));
	TestTrue(TEXT("Shared hit query contains threat target"), QueryResults.ContainsByPredicate(
		[Source](const FRPGHitQueryResult& Result) { return Result.TargetActor == Source; }));
	// Keep an actual committed windup open while the same target Pawn changes controller.
	URPGBossAttackTestProbe* DeferredProbe = NewObject<URPGBossAttackTestProbe>(Boss);
	DeferredProbe->bDeferHit = true;
	const FGameplayAbilitySpecHandle DeferredHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(
		URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, DeferredProbe));
	TestTrue(TEXT("Deferred targeted attack starts"), Boss->ASC->TryActivateAbility(DeferredHandle, false));
	URPGBossAttackTestAbility* DeferredAttack = CastChecked<URPGBossAttackTestAbility>(
		Boss->ASC->FindAbilitySpecFromHandle(DeferredHandle)->GetPrimaryInstance());
	TestTrue(TEXT("Deferred attack commits and publishes windup"), DeferredProbe->bCommitted && DeferredProbe->bTelegraphStarted);
	AAIController* ReplacementSourceController = World->SpawnActor<AAIController>();
	ReplacementSourceController->SetGenericTeamId(FGenericTeamId(0));
	SourceController->UnPossess();
	ReplacementSourceController->Possess(Source);
	TestTrue(TEXT("New target controller re-enters threat"), Threat->AddThreat(Source, 10.f, 1));
	TestTrue(TEXT("Replacement retains the same target Pawn"), Threat->GetCurrentTarget() == Source);
	FRPGHitQueryResult DeferredHit;
	TestFalse(TEXT("Old windup cannot damage a replacement target controller"), DeferredAttack->AttemptTestHit(DeferredProbe, DeferredHit));
	TestEqual(TEXT("Replacement target HP is unchanged by stale windup"), Source->Attributes->GetCurrentHealth(), 100.f);
	TestNull(TEXT("Rejected stale hit exposes no result"), DeferredHit.TargetActor.Get());
	Boss->ASC->CancelAbilityHandle(DeferredHandle);
	TestFalse(TEXT("Cancel clears old-controller windup"), Boss->Boss->GetTelegraphState().bActive);
	TestTrue(TEXT("Fresh attack can target replacement controller"), Boss->ASC->TryActivateAbility(DeferredHandle, false));
	TestTrue(TEXT("Fresh controller context permits actual server query and damage"), DeferredAttack->AttemptTestHit(DeferredProbe, DeferredHit));
	TestEqual(TEXT("Only fresh controller execution reduces HP"), Source->Attributes->GetCurrentHealth(), 69.f);
	Boss->ASC->CancelAbilityHandle(DeferredHandle);
	Source->SetTestHealth(100.f);
	ReplacementSourceController->UnPossess();
	SourceController->Possess(Source);
	Threat->ClearThreat();
	Threat->AddThreat(Source, 10.f, 1);
	const FGameplayAbilitySpecHandle AttackHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(
		URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, AttackProbe));
	bool bTriedNestedAttack = false;
	bool bNestedAttackApplied = false;
	Source->ASC->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateLambda(
		[&](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&)
		{
			if (!bTriedNestedAttack)
			{
				bTriedNestedAttack = true;
				URPGBossAttackTestAbility* ActiveAttack = CastChecked<URPGBossAttackTestAbility>(
					Boss->ASC->FindAbilitySpecFromHandle(AttackHandle)->GetPrimaryInstance());
				FRPGHitQueryResult NestedHit;
				bNestedAttackApplied = ActiveAttack->AttemptTestHit(AttackProbe, NestedHit);
			}
			return true;
		}));
	TestTrue(TEXT("Boss attack activates on the server"), Boss->ASC->TryActivateAbility(AttackHandle, false));
	Source->ASC->GameplayEffectApplicationQueries.Reset();
	TestTrue(TEXT("GE callback attempted a nested boss attack"), bTriedNestedAttack);
	TestFalse(TEXT("GE callback cannot duplicate boss damage"), bNestedAttackApplied);
	TestTrue(TEXT("Boss attack commits"), AttackProbe->bCommitted);
	TestTrue(TEXT("Server hit query applies damage"), AttackProbe->bFirstHitApplied);
	TestFalse(TEXT("One execution cannot damage twice"), AttackProbe->bDuplicateHitApplied);
	TestTrue(TEXT("Only captured threat target is used"), AttackProbe->HitActor == Source);
	TestEqual(TEXT("Boss attack target HP"), Source->Attributes->GetCurrentHealth(), 69.f);
	TestEqual(TEXT("Non-target in shape is not damaged"), Decoy->Attributes->GetCurrentHealth(), 100.f);
	URPGBossAttackTestProbe* DurationProbe = NewObject<URPGBossAttackTestProbe>(Boss);
	DurationProbe->DamageEffectClass = URPGBossTestDurationDamageEffect::StaticClass();
	const FGameplayAbilitySpecHandle DurationAttackHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(
		URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, DurationProbe));
	TestTrue(TEXT("Duration attack reaches its server execution"), Boss->ASC->TryActivateAbility(DurationAttackHandle, false));
	TestTrue(TEXT("Duration attack commits before validation"), DurationProbe->bCommitted);
	TestFalse(TEXT("Boss one-hit contract rejects duration effects"), DurationProbe->bFirstHitApplied);
	TestEqual(TEXT("Duration attack leaves HP unchanged"), Source->Attributes->GetCurrentHealth(), 69.f);
	Threat->ClearThreat();
	Source->SetTestHealth(100.f);
	Source->SetActorLocation(FVector(500.f, 0.f, 0.f));
	TestTrue(TEXT("Out-of-shape target remains a valid threat candidate"), Threat->AddThreat(Source, 10.f, 1));
	URPGBossAttackTestProbe* MissProbe = NewObject<URPGBossAttackTestProbe>(Boss);
	const FGameplayAbilitySpecHandle MissHandle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(
		URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, MissProbe));
	Boss->ASC->TryActivateAbility(MissHandle, false);
	TestFalse(TEXT("Server query misses target outside authored shape"), MissProbe->bFirstHitApplied);
	TestEqual(TEXT("Miss cannot change target HP"), Source->Attributes->GetCurrentHealth(), 100.f);
	Source->SetActorLocation(FVector::ZeroVector);
	Threat->ClearThreat();
	TestFalse(TEXT("Stale epoch rejected before damage"), Apply(Spec, 0)); Unchanged();
	TestFalse(TEXT("Empty spec rejected"), Apply(FGameplayEffectSpecHandle())); Unchanged();
	TestFalse(TEXT("Missing damage tag rejected"), Apply(Source->ASC->MakeOutgoingSpec(
		URPGBossTestDamageEffect::StaticClass(), 1.f, Source->ASC->MakeEffectContext()))); Unchanged();
	TestFalse(TEXT("Another instigator's spec rejected"), Apply(MakeBossTestDamageSpec(Boss->ASC, 31.f))); Unchanged();
	FGameplayEffectSpecHandle Duration = Source->ASC->MakeOutgoingSpec(
		URPGBossTestDurationDamageEffect::StaticClass(), 1.f, Source->ASC->MakeEffectContext());
	Duration.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage, 31.f);
	TestFalse(TEXT("Duration damage needs a separate tick contract"), Apply(Duration)); Unchanged();
	FGameplayEffectSpecHandle ModifiedDuration = MakeBossTestDamageSpec(Source->ASC, 31.f);
	ModifiedDuration.Data->SetDuration(5.f, true);
	TestFalse(TEXT("Runtime duration override cannot bypass instant policy"), Apply(ModifiedDuration)); Unchanged();
	for (float BadDamage : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1.e30f})
	{
		TestFalse(TEXT("Invalid or excessive damage rejected"), Apply(MakeBossTestDamageSpec(Source->ASC, BadDamage))); Unchanged();
	}
	SourceController->SetGenericTeamId(FGenericTeamId(1));
	TestFalse(TEXT("Friendly damage rejected"), Apply(Spec)); Unchanged();
	SourceController->SetGenericTeamId(FGenericTeamId(0));
	SourceController->UnPossess();
	TestFalse(TEXT("Disconnected source rejected"), Apply(Spec)); Unchanged();
	SourceController->Possess(Source);
	Source->SetTestHealth(0.f);
	TestFalse(TEXT("Dead source rejected"), Apply(Spec)); Unchanged();
	Source->SetTestHealth(100.f);
	Source->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client source rejected"), Apply(Spec)); Unchanged();
	Source->SetTestRole(ROLE_Authority);
	Boss->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client boss rejected"), Apply(Spec)); Unchanged();
	Boss->SetTestRole(ROLE_Authority);
	Source->SetActorLocation(FVector(1000.f, 0.f, 0.f));
	Profile.MaximumServerHitDistance = 100.f;
	TestFalse(TEXT("Shared validator rejects out-of-range hit"), Apply(Spec)); Unchanged();
	Source->SetActorLocation(FVector::ZeroVector);
	Profile = FRPGSkillSecurityProfile();
	const FHitResult BadHit(Boss, nullptr, FVector(10000.f), FVector::UpVector);
	TestFalse(TEXT("Shared validator rejects invalid impact"), Threat->ApplyDamageFromServerHit(
		Source, BadHit, Spec, 1, Profile, HealthDamage, bThreatAdded)); Unchanged();
	TestFalse(TEXT("Wrong hit actor rejected"), Threat->ApplyDamageFromServerHit(
		Source, FHitResult(Source, nullptr, FVector::ZeroVector, FVector::UpVector), Spec, 1, Profile, HealthDamage, bThreatAdded)); Unchanged();
	Boss->ASC->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateLambda(
		[](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&) { return false; }));
	TestFalse(TEXT("GAS rejection adds no threat"), Apply(Spec)); Unchanged();
	Boss->ASC->GameplayEffectApplicationQueries.Reset();
	for (float BadDefense : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(), BadDefense);
		TestFalse(TEXT("Malformed defense rejects the damage receipt"), Apply(Spec)); Unchanged();
	}
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(), 1.f);
	for (float BadAttack : {-1.f, 1.e30f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Source->ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(), BadAttack);
		TestFalse(TEXT("Malformed or overflowing attack rejects the damage receipt"), Apply(Spec)); Unchanged();
	}
	Source->ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(), 1.f);
	TestFalse(TEXT("Rounded-to-zero damage rejects the damage receipt"), Apply(MakeBossTestDamageSpec(Source->ASC, .25f))); Unchanged();
	TestTrue(TEXT("Production execution and AttributeSet apply damage"), Apply(Spec));
	TestEqual(TEXT("Actual health damage"), HealthDamage, 31.f);
	TestTrue(TEXT("Actual damage adds threat"), bThreatAdded);
	TestEqual(TEXT("HP after hit"), Boss->Attributes->GetCurrentHealth(), 69.f);
	TestEqual(TEXT("Damage drives phase change"), Boss->Boss->GetBossState().PhaseIndex, 1);
	TestEqual(TEXT("Threat equals actual health lost"), Threat->GetThreatForTarget(Source), 31.0);
	TestTrue(TEXT("Damage admits attacker without sight"), Threat->GetCurrentTarget() == Source);
	Boss->Boss->BeginReset();
	TestFalse(TEXT("Reset rejects damage"), Apply(Spec));
	Boss->SetTestHealth(100.f);
	Boss->Boss->CompleteReset();
	Boss->Boss->StartEncounter();
	TestFalse(TEXT("Old delayed hit cannot damage new encounter"), Apply(Spec)); Unchanged();
	bool bNestedAccepted = true;
	Boss->ASC->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateLambda(
		[&](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec&)
		{
			float NestedDamage; bool bNestedThreat;
			bNestedAccepted = Threat->ApplyDamageFromServerHit(Source, Hit, Spec, 2, Profile, NestedDamage, bNestedThreat);
			return true;
		}));
	TestTrue(TEXT("Outer hit applies"), Apply(Spec, 2));
	TestFalse(TEXT("Nested damage entry is rejected"), bNestedAccepted);
	TestEqual(TEXT("Nested call cannot duplicate health damage"), HealthDamage, 31.f);
	Boss->ASC->GameplayEffectApplicationQueries.Reset();
	const FDelegateHandle ResetDelegate = Boss->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Boss->Boss->BeginReset(); });
	TestTrue(TEXT("Applied hit may trigger synchronous reset"), Apply(Spec, 2));
	TestFalse(TEXT("Post-effect reset cannot resurrect threat"), bThreatAdded);
	TestNull(TEXT("Reset keeps target clear"), Threat->GetCurrentTarget());
	Boss->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(ResetDelegate);
	Boss->SetTestHealth(100.f);
	Boss->Boss->CompleteReset(); Boss->Boss->StartEncounter();
	TestTrue(TEXT("Overkill damage applies"), Apply(MakeBossTestDamageSpec(Source->ASC, 1000.f), 3));
	TestEqual(TEXT("Overkill reports only remaining HP"), HealthDamage, 100.f);
	TestFalse(TEXT("Dead encounter does not receive threat"), bThreatAdded);
	TestEqual(TEXT("Lethal hit sets HP zero"), Boss->Attributes->GetCurrentHealth(), 0.f);
	TestTrue(TEXT("Lethal hit drives boss death"), Boss->Boss->GetBossState().EncounterState == ERPGBossEncounterState::Dead);
	TestTrue(TEXT("Production death tag applied"), Boss->ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death));
	TestFalse(TEXT("Dead boss rejects another hit"), Apply(Spec, 3));
	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
