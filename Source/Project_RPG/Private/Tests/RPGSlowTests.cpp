#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Attribute/RPGAttributeSet.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayEffect/RPGSlowEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>

struct FRPGSlowTestAccess { static void Quiet(URPGCombatControlComponent& C) { C.StunVisual.Reset(); C.bShowSlowStatus = false; } };
namespace RPGSlowTests
{
	struct FScope
	{
		UWorld* World = nullptr; uint64 Frame = GFrameCounter;
		~FScope() { if (World) { RPGTestWorld::Destroy(World); } }
		bool Init()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			auto* A = World->SpawnActor<AActor>(); auto* B = NewObject<UBoxComponent>(A); A->SetRootComponent(B); A->AddInstanceComponent(B);
			B->SetBoxExtent(FVector(10000, 10000, 50)); B->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			B->SetCollisionObjectType(ECC_WorldStatic); B->SetCollisionResponseToAllChannels(ECR_Block); B->RegisterComponent(); A->SetActorLocation(FVector(0,0,-50));
			return true;
		}
		ARPGStunTestCharacter* Spawn(FVector Location = FVector(0,0,300))
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(Location, FRotator::ZeroRotator, P);
			auto* ASC = C->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(C,C);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),100); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);
			C->GetCharacterMovement()->bRunPhysicsWithNoController = true; C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			C->GetCharacterMovement()->MaxWalkSpeed = 400;
			FRPGSlowTestAccess::Quiet(*C->GetCombatControlComponent()); return C;
		}
		void Tick(float Seconds, ARPGStunTestCharacter* Moving = nullptr)
		{
			for(int i=0;i<FMath::CeilToInt(Seconds*60);++i)
			{
				Frame=FMath::Max(Frame,GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter,Frame);
				if(Moving) Moving->AddMovementInput(FVector::ForwardVector);
				World->Tick(LEVELTICK_All,1.f/60);
			}
		}
	};
	FRPGSlowSettings Settings(float Percent=40, float Duration=3) { FRPGSlowSettings S;S.Percent=Percent;S.Duration=Duration;return S; }
	FActiveGameplayEffectHandle Apply(ARPGBaseCharacter* S, ARPGBaseCharacter* T, float Percent=40, float Duration=3, FGuid Id=FGuid::NewGuid())
	{
		return T->GetCombatControlComponent()->ApplyApprovedDamageSlow(S,FHitResult(T,nullptr,T->GetActorLocation(),FVector::ForwardVector),Id,Settings(Percent,Duration));
	}
	FActiveGameplayEffectHandle Buff(UAbilitySystemComponent* ASC, FGameplayAttribute Attribute, float Amount)
	{
		auto* GE=NewObject<UGameplayEffect>(); GE->DurationPolicy=EGameplayEffectDurationType::Infinite;
		FGameplayModifierInfo M;M.Attribute=Attribute;M.ModifierOp=EGameplayModOp::Additive;M.ModifierMagnitude=FScalableFloat(Amount);GE->Modifiers.Add(M);
		return ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(GE,ASC->MakeEffectContext(),1));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlowDataTest,"ProjectRPG.Combat.Slow.DataAndFrozenSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGSlowDataTest::RunTest(const FString&)
{
	using namespace RPGSlowTests; FGameplayAbilitySpec Spec;
	FRPGPlayerAbilitySet Entry; Entry.InputTag=RPGGameplayTags::InputTag_Roll;Entry.AbilityToGrant=URPGStunTestCombatAbility::StaticClass();Entry.Slow=Settings();
	TestTrue(TEXT("Shared Player grant"),Entry.BuildAbilitySpec(1,GetTransientPackage(),Spec));Entry.Slow.Percent=0;
	TestEqual(TEXT("Frozen percentage"),RPGSlow::GetGrantedSettings(&Spec).Percent,40.f);
	Spec.SetByCallerTagMagnitudes.Remove(FGameplayTag::RequestGameplayTag(TEXT("Ability.Data.Status.SlowSeconds")));
	TestFalse(TEXT("Incomplete metadata disabled"),RPGSlow::GetGrantedSettings(&Spec).IsEnabled());
	auto* NPC=NewObject<UDataAsset_NPCStartUpData>();NPC->AttackSlows.Add(URPGNPCGameplayAbility::StaticClass(),Settings(50));
	TestTrue(TEXT("Shared NPC grant"),NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(),1,NPC,Spec));
	TestEqual(TEXT("Same metadata"),RPGSlow::GetGrantedSettings(&Spec).Percent,50.f);
	auto* D=NewObject<URPGSkillDefinition>();D->Slow=Settings(60);FRPGSkillRuntimeSpec R;D->BuildRuntimeSpec(nullptr,FRPGSkillSaveData(),R);D->Slow.Percent=0;
	TestEqual(TEXT("Skill activation freezes slow"),R.Slow.Percent,60.f);
	R.StunDuration=2;R.Knockback.Distance=200;R.Reset();
	TestFalse(TEXT("A later activation cannot inherit the old slow"),R.Slow.IsEnabled());
	TestFalse(TEXT("Activation reset also retires old displacement"),R.Knockback.IsEnabled());
	TestEqual(TEXT("Activation reset retires old stun"),R.StunDuration,0.f);
	TestFalse(TEXT("Too strong"),Settings(91).IsValid());TestFalse(TEXT("Too small"),Settings(1).IsValid());
	TestFalse(TEXT("Nonfinite"),Settings(std::numeric_limits<float>::quiet_NaN()).IsValid());TestFalse(TEXT("Unbounded duration"),Settings(40,31).IsValid());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlowStackTest,"ProjectRPG.Combat.Slow.StrongestFallbackAndBuffPreservation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGSlowStackTest::RunTest(const FString&)
{
	using namespace RPGSlowTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();W.Tick(1);
	auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();auto* M=T->GetCharacterMovement();
	const auto Boost=Buff(ASC,URPGAttributeSet::GetMoveSpeedPercentAttribute(),20);
	TestEqual(TEXT("Existing percent buffs consumed"),M->GetMaxSpeed(),480.f);
	const auto Weak=Apply(S,T,40,5),Strong=Apply(S,T,70,1);
	TestTrue(TEXT("Separate durations"),Weak.IsValid()&&Strong.IsValid());TestTrue(TEXT("Only strongest reduction"),FMath::IsNearlyEqual(M->GetMaxSpeed(),144.f,.01f));
	TestTrue(TEXT("Strong snapshot"),FMath::IsNearlyEqual(C->GetSlowPresentationState().Percent,70.f,.01f));
	W.Tick(1.2f);TestTrue(TEXT("Weak remaining effect takes over"),FMath::IsNearlyEqual(M->GetMaxSpeed(),288.f,.01f));
	TestTrue(TEXT("Snapshot falls back"),FMath::IsNearlyEqual(C->GetSlowPresentationState().Percent,40.f,.01f));
	TestTrue(TEXT("Remove one owned slow"),C->RemoveOwnedSlow(Weak));TestEqual(TEXT("Buff still applied"),M->GetMaxSpeed(),480.f);
	TestNotNull(TEXT("Buff handle preserved"),ASC->GetActiveGameplayEffect(Boost));TestFalse(TEXT("Snapshot ended"),C->GetSlowPresentationState().bActive);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlowMovementTest,"ProjectRPG.Combat.Slow.ActualMovementAndScopedSpeedLimits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGSlowMovementTest::RunTest(const FString&)
{
	using namespace RPGSlowTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();W.Tick(1);
	auto* M=CastChecked<URPGCharacterMovementComponent>(T->GetCharacterMovement());auto* ASC=T->GetRPGAbilitySystemComponent();auto* C=T->GetCombatControlComponent();
	const auto Ability=ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(),1));ASC->TryActivateAbility(Ability);
	const auto Slow=Apply(S,T,50,5);W.Tick(.5f,T);const auto Start=T->GetActorLocation();W.Tick(.5f,T);
	TestTrue(TEXT("Actual CMC speed is halved"),FMath::IsNearlyEqual(M->Velocity.Size2D(),200.f,2.f));
	TestTrue(TEXT("Actual travel is reduced"),FMath::IsNearlyEqual(FVector::Dist2D(Start,T->GetActorLocation()),100.f,3.f));
	TestTrue(TEXT("Slow does not cancel combat"),ASC->FindAbilitySpecFromHandle(Ability)->IsActive());TestFalse(TEXT("Slow is not stun"),RPGCombatControl::IsActionBlocked(T));
	const auto Lock=M->BeginWalkSpeedLimit(150);TestEqual(TEXT("Lock before multiplier"),M->GetMaxSpeed(),75.f);
	M->MaxWalkSpeed=600;const auto Gear=Buff(ASC,URPGAttributeSet::GetMoveSpeedPercentAttribute(),20);
	M->EndWalkSpeedLimit(Lock);TestTrue(TEXT("Unlock uses current gear and base speed"),FMath::IsNearlyEqual(M->GetMaxSpeed(),360.f,.01f));
	C->RemoveOwnedSlow(Slow);TestEqual(TEXT("No stale base restore"),M->GetMaxSpeed(),720.f);
	ASC->RemoveActiveGameplayEffect(Gear);TestEqual(TEXT("Latest base survives both removals"),M->GetMaxSpeed(),600.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlowLifecycleTest,"ProjectRPG.Combat.Slow.ImmunityReceiptsDeathAndReuse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGSlowLifecycleTest::RunTest(const FString&)
{
	using namespace RPGSlowTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();
	auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();
	C->SetDefaultSlowImmunity(true);TestFalse(TEXT("Default immunity"),Apply(S,T).IsValid());C->SetDefaultSlowImmunity(false);
	const auto Receipt=FGuid::NewGuid();ASC->AddLooseGameplayTag(RPGSlow::ImmunityTag());TestFalse(TEXT("Temporary immunity"),Apply(S,T,40,3,Receipt).IsValid());
	ASC->RemoveLooseGameplayTag(RPGSlow::ImmunityTag());TestFalse(TEXT("Immune receipt cannot replay"),Apply(S,T,40,3,Receipt).IsValid());
	TestFalse(TEXT("Invalid receipt"),Apply(S,T,40,3,FGuid()).IsValid());
	TestTrue(TEXT("Fresh approved hit"),Apply(S,T).IsValid());const auto Boost=Buff(ASC,URPGAttributeSet::GetMoveSpeedPercentAttribute(),20);
	T->AdvanceLife();C->ResetForAvatarReuse();TestFalse(TEXT("No old-life status"),C->GetSlowPresentationState().bActive);
	TestNotNull(TEXT("External buff survives reset"),ASC->GetActiveGameplayEffect(Boost));TestEqual(TEXT("Slow modifier gone"),ASC->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute()),1.f);
	TestTrue(TEXT("New-life status"),Apply(S,T).IsValid());ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);
	TestFalse(TEXT("Death cleans own slow"),C->GetSlowPresentationState().bActive);TestEqual(TEXT("No modifier leaked on corpse"),ASC->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute()),1.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSlowReentryTest,"ProjectRPG.Combat.Slow.InhibitionReentryAndSourceLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGSlowReentryTest::RunTest(const FString&)
{
	using namespace RPGSlowTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();
	auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();
	const auto Inhibit=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([ASC](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle H)
	{if(Spec.Def->GetClass()==URPGSlowEffect::StaticClass())ASC->SetActiveGameplayEffectInhibit(MoveTemp(H),true,false);});
	TestFalse(TEXT("Already inhibited status rejected"),Apply(S,T).IsValid());ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Inhibit);
	const auto Reset=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{if(Spec.Def->GetClass()==URPGSlowEffect::StaticClass())C->ResetForAvatarReuse();});
	TestFalse(TEXT("Reentrant reset retires pending slow"),Apply(S,T).IsValid());ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Reset);
	TestEqual(TEXT("No pending modifier leaked"),ASC->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute()),1.f);
	auto Handle=Apply(S,T,50,3);ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(Handle),true,false);
	TestEqual(TEXT("Inhibited slow does not affect movement"),T->GetCharacterMovement()->GetMaxSpeed(),400.f);
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(Handle),false,false);
	TestEqual(TEXT("Uninhibited duration resumes"),T->GetCharacterMovement()->GetMaxSpeed(),200.f);
	S->Destroy();TestEqual(TEXT("Committed slow outlives its attacker"),T->GetCharacterMovement()->GetMaxSpeed(),200.f);
	W.Tick(3.2f);TestEqual(TEXT("Natural expiry needs no live source"),T->GetCharacterMovement()->GetMaxSpeed(),400.f);return true;
}
#endif
