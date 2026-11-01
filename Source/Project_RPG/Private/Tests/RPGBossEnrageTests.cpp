#if WITH_DEV_AUTOMATION_TESTS
#include "Ability/RPGBossEnrageEffect.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossDefinition.h"
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
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossEnrageTest, "ProjectRPG.Boss.Enrage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossEnrageTest::RunTest(const FString& Parameters)
{
	URPGBossDefinition* Validation = ARPGBossTestPawn::MakeDefinition(GetTransientPackage());
	FText Error;
	for (float BadTime : {-1.f, 86401.f, std::numeric_limits<float>::quiet_NaN()})
	{
		Validation->EnrageSeconds = BadTime;
		TestFalse(TEXT("Invalid enrage time rejected"), Validation->ValidateDefinition(Error));
	}
	Validation->EnrageSeconds = 0.f;
	for (float BadMultiplier : {.5f, 11.f, std::numeric_limits<float>::infinity()})
	{
		Validation->EnragedAttackMultiplier = BadMultiplier;
		TestFalse(TEXT("Invalid enrage multiplier rejected"), Validation->ValidateDefinition(Error));
	}
	UWorld::InitializationValues Initialization;
	Initialization.EnableTraceCollision(true);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage(), true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Enrage world"), World)) { return false; }
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
	ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Pawn);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	URPGBossAIComponent* Boss = Pawn->Boss;
	URPGBossThreatComponent* Threat = NewObject<URPGBossThreatComponent>(Pawn);
	Threat->RegisterComponent();
	ARPGBossTestPawn* Target = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* TargetController = World->SpawnActor<AAIController>();
	TargetController->SetGenericTeamId(FGenericTeamId(0));
	TargetController->Possess(Target);
	Target->SetActorLocation(FVector(80.f, 0.f, 0.f));
	Target->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1000.f);
	Target->SetTestHealth(1000.f);
	Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(), 0.f);
	uint64 SimulatedFrame = GFrameCounter;
	auto Advance = [&](float Seconds)
	{
		for (int32 Index = 0; Index < FMath::CeilToInt(Seconds / .05f); ++Index)
		{
			TGuardValue<uint64> FrameGuard(GFrameCounter, ++SimulatedFrame);
			World->Tick(LEVELTICK_All, .05f);
		}
	};
	Boss->Definition->EnrageSeconds = .5f;
	Boss->Definition->EnragedAttackMultiplier = 2.f;
	TestTrue(TEXT("Timed encounter starts"), Boss->StartEncounter());
	const double Deadline = Boss->GetBossState().EnrageServerTime;
	TestTrue(TEXT("Server deadline is published in encounter snapshot"), Deadline > World->GetTimeSeconds());
	Threat->AddThreat(Target, 10.f, Boss->GetBossState().EncounterId);
	Advance(.2f);
	URPGBossAttackTestProbe* Probe = NewObject<URPGBossAttackTestProbe>(Pawn);
	const FGameplayAbilitySpecHandle Attack = Pawn->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossAttackTestAbility::StaticClass(), 1, INDEX_NONE, Probe));
	TestTrue(TEXT("Ordinary attack starts"), Pawn->ASC->TryActivateAbility(Attack, false));
	TestEqual(TEXT("Before enrage ordinary damage is 31"), Target->Attributes->GetCurrentHealth(), 969.f);
	Boss->Definition->EnrageSeconds = .01f;
	Boss->Definition->EnragedAttackMultiplier = 3.f;
	Pawn->SetTestHealth(69.f);
	TestEqual(TEXT("Phase transition keeps original enrage deadline"), Boss->GetBossState().EnrageServerTime, Deadline);
	Advance(.2f);
	TestFalse(TEXT("Frozen timer does not use edited short delay"), Boss->GetBossState().bEnraged);
	Advance(.2f);
	TestTrue(TEXT("Server timer automatically enrages"), Boss->GetBossState().bEnraged);
	TestEqual(TEXT("Frozen multiplier applies through GAS"), Pawn->Attributes->GetAttack(), 2.f);
	TestEqual(TEXT("Consumed deadline is cleared"), Boss->GetBossState().EnrageServerTime, 0.0);
	TestFalse(TEXT("Duplicate enrage cannot stack"), Boss->SetEnraged(true));
	TestTrue(TEXT("Enraged attack starts"), Pawn->ASC->TryActivateAbility(Attack, false));
	TestEqual(TEXT("GAS attack modifier doubles actual approved damage"), Target->Attributes->GetCurrentHealth(), 907.f);
	Advance(1.f);
	TestEqual(TEXT("Timer does not apply a second effect"), Pawn->Attributes->GetAttack(), 2.f);
	Pawn->SetTestRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot remove authoritative enrage"), Boss->SetEnraged(false));
	Pawn->SetTestRole(ROLE_Authority);
	TestTrue(TEXT("Manual clearing removes the owned effect"), Boss->SetEnraged(false));
	TestEqual(TEXT("Manual clearing restores normal attack"), Pawn->Attributes->GetAttack(), 1.f);
	TestTrue(TEXT("Manual re-enrage is supported"), Boss->SetEnraged(true));
	const FGameplayEffectSpecHandle Other = Pawn->ASC->MakeOutgoingSpec(URPGBossEnrageEffect::StaticClass(), 1.f, Pawn->ASC->MakeEffectContext());
	Other.Data->SetSetByCallerMagnitude(URPGBossEnrageEffect::GetMultiplierTag(), 3.f);
	const FActiveGameplayEffectHandle OtherHandle = Pawn->ASC->ApplyGameplayEffectSpecToSelf(*Other.Data);
	TestEqual(TEXT("An unrelated attack effect compounds independently"), Pawn->Attributes->GetAttack(), 6.f);
	TestTrue(TEXT("Enraged encounter wipes"), Boss->MarkWiped());
	TestFalse(TEXT("Wipe clears enrage state"), Boss->GetBossState().bEnraged);
	TestEqual(TEXT("Wipe removes only the component-owned GE"), Pawn->Attributes->GetAttack(), 3.f);
	Pawn->ASC->RemoveActiveGameplayEffect(OtherHandle);
	Boss->BeginReset(); Pawn->SetTestHealth(100.f); Boss->CompleteReset();
	Boss->Definition->EnrageSeconds = .5f;
	Boss->Definition->EnragedAttackMultiplier = 2.f;
	TestTrue(TEXT("Second timed encounter starts"), Boss->StartEncounter());
	Advance(.2f);
	Boss->BeginReset(); Boss->CompleteReset(); Boss->StartEncounter();
	Advance(.4f);
	TestFalse(TEXT("Old encounter timer cannot enrage the restarted fight early"), Boss->GetBossState().bEnraged);
	Advance(.2f);
	TestTrue(TEXT("Restart receives its own full-duration timer"), Boss->GetBossState().bEnraged);
	Boss->BeginReset();
	TestEqual(TEXT("Reset removes enrage attack buff"), Pawn->Attributes->GetAttack(), 1.f);
	Boss->CompleteReset();
	Boss->Definition->EnrageSeconds = 0.f;
	Boss->Definition->EnragedAttackMultiplier = 1.f;
	Boss->StartEncounter(); Advance(1.f);
	TestFalse(TEXT("Default disabled timer preserves legacy encounter"), Boss->GetBossState().bEnraged);
	TestTrue(TEXT("Legacy manual flag remains supported"), Boss->SetEnraged(true));
	TestEqual(TEXT("Legacy multiplier one creates no attack change"), Pawn->Attributes->GetAttack(), 1.f);
	Boss->BeginReset(); Boss->CompleteReset();
	Boss->Definition->EnragedAttackMultiplier = 2.f;
	Boss->StartEncounter();
	Pawn->ASC->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateLambda(
		[](const FActiveGameplayEffectsContainer&, const FGameplayEffectSpec& Spec)
		{
			return !Spec.Def->IsA<URPGBossEnrageEffect>();
		}));
	TestFalse(TEXT("GAS-rejected buff cannot publish fake enrage"), Boss->SetEnraged(true));
	TestFalse(TEXT("Rejected buff keeps state clear"), Boss->GetBossState().bEnraged);
	TestEqual(TEXT("Rejected buff keeps attack unchanged"), Pawn->Attributes->GetAttack(), 1.f);
	Pawn->ASC->GameplayEffectApplicationQueries.Reset();
	Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(), std::numeric_limits<float>::max());
	TestFalse(TEXT("Enrage cannot overflow authoritative attack"), Boss->SetEnraged(true));
	Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(), 1.f);
	Boss->SetEnraged(true);
	Pawn->SetTestHealth(0.f);
	TestTrue(TEXT("Death enters terminal encounter"), Boss->GetBossState().EncounterState == ERPGBossEncounterState::Dead);
	TestFalse(TEXT("Death clears enrage"), Boss->GetBossState().bEnraged);
	TestEqual(TEXT("Death removes owned attack buff"), Pawn->Attributes->GetAttack(), 1.f);
	return true;
}
#endif
