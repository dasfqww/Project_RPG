#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "AbilitySystemComponent.h"
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
#include "GameplayEffect/RPGStaggerEffect.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>
struct FRPGStaggerTestAccess
{
 static void Quiet(URPGCombatControlComponent& C){C.StunVisual.Reset();C.bShowSlowStatus=false;C.bShowBurnStatus=false;}
 static int32 Count(const URPGCombatControlComponent& C){return C.OwnedStaggers.Num();}
 static void Clear(URPGCombatControlComponent& C){C.ClearOwnedStaggers();C.RefreshControl();}
};
namespace RPGStaggerTests
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
			FRPGStaggerTestAccess::Quiet(*C->GetCombatControlComponent()); return C;
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
 FRPGStaggerSettings Settings(float Seconds=.3f){FRPGStaggerSettings S;S.Duration=Seconds;return S;}
 FActiveGameplayEffectHandle Apply(ARPGBaseCharacter* S,ARPGBaseCharacter* T,float D=.3f,FGuid Id=FGuid::NewGuid())
 {return T->GetCombatControlComponent()->ApplyApprovedDamageStagger(S,FHitResult(T,nullptr,T->GetActorLocation(),FVector::ForwardVector),Id,Settings(D),FVector::ForwardVector);}
 FActiveGameplayEffectHandle Foreign(ARPGBaseCharacter* T,int32 Flags)
 {auto* ASC=T->GetAbilitySystemComponent();FRPGCombatImmunitySettings V;V.Flags=Flags;FGameplayEffectSpec Spec(GetDefault<URPGCombatImmunityEffect>(),ASC->MakeEffectContext(),1);Spec.DynamicGrantedTags=V.GetGrantedTags();return ASC->ApplyGameplayEffectSpecToSelf(Spec);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerDataTest,"ProjectRPG.Combat.Stagger.DataAndFrozenSettings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerDataTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FRPGPlayerAbilitySet P;P.InputTag=RPGGameplayTags::InputTag_Roll;P.AbilityToGrant=URPGImmunityTestCombatAbility::StaticClass();P.Stagger=Settings(.4f);FGameplayAbilitySpec Spec;
	TestTrue(TEXT("Shared player data"),P.BuildAbilitySpec(1,GetTransientPackage(),Spec));P.Stagger.Duration=0;TestEqual(TEXT("Frozen grant"),RPGStagger::GetGrantedSettings(&Spec).Duration,.4f);
	auto* NPC=NewObject<UDataAsset_NPCStartUpData>();NPC->AttackStaggers.Add(URPGNPCGameplayAbility::StaticClass(),Settings(.6f));
	TestTrue(TEXT("Shared NPC family data"),NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(),1,NPC,Spec));TestEqual(TEXT("Same metadata"),RPGStagger::GetGrantedSettings(&Spec).Duration,.6f);
	auto* D=NewObject<URPGSkillDefinition>();D->Stagger=Settings(.8f);FRPGSkillRuntimeSpec R;D->BuildRuntimeSpec(nullptr,FRPGSkillSaveData(),R);D->Stagger.Duration=0;
	TestEqual(TEXT("Skill snapshot"),R.Stagger.Duration,.8f);R.Reset();TestFalse(TEXT("Later activation cannot inherit"),R.Stagger.IsEnabled());
	TestFalse(TEXT("Bounded"),Settings(2.1f).IsValid());TestFalse(TEXT("Minimum"),Settings(.01f).IsValid());TestFalse(TEXT("NaN"),Settings(std::numeric_limits<float>::quiet_NaN()).IsValid());
	Spec.SetByCallerTagMagnitudes.Add(FGameplayTag::RequestGameplayTag(TEXT("Ability.Data.Control.StaggerSeconds")),std::numeric_limits<float>::quiet_NaN());TestFalse(TEXT("Malformed metadata disabled"),RPGStagger::GetGrantedSettings(&Spec).IsEnabled());
	FRPGCombatImmunitySettings I;I.Flags=1;TestTrue(TEXT("Flinch group includes real stagger"),I.Protects(ERPGCombatEffectKind::Stagger));I.Flags=4;TestFalse(TEXT("Ailment protection remains independent"),I.Protects(ERPGCombatEffectKind::Stagger));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerActionTest,"ProjectRPG.Combat.Stagger.MovementGravityAndAbilityScope",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerActionTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();auto* ASC=T->GetRPGAbilitySystemComponent();auto* M=T->GetCharacterMovement();
	const auto Combat=ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(),1));ASC->TryActivateAbility(Combat);
	const auto Passive=ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestPassiveAbility::StaticClass(),1));const auto Death=ASC->GiveAbility(FGameplayAbilitySpec(URPGStunTestDeathAbility::StaticClass(),1));ASC->TryActivateAbility(Death);
	const float BeforeZ=T->GetActorLocation().Z;TestTrue(TEXT("Approved hit grants bounded control"),Apply(S,T,.6f).IsValid());
	TestFalse(TEXT("Combat cancelled"),ASC->FindAbilitySpecFromHandle(Combat)->IsActive());TestTrue(TEXT("Passive preserved"),ASC->FindAbilitySpecFromHandle(Passive)->IsActive());TestTrue(TEXT("Death ability preserved"),ASC->FindAbilitySpecFromHandle(Death)->IsActive());
	TestFalse(TEXT("New combat blocked"),ASC->TryActivateAbility(Combat));TestFalse(TEXT("Stagger is not stun"),RPGCombatControl::IsStunned(T));TestTrue(TEXT("Shared action gate"),RPGCombatControl::IsActionBlocked(T));
	TestEqual(TEXT("Voluntary speed zero"),M->GetMaxSpeed(),0.f);W.Tick(.2f,T);TestTrue(TEXT("Gravity still falls"),T->GetActorLocation().Z<BeforeZ-5);TestEqual(TEXT("No horizontal input escape"),T->GetActorLocation().X,0.);
	W.Tick(.7f);TestFalse(TEXT("GE naturally releases control"),RPGCombatControl::IsActionBlocked(T));TestEqual(TEXT("Base speed survives"),M->GetMaxSpeed(),400.f);TestTrue(TEXT("Combat available after release"),ASC->TryActivateAbility(Combat));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerOverlapTest,"ProjectRPG.Combat.Stagger.OverlapInhibitionAndExactRemoval",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerOverlapTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();
	const auto Long=Apply(S,T,1.5f);const double End=C->GetStaggerState().EndServerTime;const auto Short=Apply(S,T,.2f);
	TestTrue(TEXT("Independent duration handles"),Long.IsValid()&&Short.IsValid());TestTrue(TEXT("Short hit cannot shorten long one"),C->GetStaggerState().EndServerTime>=End-.01);TestEqual(TEXT("Frozen direction"),C->GetStaggerState().Direction,ERPGHitReactionDirection::Back);
	TestTrue(TEXT("Exact short removal"),C->RemoveOwnedStagger(Short));TestTrue(TEXT("Long remains"),RPGStagger::IsStaggered(T));
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(Long),true,false);TestFalse(TEXT("Inhibited GE does not block"),RPGCombatControl::IsActionBlocked(T));
	ASC->SetActiveGameplayEffectInhibit(FActiveGameplayEffectHandle(Long),false,false);TestTrue(TEXT("Uninhibited GE resumes remaining duration"),RPGStagger::IsStaggered(T));
	FGameplayEffectSpec Other(GetDefault<URPGStaggerEffect>(),ASC->MakeEffectContext(),1);Other.SetDuration(2,true);const auto ForeignHandle=ASC->ApplyGameplayEffectSpecToSelf(Other);
	C->ResetForAvatarReuse();TestNull(TEXT("Own handle removed"),ASC->GetActiveGameplayEffect(Long));TestNotNull(TEXT("Foreign same-class effect preserved"),ASC->GetActiveGameplayEffect(ForeignHandle));ASC->RemoveActiveGameplayEffect(ForeignHandle);
	TestFalse(TEXT("All remaining control released"),RPGCombatControl::IsActionBlocked(T));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerPriorityTest,"ProjectRPG.Combat.Stagger.ImmunityAndHigherControlPriority",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerPriorityTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();W.Tick(1);
	const auto Immune=Foreign(T,1);const auto Id=FGuid::NewGuid();TestFalse(TEXT("Hit-reaction armor rejects stagger"),Apply(S,T,.3f,Id).IsValid());ASC->RemoveActiveGameplayEffect(Immune);TestFalse(TEXT("Immune receipt consumed"),Apply(S,T,.3f,Id).IsValid());
	const auto Status=Foreign(T,4);TestTrue(TEXT("Ailment-only immunity permits stagger"),Apply(S,T).IsValid());FRPGStaggerTestAccess::Clear(*C);ASC->RemoveActiveGameplayEffect(Status);
	Apply(S,T,1.5f);const auto Stun=C->ApplyApprovedDamageStun(S,FHitResult(T,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),.4f);
	TestTrue(TEXT("Stun lands"),Stun.IsValid());TestFalse(TEXT("Higher stun retires owned stagger"),RPGStagger::IsStaggered(T));TestFalse(TEXT("No queued stagger during stun"),Apply(S,T).IsValid());C->RemoveOwnedStun(Stun);
	Apply(S,T,1.5f);FRPGKnockbackSettings K;K.Distance=200;K.Duration=.4f;
	TestTrue(TEXT("Knockback lands"),C->ApplyApprovedDamageKnockback(S,FHitResult(T,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),K,FVector::ForwardVector));TestFalse(TEXT("Displacement retires stagger"),RPGStagger::IsStaggered(T));W.Tick(.8f);
	TestFalse(TEXT("Lower constraint does not reappear after higher expiry"),RPGStagger::IsStaggered(T));
	Apply(S,T,1.5f);K.Height=180;K.DownDuration=.2f;K.GetUpDuration=.2f;
	TestTrue(TEXT("Airborne lands"),C->ApplyApprovedDamageKnockback(S,FHitResult(T,nullptr,T->GetActorLocation(),FVector::ForwardVector),FGuid::NewGuid(),K,FVector::ForwardVector));
	TestFalse(TEXT("Airborne retires stagger"),RPGStagger::IsStaggered(T));TestFalse(TEXT("No stagger during airborne sequence"),Apply(S,T).IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerLifetimeTest,"ProjectRPG.Combat.Stagger.ReentrantApplicationDeathAndReuse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerLifetimeTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();auto* C=T->GetCombatControlComponent();auto* ASC=T->GetRPGAbilitySystemComponent();
	const auto Reset=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([C](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle){if(Spec.Def==GetDefault<URPGStaggerEffect>())C->ResetForAvatarReuse();});
	TestFalse(TEXT("Reset inside apply rejects old lifetime"),Apply(S,T).IsValid());ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Reset);TestFalse(TEXT("No orphan tag"),RPGStagger::IsStaggered(T));
	const auto Pending=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([ASC](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle H){if(Spec.Def==GetDefault<URPGStaggerEffect>())ASC->SetActiveGameplayEffectInhibit(MoveTemp(H),true,false);});
	TestFalse(TEXT("Inhibited application rejected"),Apply(S,T).IsValid());ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Pending);
	Apply(S,T,.8f);S->Destroy();W.Tick(.2f);TestTrue(TEXT("Committed hit outlives attacker"),RPGStagger::IsStaggered(T));W.Tick(.8f);TestFalse(TEXT("Normal GE expiry"),RPGStagger::IsStaggered(T));
	auto* NewSource=W.Spawn(FVector(-1000,0,300));Apply(NewSource,T,1);ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);TestFalse(TEXT("Target death clears owned reaction"),RPGStagger::IsStaggered(T));
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);T->AdvanceLife();C->ResetForAvatarReuse();TestFalse(TEXT("No old state in reused life"),C->GetStaggerState().bActive);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGStaggerRouteTest,"ProjectRPG.Combat.Stagger.RoutingReceiptsAndBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGStaggerRouteTest::RunTest(const FString&)
{
	using namespace RPGStaggerTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn(FVector(-1000,0,300));auto* T=W.Spawn();auto* ASC=S->GetRPGAbilitySystemComponent();
	const auto H=ASC->GiveAbility(FGameplayAbilitySpec(URPGImmunityTestCombatAbility::StaticClass(),1));ASC->TryActivateAbility(H);auto* A=CastChecked<URPGImmunityTestCombatAbility>(ASC->FindAbilitySpecFromHandle(H)->GetPrimaryInstance());
	TestTrue(TEXT("Explicit stagger consumes legacy route"),A->RouteLegacy(T,Settings()));TestTrue(TEXT("Router applies common control"),RPGStagger::IsStaggered(T));auto* C=T->GetCombatControlComponent();FRPGStaggerTestAccess::Clear(*C);
	const auto Id=FGuid::NewGuid();TestTrue(TEXT("Receipt accepted once"),Apply(S,T,2,Id).IsValid());TestFalse(TEXT("No duplicate GE"),Apply(S,T,2,Id).IsValid());
	for(int32 I=1;I<16;++I)TestTrue(TEXT("Bounded independent hit"),Apply(S,T,2).IsValid());TestEqual(TEXT("Cap"),FRPGStaggerTestAccess::Count(*C),16);TestFalse(TEXT("Budget cannot grow unbounded"),Apply(S,T,2).IsValid());
	FRPGStaggerTestAccess::Clear(*C);const auto Combat=T->GetAbilitySystemComponent()->GiveAbility(FGameplayAbilitySpec(URPGStunTestCombatAbility::StaticClass(),1));T->GetAbilitySystemComponent()->TryActivateAbility(Combat);
	CastChecked<URPGStunTestCombatAbility>(T->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(Combat)->GetPrimaryInstance())->SetInterruptible(false);
	TestFalse(TEXT("Existing uncancellable combat policy preserved"),Apply(S,T).IsValid());return true;
}
#endif
