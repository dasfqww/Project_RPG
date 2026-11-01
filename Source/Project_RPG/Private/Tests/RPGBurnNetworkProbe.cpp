#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"

namespace
{
	UAbilitySystemComponent* BurnASC(APawn* P) { return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent(); }
	URPGCombatControlComponent* BurnControl(APawn* P) { return P->FindComponentByClass<URPGCombatControlComponent>(); }
	float BurnHP(APawn* P) { return BurnASC(P)->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
}
bool URPGBossMapE2EProbeComponent::ApplyBurnProbeDamage(bool bPlayerSource, int32 Damage, float Duration)
{
#if !UE_BUILD_SHIPPING
	auto* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	auto* Spec = BurnASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility);
	FRPGBurnSettings Settings; Settings.TickDamage=Damage; Settings.Duration=Duration; Settings.Interval=.5f;
	if (!Spec || !RPGBurn::SetGrantedSettings(*Spec,Settings)) { Fail(TEXT("BurnGrantInvalid")); return false; }
	return ApplyStunProbeDamage(bPlayerSource,0);
#else
	return false;
#endif
}
void URPGBossMapE2EProbeComponent::TickBurnServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player=LightHitPlayer.Get(); auto* NPC=LightHitNPC.Get(); auto* PC=BurnControl(Player); auto* NC=BurnControl(NPC);
	if(Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount()!=KnockbackViolationBaseline) { Fail(TEXT("BurnSecurityViolation")); return; }
	const bool Confirmed=KnockbackConfirmations.FindOrAdd(KnockbackStage).Num()==2; const double Now=GetWorld()->GetTimeSeconds();
	const auto Send=[this,Now](int32 Step,APawn* Subject) { LightHitNextAt=Now; SendKnockbackProbeStep(Step,Subject); };
	if(KnockbackStage==0&&KnockbackConfirmations.FindOrAdd(0).Num()>=1)
	{
		if(!BurnASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
		if(!ApplyBurnProbeDamage(false,2,30))return; BurnHealthBaseline=BurnHP(Player); Send(1,Player);
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_BURN_WAIT_LATE_JOIN ActiveBurn=1")); return;
	}
	if(KnockbackStage==1)
	{
		if(Now-StunDiagnosticAt>.5)
		{
			StunDiagnosticAt=Now; for(const auto& Client:ReadyClients) if(!KnockbackConfirmations.FindOrAdd(1).Contains(Client))
				Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player,NPC,1,Player->GetActorLocation());
		}
		if(!Confirmed)
		{
			if(PC->GetBurnPresentationState().EndServerTime-Now<5) { PC->CleanseBurns(); ApplyBurnProbeDamage(false,2,30); }
			return;
		}
		if(BurnHP(Player)>BurnHealthBaseline-2)return;
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_BURN_TICKS Player=1 Before=%g After=%g"),BurnHealthBaseline,BurnHP(Player));
		PC->CleanseBurns(); BurnHealthBaseline=BurnHP(Player); Send(2,Player); return;
	}
	if(KnockbackStage==2&&Confirmed&&Now-LightHitNextAt>.8)
	{
		if(BurnHP(Player)!=BurnHealthBaseline) { Fail(TEXT("BurnCleanseStillDamaging")); return; }
		if(!ApplyBurnProbeDamage(true,3,6)||!ApplyBurnProbeDamage(true,5,6))return;
		BurnHealthBaseline=BurnHP(NPC); Send(3,NPC); return;
	}
	if(KnockbackStage==3&&Confirmed)
	{
		if(BurnHP(NPC)>BurnHealthBaseline-8)return;
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_BURN_TICKS Player=0 Before=%g After=%g"),BurnHealthBaseline,BurnHP(NPC));
		TArray<FActiveGameplayEffectHandle> Handles; NC->OwnedBurns.GenerateKeyArray(Handles);
		if(Handles.Num()!=2||!NC->RemoveOwnedBurn(Handles[0])) { Fail(TEXT("BurnOverlapMissing")); return; }
		Send(4,NPC); return;
	}
	if(KnockbackStage==4&&Confirmed&&NC->GetBurnPresentationState().Stacks==0) { Send(5,NPC); return; }
	if(KnockbackStage==5&&Confirmed)
	{
		FRPGCombatImmunitySettings I; I.Flags=4; const auto Tags=I.GetGrantedTags(); BurnASC(Player)->AddLooseGameplayTags(Tags);
		const bool Damage=ApplyBurnProbeDamage(false,2,5); BurnASC(Player)->RemoveLooseGameplayTags(Tags);
		if(!Damage||PC->GetBurnPresentationState().Stacks!=0) { Fail(TEXT("BurnGroupImmunity")); return; }
		Send(6,Player); return;
	}
	if(KnockbackStage==6&&Confirmed)
	{
		if(!ApplyBurnProbeDamage(false,2,10))return; Send(7,Player); return;
	}
	if(KnockbackStage==7&&Confirmed)
	{
		BurnASC(NPC)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),0);
		if(PC->GetBurnPresentationState().Stacks>0)return;
		BurnHealthBaseline=BurnHP(Player); Send(8,Player); return;
	}
	if(KnockbackStage==8&&Confirmed&&Now-LightHitNextAt>.8)
	{
		if(BurnHP(Player)!=BurnHealthBaseline) { Fail(TEXT("BurnDeadSourceStillDamaging")); return; }
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_BURN_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 Ticks=1 Overlap=1 ExactCleanse=1 NaturalExpiry=1 Immunity=1 SourceDeath=1 StatusUI=1 LateJoin=1 SecurityViolations=0 Clients=2"));
		bFinished=true; CleanupLightHitInput();
	}
