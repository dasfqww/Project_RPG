#include "Tests/RPGBossMapE2EProbeComponent.h"
#if !UE_BUILD_SHIPPING
#include "Tests/RPGCombatControlTestFixture.h"
#include "Combat/RPGCombatImmunityTypes.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputActionValue.h"

namespace
{
	UAbilitySystemComponent* ImmuneASC(APawn* P) { return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent(); }
	URPGCombatControlComponent* ImmuneControl(APawn* P) { return P->FindComponentByClass<URPGCombatControlComponent>(); }
	FRPGCombatImmunitySettings ImmuneSettings(int32 Flags) { FRPGCombatImmunitySettings S; S.Flags=Flags; return S; }
	FGameplayAbilitySpecHandle StartShield(APawn* P,int32 Flags)
	{
		auto* ASC=ImmuneASC(P); FGameplayAbilitySpec S(URPGImmunityTestCombatAbility::StaticClass(),1);
		RPGCombatImmunity::SetGrantedSettings(S,ImmuneSettings(Flags)); const auto H=ASC->GiveAbility(S);
		return ASC->TryActivateAbility(H)?H:FGameplayAbilitySpecHandle();
	}
	bool HasProtection(APawn* P,int32 Expected)
	{
		const ERPGCombatEffectKind Kinds[]={ERPGCombatEffectKind::HitReaction,ERPGCombatEffectKind::Knockback,
			ERPGCombatEffectKind::Airborne,ERPGCombatEffectKind::Stun,ERPGCombatEffectKind::Slow};
		const auto Settings=ImmuneSettings(Expected);
		for(auto Kind:Kinds)if(RPGCombatImmunity::IsImmune(P,Kind)!=Settings.Protects(Kind))return false;
		return true;
	}
}
#endif

bool URPGBossMapE2EProbeComponent::ApplyImmunityProbeDamage(bool bPlayerSource,bool bDisplacement,bool bAirborne,bool bStun,bool bSlow)
{
#if !UE_BUILD_SHIPPING
	auto* Source=bPlayerSource?LightHitPlayer.Get():LightHitNPC.Get();
	auto* Spec=ImmuneASC(Source)->FindAbilitySpecFromHandle(bPlayerSource?LightHitPlayerAbility:LightHitNPCAbility);
	FRPGSlowSettings Slow;Slow.Percent=bSlow?50:0;Slow.Duration=10;
	FRPGKnockbackSettings Knock;Knock.Distance=bDisplacement?200:0;Knock.Duration=.4f;Knock.Height=bAirborne?200:0;
	if(!Spec||!RPGSlow::SetGrantedSettings(*Spec,Slow)||!RPGKnockback::SetGrantedSettings(*Spec,Knock))return false;
	return ApplyStunProbeDamage(bPlayerSource,bStun?8:0);
#else
	return false;
#endif
}

