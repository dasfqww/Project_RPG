#include "Tests/RPGBossMapE2EProbeComponent.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGMontageImmunityTestSupport.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "UObject/StrongObjectPtr.h"

struct FRPGMontageImmunityNetworkFixture
{
	TStrongObjectPtr<UAnimMontage> PlayerMontage, NPCMontage;
	TArray<FAnimNotifyEvent> PlayerNotifies, NPCNotifies;
	TArray<FCompositeSection> PlayerSections, NPCSections;
	bool bPlayerAutoBlend = true, bNPCAutoBlend = true;
	TWeakObjectPtr<UAnimInstance> PlayerAnim, NPCAnim;
	ERootMotionMode::Type PlayerRootMode = ERootMotionMode::RootMotionFromMontagesOnly, NPCRootMode = ERootMotionMode::RootMotionFromMontagesOnly;
	FGameplayAbilitySpecHandle PlayerAbility, NPCAbility;
	FActiveGameplayEffectHandle Foreign;
	bool bPrepared = false;
};
namespace
{
	UAbilitySystemComponent* WASC(APawn* P) { return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent(); }
	URPGCombatControlComponent* WControl(APawn* P) { return P->FindComponentByClass<URPGCombatControlComponent>(); }
	UAnimInstance* WAnim(APawn* P) { return CastChecked<ACharacter>(P)->GetMesh()->GetAnimInstance(); }
	FGameplayAbilitySpecHandle WindowGrant(APawn* P)
	{
		auto* ASC=WASC(P);FGameplayAbilitySpec Spec(URPGImmunityTestCombatAbility::StaticClass(),1);
		const auto H=ASC->GiveAbility(Spec);return ASC->TryActivateAbility(H)?H:FGameplayAbilitySpecHandle();
	}
	bool WindowPlay(APawn* P,FGameplayAbilitySpecHandle H,UAnimMontage* M,float Fraction)
	{
		auto* ASC=WASC(P);auto* Spec=ASC->FindAbilitySpecFromHandle(H);auto* A=Spec?Spec->GetPrimaryInstance():nullptr;
		return A&&ASC->PlayMontage(A,A->GetCurrentActivationInfo(),M,.001f,NAME_None,Fraction*M->GetPlayLength())>0;
	}
	bool WindowMask(APawn* P,int32 Expected)
	{
		FRPGCombatImmunitySettings S;S.Flags=Expected;
		for(auto Kind:{ERPGCombatEffectKind::HitReaction,ERPGCombatEffectKind::Knockback,ERPGCombatEffectKind::Airborne,
			ERPGCombatEffectKind::Stun,ERPGCombatEffectKind::Slow,ERPGCombatEffectKind::Burn})
			if(RPGCombatImmunity::IsImmune(P,Kind)!=S.Protects(Kind))return false;
		return true;
	}
}
#endif

void URPGBossMapE2EProbeComponent::CleanupMontageImmunityFixture()
{
#if !UE_BUILD_SHIPPING
	if(!MontageImmunityFixture)return;const auto F=MoveTemp(MontageImmunityFixture);
	if(LightHitPlayer.IsValid()) { WASC(LightHitPlayer.Get())->CancelAbilityHandle(F->PlayerAbility);if(F->Foreign.IsValid())WASC(LightHitPlayer.Get())->RemoveActiveGameplayEffect(F->Foreign); }
	if(LightHitNPC.IsValid())WASC(LightHitNPC.Get())->CancelAbilityHandle(F->NPCAbility);
	if(F->PlayerMontage.IsValid()) { F->PlayerMontage->Notifies=F->PlayerNotifies;F->PlayerMontage->CompositeSections=F->PlayerSections;F->PlayerMontage->bEnableAutoBlendOut=F->bPlayerAutoBlend;RPGMontageImmunityTests::Refresh(*F->PlayerMontage); }
	if(F->NPCMontage.IsValid()) { F->NPCMontage->Notifies=F->NPCNotifies;F->NPCMontage->CompositeSections=F->NPCSections;F->NPCMontage->bEnableAutoBlendOut=F->bNPCAutoBlend;RPGMontageImmunityTests::Refresh(*F->NPCMontage); }
	if(F->PlayerAnim.IsValid())F->PlayerAnim->SetRootMotionMode(F->PlayerRootMode);
	if(F->NPCAnim.IsValid())F->NPCAnim->SetRootMotionMode(F->NPCRootMode);
#endif
}

