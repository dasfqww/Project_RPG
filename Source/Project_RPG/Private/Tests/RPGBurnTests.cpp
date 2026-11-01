#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGBossTestFixture.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "AIController.h"
#include "Controller/RPGAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Attribute/RPGAttributeSet.h"
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
#include "GameplayEffect/RPGBurnEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>
struct FRPGBurnTestAccess
{
 static void Quiet(URPGCombatControlComponent& C) { C.StunVisual.Reset(); C.bShowSlowStatus=false; C.bShowBurnStatus=false; }
 static void Hitch(URPGCombatControlComponent& C) { for(auto& E:C.OwnedBurns) E.Value.NextTick-=100.; C.TickBurns(); }
};
namespace RPGBurnTests
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
			FRPGBurnTestAccess::Quiet(*C->GetCombatControlComponent()); return C;
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
 FRPGBurnSettings Settings(int32 Damage=7,float Duration=5,float Interval=.5f) { FRPGBurnSettings S;S.TickDamage=Damage;S.Duration=Duration;S.Interval=Interval;return S; }
 FActiveGameplayEffectHandle Apply(ARPGBaseCharacter* S,ARPGBaseCharacter* T,FRPGBurnSettings V=Settings(),FGuid Id=FGuid::NewGuid())
 { return T->GetCombatControlComponent()->ApplyApprovedDamageBurn(S,FHitResult(T,nullptr,T->GetActorLocation(),FVector::ForwardVector),Id,V); }
 float Health(ARPGBaseCharacter* T) { return T->GetAbilitySystemComponent()->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnDataTest,"ProjectRPG.Combat.Burn.DataAndFrozenSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnDataTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FGameplayAbilitySpec Spec;
	FRPGPlayerAbilitySet Entry; Entry.InputTag=RPGGameplayTags::InputTag_Roll; Entry.AbilityToGrant=URPGStunTestCombatAbility::StaticClass(); Entry.Burn=Settings();
	TestTrue(TEXT("Player data"),Entry.BuildAbilitySpec(1,GetTransientPackage(),Spec)); Entry.Burn.TickDamage=0;
	TestEqual(TEXT("Frozen damage"),RPGBurn::GetGrantedSettings(&Spec).TickDamage,7);
	Spec.SetByCallerTagMagnitudes.Remove(FGameplayTag::RequestGameplayTag(TEXT("Ability.Data.Status.BurnInterval")));
	TestFalse(TEXT("Incomplete metadata disabled"),RPGBurn::GetGrantedSettings(&Spec).IsEnabled());
	auto* NPC=NewObject<UDataAsset_NPCStartUpData>(); NPC->AttackBurns.Add(URPGNPCGameplayAbility::StaticClass(),Settings(9));
	TestTrue(TEXT("NPC data"),NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(),1,NPC,Spec));
	TestEqual(TEXT("Shared metadata"),RPGBurn::GetGrantedSettings(&Spec).TickDamage,9);
	auto* D=NewObject<URPGSkillDefinition>(); D->Burn=Settings(11); FRPGSkillRuntimeSpec R; D->BuildRuntimeSpec(nullptr,FRPGSkillSaveData(),R); D->Burn.TickDamage=0;
	TestEqual(TEXT("Frozen skill"),R.Burn.TickDamage,11); R.Reset(); TestFalse(TEXT("Reset"),R.Burn.IsEnabled());
	TestFalse(TEXT("Bounded damage"),Settings(10001).IsValid()); TestFalse(TEXT("Negative"),Settings(-1).IsValid());
	TestFalse(TEXT("Short interval"),Settings(1,5,.1f).IsValid()); TestFalse(TEXT("No scheduled slot"),Settings(1,1,1).IsValid());
	TestFalse(TEXT("Finite duration"),Settings(1,std::numeric_limits<float>::quiet_NaN()).IsValid());
	FRPGCombatImmunitySettings I; I.Flags=128; TestTrue(TEXT("Burn bit"),I.IsValid()&&I.Protects(ERPGCombatEffectKind::Burn));
	I.Flags=4; TestTrue(TEXT("Status group"),I.Protects(ERPGCombatEffectKind::Burn)); I.Flags=3; TestFalse(TEXT("Control protection separate"),I.Protects(ERPGCombatEffectKind::Burn)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnAuthorityTest,"ProjectRPG.Combat.Burn.TickAuthorityAndNoRecursiveEffects",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnAuthorityTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false;
	auto* S=W.Spawn(FVector(-1000,0,300)); auto* T=W.Spawn(); auto* Other=W.Spawn(FVector(1000,0,300));
	auto* SA=S->GetAbilitySystemComponent(); auto* TA=T->GetAbilitySystemComponent();
	const auto Ability=SA->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(),1)); TestTrue(TEXT("Activate source"),SA->TryActivateAbility(Ability));
	const auto Handle=Apply(S,T); TestTrue(TEXT("Accepted"),Handle.IsValid()); TestEqual(TEXT("No immediate tick"),Health(T),100.f);
	SA->CancelAbilityHandle(Ability); S->SetActorLocation(FVector(-5000,0,300));
	S->GetCombatControlComponent()->ApplyApprovedDamageStun(Other,FHitResult(S,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),3);
	TestTrue(TEXT("Source action blocked"),RPGCombatControl::IsActionBlocked(S));
	int32 Ticks=0; bool bRightSource=true;
	const auto Callback=TA->OnGameplayEffectAppliedDelegateToSelf.AddLambda([&](UAbilitySystemComponent* Source,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{
		if(Spec.Def!=GetDefault<URPGBurnTickEffect>())return;
		++Ticks; bRightSource &= Source==SA&&Spec.GetContext().GetOriginalInstigator()==S;
		if(Ticks==1) { Source->ApplyGameplayEffectSpecToTarget(Spec,TA); Source->ApplyGameplayEffectSpecToTarget(Spec,Other->GetAbilitySystemComponent()); }
	});
	W.Tick(.65f); TA->OnGameplayEffectAppliedDelegateToSelf.Remove(Callback);
	TestEqual(TEXT("Out of range, ended ability and stunned source still tick once"),Health(T),93.f);
	TestEqual(TEXT("Another target cannot borrow permit"),Health(Other),100.f); TestTrue(TEXT("Original source retained"),bRightSource);
	TestEqual(TEXT("No identity gain"),SA->GetNumericAttribute(URPGAttributeSet::GetCurrentIdentityGaugeAttribute()),0.f);
	TestFalse(TEXT("Burn does not block target actions"),RPGCombatControl::IsActionBlocked(T));
	FGameplayEffectSpec Forged(GetDefault<URPGBurnTickEffect>(),SA->MakeEffectContext(),1);
	Forged.SetSetByCallerMagnitude(RPGGameplayTags::Shared_SetByCaller_BaseDamage,10000);
	SA->ApplyGameplayEffectSpecToTarget(Forged,TA); TestEqual(TEXT("Unscheduled direct ASC damage denied"),Health(T),93.f);
	TA->AddLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible); W.Tick(.5f); TestEqual(TEXT("Invulnerability consumes slot without damage"),Health(T),93.f);
	TA->RemoveLooseGameplayTag(RPGGameplayTags::Shared_Status_Invincible); W.Tick(.5f); TestEqual(TEXT("No catch-up after invulnerability"),Health(T),86.f); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnStackTest,"ProjectRPG.Combat.Burn.StackCapCleanseAndForeignOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnStackTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-1000,0,300)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* ASC=T->GetAbilitySystemComponent();
	const auto First=Apply(S,T),Second=Apply(S,T,Settings(3)); W.Tick(.65f);
	TestEqual(TEXT("Independent instances"),Health(T),90.f); TestTrue(TEXT("Exact handle removal"),C->RemoveOwnedBurn(First));
	W.Tick(.5f); TestEqual(TEXT("Other instance survives"),Health(T),87.f);
	for(int32 I=0;I<7;++I) TestTrue(TEXT("Bounded stack accepted"),Apply(S,T).IsValid());
	const auto Rejected=FGuid::NewGuid(); TestFalse(TEXT("Ninth status rejected"),Apply(S,T,Settings(),Rejected).IsValid());
	FGameplayEffectSpec ForeignSpec(GetDefault<URPGBurnEffect>(),ASC->MakeEffectContext(),1); ForeignSpec.SetDuration(10,true);
	const auto Foreign=ASC->ApplyGameplayEffectSpecToSelf(ForeignSpec);
	bool bReentrantAccepted=false;
	const auto OnRemove=ASC->OnAnyGameplayEffectRemovedDelegate().AddLambda([&](const FActiveGameplayEffect& E) { if(E.Spec.GetContext().GetSourceObject()==C) bReentrantAccepted|=Apply(S,T).IsValid(); });
	C->CleanseBurns(); ASC->OnAnyGameplayEffectRemovedDelegate().Remove(OnRemove);
	TestFalse(TEXT("Cleanse reentry rejected"),bReentrantAccepted); TestNotNull(TEXT("Foreign status not broadly removed"),ASC->GetActiveGameplayEffect(Foreign));
	TestFalse(TEXT("Previously rejected receipt remains consumed"),Apply(S,T,Settings(),Rejected).IsValid());
	const float Before=Health(T); W.Tick(.6f); TestEqual(TEXT("Foreign GE cannot schedule ticks"),Health(T),Before); TestEqual(TEXT("Projection clear"),C->GetBurnPresentationState().Stacks,0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnLifetimeTest,"ProjectRPG.Combat.Burn.ImmunityDeathReuseAndAvatarChange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnLifetimeTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-1000,0,300)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* TA=T->GetAbilitySystemComponent(); auto* SA=S->GetAbilitySystemComponent();
	const FGuid Id=FGuid::NewGuid(); TA->AddLooseGameplayTag(RPGBurn::ImmunityTag()); TestFalse(TEXT("Leaf immunity"),Apply(S,T,Settings(),Id).IsValid()); TA->RemoveLooseGameplayTag(RPGBurn::ImmunityTag());
	TestFalse(TEXT("Immune hit cannot retry"),Apply(S,T,Settings(),Id).IsValid());
	FRPGCombatImmunitySettings I; I.Flags=4; const auto Tags=I.GetGrantedTags(); TA->AddLooseGameplayTags(Tags); TestFalse(TEXT("Group immunity"),Apply(S,T).IsValid()); TA->RemoveLooseGameplayTags(Tags);
	const auto H=Apply(S,T); TA->AddLooseGameplayTag(RPGBurn::ImmunityTag()); W.Tick(.65f); TestEqual(TEXT("Later immunity does not cleanse"),Health(T),93.f); TA->RemoveLooseGameplayTag(RPGBurn::ImmunityTag());
	SA->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0); W.Tick(.65f); TestEqual(TEXT("Dead source no damage"),Health(T),93.f); TestNull(TEXT("Source death ends status"),TA->GetActiveGameplayEffect(H));
	SA->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100); Apply(S,T); T->AdvanceLife(); C->ResetForAvatarReuse(); W.Tick(.65f); TestEqual(TEXT("Reused target protected"),Health(T),93.f);
	Apply(S,T); auto* Replacement=W.Spawn(FVector(2000,0,300)); SA->InitAbilityActorInfo(S,Replacement); W.Tick(.65f); TestEqual(TEXT("ActorInfo swap stops old ticks"),Health(T),93.f); SA->InitAbilityActorInfo(S,S);
	auto* Controller=W.World->SpawnActor<AAIController>(); Controller->Possess(S); const auto LostController=Apply(S,T);
	Controller->UnPossess(); Controller->Destroy(); W.Tick(.65f); TestEqual(TEXT("Controller loss stops ticks"),Health(T),93.f);
	TestNull(TEXT("Expired controller does not become an unowned source"),TA->GetActiveGameplayEffect(LostController)); SA->InitAbilityActorInfo(S,S);
	Apply(S,T); S->Destroy(); W.Tick(.65f); TestEqual(TEXT("Removed source stops ticks"),Health(T),93.f);
	auto* S2=W.Spawn(FVector(-1000,0,300)); Apply(S2,T); TA->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),1); W.Tick(.65f);
	TestEqual(TEXT("Lethal tick uses AttributeSet death"),Health(T),0.f); TestTrue(TEXT("Death tag"),TA->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)); TestEqual(TEXT("Death clears projection"),C->GetBurnPresentationState().Stacks,0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnScheduleTest,"ProjectRPG.Combat.Burn.SchedulingInhibitionAndNaturalExpiry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnScheduleTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-1000,0,300)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* ASC=T->GetAbilitySystemComponent();
	const auto H=Apply(S,T,Settings(7,2.8f)); W.Tick(.65f); TestEqual(TEXT("First scheduled tick"),Health(T),93.f);
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(H),true,false); W.Tick(1); TestEqual(TEXT("Inhibited period no damage"),Health(T),93.f); TestEqual(TEXT("Inhibition hides status"),C->GetBurnPresentationState().Stacks,0);
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(H),false,false); FRPGBurnTestAccess::Hitch(*C); TestEqual(TEXT("Many missed slots produce only one tick"),Health(T),86.f);
	C->CleanseBurns(); const auto Short=Apply(S,T,Settings(2,1.8f)); const float Before=Health(T); W.Tick(2);
	TestEqual(TEXT("Only live scheduled slots"),Health(T),Before-6); TestNull(TEXT("GE expires naturally"),ASC->GetActiveGameplayEffect(Short)); TestEqual(TEXT("Expired status projection"),C->GetBurnPresentationState().Stacks,0); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnReentryTest,"ProjectRPG.Combat.Burn.ReentrantApplyAndTickCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnReentryTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(FVector(-1000,0,300)); auto* T=W.Spawn(); auto* C=T->GetCombatControlComponent(); auto* ASC=T->GetAbilitySystemComponent();
	const auto Pending=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle) { if(Spec.Def==GetDefault<URPGBurnEffect>()) C->CleanseBurns(); });
	TestFalse(TEXT("Cleanse during application"),Apply(S,T).IsValid()); ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Pending); W.Tick(.65f); TestEqual(TEXT("No pending orphan tick"),Health(T),100.f);
	Apply(S,T); Apply(S,T,Settings(3));
	const auto Tick=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle) { if(Spec.Def==GetDefault<URPGBurnTickEffect>()) C->ResetForAvatarReuse(); });
	W.Tick(.65f); ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Tick);
	TestTrue(TEXT("Reset in first tick stops other due ticks"),Health(T)==93.f||Health(T)==97.f); const float Before=Health(T); W.Tick(.6f); TestEqual(TEXT("Stale scheduler retired"),Health(T),Before); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGBurnEncounterTest,"ProjectRPG.Combat.Burn.BossEncounterAndThreatAttribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGBurnEncounterTest::RunTest(const FString&)
{
	using namespace RPGBurnTests; FScope W; if(!W.Init())return false; W.World->CreateAISystem();
	auto* Boss=W.World->SpawnActor<ARPGBossTestPawn>(); auto* AI=W.World->SpawnActor<ARPGAIController>(); AI->Possess(Boss);
	AI->GetPerceptionComponent()->SetSenseEnabled(UAISense_Sight::StaticClass(),false);
	auto* Control=NewObject<URPGCombatControlComponent>(Boss); Control->RegisterComponent(); FRPGBurnTestAccess::Quiet(*Control);
	auto* Threat=NewObject<URPGBossThreatComponent>(Boss); Threat->RegisterComponent();
	auto* S=W.Spawn(FVector(-1000,0,300)); auto* Controller=W.World->SpawnActor<AAIController>(); Controller->SetGenericTeamId(FGenericTeamId(0)); Controller->Possess(S);
	const auto ApplyBoss=[&]() { return Control->ApplyApprovedDamageBurn(S,FHitResult(Boss,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),Settings()); };
	TestFalse(TEXT("Idle boss rejects status"),ApplyBoss().IsValid()); TestTrue(TEXT("Start encounter"),Boss->Boss->StartEncounter());
	const auto H=ApplyBoss(); TestTrue(TEXT("Encounter hit"),H.IsValid()); W.Tick(.65f);
	TestEqual(TEXT("Actual health loss"),Boss->Attributes->GetCurrentHealth(),93.f); TestEqual(TEXT("Threat belongs to original source"),Threat->GetThreatForTarget(S),7.);
	TestTrue(TEXT("Begin reset"),Boss->Boss->BeginReset()); W.Tick(.65f);
	TestEqual(TEXT("Resetting boss cannot keep taking ticks"),Boss->Attributes->GetCurrentHealth(),93.f); TestNull(TEXT("Reset ends owned status"),Boss->ASC->GetActiveGameplayEffect(H));
	Boss->SetTestHealth(100); TestTrue(TEXT("Complete reset"),Boss->Boss->CompleteReset()); TestTrue(TEXT("Restart"),Boss->Boss->StartEncounter()); W.Tick(.65f);
	TestEqual(TEXT("New encounter cannot inherit old ticks"),Boss->Attributes->GetCurrentHealth(),100.f);
	const auto SourceH=S->GetCombatControlComponent()->ApplyApprovedDamageBurn(Boss,FHitResult(S,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),Settings());
	TestTrue(TEXT("Boss source accepted"),SourceH.IsValid()); Boss->Boss->MarkWiped(); W.Tick(.65f); TestEqual(TEXT("Wiped boss source stops ticks"),Health(S),100.f);
	TestNull(TEXT("Old boss attack status removed"),S->GetAbilitySystemComponent()->GetActiveGameplayEffect(SourceH)); return true;
}
#endif