void URPGBossMapE2EProbeComponent::TickImmunityServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player=LightHitPlayer.Get();auto* NPC=LightHitNPC.Get();auto* PC=ImmuneControl(Player);auto* NC=ImmuneControl(NPC);
	if(Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount()!=KnockbackViolationBaseline){Fail(TEXT("ImmunityMovementViolation"));return;}
	const bool bConfirmed=KnockbackConfirmations.FindOrAdd(KnockbackStage).Num()==2;
	const double Now=GetWorld()->GetTimeSeconds();
	if(KnockbackStage==0&&KnockbackConfirmations.FindOrAdd(0).Num()>=1)
	{
		if(!ImmunityPlayerA.IsValid())
		{
			if(!ImmuneASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
			ImmunityPlayerA=StartShield(Player,7);LightHitNextAt=Now;
			if(!ImmunityPlayerA.IsValid())Fail(TEXT("ImmunityFirstGrant"));
			return;
		}
		// The fixture obeys the same per-class request interval as real input abilities.
		const float Interval=Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetEffectivePolicyConfig().Ability.MinimumSameAbilityIntervalSeconds;
		if(Now-LightHitNextAt<Interval+.05f)return;
		ImmunityPlayerB=StartShield(Player,7);
		if(!ImmunityPlayerA.IsValid()||!ImmunityPlayerB.IsValid()||!HasProtection(Player,7)){Fail(TEXT("ImmunityPlayerGrant"));return;}
		SendKnockbackProbeStep(1,Player);UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_IMMUNITY_WAIT_LATE_JOIN ActiveImmunity=1"));return;
	}
	if(KnockbackStage==1)
	{
		if(Now-StunDiagnosticAt>.5)
		{
			StunDiagnosticAt=Now;for(const auto& Client:ReadyClients)
				if(!KnockbackConfirmations.FindOrAdd(1).Contains(Client))Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player,NPC,1,Player->GetActorLocation());
		}
		if(ReadyClients.Num()!=2||!bConfirmed)return;
		if(!ApplyImmunityProbeDamage(false,true,false,true,true)||!ApplyImmunityProbeDamage(false,true,true,true,true))return;
		if(PC->GetSlowPresentationState().bActive||RPGCombatControl::IsActionBlocked(Player)){Fail(TEXT("ImmunityRejectedControl"));return;}
		ImmuneASC(Player)->CancelAbilityHandle(ImmunityPlayerA);
		if(!HasProtection(Player,7)||!ImmuneASC(Player)->FindAbilitySpecFromHandle(ImmunityPlayerB)->IsActive()){Fail(TEXT("ImmunityOverlap"));return;}
		SendKnockbackProbeStep(2,Player);return;
	}
	if(KnockbackStage==2&&bConfirmed)
	{
		FGameplayEffectSpec Foreign(GetDefault<URPGCombatImmunityEffect>(),ImmuneASC(Player)->MakeEffectContext(),1);
		Foreign.DynamicGrantedTags=ImmuneSettings(1).GetGrantedTags();ImmunityForeignEffect=ImmuneASC(Player)->ApplyGameplayEffectSpecToSelf(Foreign);
		CastChecked<URPGImmunityTestCombatAbility>(ImmuneASC(Player)->FindAbilitySpecFromHandle(ImmunityPlayerB)->GetPrimaryInstance())->Finish();
		if(!HasProtection(Player,1)||!ImmuneASC(Player)->GetActiveGameplayEffect(ImmunityForeignEffect)){Fail(TEXT("ImmunityForeignOwnership"));return;}
		SendKnockbackProbeStep(3,Player);return;
	}
	if(KnockbackStage==3&&bConfirmed)
	{
		ImmuneASC(Player)->RemoveActiveGameplayEffect(ImmunityForeignEffect);ImmunityPlayerA=StartShield(Player,3);
		if(!ImmunityPlayerA.IsValid()||!ApplyImmunityProbeDamage(false,true,false,true,false))return;
		if(!RPGCombatControl::IsStunned(Player)||!HasProtection(Player,0)||ImmuneASC(Player)->FindAbilitySpecFromHandle(ImmunityPlayerA)->IsActive())
		{Fail(TEXT("ImmunitySelectiveStunAndCancellation"));return;}
		SendKnockbackProbeStep(4,Player);return;
	}
	if(KnockbackStage==4&&bConfirmed)
	{
		const auto Stuns=PC->OwnedStuns;for(auto H:Stuns)PC->RemoveOwnedStun(H);
		ImmunityNPC=StartShield(NPC,7);
		if(!ImmunityNPC.IsValid()||!ApplyImmunityProbeDamage(true,true,false,true,true)||!ApplyImmunityProbeDamage(true,true,true,true,true))return;
		if(RPGCombatControl::IsActionBlocked(NPC)||NC->GetSlowPresentationState().bActive){Fail(TEXT("ImmunityNPCControl"));return;}
		SendKnockbackProbeStep(5,NPC);return;
	}
	if(KnockbackStage==5&&bConfirmed)
	{
		ImmuneASC(NPC)->CancelAbilityHandle(ImmunityNPC);
		if(!ApplyImmunityProbeDamage(true,false,false,false,true))return;
		if(!HasProtection(NPC,0)||!NC->GetSlowPresentationState().bActive){Fail(TEXT("ImmunityNPCRecovery"));return;}
		SendKnockbackProbeStep(6,NPC);return;
	}
	if(KnockbackStage==6&&bConfirmed)
	{
		const auto Slows=NC->OwnedSlows;for(auto H:Slows)NC->RemoveOwnedSlow(H);
		ImmunityPlayerA=StartShield(Player,7);ImmuneASC(Player)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),1);
		if(!ImmunityPlayerA.IsValid()||!ApplyImmunityProbeDamage(false,true,true,true,true))return;
		if(!HasProtection(Player,0)||PC->GetSlowPresentationState().bActive||RPGCombatControl::IsActionBlocked(Player)){Fail(TEXT("ImmunityDeathCleanup"));return;}
		SendKnockbackProbeStep(7,Player);return;
	}
	if(KnockbackStage==7&&bConfirmed)
	{
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_IMMUNITY_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ScopedAbility=1 Overlap=1 SelectiveStun=1 ForeignPreserved=1 DamagePreserved=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2"));
		bFinished=true;CleanupLightHitInput();
	}
#endif
}

void URPGBossMapE2EProbeComponent::TickImmunityClient()
{
#if !UE_BUILD_SHIPPING
	const bool bOwner=CastChecked<APlayerController>(GetOwner())->GetPawn()==LightHitPlayer.Get();
	if(bOwner&&KnockbackInputStep!=KnockbackExpected)
	{
		KnockbackInputStep=KnockbackExpected;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		if(KnockbackExpected==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
	}
	if(KnockbackConfirmed==KnockbackExpected)return;
	const int32 Step=KnockbackExpected;APawn* Subject=Step==5||Step==6?LightHitNPC.Get():LightHitPlayer.Get();
	const int32 Expected=Step==1||Step==2||Step==5?7:Step==3?1:0;
	if(!HasProtection(Subject,Expected))return;
	if(Step==0&&bOwner&&!ImmuneASC(Subject)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
	auto* C=ImmuneControl(Subject);const bool bStunned=Step==4,bSlowed=Step==6;
	if(RPGCombatControl::IsStunned(Subject)!=bStunned||RPGCombatControl::IsActionBlocked(Subject)!=bStunned
		||C->GetSlowPresentationState().bActive!=bSlowed)return;
	if(Step==2&&ImmuneASC(Subject)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())>=10000)return;
	if(Step==5&&ImmuneASC(Subject)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())>=10000)return;
	if(Step==7&&ImmuneASC(Subject)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())!=0)return;
	UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_IMMUNITY_CLIENT Step=%d Flags=%d Stun=%d Slow=%d Owner=%d"),Step,Expected,bStunned,bSlowed,
		CastChecked<APlayerController>(GetOwner())->GetPawn()==LightHitPlayer.Get());
	KnockbackConfirmed=Step;ServerConfirmKnockback(Step);
#endif
}
