#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Ability/RPGNPCGameplayAbility.h"
#include "DataAsset/StartUpData/DataAsset_NPCStartUpData.h"
#include "Skill/RPGSkillDefinition.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/WorldSettings.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include <limits>

struct FRPGImmunityTestAccess
{
	static void Quiet(URPGCombatControlComponent& C) { C.StunVisual.Reset(); C.bShowSlowStatus = false; }
};
namespace RPGImmunityTests
{
	FRPGCombatImmunitySettings Settings(int32 Flags) { FRPGCombatImmunitySettings S; S.Flags = Flags; return S; }
	struct FScope
	{
		UWorld* World = nullptr;
		~FScope() { if (World) { RPGTestWorld::Destroy(World); } }
		bool Init()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay(); return true;
		}
		ARPGStunTestCharacter* Spawn()
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C = World->SpawnActor<ARPGStunTestCharacter>(P); auto* ASC = C->GetRPGAbilitySystemComponent();
			ASC->InitAbilityActorInfo(C,C); ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),100);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100); FRPGImmunityTestAccess::Quiet(*C->GetCombatControlComponent()); return C;
		}
	};
	FGameplayAbilitySpecHandle Grant(ARPGBaseCharacter* C, int32 Flags, TSubclassOf<UGameplayAbility> Class = URPGImmunityTestCombatAbility::StaticClass())
	{
		FGameplayAbilitySpec S(Class,1); RPGCombatImmunity::SetGrantedSettings(S, Settings(Flags)); return C->GetAbilitySystemComponent()->GiveAbility(S);
	}
	FActiveGameplayEffectHandle Foreign(UAbilitySystemComponent* ASC, int32 Flags)
	{
		FGameplayEffectSpec S(GetDefault<URPGCombatImmunityEffect>(),ASC->MakeEffectContext(),1);
		S.DynamicGrantedTags = Settings(Flags).GetGrantedTags(); return ASC->ApplyGameplayEffectSpecToSelf(S);
	}
	FActiveGameplayEffectHandle Stun(ARPGBaseCharacter* S, ARPGBaseCharacter* T)
	{
		return T->GetCombatControlComponent()->ApplyApprovedDamageStun(S,FHitResult(T,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),3);
	}
	FActiveGameplayEffectHandle Slow(ARPGBaseCharacter* S, ARPGBaseCharacter* T)
	{
		FRPGSlowSettings V; V.Percent=40; return T->GetCombatControlComponent()->ApplyApprovedDamageSlow(S,FHitResult(T,nullptr,FVector::ZeroVector,FVector::ForwardVector),FGuid::NewGuid(),V);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunityDataTest,"ProjectRPG.Combat.Immunity.GroupsAndFrozenData",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunityDataTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests;
	TestTrue(TEXT("Displacement covers knockback"),Settings(2).Protects(ERPGCombatEffectKind::Knockback));
	TestTrue(TEXT("Displacement covers airborne"),Settings(2).Protects(ERPGCombatEffectKind::Airborne));
	TestFalse(TEXT("Displacement is not stun protection"),Settings(2).Protects(ERPGCombatEffectKind::Stun));
	TestFalse(TEXT("Stun does not imply flinch protection"),Settings(8).Protects(ERPGCombatEffectKind::HitReaction));
	TestTrue(TEXT("Status category covers slow"),Settings(4).Protects(ERPGCombatEffectKind::Slow));
	TestFalse(TEXT("Unknown bits rejected"),Settings(256).IsValid());
	FRPGPlayerAbilitySet P; P.InputTag=RPGGameplayTags::InputTag_Roll; P.AbilityToGrant=URPGImmunityTestCombatAbility::StaticClass(); P.ActivationImmunities=Settings(3);
	FGameplayAbilitySpec Spec; TestTrue(TEXT("Player data grant"),P.BuildAbilitySpec(1,GetTransientPackage(),Spec)); P.ActivationImmunities.Flags=0;
	TestEqual(TEXT("Grant freezes flags"),RPGCombatImmunity::GetGrantedSettings(&Spec).Flags,3);
	auto* NPC=NewObject<UDataAsset_NPCStartUpData>(); NPC->AbilityImmunities.Add(URPGNPCGameplayAbility::StaticClass(),Settings(4));
	TestTrue(TEXT("NPC data grant"),NPC->BuildAttackAbilitySpec(URPGNPCGameplayAbility::StaticClass(),1,NPC,Spec));
	TestEqual(TEXT("NPC uses shared settings"),RPGCombatImmunity::GetGrantedSettings(&Spec).Flags,4);
	auto* D=NewObject<URPGSkillDefinition>(); D->ActivationImmunities=Settings(7); FRPGSkillRuntimeSpec Runtime; D->BuildRuntimeSpec(nullptr,FRPGSkillSaveData(),Runtime); D->ActivationImmunities.Flags=0;
	TestEqual(TEXT("Skill snapshot"),Runtime.ActivationImmunities.Flags,7); Runtime.Reset(); TestEqual(TEXT("No next-activation inheritance"),Runtime.ActivationImmunities.Flags,0);
	Spec.SetByCallerTagMagnitudes.Add(FGameplayTag::RequestGameplayTag(TEXT("Ability.Data.Immunity.Flags")),std::numeric_limits<float>::quiet_NaN());
	TestFalse(TEXT("Corrupt metadata rejected"),RPGCombatImmunity::GetGrantedSettings(&Spec).IsValid()); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunitySelectiveTest,"ProjectRPG.Combat.Immunity.SelectiveControlAndNoCleanse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunitySelectiveTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests; FScope W; if(!W.Init())return false; auto* S=W.Spawn(); auto* T=W.Spawn(); auto* ASC=T->GetAbilitySystemComponent(); auto* C=T->GetCombatControlComponent();
	const auto H=Grant(T,3); TestTrue(TEXT("Activate partial protection"),ASC->TryActivateAbility(H));
	FRPGKnockbackSettings K; K.Distance=200; K.Duration=.4f;
	const FHitResult Hit(T,nullptr,FVector::ZeroVector,FVector::ForwardVector);
	TestFalse(TEXT("Horizontal knockback blocked"),C->ApplyApprovedDamageKnockback(S,Hit,FGuid::NewGuid(),K,FVector::ForwardVector));
	K.Height=200; TestFalse(TEXT("Airborne blocked"),C->ApplyApprovedDamageKnockback(S,Hit,FGuid::NewGuid(),K,FVector::ForwardVector));
	TestTrue(TEXT("Independent stun still lands"),Stun(S,T).IsValid());
	TestFalse(TEXT("Stun cancels protected combat ability"),ASC->FindAbilitySpecFromHandle(H)->IsActive());
	TestFalse(TEXT("Cancelled ability drops its protection"),C->IsImmuneTo(ERPGCombatEffectKind::HitReaction));
	const auto Shield=Foreign(ASC,4); TestTrue(TEXT("New stun rejected"),!Stun(S,T).IsValid()); TestFalse(TEXT("New slow rejected"),Slow(S,T).IsValid());
	TestTrue(TEXT("Granting immunity does not cleanse existing stun"),RPGCombatControl::IsStunned(T)); ASC->RemoveActiveGameplayEffect(Shield); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunityOverlapTest,"ProjectRPG.Combat.Immunity.OverlapAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunityOverlapTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests; FScope W;if(!W.Init())return false;auto* T=W.Spawn();auto* S=W.Spawn();auto* ASC=T->GetAbilitySystemComponent();
	const auto A=Grant(T,7),B=Grant(T,7); TestTrue(TEXT("Two separate activations"),ASC->TryActivateAbility(A)&&ASC->TryActivateAbility(B));
	ASC->CancelAbilityHandle(A); TestTrue(TEXT("One removal preserves overlap"),RPGCombatImmunity::IsImmune(T,ERPGCombatEffectKind::Slow));
	TestFalse(TEXT("Stun rejected without cancelling protected ability"),Stun(S,T).IsValid()); TestTrue(TEXT("Second remains active"),ASC->FindAbilitySpecFromHandle(B)->IsActive());
	const auto External=Foreign(ASC,1); CastChecked<URPGImmunityTestCombatAbility>(ASC->FindAbilitySpecFromHandle(B)->GetPrimaryInstance())->Finish();
	TestFalse(TEXT("Normal completion removes status immunity"),RPGCombatImmunity::IsImmune(T,ERPGCombatEffectKind::Stun));
	TestTrue(TEXT("External flinch immunity preserved"),RPGCombatImmunity::IsImmune(T,ERPGCombatEffectKind::HitReaction));
	TestNotNull(TEXT("Exact foreign handle preserved"),ASC->GetActiveGameplayEffect(External));
	TestTrue(TEXT("Repeat activation gets a fresh scope"),ASC->TryActivateAbility(A)); ASC->ClearAbility(A);
	TestFalse(TEXT("Revoking grant removes immunity"),RPGCombatImmunity::IsImmune(T,ERPGCombatEffectKind::Slow)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunityLegacyTest,"ProjectRPG.Combat.Immunity.LegacyRoutingAndActivationGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunityLegacyTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests;FScope W;if(!W.Init())return false;auto* S=W.Spawn();auto* T=W.Spawn();auto* ASC=T->GetAbilitySystemComponent();
	const auto Attack=Grant(S,0);S->GetAbilitySystemComponent()->TryActivateAbility(Attack);auto* A=CastChecked<URPGImmunityTestCombatAbility>(S->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(Attack)->GetPrimaryInstance());
	const auto Reaction=Grant(T,0,URPGImmunityTestReactionAbility::StaticClass());
	TestFalse(TEXT("Unprotected legacy route retained"),A->RouteLegacy(T)); const auto Shield=Foreign(ASC,1);
	TestTrue(TEXT("Protected damage consumes the legacy event"),A->RouteLegacy(T)); TestFalse(TEXT("Direct reaction activation rejected"),ASC->TryActivateAbility(Reaction));
	FGameplayEventData Event;Event.Instigator=S;Event.Target=T;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(T,RPGGameplayTags::Shared_Event_HitReact,Event);
	TestFalse(TEXT("Legacy shared event cannot bypass immunity"),ASC->FindAbilitySpecFromHandle(Reaction)->IsActive());
	ASC->RemoveActiveGameplayEffect(Shield); TestTrue(TEXT("Reaction available after protection ends"),ASC->TryActivateAbility(Reaction)); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunityLifeTest,"ProjectRPG.Combat.Immunity.DeathReuseAndAvatarChange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunityLifeTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests;FScope W;if(!W.Init())return false;auto* T=W.Spawn();auto* Replacement=W.Spawn();auto* ASC=T->GetRPGAbilitySystemComponent();auto* C=T->GetCombatControlComponent();
	const auto External=Foreign(ASC,1);const auto A=Grant(T,6);TestTrue(TEXT("First life activates"),ASC->TryActivateAbility(A));
	T->AdvanceLife();C->ResetForAvatarReuse();TestFalse(TEXT("Reuse removes protection even while old ability remains"),C->IsImmuneTo(ERPGCombatEffectKind::Slow));
	TestNotNull(TEXT("Reuse preserves foreign effect"),ASC->GetActiveGameplayEffect(External));ASC->CancelAbilityHandle(A);TestTrue(TEXT("New life activates"),ASC->TryActivateAbility(A));
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);TestFalse(TEXT("Death removes protection"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));
	ASC->CancelAbilityHandle(A);ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);TestTrue(TEXT("Live again"),ASC->TryActivateAbility(A));
	ASC->InitAbilityActorInfo(T,Replacement);W.World->Tick(LEVELTICK_All,.1f);
	TestFalse(TEXT("ActorInfo replacement removes old avatar scope"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGImmunityReentryTest,"ProjectRPG.Combat.Immunity.ReentrantApplicationAndForeignOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGImmunityReentryTest::RunTest(const FString&)
{
	using namespace RPGImmunityTests;FScope W;if(!W.Init())return false;auto* T=W.Spawn();auto* ASC=T->GetAbilitySystemComponent();const auto A=Grant(T,7);const auto External=Foreign(ASC,1);
	const auto Callback=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([ASC,A](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{if(Spec.Def->GetClass()==URPGCombatImmunityEffect::StaticClass())ASC->CancelAbilityHandle(A);});
	ASC->TryActivateAbility(A);ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Callback);
	TestFalse(TEXT("Cancellation during GE application ends ability"),ASC->FindAbilitySpecFromHandle(A)->IsActive());TestFalse(TEXT("No pending immunity leaked"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));
	const auto Reset=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([T](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{if(Spec.Def->GetClass()==URPGCombatImmunityEffect::StaticClass())T->GetCombatControlComponent()->ResetForAvatarReuse();});
	ASC->TryActivateAbility(A);ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Reset);
	TestFalse(TEXT("Reset during GE application cannot leave a scope"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));
	TestNotNull(TEXT("Both callbacks preserve unrelated same-class GE"),ASC->GetActiveGameplayEffect(External));
	bool bReentered=false,bNewStarted=false;
	const auto Restart=ASC->OnActiveGameplayEffectAddedDelegateToSelf.AddLambda([ASC,A,&bReentered,&bNewStarted](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{
		if(Spec.Def->GetClass()!=URPGCombatImmunityEffect::StaticClass()||bReentered)return;
		bReentered=true;ASC->CancelAbilityHandle(A);bNewStarted=ASC->TryActivateAbility(A);
	});
	ASC->TryActivateAbility(A);ASC->OnActiveGameplayEffectAddedDelegateToSelf.Remove(Restart);
	TestTrue(TEXT("Apply callback can start a fresh execution on the same instance"),bNewStarted);
	TestTrue(TEXT("Old failed application cannot end the new execution"),ASC->FindAbilitySpecFromHandle(A)->IsActive());
	TestTrue(TEXT("Fresh execution retains its immunity"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));
	int32 Owned=0;for(auto H:ASC->GetActiveEffects(FGameplayEffectQuery()))
		if(ASC->GetActiveGameplayEffect(H)->Spec.GetContext().GetSourceObject()==T->GetCombatControlComponent())++Owned;
	TestEqual(TEXT("Only the new execution's owned effect remains"),Owned,1);
	ASC->CancelAbilityHandle(A);TestFalse(TEXT("Fresh execution still cleans up normally"),RPGCombatImmunity::HasTags(ASC,ERPGCombatEffectKind::Slow));return true;
}
#endif