#endif
}
void URPGBossMapE2EProbeComponent::TickBurnClient()
{
#if !UE_BUILD_SHIPPING
	auto* Controller=Cast<APlayerController>(GetOwner()); auto* Player=LightHitPlayer.Get(); if(!Controller||!Player||!LightHitNPC.IsValid())return;
	const bool Owner=Controller->GetPawn()==Player; const int32 Step=KnockbackExpected;
	if(Owner&&KnockbackInputStep!=Step)
	{
		KnockbackInputStep=Step; LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		if(Step==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
	}
	if(KnockbackConfirmed==Step)return;
	auto* Subject=Step>=3&&Step<=5?LightHitNPC.Get():Player; auto* C=BurnControl(Subject); auto* ASC=BurnASC(Subject);
	const int32 Stacks=Step==3?2:Step==1||Step==4||Step==7?1:0;
	if(C->GetBurnPresentationState().Stacks!=Stacks||ASC->HasMatchingGameplayTag(RPGBurn::StatusTag())!=(Stacks>0))return;
	if(RPGCombatControl::IsActionBlocked(Subject)) { Fail(TEXT("BurnUnexpectedControlBlock")); return; }
	if((Stacks>0)&&(!C->BurnStatusWidget||!C->BurnStatusWidget->GetUserWidgetObject()))return;
	if(Stacks==0&&C->BurnStatusWidget)return;
	if(Step==0&&Owner&&!ASC->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
	if(BurnClientStep!=Step) { BurnClientStep=Step; BurnClientHealth=BurnHP(Subject); }
	if((Step==1||Step==3)&&BurnHP(Subject)>=BurnClientHealth)return; // Observe a real periodic HP delta after initial hit replication.
	if(Step==8&&BurnHP(LightHitNPC.Get())!=0)return;
	UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_BURN_CLIENT Step=%d Owner=%d Stacks=%d HP=%g StatusUI=%d"),Step,Owner,Stacks,BurnHP(Subject),C->BurnStatusWidget!=nullptr);
	KnockbackConfirmed=Step; ServerConfirmKnockback(Step);
#endif
}
