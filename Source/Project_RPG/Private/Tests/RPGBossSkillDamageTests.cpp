#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

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

namespace RPGBossSkillDamageTests
{
struct FFixture
{
	UWorld* World = nullptr;
	ARPGBossTestPawn* Source = nullptr;
	URPGSkillDamageSecurityTestAbility* Ability = nullptr;
	FGameplayAbilitySpecHandle Handle;
	~FFixture()
	{
		if (Source) Source->ASC->CancelAllAbilities();
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	}
	ARPGBossTestPawn* Boss(FVector Location, bool bStart)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Pawn = World->SpawnActor<ARPGBossTestPawn>(Location, FRotator::ZeroRotator, Params);
		auto* Controller = World->SpawnActor<ARPGAIController>();
		if (!Pawn || !Controller) return nullptr;
		Controller->Possess(Pawn);
		Controller->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(), false);
		Pawn->Attributes->SetDefense(1.f);
		auto* Threat = NewObject<URPGBossThreatComponent>(Pawn);
		Threat->RegisterComponent();
		if (bStart && !Pawn->Boss->StartEncounter()) return nullptr;
		return Pawn;
	}
	bool Initialize(FAutomationTestBase& Test)
	{
		World = UWorld::CreateWorld(EWorldType::Game, false,
			MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!Test.TestNotNull(TEXT("Boss skill world"), World)) return false;
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
		Source->ASC->AddLooseGameplayTag(RPGGameplayTags::Player_Status_Rage_Active);
		Handle = Source->ASC->GiveAbility(FGameplayAbilitySpec(URPGSkillDamageSecurityTestAbility::StaticClass(), 1));
		const auto* Spec = Source->ASC->FindAbilitySpecFromHandle(Handle);
		Ability = Spec ? Cast<URPGSkillDamageSecurityTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
		return Test.TestNotNull(TEXT("Boss skill ability"), Ability);
	}
	bool Start(FAutomationTestBase& Test, FGameplayTag Event, int32 HitBudget = 8)
	{
		FRPGSkillRuntimeSpec Runtime;
		Runtime.ExecutionPolicyClass = URPGSkillExecutionPolicy::StaticClass();
		Runtime.BaseDamage = 10.f;
		Runtime.TargetingProfile.Shape.Type = ERPGHitQueryShape::Sector;
		Runtime.TargetingProfile.Shape.Radius = 500.f;
		Runtime.TargetingProfile.Shape.AngleDegrees = 90.f;
		Runtime.TargetingProfile.Shape.HalfHeight = 100.f;
		Runtime.TargetingProfile.Filter.TeamRule = ERPGHitQueryTeamRule::HostileOnly;
		Runtime.SecurityProfile.MaximumServerHitDistance = 600.f;
		Runtime.SecurityProfile.MaximumTargetsPerQuery = 8;
		Runtime.SecurityProfile.MaximumHitsPerActivation = HitBudget;
		Runtime.SecurityProfile.MaximumDamagePerHit = 100.f;
		Ability->Configure(Runtime, Event);
		World->Tick(LEVELTICK_All, 0.01f);
		return Test.TestTrue(TEXT("Boss skill activation"), Source->ASC->TryActivateAbility(Handle));
	}
	void Event(FGameplayTag Tag, AActor* ForgedTarget)
	{
		FGameplayEventData Payload;
		Payload.EventTag = Tag;
		Payload.Target = ForgedTarget;
		Payload.EventMagnitude = 1.e9f;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Source, Tag, Payload);
	}
	bool Apply(TSubclassOf<UGameplayEffect> Effect = URPGSkillDamageSecurityTestEffect::StaticClass())
	{
		TArray<FRPGHitQueryResult> Hits;
		int32 Count = 0;
		FText Error;
		return Ability->ExecuteAuthorizedSkillDamage(Effect, 10.f, FGameplayTag(), Hits, Count, Error);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBossSkillDamageTest, "ProjectRPG.Boss.SkillDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGBossSkillDamageTest::RunTest(const FString&)
{
	using namespace RPGBossSkillDamageTests;
	// Actual native event -> server shape query -> receipt-backed GE -> HP/threat.
	// The fixture bypasses montage/input, not the production damage operation.
	const FGameplayTag Events[] = { RPGGameplayTags::Player_Event_AOE, RPGGameplayTags::Shared_Event_Hit };
	for (FGameplayTag Tag : Events)
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* First = F.Boss(FVector(220, -60, 0), true);
		auto* Second = F.Boss(FVector(320, 60, 0), true);
		auto* Idle = F.Boss(FVector(270, 0, 0), false);
		auto* Far = F.Boss(FVector(800, 0, 0), true);
		if (!First || !Second || !Idle || !Far) return false;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Ordinary = F.World->SpawnActor<ARPGSkillDamageSecurityTestPawn>(FVector(250, 100, 0), FRotator::ZeroRotator, Params);
		auto* OrdinaryController = F.World->SpawnActor<AAIController>();
		if (!Ordinary || !OrdinaryController) return false;
		OrdinaryController->SetGenericTeamId(FGenericTeamId(1));
		OrdinaryController->Possess(Ordinary);
		auto* FirstThreat = First->FindComponentByClass<URPGBossThreatComponent>();
		auto* SecondThreat = Second->FindComponentByClass<URPGBossThreatComponent>();
		bool bAbilityContext = false;
		const FDelegateHandle ContextDelegate = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
			{
				bAbilityContext = Spec.GetContext().GetAbilityInstance_NotReplicated() == F.Ability &&
					Spec.GetContext().GetHitResult() && Spec.GetContext().GetHitResult()->GetActor() == First;
			});
		if (!F.Start(*this, Tag, 3)) return false;
		F.Event(Tag, Far);
		TestEqual(TEXT("First queried boss loses authored HP, not forged magnitude"), First->Attributes->GetCurrentHealth(), 90.f);
		TestEqual(TEXT("Second queried boss receives the same AOE damage"), Second->Attributes->GetCurrentHealth(), 90.f);
		TestEqual(TEXT("Ordinary NPC in mixed AOE retains existing damage route"), Ordinary->Attributes()->GetCurrentHealth(), 990.f);
		TestEqual(TEXT("First threat equals real HP damage"), FirstThreat->GetThreatForTarget(F.Source), 10.0);
		TestEqual(TEXT("Second threat equals real HP damage"), SecondThreat->GetThreatForTarget(F.Source), 10.0);
		TestTrue(TEXT("Boss effect preserves ability and authoritative hit context"), bAbilityContext);
		TestEqual(TEXT("Idle boss is not sent through generic fallback"), Idle->Attributes->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Forged out-of-range target is not hit"), Far->Attributes->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Only approved targets spend the cumulative budget"), F.Ability->HitCount(), 3);
		F.Event(Tag, First);
		TestEqual(TEXT("Repeated notify cannot exceed activation hit budget"), First->Attributes->GetCurrentHealth(), 90.f);
		F.Ability->Finish();
		F.Event(Tag, First);
		TestEqual(TEXT("Ended skill cannot dispatch late boss damage"), Second->Attributes->GetCurrentHealth(), 90.f);
		First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(ContextDelegate);
	}
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* Boss = F.Boss(FVector(300, 0, 0), false);
		if (!Boss) return false;
		auto* Threat = Boss->FindComponentByClass<URPGBossThreatComponent>();
		if (!F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		TestTrue(TEXT("Encounter begins after skill activation"), Boss->Boss->StartEncounter());
		TestFalse(TEXT("No impact-time encounter capture"), F.Apply());
		TestEqual(TEXT("Late-start boss retains HP"), Boss->Attributes->GetCurrentHealth(), 100.f);
		F.Ability->Finish();
		if (!F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		TestTrue(TEXT("Reset starts"), Boss->Boss->BeginReset());
		Boss->SetTestHealth(100.f);
		TestTrue(TEXT("Reset completes"), Boss->Boss->CompleteReset());
		TestTrue(TEXT("Encounter restarts"), Boss->Boss->StartEncounter());
		TestFalse(TEXT("Old skill cannot damage a restarted encounter"), F.Apply());
		TestEqual(TEXT("Old skill preserves restarted HP"), Boss->Attributes->GetCurrentHealth(), 100.f);
		TestEqual(TEXT("Old skill adds no threat"), Threat->GetThreatForTarget(F.Source), 0.0);
		TestEqual(TEXT("Rejected encounter consumes no hit budget"), F.Ability->HitCount(), 0);
		F.Ability->Finish();
		if (!F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		Boss->ASC->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		TestFalse(TEXT("Invincible boss is skipped"), F.Apply());
		Boss->ASC->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible);
		TestFalse(TEXT("Unsupported effect cannot reach extra HP modifier"), F.Apply(URPGSkillDamageSecurityExtraModifierEffect::StaticClass()));
		TestEqual(TEXT("Invincible and unsupported GE preserve HP"), Boss->Attributes->GetCurrentHealth(), 100.f);
		TestTrue(TEXT("New activation can damage the new encounter"), F.Apply());
		TestEqual(TEXT("New encounter HP decreases once"), Boss->Attributes->GetCurrentHealth(), 90.f);
		TestEqual(TEXT("New encounter receives damage threat"), Threat->GetThreatForTarget(F.Source), 10.0);
		F.Ability->Finish();
		if (!F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		F.Source->GetController()->UnPossess();
		TestFalse(TEXT("Unpossessed skill source is rejected"), F.Apply());
		auto* Replacement = F.World->SpawnActor<AAIController>();
		Replacement->SetGenericTeamId(FGenericTeamId(0));
		Replacement->Possess(F.Source);
		TestFalse(TEXT("New controller cannot inherit the old skill snapshot"), F.Apply());
		F.Ability->Finish();
		if (!F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		F.Source->SetTestRole(ROLE_SimulatedProxy);
		TestFalse(TEXT("Client cannot apply skill boss damage"), F.Apply());
		F.Source->SetTestRole(ROLE_Authority);
		TestEqual(TEXT("Rejected identities retain HP"), Boss->Attributes->GetCurrentHealth(), 90.f);
	}
	{
		FFixture F;
		if (!F.Initialize(*this)) return false;
		auto* First = F.Boss(FVector(220, -60, 0), true);
		auto* Second = F.Boss(FVector(320, 60, 0), true);
		if (!First || !Second || !F.Start(*this, RPGGameplayTags::Player_Event_AOE)) return false;
		bool bReentrantApplied = true;
		const FDelegateHandle Delegate = First->ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle)
			{
				bReentrantApplied = F.Apply();
				F.Ability->Finish();
			});
		TestTrue(TEXT("First GE completes even when callback ends skill"), F.Apply());
		TestFalse(TEXT("GE callback cannot reenter boss damage"), bReentrantApplied);
		TestEqual(TEXT("First boss loses HP once"), First->Attributes->GetCurrentHealth(), 90.f);
		TestEqual(TEXT("End callback stops remaining AOE targets"), Second->Attributes->GetCurrentHealth(), 100.f);
		First->ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Delegate);
	}
	return true;
}
#endif
