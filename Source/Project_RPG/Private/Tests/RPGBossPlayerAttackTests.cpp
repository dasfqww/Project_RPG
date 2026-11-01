#if WITH_DEV_AUTOMATION_TESTS
#include "Ability/RPGComboSkillAbility.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Tests/RPGBossDamageTestFixture.h"
#include "Tests/RPGBossTestFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossPlayerAttackTest, "ProjectRPG.Boss.PlayerAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossPlayerAttackTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Player attack world"), World)) { return false; }
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->CreateAISystem();
	World->BeginPlay();
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Boss = World->SpawnActor<ARPGBossTestPawn>();
	ARPGAIController* Controller = World->SpawnActor<ARPGAIController>();
	Controller->Possess(Boss);
	Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
	auto* Threat = NewObject<URPGBossThreatComponent>(Boss);
	Threat->RegisterComponent();
	ARPGBossTestPawn* Source = World->SpawnActor<ARPGBossTestPawn>();
	AAIController* SourceController = World->SpawnActor<AAIController>();
	SourceController->SetGenericTeamId(FGenericTeamId(0));
	SourceController->Possess(Source);
	auto* Attack = NewObject<URPGComboSkillAbility>(World);
	Attack->CaptureBossEncounters(Source);
	TestEqual(TEXT("Idle boss is not captured"), Attack->GetCapturedBossEncounter(Source, Threat), 0);
	Boss->Boss->StartEncounter();
	TestEqual(TEXT("Boss entering combat after attack cannot be captured at impact"),
		Attack->GetCapturedBossEncounter(Source, Threat), 0);
	Attack->CaptureBossEncounters(Source);
	TestEqual(TEXT("Active encounter captured at attack creation"), Attack->GetCapturedBossEncounter(Source, Threat), 1);
	TestEqual(TEXT("Another source cannot reuse attack snapshot"), Attack->GetCapturedBossEncounter(Boss, Threat), 0);
	SourceController->UnPossess();
	TestEqual(TEXT("Unpossessed source is rejected"), Attack->GetCapturedBossEncounter(Source, Threat), 0);
	AAIController* Replacement = World->SpawnActor<AAIController>();
	Replacement->SetGenericTeamId(FGenericTeamId(0));
	Replacement->Possess(Source);
	TestEqual(TEXT("Replacement controller cannot inherit the attack"), Attack->GetCapturedBossEncounter(Source, Threat), 0);
	Attack->CaptureBossEncounters(Source);
	Boss->Boss->BeginReset();
	Boss->SetTestHealth(100.f);
	Boss->Boss->CompleteReset();
	Boss->Boss->StartEncounter();
	TestEqual(TEXT("Old attack retains its original encounter ID"), Attack->GetCapturedBossEncounter(Source, Threat), 1);
	const FHitResult Hit(Boss, nullptr, Boss->GetActorLocation(), FVector::UpVector);
	const FGameplayEffectSpecHandle Spec = MakeBossTestDamageSpec(Source->ASC, 20.f);
	FRPGSkillSecurityProfile Profile;
	float Damage = -1.f;
	bool bThreatAdded = true;
	TestFalse(TEXT("Old attack cannot damage restarted boss"), Threat->ApplyDamageFromServerHit(Source, Hit, Spec,
		Attack->GetCapturedBossEncounter(Source, Threat), Profile, Damage, bThreatAdded));
	TestEqual(TEXT("Restarted boss HP remains unchanged"), Boss->Attributes->GetCurrentHealth(), 100.f);
	TestEqual(TEXT("Old attack adds no threat"), Threat->GetThreatForTarget(Source), 0.0);
	Attack->CaptureBossEncounters(Source);
	TestEqual(TEXT("New attack captures new encounter"), Attack->GetCapturedBossEncounter(Source, Threat), 2);
	TestTrue(TEXT("New server hit applies damage through threat route"), Threat->ApplyDamageFromServerHit(Source, Hit, Spec,
		Attack->GetCapturedBossEncounter(Source, Threat), Profile, Damage, bThreatAdded));
	TestTrue(TEXT("New hit adds positive damage threat"), bThreatAdded && Damage > 0.f && Threat->GetThreatForTarget(Source) > 0.0);
	Attack->CaptureBossEncounters(nullptr);
	TestEqual(TEXT("Invalid activation clears previous snapshots"), Attack->GetCapturedBossEncounter(Source, Threat), 0);
	Source->SetTestRole(ROLE_SimulatedProxy);
	Attack->CaptureBossEncounters(Source);
	TestEqual(TEXT("Client cannot capture authoritative attack context"), Attack->GetCapturedBossEncounter(Source, Threat), 0);
	Source->SetTestRole(ROLE_Authority);
	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
