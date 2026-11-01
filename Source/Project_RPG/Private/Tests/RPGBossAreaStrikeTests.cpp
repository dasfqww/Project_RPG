#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossThreatComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossAreaStrikeTest, "ProjectRPG.Boss.AreaStrike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossAreaStrikeTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Initialization;
	Initialization.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Area strike world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	auto SpawnTarget = [&](FVector Position, uint8 Team)
	{
		ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
		AAIController* AI = World->SpawnActor<AAIController>();
		AI->SetGenericTeamId(FGenericTeamId(Team));
		AI->Possess(Pawn);
		Pawn->SetActorLocation(Position);
		return Pawn;
	};
	ARPGBossTestPawn* First = SpawnTarget(FVector(60.f, 0.f, 0.f), 0);
	ARPGBossTestPawn* Second = SpawnTarget(FVector(130.f, 0.f, 0.f), 0);
	ARPGBossTestPawn* Outsider = SpawnTarget(FVector(0.f, 160.f, 0.f), 0);
	ARPGBossTestPawn* Friendly = SpawnTarget(FVector(0.f, -100.f, 0.f), 1);
	URPGBossAttackTestProbe* Probe = NewObject<URPGBossAttackTestProbe>(Boss);
	Probe->bAreaAttack = true;
	const FGameplayAbilitySpecHandle Handle = Boss->ASC->GiveAbility(
		FGameplayAbilitySpec(URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, Probe));
	URPGBossAttackTestAbility* Attack = CastChecked<URPGBossAttackTestAbility>(Boss->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	TestTrue(TEXT("Area encounter starts"), Boss->Boss->StartEncounter());
	auto RestoreTargets = [&]()
	{
		First->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death, 1, EGameplayTagReplicationState::TagOnly);
		Second->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death, 1, EGameplayTagReplicationState::TagOnly);
		First->SetTestHealth(100.f); Second->SetTestHealth(100.f);
		Threat->ClearThreat();
		Threat->AddThreat(First, 20.f, Boss->Boss->GetBossState().EncounterId);
		Threat->AddThreat(Second, 10.f, Boss->Boss->GetBossState().EncounterId);
		World->Tick(LEVELTICK_All, .02f);
	};
	RestoreTargets();
	TestTrue(TEXT("Server area attack activates"), Boss->ASC->TryActivateAbility(Handle, false));
	TestEqual(TEXT("One impact hits both threat participants"), Probe->AreaHitCount, 2);
	TestEqual(TEXT("First participant takes one hit"), First->Attributes->GetCurrentHealth(), 69.f);
	TestEqual(TEXT("Non-aggro participant takes one hit too"), Second->Attributes->GetCurrentHealth(), 69.f);
	TestEqual(TEXT("Unregistered hostile is not an encounter participant"), Outsider->Attributes->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Friendly is untouched"), Friendly->Attributes->GetCurrentHealth(), 100.f);
	TestFalse(TEXT("Repeated area impact is rejected"), Probe->bDuplicateHitApplied);
	TArray<FRPGHitQueryResult> LateHits;
	TestEqual(TEXT("Ended attack cannot apply area damage"), Attack->AttemptTestAreaHit(Probe, LateHits), 0);

	RestoreTargets();
	Probe->SecurityProfile.MaximumTargetsPerQuery = 1;
	Boss->ASC->TryActivateAbility(Handle, false);
	TestEqual(TEXT("Security target cap limits the impact"), Probe->AreaHitCount, 1);
	TestEqual(TEXT("Target outside capped query is untouched"), Second->Attributes->GetCurrentHealth(), 100.f);
	Probe->SecurityProfile.MaximumTargetsPerQuery = 32;

	RestoreTargets();
	First->SetTestHealth(10.f);
	Boss->ASC->TryActivateAbility(Handle, false);
	TestEqual(TEXT("Lethal aimed hit does not skip the other participant"), Probe->AreaHitCount, 2);
	TestEqual(TEXT("Aimed participant dies"), First->Attributes->GetCurrentHealth(), 0.f);
	TestEqual(TEXT("Second participant still takes this impact"), Second->Attributes->GetCurrentHealth(), 69.f);

	RestoreTargets();
	const FDelegateHandle Reset = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Boss->Boss->BeginReset(); });
	Boss->ASC->TryActivateAbility(Handle, false);
	TestEqual(TEXT("Reset callback stops the remaining area hits"), Probe->AreaHitCount, 1);
	TestEqual(TEXT("Second participant is safe after reset"), Second->Attributes->GetCurrentHealth(), 100.f);
	First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Reset);
	TestFalse(TEXT("Reset clears the warning"), Boss->Boss->GetTelegraphState().bActive);
	Boss->SetTestHealth(100.f);
	Boss->Boss->CompleteReset(); Boss->Boss->StartEncounter();
	RestoreTargets();
	const FDelegateHandle Swap = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			AAIController* Replacement = World->SpawnActor<AAIController>();
			Replacement->SetGenericTeamId(FGenericTeamId(0));
			Replacement->Possess(Second);
			Threat->AddThreat(Second, 1.f, Boss->Boss->GetBossState().EncounterId);
		});
	Boss->ASC->TryActivateAbility(Handle, false);
	TestEqual(TEXT("Controller replacement cannot inherit an old pending area hit"), Probe->AreaHitCount, 1);
	TestEqual(TEXT("Repossessed participant is untouched"), Second->Attributes->GetCurrentHealth(), 100.f);
	First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Swap);

	RestoreTargets();
	Probe->bDeferHit = true;
	TestTrue(TEXT("Deferred area starts its warning"), Boss->ASC->TryActivateAbility(Handle, false));
	Second->SetActorLocation(FVector(900.f, 0.f, 0.f));
	World->Tick(LEVELTICK_All, .02f);
	TestEqual(TEXT("Participant leaving the impact area dodges"), Attack->AttemptTestAreaHit(Probe, LateHits), 1);
	TestEqual(TEXT("Dodging participant keeps HP"), Second->Attributes->GetCurrentHealth(), 100.f);
	Attack->CancelAbility(Handle, Boss->ASC->AbilityActorInfo.Get(), Attack->GetCurrentActivationInfo(), true);
	TestEqual(TEXT("Cancelled area cannot hit again"), Attack->AttemptTestAreaHit(Probe, LateHits), 0);
	return true;
}
#endif
