#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGDefenseReductionTestFixture.h"
#include "Tests/RPGSkillDamageSecurityTestFixture.h"
#include "Tests/RPGBossStartupTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "AI/RPGBossAIComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Controller/RPGAIController.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayEffect/RPGDefenseReductionEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>

namespace RPGDefenseReductionTests
{
	struct FScope
	{
		UWorld* World = nullptr;
		uint64 Frame = GFrameCounter;
		~FScope() { if (World) RPGTestWorld::Destroy(World); }
		bool Init()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			return true;
		}
		ARPGStunTestCharacter* Spawn(FVector Location = FVector::ZeroVector)
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(Location, FRotator::ZeroRotator, P);
			auto* ASC = C->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(C, C);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(), 100);
			return C;
		}
		void Tick(float Seconds)
		{
			for (int32 I=0; I<FMath::CeilToInt(Seconds*60); ++I)
			{
				Frame=FMath::Max(Frame,GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter,Frame);
				World->Tick(LEVELTICK_All,1.f/60);
			}
		}
	};
	FRPGDefenseReductionSettings Settings(float Percent=20, float Duration=3) { FRPGDefenseReductionSettings S; S.Percent=Percent; S.Duration=Duration; return S; }
	FActiveGameplayEffectHandle Apply(ARPGBaseCharacter* S, ARPGBaseCharacter* T, float Percent=20, float Duration=3, FGuid Id=FGuid::NewGuid())
	{
		return T->GetCombatControlComponent()->ApplyApprovedDamageDefenseReduction(S,FHitResult(T,nullptr,T->GetActorLocation(),FVector::ForwardVector),Id,Settings(Percent,Duration));
	}
	float Defense(ARPGBaseCharacter* T) { return T->GetRPGAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetDefenseAttribute()); }
	struct FWaitForBossData : IAutomationLatentCommand
	{
		TFunction<bool()> Check;
		explicit FWaitForBossData(TFunction<bool()> InCheck) : Check(MoveTemp(InCheck)) {}
		virtual bool Update() override { return Check(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseDataTest,"ProjectRPG.Combat.DefenseReduction.GrantAndSkillData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseDataTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests;
	FRPGPlayerAbilitySet Entry; Entry.InputTag=RPGGameplayTags::InputTag_Roll; Entry.AbilityToGrant=URPGStunTestCombatAbility::StaticClass(); Entry.DefenseReduction=Settings();
	FGameplayAbilitySpec Spec;
	TestTrue(TEXT("Player data grant"),Entry.BuildAbilitySpec(1,GetTransientPackage(),Spec)); Entry.DefenseReduction.Percent=0;
	TestEqual(TEXT("Frozen grant"),RPGDefenseReduction::GetGrantedSettings(&Spec).Percent,20.f);
	auto* NPC=NewObject<UDataAsset_NPCStartUpData>(); NPC->AttackDefenseReductions.Add(URPGNPCGameplayAbility::StaticClass(),Settings(30));
	TestTrue(TEXT("NPC shared ancestor grant"),NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(),1,NPC,Spec));
	TestEqual(TEXT("NPC metadata"),RPGDefenseReduction::GetGrantedSettings(&Spec).Percent,30.f);
	auto* D=NewObject<URPGSkillDefinition>(); D->DefenseReduction=Settings(40); FRPGSkillRuntimeSpec Runtime; D->BuildRuntimeSpec(nullptr,FRPGSkillSaveData(),Runtime); D->DefenseReduction.Percent=0;
	TestEqual(TEXT("Skill snapshots effect"),Runtime.DefenseReduction.Percent,40.f); Runtime.Reset();
	TestFalse(TEXT("Reset cannot inherit effect"),Runtime.DefenseReduction.IsEnabled());
	TestFalse(TEXT("NaN rejected"),Settings(std::numeric_limits<float>::quiet_NaN()).IsValid());
	TestFalse(TEXT("Strength bounded"),Settings(81).IsValid()); TestFalse(TEXT("Duration bounded"),Settings(20,31).IsValid()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseStackTest,"ProjectRPG.Combat.DefenseReduction.CompoundExpiryAndCurrentBuffs",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseStackTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests; FScope W; if(!W.Init())return false;
	auto* S=W.Spawn(FVector(-300,0,0)); auto* T=W.Spawn(); auto* ASC=T->GetRPGAbilitySystemComponent(); auto* C=T->GetCombatControlComponent();
	const auto A=Apply(S,T,20,5),B=Apply(S,T,30,.2f);
	TestTrue(TEXT("Independent handles"),A.IsValid()&&B.IsValid()); TestTrue(TEXT("80% times 70% remains"),FMath::IsNearlyEqual(Defense(T),56.f,.001f));
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(),200);
	TestTrue(TEXT("Current equipment base consumed"),FMath::IsNearlyEqual(Defense(T),112.f,.001f));
	W.Tick(.4f); TestTrue(TEXT("Only expired modifier removed"),FMath::IsNearlyEqual(Defense(T),160.f,.001f));
	auto* GE=NewObject<UGameplayEffect>(); GE->DurationPolicy=EGameplayEffectDurationType::Infinite;
	FGameplayModifierInfo M; M.Attribute=URPGAttributeSet::GetDefenseAttribute(); M.ModifierOp=EGameplayModOp::Additive; M.ModifierMagnitude=FScalableFloat(50); GE->Modifiers.Add(M);
	const auto Buff=ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(GE,ASC->MakeEffectContext(),1));
	TestTrue(TEXT("Concurrent buff participates"),FMath::IsNearlyEqual(Defense(T),200.f,.001f));
	TestTrue(TEXT("Remove exact owned handle"),C->RemoveOwnedDefenseReduction(A));
	TestEqual(TEXT("Current base plus buff preserved"),Defense(T),250.f); TestNotNull(TEXT("Buff survives"),ASC->GetActiveGameplayEffect(Buff));
	TestFalse(TEXT("No action control"),RPGCombatControl::IsActionBlocked(T)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseImmunityTest,"ProjectRPG.Combat.DefenseReduction.ImmunityReceiptsAndBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseImmunityTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-300,0,0)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* ASC=T->GetRPGAbilitySystemComponent();
	C->SetDefaultHitReactionImmunity(true); C->SetDefaultStunImmunity(true); C->SetDefaultKnockbackImmunity(true); C->SetDefaultAirborneImmunity(true); C->SetDefaultSlowImmunity(true);
	TestTrue(TEXT("Control immunity permits stat debuff"),Apply(S,T).IsValid()); C->CleanseDefenseReductions();
	const auto Id=FGuid::NewGuid(); ASC->AddLooseGameplayTag(RPGDefenseReduction::ImmunityTag());
	TestFalse(TEXT("Individual immunity rejects new effect"),Apply(S,T,20,3,Id).IsValid()); ASC->RemoveLooseGameplayTag(RPGDefenseReduction::ImmunityTag());
	TestFalse(TEXT("Rejected receipt cannot replay after immunity"),Apply(S,T,20,3,Id).IsValid());
	const auto Group=FGameplayTag::RequestGameplayTag(TEXT("Shared.Immunity.StatusEffects")); ASC->AddLooseGameplayTag(Group);
	TestFalse(TEXT("Status group rejects new effect"),Apply(S,T).IsValid()); ASC->RemoveLooseGameplayTag(Group);
	for(int I=0;I<8;++I) TestTrue(TEXT("Bounded independent stack"),Apply(S,T,80).IsValid());
	TestTrue(TEXT("Defense remains positive"),Defense(T)>0.f); TestFalse(TEXT("Ninth effect rejected"),Apply(S,T).IsValid());
	C->CleanseDefenseReductions(); TestEqual(TEXT("Cleanse restores current base"),Defense(T),100.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseLifetimeTest,"ProjectRPG.Combat.DefenseReduction.ReentrantCleanseDeathAndReuse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseLifetimeTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-300,0,0)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* ASC=T->GetRPGAbilitySystemComponent();
	const auto Callback=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle){if(Spec.Def==GetDefault<URPGDefenseReductionEffect>())C->CleanseDefenseReductions();});
	TestFalse(TEXT("Cleanse during application retires pending effect"),Apply(S,T).IsValid()); ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Callback);
	TestEqual(TEXT("No orphan modifier"),Defense(T),100.f);
	Apply(S,T); S->Destroy(); W.Tick(.1f); TestEqual(TEXT("Committed stat debuff survives source destruction"),Defense(T),80.f);
	C->ResetForAvatarReuse(); TestEqual(TEXT("Reuse clears effect"),Defense(T),100.f);
	auto* Next=W.Spawn(FVector(-300,0,0)); Apply(Next,T); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);
	TestEqual(TEXT("Target death clears effect"),Defense(T),100.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseBossTest,"ProjectRPG.Combat.DefenseReduction.RealBossImmunityAndEncounter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseBossTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests; auto W=MakeShared<FScope>(); if(!W->Init())return false; W->World->CreateAISystem(); auto* S=W->Spawn(FVector(-300,0,0));
	auto* B=W->World->SpawnActor<ARPGBossStartupTestCharacter>(); auto* AI=W->World->SpawnActor<ARPGAIController>(); AI->Possess(B);
	const double Deadline=FPlatformTime::Seconds()+10.;
	AddCommand(new FWaitForBossData([this,W,S,B,Deadline]()
	{
		W->Tick(1.f/60);
		if(!B->IsNPCStartUpDataReady())
		{
			if(FPlatformTime::Seconds()<Deadline)return false;
			AddError(TEXT("Native boss startup timed out")); return true;
		}
		auto* C=B->GetCombatControlComponent(); auto* ASC=B->GetRPGAbilitySystemComponent(); ASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(),100);
		TestTrue(TEXT("Boss hit reaction immunity"),C->IsImmuneTo(ERPGCombatEffectKind::Stagger)); TestTrue(TEXT("Boss stun immunity"),C->IsImmuneTo(ERPGCombatEffectKind::Stun));
		TestFalse(TEXT("Idle encounter rejects effect"),Apply(S,B).IsValid());
		if(!TestTrue(TEXT("Start encounter"),B->BossAIComponent->StartEncounter()))return true;
		TestTrue(TEXT("Actual boss accepts stat debuff"),Apply(S,B).IsValid()); TestEqual(TEXT("Boss defense reduced"),Defense(B),80.f);
		TestTrue(TEXT("Begin reset"),B->BossAIComponent->BeginReset()); W->Tick(.1f);
		TestEqual(TEXT("Encounter reset retires modifier"),Defense(B),100.f); return true;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGDefenseDamageTest,"ProjectRPG.Combat.DefenseReduction.ApprovedDamageRouteAndNextHit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGDefenseDamageTest::RunTest(const FString&)
{
	using namespace RPGDefenseReductionTests; FScope W; if(!W.Init())return false; W.World->CreateAISystem();
	auto* S=W.Spawn(); auto* T=W.Spawn(FVector(150,0,0));
	for(auto* Pawn : {S,T}) { auto* AI=W.World->SpawnActor<AAIController>(); AI->SetGenericTeamId(FGenericTeamId(Pawn==S?0:1)); AI->Possess(Pawn); }
	auto* SourceASC=S->GetRPGAbilitySystemComponent(); auto* TargetASC=T->GetRPGAbilitySystemComponent();
	SourceASC->SetNumericAttributeBase(URPGAttributeSet::GetAttackAttribute(),1); SourceASC->SetNumericAttributeBase(URPGAttributeSet::GetCriticalChanceAttribute(),0);
	TargetASC->SetNumericAttributeBase(URPGAttributeSet::GetDefenseAttribute(),2);
	FGameplayAbilitySpec Grant(URPGDefenseReductionTestAbility::StaticClass(),1); RPGDefenseReduction::SetGrantedSettings(Grant,Settings(50));
	const auto Handle=SourceASC->GiveAbility(Grant); SourceASC->TryActivateAbility(Handle);
	auto* Ability=Cast<URPGDefenseReductionTestAbility>(SourceASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance()); if(!TestNotNull(TEXT("Instanced attack"),Ability))return false;
	const auto Hit=[&]() { auto Spec=SourceASC->MakeOutgoingSpec(URPGSkillDamageSecurityTestEffect::StaticClass(),1,SourceASC->MakeEffectContext()); Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage,10); ERPGSuccessType Success; bool Legacy=false; Ability->BP_ApplyEffectSpecHandleToTarget(T,Spec,Success,Legacy); return Success; };
	TestTrue(TEXT("Approved first damage"),Hit()==ERPGSuccessType::Successful); TestEqual(TEXT("First hit uses prior defense"),TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()),95.f);
	TestEqual(TEXT("Post-hit effect routed"),Defense(T),1.f);
	TestTrue(TEXT("Approved next damage"),Hit()==ERPGSuccessType::Successful); TestEqual(TEXT("Next damage consumes reduced defense"),TargetASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()),85.f);
	return true;
}
#endif
