#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/RPGBossTestFixture.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossDefinition.h"
#include "Ability/RPGBossGameplayAbility.h"
#include "Tests/RPGBossAbilityTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BossLifecycleTelegraph, "Tests.RaidBoss.Lifecycle.Telegraph");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossDefinitionTest,
	"ProjectRPG.Boss.Definition", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossDefinitionTest::RunTest(const FString& Parameters)
{
	URPGBossDefinition* Definition = ARPGBossTestPawn::MakeDefinition(GetTransientPackage());
	FText Error;
	TestTrue(TEXT("Ordered unique phases are valid"), Definition->ValidateDefinition(Error));
	TestEqual(TEXT("Exact threshold enters phase two"), Definition->ResolvePhase(0.7f, 0), 1);
	TestEqual(TEXT("Large hit skips to the correct phase"), Definition->ResolvePhase(0.2f, 0), 2);
	TestEqual(TEXT("Healing cannot revert a phase"), Definition->ResolvePhase(1.0f, 2), 2);
	Definition->Phases[1].HealthThreshold = 1.0f;
	TestFalse(TEXT("Duplicate thresholds are rejected"), Definition->ValidateDefinition(Error));
	Definition->Phases[1].HealthThreshold = 0.7f;
	Definition->Phases[1].PhaseTag = Definition->Phases[0].PhaseTag;
	TestFalse(TEXT("Duplicate phase tags are rejected"), Definition->ValidateDefinition(Error));
	Definition = ARPGBossTestPawn::MakeDefinition(GetTransientPackage());
	Definition->Phases[0].AbilityPatterns = { nullptr };
	TestFalse(TEXT("Null phase pattern is rejected"), Definition->ValidateDefinition(Error));
	Definition->Phases[0].AbilityPatterns = { URPGBossGameplayAbility::StaticClass() };
	TestFalse(TEXT("Abstract phase pattern is rejected"), Definition->ValidateDefinition(Error));
	Definition->Phases[0].AbilityPatterns = { URPGBossTestAbility::StaticClass(), URPGBossTestAbility::StaticClass() };
	TestFalse(TEXT("Duplicate phase pattern is rejected"), Definition->ValidateDefinition(Error));
	Definition->Phases[0].AbilityPatterns = { URPGBossTestAbility::StaticClass() };
	Definition->Phases[1].AbilityPatterns = { URPGBossTestAbility::StaticClass() };
	TestTrue(TEXT("An ability may be reused in different phases; empty legacy phase is valid"), Definition->ValidateDefinition(Error));
	Definition->Phases.Reset();
	TestFalse(TEXT("Missing phases are rejected"), Definition->ValidateDefinition(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossLifecycleTest,
	"ProjectRPG.Boss.Lifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossLifecycleTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Test world"), World))
	{
		return false;
	}
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	// This isolated world has no GameMode/GameState to dispatch actor BeginPlay.
	World->GetWorldSettings()->NotifyBeginPlay();
	ARPGBossTestPawn* Pawn = World->SpawnActor<ARPGBossTestPawn>();
	if (TestNotNull(TEXT("Test boss"), Pawn))
	{
		URPGBossAIComponent* Boss = Pawn->Boss;
		TestTrue(TEXT("Fixture BeginPlay was dispatched"), Pawn->HasActorBegunPlay());
		TestTrue(TEXT("Fixture ASC avatar is initialized"), Pawn->ASC->GetAvatarActor() == Pawn);
		TestFalse(TEXT("Cannot finish a reset before entering it"), Boss->CompleteReset());
		Pawn->SetTestRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("Client cannot start encounter"), Boss->StartEncounter());
		Pawn->SetTestRole(ROLE_Authority);
		// Finite attributes can still overflow when divided without clamping.
		Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 1.e-20f);
		Pawn->SetTestHealth(1.e20f);
		TestTrue(TEXT("Server starts initialized boss"), Boss->StartEncounter());
		TestEqual(TEXT("Over-max health resolves safely to first phase"), Boss->GetBossState().PhaseIndex, 0);
		Pawn->ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.0f);
		Pawn->SetTestHealth(100.0f);
		TestFalse(TEXT("Duplicate start is rejected"), Boss->StartEncounter());
		TestEqual(TEXT("First encounter has generation one"), Boss->GetBossState().EncounterId, 1);
		TestEqual(TEXT("Invalid action tag cannot start a telegraph"),
			Boss->BeginTelegraph(FGameplayTag(), FVector::ZeroVector, 1.f), 0);
		TestEqual(TEXT("Invalid windup cannot start a telegraph"),
			Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, -1.f), 0);
		Pawn->SetTestRole(ROLE_SimulatedProxy);
		TestEqual(TEXT("Client cannot start a telegraph"),
			Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, 10.f), 0);
		Pawn->SetTestRole(ROLE_Authority);
		const int32 FirstTelegraph = Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector(50.f, 0.f, 0.f), 10.f);
		TestEqual(TEXT("First telegraph has sequence one"), FirstTelegraph, 1);
		TestTrue(TEXT("Telegraph snapshot is active"), Boss->GetTelegraphState().bActive);
		TestEqual(TEXT("Duplicate active telegraph is rejected"),
			Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, 10.f), 0);
		TestFalse(TEXT("Stale sequence cannot clear telegraph"), Boss->EndTelegraph(1, FirstTelegraph + 1));
		Pawn->SetTestRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("Client cannot end a telegraph"), Boss->EndTelegraph(1, FirstTelegraph));
		Pawn->SetTestRole(ROLE_Authority);
		TestTrue(TEXT("Correct generation clears telegraph"), Boss->EndTelegraph(1, FirstTelegraph));
		const int32 PhaseTelegraph = Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, 10.f);
		TestTrue(TEXT("Second telegraph starts"), PhaseTelegraph > FirstTelegraph);
		Boss->Definition->Phases[1].HealthThreshold = 0.95f;
		Pawn->SetTestHealth(80.0f);
		TestEqual(TEXT("Active encounter keeps its frozen definition"), Boss->GetBossState().PhaseIndex, 0);
		Boss->Definition->Phases[1].HealthThreshold = 0.7f;
		Pawn->SetTestHealth(20.0f);
		TestEqual(TEXT("ASC change enters third phase"), Boss->GetBossState().PhaseIndex, 2);
		TestFalse(TEXT("Phase transition immediately clears old telegraph"), Boss->GetTelegraphState().bActive);
		const int32 WipeTelegraph = Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, 10.f);
		TestTrue(TEXT("New phase can immediately begin its telegraph"), WipeTelegraph > PhaseTelegraph);
		TestFalse(TEXT("Old phase callback cannot clear new telegraph"), Boss->EndTelegraph(1, PhaseTelegraph));
		Pawn->SetTestHealth(100.0f);
		TestEqual(TEXT("Healing retains third phase"), Boss->GetBossState().PhaseIndex, 2);
		Pawn->SetTestRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("Client cannot enrage"), Boss->SetEnraged(true));
		TestFalse(TEXT("Client cannot wipe"), Boss->MarkWiped());
		TestFalse(TEXT("Client cannot reset"), Boss->BeginReset());
		Pawn->SetTestRole(ROLE_Authority);
		TestTrue(TEXT("Server can enrage"), Boss->SetEnraged(true));
		TestTrue(TEXT("Server marks wipe"), Boss->MarkWiped());
		TestFalse(TEXT("Wipe clears active telegraph"), Boss->GetTelegraphState().bActive);
		TestTrue(TEXT("Server enters reset"), Boss->BeginReset());
		TestFalse(TEXT("Reset clears enrage"), Boss->GetBossState().bEnraged);
		TestEqual(TEXT("Reset clears phase"), Boss->GetBossState().PhaseIndex, INDEX_NONE);
		Pawn->SetTestHealth(0.0f);
		TestFalse(TEXT("Cannot finish reset with zero health"), Boss->CompleteReset());
		TestTrue(TEXT("Reset health changes cannot kill the encounter"), Boss->GetBossState().EncounterState == ERPGBossEncounterState::Resetting);
		Pawn->SetTestHealth(100.0f);
		TestTrue(TEXT("Complete reset after restoring health"), Boss->CompleteReset());
		TestTrue(TEXT("Restart allowed"), Boss->StartEncounter());
		TestEqual(TEXT("Restart advances generation"), Boss->GetBossState().EncounterId, 2);
		TestEqual(TEXT("Restart restores first phase"), Boss->GetBossState().PhaseIndex, 0);
		const int32 RestartTelegraph = Boss->BeginTelegraph(TAG_BossLifecycleTelegraph, FVector::ZeroVector, 10.f);
		TestTrue(TEXT("Restart can telegraph"), RestartTelegraph > WipeTelegraph);
		TestFalse(TEXT("Old encounter cannot clear current telegraph"), Boss->EndTelegraph(1, RestartTelegraph));
		Pawn->SetTestHealth(0.0f);
		TestTrue(TEXT("Lethal damage wins over phase thresholds"), Boss->GetBossState().EncounterState == ERPGBossEncounterState::Dead);
		TestFalse(TEXT("Death clears active telegraph"), Boss->GetTelegraphState().bActive);
		TestFalse(TEXT("Dead encounter cannot be reset into a second victory"), Boss->BeginReset());
		TestFalse(TEXT("Dead encounter cannot restart"), Boss->StartEncounter());
		Pawn->Destroy();
	}
	World->EndPlay(EEndPlayReason::Quit);
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}
#endif