void URPGBossMapE2EProbeComponent::TickMontageImmunityServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player=LightHitPlayer.Get();auto* NPC=LightHitNPC.Get();const double Now=GetWorld()->GetTimeSeconds();
	if(Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount()!=KnockbackViolationBaseline) { Fail(TEXT("MontageImmunitySecurityViolation"));return; }
	const bool Confirmed=KnockbackConfirmations.FindOrAdd(KnockbackStage).Num()==2;
	const auto Send=[this,Now](int32 Step,APawn* Subject){LightHitNextAt=Now;SendKnockbackProbeStep(Step,Subject);};
	if(KnockbackStage==0&&KnockbackConfirmations.FindOrAdd(0).Num()>=1)
	{
		if(!MontageImmunityFixture)
		{
			MontageImmunityFixture=MakeShared<FRPGMontageImmunityNetworkFixture>();LightHitNextAt=Now;return;
		}
		if(Now-LightHitNextAt<.5)return;auto& F=*MontageImmunityFixture;
		F.PlayerMontage.Reset(RPGMontageImmunityTests::LoadPlayerMontage());
		F.NPCMontage.Reset(LoadObject<UAnimMontage>(nullptr,TEXT("/Game/Blueprints/Character/NPC/Gruntling/Anim/Montage/AM_Guardian_MeleeAttack.AM_Guardian_MeleeAttack")));
		if(!F.PlayerMontage.IsValid()||!F.NPCMontage.IsValid()) { Fail(TEXT("MontageImmunityAssets"));return; }
		F.PlayerNotifies=F.PlayerMontage->Notifies;F.NPCNotifies=F.NPCMontage->Notifies;
		F.PlayerSections=F.PlayerMontage->CompositeSections;F.NPCSections=F.NPCMontage->CompositeSections;
		F.bPlayerAutoBlend=F.PlayerMontage->bEnableAutoBlendOut;F.bNPCAutoBlend=F.NPCMontage->bEnableAutoBlendOut;
		for(auto* M:{F.PlayerMontage.Get(),F.NPCMontage.Get()})
		{
			M->bEnableAutoBlendOut=true;
			for(int32 I=0;I<M->CompositeSections.Num();++I)M->CompositeSections[I].NextSectionName=I+1<M->CompositeSections.Num()?M->CompositeSections[I+1].SectionName:NAME_None;
			M->Notifies.Reset();RPGMontageImmunityTests::AddWindow(*M,.1f,.65f,3);RPGMontageImmunityTests::AddWindow(*M,.35f,.8f,64);RPGMontageImmunityTests::Refresh(*M);
		}
		F.PlayerAnim=WAnim(Player);F.NPCAnim=WAnim(NPC);F.PlayerRootMode=F.PlayerAnim->RootMotionMode;F.NPCRootMode=F.NPCAnim->RootMotionMode;
		F.PlayerAnim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);F.NPCAnim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		WASC(Player)->CancelAbilityHandle(LightHitPlayerAbility);F.PlayerAbility=WindowGrant(Player);F.NPCAbility=WindowGrant(NPC);
		if(!F.PlayerAbility.IsValid()||!F.NPCAbility.IsValid()||!WindowPlay(Player,F.PlayerAbility,F.PlayerMontage.Get(),.4f)||!WindowMask(Player,67)) { Fail(TEXT("MontageImmunityStart"));return; }
		F.bPrepared=true;Send(1,Player);UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_MONTAGE_IMMUNITY_WAIT_LATE_JOIN ActiveWindow=1"));return;
	}
	if(!MontageImmunityFixture||!MontageImmunityFixture->bPrepared)return;auto& F=*MontageImmunityFixture;
	if(KnockbackStage==1)
	{
		if(Now-StunDiagnosticAt>.5)
		{
			StunDiagnosticAt=Now;for(const auto& Client:ReadyClients)if(!KnockbackConfirmations.FindOrAdd(1).Contains(Client))
				Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player,NPC,1,Player->GetActorLocation());
		}
		if(!Confirmed)return;
		if(!ApplyImmunityProbeDamage(false,true,false,false,true)||RPGCombatControl::IsActionBlocked(Player)||WControl(Player)->GetSlowPresentationState().bActive) { Fail(TEXT("MontageImmunityDamageAndRejection"));return; }
		WAnim(Player)->Montage_SetPosition(F.PlayerMontage.Get(),F.PlayerMontage->GetPlayLength()*.7f);if(!WindowMask(Player,64)) { Fail(TEXT("MontageImmunitySeek"));return; }Send(2,Player);return;
	}
	if(KnockbackStage==2&&Confirmed) { WAnim(Player)->Montage_Pause(F.PlayerMontage.Get());if(!WindowMask(Player,0)){Fail(TEXT("MontageImmunityPause"));return;}Send(3,Player);return; }
	if(KnockbackStage==3&&Confirmed)
	{
		WAnim(Player)->Montage_SetPosition(F.PlayerMontage.Get(),F.PlayerMontage->GetPlayLength()*.4f);WAnim(Player)->Montage_Resume(F.PlayerMontage.Get());
		if(!WindowMask(Player,67)){Fail(TEXT("MontageImmunityResume"));return;}Send(4,Player);return;
	}
	if(KnockbackStage==4&&Confirmed)
	{
		FRPGCombatImmunitySettings S;S.Flags=1;auto* ASC=WASC(Player);FGameplayEffectSpec Spec(GetDefault<URPGCombatImmunityEffect>(),ASC->MakeEffectContext(),1);Spec.DynamicGrantedTags=S.GetGrantedTags();F.Foreign=ASC->ApplyGameplayEffectSpecToSelf(Spec);
		WAnim(Player)->Montage_Stop(.25f,F.PlayerMontage.Get());if(!WindowMask(Player,1)||!ASC->GetActiveGameplayEffect(F.Foreign)){Fail(TEXT("MontageImmunityForeign"));return;}Send(5,Player);return;
	}
	if(KnockbackStage==5&&Confirmed)
	{
		WASC(Player)->RemoveActiveGameplayEffect(F.Foreign);F.Foreign.Invalidate();
		if(!WindowPlay(NPC,F.NPCAbility,F.NPCMontage.Get(),.2f)||!WindowMask(NPC,3)){Fail(TEXT("MontageImmunityNPCStart"));return;}Send(6,NPC);return;
	}
	if(KnockbackStage==6&&Confirmed)
	{
		if(!ApplyImmunityProbeDamage(true,false,false,true,false)||!RPGCombatControl::IsStunned(NPC)||!WindowMask(NPC,0)) { Fail(TEXT("MontageImmunitySelectiveStun"));return; }Send(7,NPC);return;
	}
	if(KnockbackStage==7&&Confirmed)
	{
		const auto Stuns=WControl(NPC)->OwnedStuns;for(auto H:Stuns)WControl(NPC)->RemoveOwnedStun(H);
		if(!WASC(NPC)->TryActivateAbility(F.NPCAbility)||!WindowPlay(NPC,F.NPCAbility,F.NPCMontage.Get(),.2f)||!WindowMask(NPC,3)){Fail(TEXT("MontageImmunityNPCRestart"));return;}Send(8,NPC);return;
	}
	if(KnockbackStage==8&&Confirmed)
	{
		WASC(NPC)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);if(!WindowMask(NPC,0)||WControl(NPC)->HasMontageImmunityTracking()){Fail(TEXT("MontageImmunityDeath"));return;}Send(9,NPC);return;
	}
	if(KnockbackStage==9&&Confirmed)
	{
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_MONTAGE_IMMUNITY_COMPLETE ServerMontage=1 OwnerObserver=1 Overlap=1 Seek=1 PauseResume=1 BlendOut=1 ForeignPreserved=1 SelectiveStun=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2"));
		bFinished=true;CleanupLightHitInput();CleanupMontageImmunityFixture();
	}
