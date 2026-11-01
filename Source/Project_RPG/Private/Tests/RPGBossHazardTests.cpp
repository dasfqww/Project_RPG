#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "Ability/RPGBossPersistentHazardAbility.h"
#include "AI/RPGBossPersistentHazard.h"
#include "AI/RPGBossThreatComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "NativeGameplayTags.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"
#include <limits>

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossHazardTestAction, "Tests.RaidBoss.Hazard.Action");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossHazardTest, "ProjectRPG.Boss.Hazard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossHazardTest::RunTest(const FString& Parameters)
{
	UWorld::InitializationValues Initialization;
	Initialization.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Hazard world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL()); World->CreateAISystem();
	World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	if (!TestNotNull(TEXT("Hazard source"), Boss) || !TestNotNull(TEXT("Hazard controller"), Controller)) { return false; }
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	Boss->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	auto SpawnTarget = [&](FVector Location, uint8 Team)
	{
		ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
		AAIController* AI = World->SpawnActor<AAIController>();
		AI->SetGenericTeamId(FGenericTeamId(Team)); AI->Possess(Pawn);
		Pawn->SetActorLocation(Location);
		Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
		Pawn->SetTestHealth(1000.f);
		return Pawn;
	};
	ARPGBossTestPawn* First = SpawnTarget(FVector(100.f, 0.f, 0.f), 0);
	ARPGBossTestPawn* Second = SpawnTarget(FVector(100.f, 100.f, 0.f), 0);
	ARPGBossTestPawn* Outsider = SpawnTarget(FVector(100.f, -100.f, 0.f), 0);
	ARPGBossTestPawn* Friendly = SpawnTarget(FVector(100.f, 50.f, 0.f), 1);
	const FGameplayAbilitySpecHandle Handle = Boss->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossPersistentHazardAbility::StaticClass(), 1));
	URPGBossPersistentHazardAbility* Ability = CastChecked<URPGBossPersistentHazardAbility>(Boss->ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	Ability->ActionTag = TAG_BossHazardTestAction;
	Ability->DamageEffectClass = URPGBossTestDamageEffect::StaticClass();
	Ability->BaseDamage.Value = 10.f;
	Ability->WindupSeconds = .1f;
	Ability->DurationSeconds = .65f;
	Ability->PulseInterval = .2f;
	Ability->Radius = 200.f;
	Ability->MaximumActiveHazards = 1;
	uint64 Frame = GFrameCounter;
	auto Advance = [&](float Seconds)
	{
		for (int32 Index = 0; Index < FMath::CeilToInt(Seconds / .02f); ++Index)
		{
			TGuardValue<uint64> FrameGuard(GFrameCounter, ++Frame);
			World->Tick(LEVELTICK_All, .02f); Threat->RefreshTargets();
		}
	};
	auto Hazards = [&]()
	{
		TArray<ARPGBossPersistentHazard*> Result;
		for (TActorIterator<ARPGBossPersistentHazard> It(World); It; ++It) { if (!It->IsActorBeingDestroyed()) { Result.Add(*It); } }
		return Result;
	};
	auto Restore = [&]()
	{
		for (ARPGBossPersistentHazard* Area : Hazards()) { Area->Destroy(); }
		Boss->SetTestHealth(100.f);
		if (Boss->Boss->GetBossState().EncounterState != ERPGBossEncounterState::Idle)
		{
			Boss->Boss->BeginReset(); Boss->Boss->CompleteReset();
		}
		Boss->Boss->StartEncounter();
		First->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death, 1, EGameplayTagReplicationState::TagOnly);
		Second->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Death, 1, EGameplayTagReplicationState::TagOnly);
		First->SetTestHealth(1000.f); Second->SetTestHealth(1000.f);
		First->SetActorLocation(FVector(100.f, 0.f, 0.f)); Second->SetActorLocation(FVector(100.f, 100.f, 0.f));
		Threat->ClearThreat();
		Threat->AddThreat(First, 20.f, Boss->Boss->GetBossState().EncounterId);
		Threat->AddThreat(Second, 10.f, Boss->Boss->GetBossState().EncounterId);
	};
	Restore();
	Boss->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot activate hazard cast"), Boss->ASC->TryActivateAbility(Handle));
	Boss->SetTestRole(ROLE_Authority);
	TestTrue(TEXT("Hazard cast activates on server"), Boss->ASC->TryActivateAbility(Handle));
	Advance(.06f);
	TestEqual(TEXT("Windup cannot hurt"), First->Attributes->GetCurrentHealth(), 1000.f);
	Advance(.08f);
	if (!TestEqual(TEXT("Cast creates one persistent area"), Hazards().Num(), 1)) { return false; }
	TWeakObjectPtr<ARPGBossPersistentHazard> Area = Hazards()[0];
	TestFalse(TEXT("Casting ability releases BT before the area ends"), Ability->IsActive());
	TestTrue(TEXT("State has a persistent phase/encounter/deadline"), Area->GetHazardState().EncounterId == Boss->Boss->GetBossState().EncounterId &&
		Area->GetHazardState().CastSequence > 0 && Area->GetHazardState().EndServerTime > Area->GetHazardState().StartServerTime && Area->GetIsReplicated());
	TestEqual(TEXT("First participant takes one immediate pulse"), First->Attributes->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Other participant takes the same pulse"), Second->Attributes->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Unregistered hostile is untouched"), Outsider->Attributes->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Friendly is untouched"), Friendly->Attributes->GetCurrentHealth(), 1000.f);
	TestFalse(TEXT("Overlap capacity blocks a new cast"), Boss->ASC->TryActivateAbility(Handle));
	Ability->BaseDamage.Value = 999.f; Ability->Radius = 1.f; // Cannot change an existing area's frozen spec/geometry.
	Second->SetActorLocation(FVector(900.f, 0.f, 0.f));
	Advance(.22f);
	TestEqual(TEXT("Repeated pulse keeps frozen damage"), First->Attributes->GetCurrentHealth(), 980.f);
	TestEqual(TEXT("Leaving the area avoids its next pulse"), Second->Attributes->GetCurrentHealth(), 990.f);
	Second->SetActorLocation(FVector(100.f, 100.f, 0.f));
	Advance(.22f);
	TestEqual(TEXT("Reentering the area takes a later pulse"), Second->Attributes->GetCurrentHealth(), 980.f);
	Advance(.4f);
	TestFalse(TEXT("Lifetime expiration removes the area"), Area.IsValid());
	const float FinalHealth = First->Attributes->GetCurrentHealth();
	Advance(.4f);
	TestEqual(TEXT("Expired area cannot deal stale damage"), First->Attributes->GetCurrentHealth(), FinalHealth);
	Ability->BaseDamage.Value = 10.f; Ability->Radius = 200.f;
	Restore();
	Boss->ASC->TryActivateAbility(Handle);
	Ability->CancelAbility(Handle, Boss->ASC->AbilityActorInfo.Get(), Ability->GetCurrentActivationInfo(), true);
	Advance(.2f);
	TestEqual(TEXT("Cancelled windup creates no area"), Hazards().Num(), 0);
	TestEqual(TEXT("Cancelled windup causes no damage"), First->Attributes->GetCurrentHealth(), 1000.f);
	Restore();
	First->SetTestHealth(5.f);
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	TestEqual(TEXT("Killing aimed participant does not skip the other hit"), Second->Attributes->GetCurrentHealth(), 990.f);
	TestEqual(TEXT("Area survives the aimed participant dying"), Hazards().Num(), 1);
	Boss->Boss->MarkWiped();
	TestEqual(TEXT("Wipe immediately removes persistent area"), Hazards().Num(), 0);
	Advance(.3f);
	TestEqual(TEXT("Wipe prevents future area pulses"), Second->Attributes->GetCurrentHealth(), 990.f);
	Restore();
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	Boss->SetTestHealth(69.f);
	TestEqual(TEXT("Phase transition removes the area"), Hazards().Num(), 0);
	Restore();
	const FDelegateHandle Reset = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { Boss->Boss->BeginReset(); });
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	TestEqual(TEXT("Reset during pulse protects remaining participant"), Second->Attributes->GetCurrentHealth(), 1000.f);
	TestEqual(TEXT("Reset during pulse destroys the area"), Hazards().Num(), 0);
	First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Reset);
	Restore();
	const FDelegateHandle Swap = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			AAIController* Replacement = World->SpawnActor<AAIController>();
			Replacement->SetGenericTeamId(FGenericTeamId(0)); Replacement->Possess(Second);
			Threat->AddThreat(Second, 1.f, Boss->Boss->GetBossState().EncounterId);
		});
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	TestEqual(TEXT("Controller replacement cannot inherit a pending old pulse hit"), Second->Attributes->GetCurrentHealth(), 1000.f);
	First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Swap);
	Advance(.22f);
	TestEqual(TEXT("Newly admitted controller may take a future pulse"), Second->Attributes->GetCurrentHealth(), 990.f);
	Restore();
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	Controller->UnPossess(); Advance(.22f);
	TestEqual(TEXT("Source controller loss stops area"), Hazards().Num(), 0);
	TestEqual(TEXT("No pulse after source controller loss"), First->Attributes->GetCurrentHealth(), 990.f);
	Controller->Possess(Boss);
	Restore();
	Boss->ASC->TryActivateAbility(Handle); Advance(.14f);
	Boss->SetTestHealth(0.f);
	TestEqual(TEXT("Boss death removes the area"), Hazards().Num(), 0);
	TestTrue(TEXT("Reject excessive pulse budget"), !ARPGBossPersistentHazard::IsTimingValid(120.f, .1f));
	TestTrue(TEXT("Reject nonfinite timing"), !ARPGBossPersistentHazard::IsTimingValid(std::numeric_limits<float>::infinity(), 1.f));
	TestTrue(TEXT("Reject interval longer than duration"), !ARPGBossPersistentHazard::IsTimingValid(1.f, 2.f));
	return true;
}
#endif
