#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGMontageImmunityTestSupport.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "AnimNotifyState/RPGAnimNotifyState_CombatImmunity.h"
#include "Attribute/RPGAttributeSet.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include <limits>

namespace RPGMontageImmunityTests
{
	struct FScope
	{
		UWorld* World = nullptr; uint64 Frame = GFrameCounter;
		~FScope() { if (World) { RPGTestWorld::Destroy(World); } }
		bool Init()
		{
			World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass()));
			if(!World)return false; GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay(); return true;
		}
		ARPGStunTestCharacter* Spawn()
		{
			FActorSpawnParameters P; P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* C=World->SpawnActor<ARPGStunTestCharacter>(P); auto* ASC=C->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(C,C);
			ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(),100); ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),100);
			C->GetCharacterMovement()->SetMovementMode(MOVE_None);
			auto* Class=LoadClass<ACharacter>(nullptr,TEXT("/Game/Blueprints/Character/Player/BP_Player.BP_Player_C"));
			C->GetMesh()->SetSkeletalMesh(Class->GetDefaultObject<ACharacter>()->GetMesh()->GetSkeletalMeshAsset());
			C->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
			C->GetMesh()->GetAnimInstance()->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
			C->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
			return C;
		}
		void Tick(float Seconds)
		{
			for(int32 I=0;I<FMath::CeilToInt(Seconds*60);++I)
			{ Frame=FMath::Max(Frame,GFrameCounter)+1; TGuardValue<uint64> Guard(GFrameCounter,Frame); World->Tick(LEVELTICK_All,1.f/60); }
		}
	};
	UAnimMontage* Montage()
	{
		auto* Original=LoadPlayerMontage(); if(!Original)return nullptr;
		auto* M=DuplicateObject<UAnimMontage>(Original,GetTransientPackage()); M->Notifies.Reset(); M->RateScale=1;
		M->bEnableAutoBlendOut=true;
		for(int32 I=0;I<M->CompositeSections.Num();++I)
			M->CompositeSections[I].NextSectionName=I+1<M->CompositeSections.Num()?M->CompositeSections[I+1].SectionName:NAME_None;
		AddWindow(*M,.1f,.5f,3); AddWindow(*M,.3f,.8f,64); Refresh(*M); return M;
	}
	FGameplayAbilitySpecHandle Grant(ARPGStunTestCharacter* C,int32 Flags=0)
	{
		FGameplayAbilitySpec S(URPGImmunityTestCombatAbility::StaticClass(),1); FRPGCombatImmunitySettings V;V.Flags=Flags;RPGCombatImmunity::SetGrantedSettings(S,V);
		const auto H=C->GetAbilitySystemComponent()->GiveAbility(S); C->GetAbilitySystemComponent()->TryActivateAbility(H); return H;
	}
	URPGImmunityTestCombatAbility* Ability(ARPGStunTestCharacter* C,FGameplayAbilitySpecHandle H) { return CastChecked<URPGImmunityTestCombatAbility>(C->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(H)->GetPrimaryInstance()); }
	float Play(ARPGStunTestCharacter* C,FGameplayAbilitySpecHandle H,UAnimMontage* M,float Fraction=0,float Rate=1)
	{ auto* A=Ability(C,H);return C->GetRPGAbilitySystemComponent()->PlayMontage(A,A->GetCurrentActivationInfo(),M,Rate,NAME_None,Fraction*M->GetPlayLength()); }
	void Seek(ARPGStunTestCharacter* C,UAnimMontage* M,float Fraction) { C->GetMesh()->GetAnimInstance()->Montage_SetPosition(M,Fraction*M->GetPlayLength()); }
	bool Immune(ARPGStunTestCharacter* C,ERPGCombatEffectKind K) { return C->GetCombatControlComponent()->IsImmuneTo(K); }
	FActiveGameplayEffectHandle Foreign(ARPGStunTestCharacter* C,int32 Flags)
	{
		FRPGCombatImmunitySettings I;I.Flags=Flags;auto* ASC=C->GetAbilitySystemComponent();FGameplayEffectSpec S(GetDefault<URPGCombatImmunityEffect>(),ASC->MakeEffectContext(),1);S.DynamicGrantedTags=I.GetGrantedTags();return ASC->ApplyGameplayEffectSpecToSelf(S);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMontageImmunityDataTest,"ProjectRPG.Combat.MontageImmunity.DataAndFrozenWindows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGMontageImmunityDataTest::RunTest(const FString&)
{
	using namespace RPGMontageImmunityTests;auto* M=Montage();if(!TestNotNull(TEXT("Real compatible montage"),M))return false;
	TArray<FRPGMontageImmunityWindow> V;TestTrue(TEXT("Read authored notify windows"),RPGMontageImmunity::ReadWindows(*M,V));TestEqual(TEXT("Two windows"),V.Num(),2);
	CastChecked<URPGAnimNotifyState_CombatImmunity>(M->Notifies[0].NotifyStateClass)->Immunities.Flags=4;
	TestEqual(TEXT("Snapshot owns immutable mask"),V[0].Immunities.Flags,3);TestFalse(TEXT("Half-open end"),V[0].Contains(V[0].End));
	CastChecked<URPGAnimNotifyState_CombatImmunity>(M->Notifies[0].NotifyStateClass)->Immunities.Flags=256;
	TestFalse(TEXT("Invalid mask grants no partial windows"),RPGMontageImmunity::ReadWindows(*M,V));TestTrue(TEXT("Invalid snapshot empty"),V.IsEmpty());
	M=Montage();for(int32 I=0;I<7;++I)AddWindow(*M,.1f,.2f,1);TestFalse(TEXT("Bounded window count"),RPGMontageImmunity::ReadWindows(*M,V));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMontageImmunityTimeTest,"ProjectRPG.Combat.MontageImmunity.PositionRatePauseAndNaturalEnd",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGMontageImmunityTimeTest::RunTest(const FString&)
{
	using namespace RPGMontageImmunityTests;FScope W;if(!W.Init())return false;auto* C=W.Spawn();auto* M=Montage();const auto H=Grant(C);
	TestTrue(TEXT("GAS play"),Play(C,H,M)>0);TestFalse(TEXT("Before window"),Immune(C,ERPGCombatEffectKind::HitReaction));
	W.Tick(M->GetPlayLength()*.2f);TestTrue(TEXT("Actual hidden mesh time reaches window"),Immune(C,ERPGCombatEffectKind::Airborne));
	TestTrue(TEXT("Hidden server montage ticks upgraded"),C->GetMesh()->VisibilityBasedAnimTickOption!=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
	auto* Anim=C->GetMesh()->GetAnimInstance();Anim->Montage_Pause(M);TestFalse(TEXT("Paused time does not grant indefinite armor"),Immune(C,ERPGCombatEffectKind::HitReaction));
	Anim->Montage_Resume(M);TestTrue(TEXT("Resume reevaluates current position"),Immune(C,ERPGCombatEffectKind::HitReaction));
	Anim->Montage_SetPlayRate(M,0);TestFalse(TEXT("Zero play rate releases armor"),Immune(C,ERPGCombatEffectKind::HitReaction));Anim->Montage_SetPlayRate(M,1);
	C->GetMesh()->bPauseAnims=true;TestFalse(TEXT("Paused mesh releases armor"),Immune(C,ERPGCombatEffectKind::HitReaction));C->GetMesh()->bPauseAnims=false;
	Anim->Montage_SetPlayRate(M,2);W.Tick(M->GetPlayLength()*.19f);TestFalse(TEXT("Rate follows animation rather than wall-clock timer"),Immune(C,ERPGCombatEffectKind::HitReaction));
	AddInfo(FString::Printf(TEXT("Rate boundary position=%g length=%g sections=%d"),Anim->Montage_GetPosition(M),M->GetPlayLength(),M->CompositeSections.Num()));
	TestTrue(TEXT("Remaining independent window"),Immune(C,ERPGCombatEffectKind::Slow));W.Tick(M->GetPlayLength());
	TestFalse(TEXT("Natural end drops last window"),Immune(C,ERPGCombatEffectKind::Slow));TestFalse(TEXT("Ended tracking"),C->GetCombatControlComponent()->HasMontageImmunityTracking());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMontageImmunityOverlapTest,"ProjectRPG.Combat.MontageImmunity.OverlapSeekAndForeignOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGMontageImmunityOverlapTest::RunTest(const FString&)
{
	using namespace RPGMontageImmunityTests;FScope W;if(!W.Init())return false;auto* C=W.Spawn();auto* M=Montage();const auto Whole=Grant(C,4),H=Grant(C);
	Play(C,H,M,.4f);TestTrue(TEXT("Overlapping windows"),Immune(C,ERPGCombatEffectKind::Airborne)&&Immune(C,ERPGCombatEffectKind::Slow));
	CastChecked<URPGAnimNotifyState_CombatImmunity>(M->Notifies[0].NotifyStateClass)->Immunities.Flags=128;
	Seek(C,M,.6f);TestFalse(TEXT("Leaving one window retires only that mask"),Immune(C,ERPGCombatEffectKind::HitReaction));TestTrue(TEXT("Whole execution survives"),Immune(C,ERPGCombatEffectKind::Stun));
	Seek(C,M,.05f);TestFalse(TEXT("Backward seek drops window without end notify"),Immune(C,ERPGCombatEffectKind::Knockback));Seek(C,M,.2f);
	TestTrue(TEXT("Reentry uses original frozen mask"),Immune(C,ERPGCombatEffectKind::Knockback));
	const auto Other=Foreign(C,1);C->GetMesh()->GetAnimInstance()->Montage_Stop(.2f,M);
	TestTrue(TEXT("Foreign same-class GE preserved"),Immune(C,ERPGCombatEffectKind::HitReaction));TestFalse(TEXT("Blend-out is outside protection"),Immune(C,ERPGCombatEffectKind::Knockback));
	TestNotNull(TEXT("Foreign handle"),C->GetAbilitySystemComponent()->GetActiveGameplayEffect(Other));C->GetAbilitySystemComponent()->CancelAbilityHandle(Whole);TestFalse(TEXT("Whole execution ends independently"),Immune(C,ERPGCombatEffectKind::Stun));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMontageImmunityLifeTest,"ProjectRPG.Combat.MontageImmunity.CancelDeathReuseAndMeshReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGMontageImmunityLifeTest::RunTest(const FString&)
{
	using namespace RPGMontageImmunityTests;FScope W;if(!W.Init())return false;auto* C=W.Spawn();auto* M=Montage();auto* ASC=C->GetRPGAbilitySystemComponent();auto* Control=C->GetCombatControlComponent();auto H=Grant(C);Play(C,H,M,.2f);
	ASC->CancelAbilityHandle(H);TestFalse(TEXT("Cancel removes window synchronously"),Immune(C,ERPGCombatEffectKind::HitReaction));
	ASC->TryActivateAbility(H);Play(C,H,M,.2f);Control->ResetForAvatarReuse();TestFalse(TEXT("Reuse removes old montage scope"),Immune(C,ERPGCombatEffectKind::Knockback));
	Play(C,H,M,.2f);TestFalse(TEXT("Old activation cannot replay armor in a new life"),Immune(C,ERPGCombatEffectKind::HitReaction));ASC->CancelAbilityHandle(H);ASC->TryActivateAbility(H);
	Play(C,H,M,.2f);C->GetMesh()->SetAnimInstanceClass(nullptr);TestFalse(TEXT("Mesh anim replacement"),Immune(C,ERPGCombatEffectKind::HitReaction));
	C->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());ASC->RefreshAbilityActorInfo();Play(C,H,M,.2f);
	TestTrue(TEXT("Fresh current execution grants before lethal cleanup"),Immune(C,ERPGCombatEffectKind::HitReaction));
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);TestFalse(TEXT("Death removes window"),Control->HasMontageImmunityTracking());TestFalse(TEXT("No stale immunity tag"),ASC->HasMatchingGameplayTag(RPGCombatImmunity::HitReactionTag()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGMontageImmunityReentryTest,"ProjectRPG.Combat.MontageImmunity.ReentrantCancelAndSameInstanceRestart",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRPGMontageImmunityReentryTest::RunTest(const FString&)
{
	using namespace RPGMontageImmunityTests;FScope W;if(!W.Init())return false;auto* C=W.Spawn();auto* M=Montage();auto* ASC=C->GetRPGAbilitySystemComponent();const auto H=Grant(C);bool Restarted=false;
	const auto Callback=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([&](UAbilitySystemComponent*,const FGameplayEffectSpec& Spec,FActiveGameplayEffectHandle)
	{
		if(Restarted||Spec.Def!=GetDefault<URPGCombatImmunityEffect>())return;Restarted=true;ASC->CancelAbilityHandle(H);ASC->TryActivateAbility(H);Play(C,H,M,.6f);
	});
	Play(C,H,M,.2f);ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Callback);
	TestTrue(TEXT("Same instance restarted inside GE callback"),Restarted&&Ability(C,H)->IsActive());TestFalse(TEXT("Old callback cannot retain old window"),Immune(C,ERPGCombatEffectKind::HitReaction));
	TestTrue(TEXT("New window remains"),Immune(C,ERPGCombatEffectKind::Slow));ASC->CancelAbilityHandle(H);TestFalse(TEXT("Later cancel owns new token"),Immune(C,ERPGCombatEffectKind::Slow));return true;
}
#endif