#endif
}

void URPGBossMapE2EProbeComponent::TickMontageImmunityClient()
{
#if !UE_BUILD_SHIPPING
	auto* Controller=Cast<APlayerController>(GetOwner());auto* Player=LightHitPlayer.Get();if(!Controller||!Player||!LightHitNPC.IsValid())return;
	const int32 Step=KnockbackExpected;const bool Owner=Controller->GetPawn()==Player;
	if(Owner&&KnockbackInputStep!=Step)
	{
		KnockbackInputStep=Step;LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		if(Step==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
	}
	if(KnockbackConfirmed==Step)return;
	auto* Subject=Step>=6?LightHitNPC.Get():Player;const int32 Expected=Step==1||Step==4?67:Step==2?64:Step==5?1:Step==6||Step==8?3:0;
	if(!WindowMask(Subject,Expected))return;
	if(WControl(Subject)->HasMontageImmunityTracking()||!WControl(Subject)->AbilityImmunities.IsEmpty()){Fail(TEXT("MontageImmunityClientAuthoredScope"));return;}
	if(Step==0&&Owner&&!WASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
	if(Step==7&&!RPGCombatControl::IsStunned(Subject))return;
	if(Step==9&&WASC(Subject)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())!=0)return;
	if(Owner&&Step==0)LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
	UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_MONTAGE_IMMUNITY_CLIENT Step=%d Owner=%d Mask=%d ClientScopes=0"),Step,Owner,Expected);
	KnockbackConfirmed=Step;ServerConfirmKnockback(Step);
#endif
}
