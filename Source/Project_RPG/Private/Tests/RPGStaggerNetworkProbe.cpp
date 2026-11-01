#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Combat/RPGHitReactionProfile.h"
#include "Animation/AnimMontage.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Controller/RPGAIController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Navigation/PathFollowingComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	UAbilitySystemComponent* SASC(APawn* P){return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent();}
	URPGCombatControlComponent* SControl(APawn* P){return P->FindComponentByClass<URPGCombatControlComponent>();}
}
bool URPGBossMapE2EProbeComponent::ApplyStaggerProbeDamage(bool PlayerSource,float Duration)
{
#if !UE_BUILD_SHIPPING
	auto* P=PlayerSource?LightHitPlayer.Get():LightHitNPC.Get();auto* Spec=SASC(P)->FindAbilitySpecFromHandle(PlayerSource?LightHitPlayerAbility:LightHitNPCAbility);
	FRPGStaggerSettings Settings;Settings.Duration=Duration;
	if(!Spec||!RPGStagger::SetGrantedSettings(*Spec,Settings)){Fail(TEXT("StaggerGrantInvalid"));return false;}
	return ApplyStunProbeDamage(PlayerSource,0);
#else
	return false;
#endif
}
void URPGBossMapE2EProbeComponent::TickStaggerServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player=LightHitPlayer.Get();auto* NPC=LightHitNPC.Get();auto* PC=SControl(Player);auto* NC=SControl(NPC);
	if(Player->FindComponentByClass<URPGSecurityValidationComponent>()->GetTotalViolationCount()!=KnockbackViolationBaseline){Fail(TEXT("StaggerMovementViolation"));return;}
	const bool Confirmed=KnockbackConfirmations.FindOrAdd(KnockbackStage).Num()==2;const double Now=GetWorld()->GetTimeSeconds();
	const auto Send=[this,Now](int32 Step,APawn* Subject){LightHitNextAt=Now;SendKnockbackProbeStep(Step,Subject);};
	if(FParse::Param(FCommandLine::Get(),TEXT("RPGBossMapE2EAuthoredStagger")))
	{
		if(Now-LightHitNextAt>30&&KnockbackStage>0){Fail(TEXT("AuthoredStaggerPoseOrReplicationTimeout"));return;}
		const auto ApplyAuthored=[this,Now](bool PlayerSource)
		{
			if(!ApplyStunProbeDamage(PlayerSource,0))return false;
			auto* Target=PlayerSource?LightHitNPC.Get():LightHitPlayer.Get();
			const float Duration=PlayerSource?.25f:.35f;const auto State=SControl(Target)->GetStaggerState();
			if(!RPGStagger::IsStaggered(Target)||!State.bActive||FMath::Abs(State.EndServerTime-Now-Duration)>.05)
			{Fail(TEXT("AuthoredStaggerDurationMismatch"));return false;}
			StunDiagnosticAt=Now;return true;
		};
		if(KnockbackStage==0&&Confirmed&&SASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()
			&&!RPGStagger::IsStaggered(NPC))
		{
			if(!ApplyAuthored(false)||!bLightInputCancelled){Fail(TEXT("AuthoredStaggerAttackNotCancelled"));return;}
			Send(1,Player);return;
		}
		if(KnockbackStage==1)
		{
			if(!Confirmed&&Now-StunDiagnosticAt>.6){ApplyAuthored(false);return;}
			if(Confirmed&&!RPGStagger::IsStaggered(Player)){Send(2,Player);return;}
		}
		if(KnockbackStage==2&&Confirmed){if(ApplyAuthored(true))Send(3,NPC);return;}
		if(KnockbackStage==3)
		{
			if(!Confirmed&&Now-StunDiagnosticAt>.6){ApplyAuthored(true);return;}
			if(Confirmed&&!RPGStagger::IsStaggered(NPC)){Send(4,NPC);return;}
		}
		if(KnockbackStage==4&&Confirmed)
		{
			UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_AUTHORED_STAGGER_COMPLETE PlayerSeconds=0.25 NPCSeconds=0.35 OriginalStartupGrants=1 PlayerPose=1 NPCPose=1 CancelCombat=1 NaturalExpiry=1 SecurityViolations=0 Clients=2"));
			bFinished=true;CleanupLightHitInput();
		}
		return;
	}
	if(KnockbackStage==0&&KnockbackConfirmations.FindOrAdd(0).Num()>=1)
	{
		if(!SASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
		if(!ApplyStaggerProbeDamage(false,2)||!RPGStagger::IsStaggered(Player)||!bLightInputCancelled){Fail(TEXT("StaggerDidNotCancelCombat"));return;}
		KnockbackProbeOrigin=Player->GetActorLocation();Send(1,Player);UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_STAGGER_WAIT_LATE_JOIN ActiveStagger=1"));return;
	}
	if(KnockbackStage==1)
	{
		if(Now-StunDiagnosticAt>.4)
		{
			StunDiagnosticAt=Now;for(const auto& Client:ReadyClients)if(!KnockbackConfirmations.FindOrAdd(1).Contains(Client))
				Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player,NPC,1,Player->GetActorLocation());
		}
		if(!Confirmed)
		{
			if(PC->GetStaggerState().EndServerTime-Now<1)ApplyStaggerProbeDamage(false,2);
			return;
		}
		if(RPGStagger::IsStaggered(Player))
		{
			if(FVector::Dist2D(KnockbackProbeOrigin,Player->GetActorLocation())>15||SASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()){Fail(TEXT("StaggerInputEscaped"));return;}
			return;
		}
		Send(2,Player);return;
	}
	if(KnockbackStage==2&&Confirmed)
	{
		if(!ApplyStaggerProbeDamage(true,2))return;
		auto* AI=Cast<ARPGAIController>(NPC->GetController());FAIMoveRequest Move(NPC->GetActorLocation()+FVector(400,0,0));Move.SetUsePathfinding(false);Move.SetProjectGoalLocation(false);
		if(!RPGStagger::IsStaggered(NPC)||!AI||AI->MoveTo(Move).Code!=EPathFollowingRequestResult::Failed){Fail(TEXT("StaggerNPCMoveGate"));return;}
		Send(3,NPC);return;
	}
	if(KnockbackStage==3&&Confirmed)
	{
		const double End=NC->GetStaggerState().EndServerTime;if(!ApplyStaggerProbeDamage(true,.5f))return;
		if(NC->OwnedStaggers.Num()!=2){Fail(TEXT("StaggerOverlap"));return;}
		const auto Handles=NC->OwnedStaggers;for(auto H:Handles)if(SASC(NPC)->GetActiveGameplayEffect(H)->GetDuration()<1)NC->RemoveOwnedStagger(H);
		if(!RPGStagger::IsStaggered(NPC)||NC->GetStaggerState().EndServerTime<End-.02){Fail(TEXT("StaggerExactRemoval"));return;}Send(4,NPC);return;
	}
	if(KnockbackStage==4&&Confirmed&&!RPGStagger::IsStaggered(NPC)){Send(5,NPC);return;}
	if(KnockbackStage==5&&Confirmed)
	{
		FRPGCombatImmunitySettings I;I.Flags=1;auto* ASC=SASC(Player);FGameplayEffectSpec Shield(GetDefault<URPGCombatImmunityEffect>(),ASC->MakeEffectContext(),1);Shield.DynamicGrantedTags=I.GetGrantedTags();
		StaggerForeignEffect=ASC->ApplyGameplayEffectSpecToSelf(Shield);const bool Damage=ApplyStaggerProbeDamage(false,1);ASC->RemoveActiveGameplayEffect(StaggerForeignEffect);
		if(!Damage||RPGStagger::IsStaggered(Player)){Fail(TEXT("StaggerImmunity"));return;}
		I.Flags=4;Shield.DynamicGrantedTags=I.GetGrantedTags();StaggerForeignEffect=ASC->ApplyGameplayEffectSpecToSelf(Shield);
		if(!ApplyStaggerProbeDamage(false,2)||!RPGStagger::IsStaggered(Player)){Fail(TEXT("StaggerAilmentSeparation"));return;}Send(6,Player);return;
	}
	if(KnockbackStage==6&&Confirmed)
	{
		SASC(Player)->RemoveActiveGameplayEffect(StaggerForeignEffect);StaggerForeignEffect.Invalidate();
		if(!ApplyStunProbeDamage(false,2)||RPGStagger::IsStaggered(Player)||!RPGCombatControl::IsStunned(Player)){Fail(TEXT("StaggerHigherStunPriority"));return;}Send(7,Player);return;
	}
	if(KnockbackStage==7&&Confirmed)
	{
		const auto Stuns=PC->OwnedStuns;for(auto H:Stuns)PC->RemoveOwnedStun(H);
		if(!ApplyStaggerProbeDamage(false,2)||!RPGStagger::IsStaggered(Player))return;Send(8,Player);return;
	}
	if(KnockbackStage==8&&Confirmed)
	{
		SASC(Player)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),1);
		if(!ApplyStaggerProbeDamage(false,1)||RPGStagger::IsStaggered(Player)||PC->GetStaggerState().bActive){Fail(TEXT("StaggerDeathCleanup"));return;}Send(9,Player);return;
	}
	if(KnockbackStage==9&&Confirmed)
	{
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_STAGGER_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 CancelCombat=1 InputBlocked=1 NaturalExpiry=1 ExactRemoval=1 Immunity=1 SeparateFromStun=1 HigherStun=1 Death=1 LateJoin=1 SecurityViolations=0 Clients=2"));
		bFinished=true;CleanupLightHitInput();
	}
