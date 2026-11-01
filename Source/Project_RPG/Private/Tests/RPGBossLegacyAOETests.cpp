#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGBossLegacyAOETestAbility.h"
#include "Tests/RPGBossTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Controller/RPGAIController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "RPGGameplayTags.h"

namespace RPGBossLegacyAOETests
{
struct FFixture
{
	UWorld* World = nullptr;
	ARPGBossTestPawn* Source = nullptr;
	URPGBossLegacyAOETestAbility* Ability = nullptr;
	FGameplayAbilitySpecHandle Handle;
	~FFixture()
	{
		if (Source) Source->ASC->CancelAllAbilities();
		if (!World) return;
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Test.TestNotNull(TEXT("Legacy AOE world"), World)) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->CreateAISystem();
		World->BeginPlay();
		World->GetWorldSettings()->NotifyBeginPlay();
		Source = World->SpawnActor<ARPGBossTestPawn>();
		auto* Controller = World->SpawnActor<AAIController>();
		if (!Source || !Controller) return false;
		Controller->SetGenericTeamId(FGenericTeamId(0));
		Controller->Possess(Source);
		Source->Attributes->SetAttack(1.f);
		Source->Attributes->SetCriticalChance(0.f);
		// Suppress optional identity feedback only, not the damage path under test.
		Source->ASC->AddLooseGameplayTag(RPGGameplayTags::Player_Status_Rage_Active);
		Handle = Source->ASC->GiveAbility(FGameplayAbilitySpec(URPGBossLegacyAOETestAbility::StaticClass(), 1));
		const auto* Spec = Source->ASC->FindAbilitySpecFromHandle(Handle);
		Ability = Spec ? Cast<URPGBossLegacyAOETestAbility>(Spec->GetPrimaryInstance()) : nullptr;
		return Test.TestNotNull(TEXT("Legacy GAS ability instance"), Ability);
	}
	ARPGBossTestPawn* Boss(FVector Location, bool bStart = true, bool bThreat = true)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Pawn = World->SpawnActor<ARPGBossTestPawn>(Location, FRotator::ZeroRotator, Params);
		auto* Controller = World->SpawnActor<ARPGAIController>();
		if (!Pawn || !Controller) return nullptr;
		Controller->Possess(Pawn);
		Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		Pawn->Attributes->SetDefense(1.f);
		if (bThreat) NewObject<URPGBossThreatComponent>(Pawn)->RegisterComponent();
		if (bStart && !Pawn->Boss->StartEncounter()) return nullptr;
		return Pawn;
	}
	bool Start() { World->Tick(LEVELTICK_All, .01f); return Source->ASC->TryActivateAbility(Handle); }
	void Event(AActor* ForgedTarget = nullptr)
	{
		FGameplayEventData Payload;
		Payload.EventTag = RPGGameplayTags::Player_Event_AOE;
		Payload.Target = ForgedTarget;
		Payload.EventMagnitude = 1.e9f;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Source, Payload.EventTag, Payload);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossLegacyAOETest, "ProjectRPG.Boss.LegacyAOE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossLegacyAOETest::RunTest(const FString&)
{
	using namespace RPGBossLegacyAOETests;
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Near = F.Boss(FVector(140, 0, 0));
		auto* Other = F.Boss(FVector(0, 140, 0));
		auto* Far = F.Boss(FVector(800, 0, 0));
		auto* MissingThreat = F.Boss(FVector(-140, 0, 0), true, false);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Ordinary = F.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>(FVector(0, -140, 0), FRotator::ZeroRotator, Params);
		auto* OrdinaryController = F.World->SpawnActor<AAIController>();
		if (!TestTrue(TEXT("Ordinary NPC available"), Ordinary && OrdinaryController)) return false;
		OrdinaryController->SetGenericTeamId(FGenericTeamId(1));
		OrdinaryController->Possess(Ordinary);
		if (!TestTrue(TEXT("AOE targets available"), Near && Other && Far && MissingThreat) ||
			!TestTrue(TEXT("Legacy activation uses production base"), F.Start())) return false;
		int32 NearHits = 0, OtherHits = 0;
		auto Observe = [&](ARPGBossTestPawn* Target, int32& Count)
		{
			return Target->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[&, Target](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
				{
					++Count;
					TestTrue(TEXT("Each boss GE retains its actual server hit"), Spec.GetContext().GetHitResult() &&
						Spec.GetContext().GetHitResult()->GetActor() == Target);
					TestTrue(TEXT("GE carries active legacy ability, not a policy fixture"),
						Spec.GetContext().GetAbilityInstance_NotReplicated() == F.Ability);
				});
		};
		const auto NearDelegate = Observe(Near, NearHits);
		const auto OtherDelegate = Observe(Other, OtherHits);
		F.Event(Far); // Client-style target/magnitude cannot replace the server sphere query.
		Near->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(NearDelegate);
		Other->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(OtherDelegate);
		TestEqual(TEXT("First boss damaged once"), NearHits, 1);
		TestEqual(TEXT("Second boss damaged once"), OtherHits, 1);
		TestTrue(TEXT("Actual HP damage adds first boss threat"), Near->Attributes->GetCurrentHealth() < 100.f &&
			Near->FindComponentByClass<URPGBossThreatComponent>()->GetThreatForTarget(F.Source) > 0.0);
		TestTrue(TEXT("Actual HP damage adds second boss threat"), Other->Attributes->GetCurrentHealth() < 100.f &&
			Other->FindComponentByClass<URPGBossThreatComponent>()->GetThreatForTarget(F.Source) > 0.0);
		TestEqual(TEXT("Forged distant target not damaged"), Far->Attributes->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Boss without threat cannot fall back to NPC damage"), MissingThreat->Attributes->GetCurrentHealth(), 100.f);
		TestTrue(TEXT("Ordinary NPC keeps existing GE damage route"), Ordinary->Attributes()->GetCurrentHealth() < 1000.f);
		const float Before = Near->Attributes->GetCurrentHealth();
		F.Ability->Finish();
		F.Event();
		F.Ability->DispatchAOE(); // Even a late native callback cannot revive the cleared boss snapshot.
		TestEqual(TEXT("Ended AOE cannot damage boss"), Near->Attributes->GetCurrentHealth(), Before);
	}
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Boss = F.Boss(FVector(140, 0, 0), false);
		if (!TestNotNull(TEXT("Initially idle boss"), Boss) || !TestTrue(TEXT("Idle activation"), F.Start())) return false;
		Boss->Boss->StartEncounter();
		F.Event();
		TestEqual(TEXT("Impact cannot acquire a later-started encounter"), Boss->Attributes->GetCurrentHealth(), 100.f);
		F.Ability->Finish();
		if (!TestTrue(TEXT("Fresh activation captures active encounter"), F.Start())) return false;
		Boss->Boss->BeginReset();
		Boss->SetTestHealth(100.f);
		Boss->Boss->CompleteReset();
		Boss->Boss->StartEncounter();
		F.Event();
		TestEqual(TEXT("Old AOE cannot damage restarted encounter"), Boss->Attributes->GetCurrentHealth(), 100.f);
		F.Ability->Finish();
		if (!TestTrue(TEXT("New encounter activation"), F.Start())) return false;
		F.Event();
		TestTrue(TEXT("New AOE damages restarted boss"), Boss->Attributes->GetCurrentHealth() < 100.f);
		const float Before = Boss->Attributes->GetCurrentHealth();
		auto* Replacement = F.World->SpawnActor<AAIController>();
		Replacement->SetGenericTeamId(FGenericTeamId(0));
		F.Source->GetController()->UnPossess();
		Replacement->Possess(F.Source);
		F.Event();
		TestEqual(TEXT("Controller replacement cannot inherit active AOE"), Boss->Attributes->GetCurrentHealth(), Before);
		F.Source->SetTestRole(ROLE_SimulatedProxy);
		F.Ability->DispatchAOE();
		TestEqual(TEXT("Client callback cannot apply damage"), Boss->Attributes->GetCurrentHealth(), Before);
		F.Source->SetTestRole(ROLE_Authority);
	}
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* First = F.Boss(FVector(140, 0, 0));
		auto* Second = F.Boss(FVector(0, 140, 0));
		if (!TestTrue(TEXT("Reentrant targets"), First && Second) || !TestTrue(TEXT("Reentrant activation"), F.Start())) return false;
		bool bRestarted = false;
		auto Restart = [&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
		{
			if (bRestarted) return;
			bRestarted = true;
			F.Ability->Finish();
			TestTrue(TEXT("Synchronous callback starts a new activation"), F.Start());
		};
		const auto A = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(Restart);
		const auto B = Second->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(Restart);
		F.Event();
		First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(A);
		Second->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(B);
		TestTrue(TEXT("Damage invoked synchronous restart"), bRestarted);
		TestTrue(TEXT("Old query stops before another target of the new activation"),
			(First->Attributes->GetCurrentHealth() == 100.f) != (Second->Attributes->GetCurrentHealth() == 100.f));
		F.Event();
		TestTrue(TEXT("New activation can perform its own fresh query"),
			First->Attributes->GetCurrentHealth() < 100.f && Second->Attributes->GetCurrentHealth() < 100.f);
	}
	return true;
}
#endif