#endif
}
void URPGBossMapE2EProbeComponent::TickStaggerClient()
{
#if !UE_BUILD_SHIPPING
	auto* Controller=Cast<APlayerController>(GetOwner());auto* Player=LightHitPlayer.Get();if(!Controller||!Player||!LightHitNPC.IsValid())return;
	const int32 Step=KnockbackExpected;const bool Owner=Controller->GetPawn()==Player;const double Now=FPlatformTime::Seconds();
	if(FParse::Param(FCommandLine::Get(),TEXT("RPGBossMapE2EAuthoredStagger")))
	{
		if(Owner&&KnockbackInputStep!=Step)
		{
			KnockbackInputStep=Step;LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
			if(Step==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
		}
		if(KnockbackConfirmed==Step)return;
		auto* Subject=Step>=3?LightHitNPC.Get():Player;auto* C=SControl(Subject);const bool Active=Step==1||Step==3;
		if(C->GetStaggerState().bActive!=Active||RPGStagger::IsStaggered(Subject)!=Active)return;
		if(Step==0&&Owner&&!SASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
		if(Active)
		{
			if(!RPGCombatControl::IsActionBlocked(Subject)||CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GetMaxSpeed()!=0)return;
			if(C->StaggerMontageInstanceId==INDEX_NONE)
			{
				if(Now-StunDiagnosticAt>.5)
				{
					StunDiagnosticAt=Now;auto* Mesh=CastChecked<ACharacter>(Subject)->GetMesh();auto* Anim=Mesh->GetAnimInstance();
					auto* Receiver=Subject->FindComponentByClass<URPGCombatReactionComponent>();const auto* Profile=Receiver?Receiver->PresentationProfile.Get():nullptr;
					UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_AUTHORED_STAGGER_POSE_WAIT Step=%d Owner=%d Profile=%s Anim=%s OtherMontage=%d GASMontage=%d RootMotion=%d"),
						Step,Owner,*GetNameSafe(Profile),*GetNameSafe(Anim),Anim&&Anim->IsAnyMontagePlaying(),SASC(Subject)->GetAnimatingAbility()!=nullptr,CastChecked<ACharacter>(Subject)->IsPlayingRootMotion());
				}
				return;
			}
		}
		else if(Step>0&&RPGCombatControl::IsActionBlocked(Subject))return;
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_AUTHORED_STAGGER_CLIENT Step=%d Owner=%d Active=%d Pose=%d"),Step,Owner,Active,C->StaggerMontageInstanceId!=INDEX_NONE);
		KnockbackConfirmed=Step;ServerConfirmKnockback(Step);return;
	}
	if(Owner&&KnockbackInputStep!=Step)
	{
		KnockbackInputStep=Step;KnockbackInputAt=Now;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		if(Step==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
		// Strafe into the free lane so the nearby attacking NPC cannot mask movement recovery.
		if(Step==1||Step==2)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(),FInputActionValue(FVector2D(1,0)),{},{});
	}
	if(KnockbackConfirmed==Step)return;auto* Subject=Step>=3&&Step<=5?LightHitNPC.Get():Player;
	const bool Expected=Step==1||Step==3||Step==4||Step==6||Step==8;auto* C=SControl(Subject);auto* ASC=SASC(Subject);
	if(C->GetStaggerState().bActive!=Expected||RPGStagger::IsStaggered(Subject)!=Expected)return;
	if(Step!=7&&Step!=9&&RPGCombatControl::IsStunned(Subject))return;
	if(Step==7&&!RPGCombatControl::IsStunned(Subject))return;
	if(Expected&&!RPGCombatControl::IsActionBlocked(Subject))return;
	if(Expected&&CastChecked<ACharacter>(Subject)->GetCharacterMovement()->GetMaxSpeed()!=0)return;
	if(Step==0&&Owner&&!ASC->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
	if(Step==9&&ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())!=0)return;
	if(Owner&&Step==1)
	{
		if(Now-KnockbackInputAt<.6)return;
		if(CastChecked<ACharacter>(Player)->GetCharacterMovement()->Velocity.Size2D()>1)return;
	}
	if(Owner&&Step==2)
	{
		if(Now-KnockbackInputAt<.7||CastChecked<ACharacter>(Player)->GetCharacterMovement()->Velocity.Size2D()<100)return;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_STAGGER_CLIENT Step=%d Owner=%d Stagger=%d Stun=%d Pose=%d"),Step,Owner,Expected,RPGCombatControl::IsStunned(Subject),C->StaggerMontageInstanceId!=INDEX_NONE);
	KnockbackConfirmed=Step;ServerConfirmKnockback(Step);
#endif
}
