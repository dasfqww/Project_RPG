#include "Tests/RPGBossMapE2EProbeComponent.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
	UAbilitySystemComponent* SlowASC(APawn* P) { return CastChecked<ARPGBaseCharacter>(P)->GetAbilitySystemComponent(); }
	URPGCombatControlComponent* SlowControl(APawn* P) { return P->FindComponentByClass<URPGCombatControlComponent>(); }
	UCharacterMovementComponent* SlowMovement(APawn* P) { return CastChecked<ACharacter>(P)->GetCharacterMovement(); }
}
bool URPGBossMapE2EProbeComponent::ApplySlowProbeDamage(bool bPlayerSource, float Percent, float Duration)
{
#if !UE_BUILD_SHIPPING
	auto* Source = bPlayerSource ? LightHitPlayer.Get() : LightHitNPC.Get();
	auto* Spec = SlowASC(Source)->FindAbilitySpecFromHandle(bPlayerSource ? LightHitPlayerAbility : LightHitNPCAbility);
	FRPGSlowSettings Settings; Settings.Percent = Percent; Settings.Duration = Duration;
	if (!Spec || !RPGSlow::SetGrantedSettings(*Spec,Settings)) { Fail(TEXT("SlowGrantInvalid")); return false; }
	if(!ApplyStunProbeDamage(bPlayerSource,0)) return false;
	// Keep the damage fixture's real capsule out of the subsequent walking lane.
	if(!bPlayerSource) LightHitNPC->SetActorLocation(LightHitPlayer->GetActorLocation()+LightHitPlayer->GetActorRightVector()*400);
	return true;
#else
	return false;
#endif
}
void URPGBossMapE2EProbeComponent::TickSlowServer()
{
#if !UE_BUILD_SHIPPING
	auto* Player = LightHitPlayer.Get(); auto* NPC = LightHitNPC.Get();
	auto* Security = Player->FindComponentByClass<URPGSecurityValidationComponent>();
	if (Security->GetTotalViolationCount() != KnockbackViolationBaseline) { Fail(TEXT("SlowMovementViolation")); return; }
	const bool bConfirmed = KnockbackConfirmations.FindOrAdd(KnockbackStage).Num()==2;
	const double Now = GetWorld()->GetTimeSeconds();
	auto* PC = SlowControl(Player); auto* NC = SlowControl(NPC);
	const auto Send = [this,Now](int32 Step, APawn* Subject) { LightHitNextAt = Now; SendKnockbackProbeStep(Step,Subject); };
	if (KnockbackStage==0 && KnockbackConfirmations.FindOrAdd(0).Num()>=1)
	{
		if (!SlowASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive()) return;
		if (!ApplySlowProbeDamage(false,50,30)) return;
		if (bLightInputCancelled || RPGCombatControl::IsActionBlocked(Player) || !FMath::IsNearlyEqual(SlowMovement(Player)->GetMaxSpeed(),200.f,.1f))
		{ Fail(TEXT("SlowCancelledCombatOrWrongSpeed")); return; }
		Send(1,Player);
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_SLOW_WAIT_LATE_JOIN ActiveSlow=1 CombatNotCancelled=1"));
		return;
	}
	if (KnockbackStage==1)
	{
		if (Now-StunDiagnosticAt>.5)
		{
			StunDiagnosticAt=Now;
			for(const auto& Client:ReadyClients)
				if(!KnockbackConfirmations.FindOrAdd(1).Contains(Client))
					Client->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveKnockback(Player,NPC,1,Player->GetActorLocation());
		}
		if(ReadyClients.Num()!=2||!bConfirmed)
		{
			if(PC->GetSlowPresentationState().EndServerTime-Now<5)ApplySlowProbeDamage(false,50,30);
			return;
		}
		if(SlowASC(Player)->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
		for(const auto& Client:ReadyClients)
		{
			APawn* Observer=Client->GetPawn();if(!Observer||Observer==Player)continue;
			auto* Lease=Observer->FindComponentByClass<URPGSecurityValidationComponent>();
			if(!Lease||Lease->IsMovementAuthorizationActive())return;
			const auto Id=Lease->BeginMovementAuthorization(.25f,800,TEXT("SlowObserverFixture"));
			if(!Id.IsValid()){Fail(TEXT("SlowObserverLease"));return;}
			Observer->SetActorLocation(Player->GetActorLocation()+Player->GetActorRightVector()*700);Lease->EndMovementAuthorization(Id,true);
		}
		KnockbackProbeOrigin=Player->GetActorLocation(); Send(2,Player); return;
	}
	if (KnockbackStage==2 && bConfirmed && Now-LightHitNextAt>1.5)
	{
		const float Travel = FVector::Dist2D(KnockbackProbeOrigin,Player->GetActorLocation());
		if (Travel<100 || Travel>400) { Fail(TEXT("SlowPlayerTravel")); return; }
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_SLOW_TRAVEL Player=1 Distance=%g"),Travel);
		if (!ApplySlowProbeDamage(false,75,2.5f)) return;
		Send(3,Player); return;
	}
	if (KnockbackStage==3 && bConfirmed && FMath::IsNearlyEqual(PC->GetSlowPresentationState().Percent,50.f,.1f)) { Send(4,Player); return; }
	if (KnockbackStage==4 && bConfirmed)
	{
		const auto Handles=PC->OwnedSlows;for(auto Handle:Handles)PC->RemoveOwnedSlow(Handle);
		Send(5,Player); return;
	}
	if (KnockbackStage==5 && bConfirmed)
	{
		if (!ApplySlowProbeDamage(true,50,3.5f)) return;
		KnockbackProbeOrigin=NPC->GetActorLocation();Send(6,NPC);return;
	}
	if (KnockbackStage==6)
	{
		NPC->AddMovementInput(Player->GetActorRightVector());
		if (NC->GetSlowPresentationState().bActive)
		{
			if (Now-LightHitNextAt>.6 && !FMath::IsNearlyEqual(SlowMovement(NPC)->Velocity.Size2D(),150.f,15.f)) { Fail(TEXT("SlowNPCActualVelocity")); return; }
			return;
		}
		if (!bConfirmed) return;
		if (FVector::Dist2D(KnockbackProbeOrigin,NPC->GetActorLocation())<100) { Fail(TEXT("SlowNPCTravel")); return; }
		NPC->ConsumeMovementInputVector();SlowMovement(NPC)->StopMovementImmediately();Send(7,NPC);return;
	}
	if (KnockbackStage==7 && bConfirmed)
	{
		SlowASC(Player)->AddLooseGameplayTag(RPGSlow::ImmunityTag());
		if (!ApplySlowProbeDamage(false,50,3)) return;
		SlowASC(Player)->RemoveLooseGameplayTag(RPGSlow::ImmunityTag());
		if (PC->GetSlowPresentationState().bActive) { Fail(TEXT("SlowImmunity")); return; }
		Send(8,Player);return;
	}
	if (KnockbackStage==8 && bConfirmed)
	{
		if (!ApplySlowProbeDamage(false,40,5)) return;
		Send(9,Player);return;
	}
	if (KnockbackStage==9 && bConfirmed)
	{
		SlowASC(Player)->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(),1);
		if (!ApplySlowProbeDamage(false,40,5)) return;
		if (PC->GetSlowPresentationState().bActive || SlowASC(Player)->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute())!=1.f)
		{ Fail(TEXT("SlowDeathCleanup"));return; }
		Send(10,Player);return;
	}
	if (KnockbackStage==10 && bConfirmed)
	{
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_SLOW_COMPLETE PlayerNPC=1 NPCPlayer=1 OwnerObserver=1 ActualMovement=1 StrongestFallback=1 Cleanse=1 NaturalExpiry=1 Immunity=1 Death=1 StatusUI=1 LateJoin=1 SecurityViolations=0 Clients=2"));
		bFinished=true;CleanupLightHitInput();
	}
#endif
}
void URPGBossMapE2EProbeComponent::TickSlowClient()
{
#if !UE_BUILD_SHIPPING
	auto* PC=Cast<APlayerController>(GetOwner());auto* Player=LightHitPlayer.Get();if(!PC||!Player||!LightHitNPC.IsValid())return;
	const bool bOwner=PC->GetPawn()==Player;const int32 Step=KnockbackExpected;const double Now=FPlatformTime::Seconds();
	const bool bMoving=Step==2||Step==3||Step==4||Step==5;
	if(bOwner&&KnockbackInputStep!=Step)
	{
		KnockbackInputStep=Step;KnockbackInputAt=Now;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputAttackAction.Get());
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
		if(Step==0)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputAttackAction.Get(),FInputActionValue(true),{},{});
		if(bMoving)LightInputSubsystem->StartContinuousInputInjectionForAction(LightInputMoveAction.Get(),FInputActionValue(FVector2D(0,1)),{},{});
	}
	// Once observed, stop voluntary input while the reliable acknowledgement is delivered.
	if(bOwner&&KnockbackConfirmed==Step&&bMoving)LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	if(KnockbackConfirmed==Step)return;
	auto* Subject=Step==6||Step==7?LightHitNPC.Get():Player;auto* C=SlowControl(Subject);auto* ASC=SlowASC(Subject);
	const float Percent=Step==1||Step==2||Step==4||Step==6?50.f:Step==3?75.f:Step==9?40.f:0.f;
	const float Base=Subject==Player?400.f:300.f;const auto State=C->GetSlowPresentationState();
	if(Now-StunDiagnosticAt>.5)
	{
		StunDiagnosticAt=Now;
		UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_SLOW_WAIT Step=%d Owner=%d Active=%d Percent=%g Scale=%g Max=%g Speed=%g Accel=%g Mode=%d UI=%d"),
			Step,bOwner,State.bActive,State.Percent,ASC->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute()),SlowMovement(Subject)->GetMaxSpeed(),
			SlowMovement(Subject)->Velocity.Size2D(),SlowMovement(Subject)->GetCurrentAcceleration().Size2D(),int32(SlowMovement(Subject)->MovementMode),C->SlowStatusWidget!=nullptr);
	}
	if(State.bActive!=(Percent>0)||!FMath::IsNearlyEqual(State.Percent,Percent,.1f)||ASC->HasMatchingGameplayTag(RPGSlow::StatusTag())!=(Percent>0))return;
	if(!FMath::IsNearlyEqual(ASC->GetNumericAttribute(URPGAttributeSet::GetMoveSpeedAttribute()),1.f-Percent/100.f,.01f))return;
	if(Step!=10&&!FMath::IsNearlyEqual(SlowMovement(Subject)->GetMaxSpeed(),Base*(1.f-Percent/100.f),.1f))return;
	if(Step!=10&&RPGCombatControl::IsActionBlocked(Subject)) { Fail(TEXT("SlowUnexpectedActionBlock"));return; }
	if(Percent>0&&(!C->SlowStatusWidget||!C->SlowStatusWidget->GetUserWidgetObject()))return;
	if(Percent==0&&C->SlowStatusWidget)return;
	if(Step==0&&bOwner&&!ASC->FindAbilitySpecFromHandle(LightHitPlayerAbility)->IsActive())return;
	if(bOwner&&Step==1&&bLightInputCancelled){Fail(TEXT("SlowClientCombatCancelled"));return;}
	if(bOwner&&bMoving)
	{
		if(Now-KnockbackInputAt<.8)return;
		if(!FMath::IsNearlyEqual(SlowMovement(Player)->Velocity.Size2D(),Base*(1.f-Percent/100.f),10.f))return;
		LightInputSubsystem->StopContinuousInputInjectionForAction(LightInputMoveAction.Get());
	}
	if(Step==6&&!FMath::IsNearlyEqual(SlowMovement(Subject)->Velocity.Size2D(),150.f,20.f))return;
	if(Step==7&&FVector::Dist2D(Subject->GetActorLocation(),KnockbackExpectedLocation)>35.f)return;
	if(Step==10&&ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute())!=0)return;
	if(bOwner&&(Step==1||Step==6))
	{
		FString Directory;if(FParse::Value(FCommandLine::Get(),TEXT("RPGLightHitCaptureDir="),Directory))
			FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,FString::Printf(TEXT("Slow-Step%d.png"),Step)),true,false);
	}
	UE_LOG(LogTemp,Display,TEXT("RAIDMAP_E2E_SLOW_CLIENT Step=%d Percent=%g Owner=%d MaxSpeed=%g ActualSpeed=%g StatusUI=%d"),Step,Percent,bOwner,
		SlowMovement(Subject)->GetMaxSpeed(),SlowMovement(Subject)->Velocity.Size2D(),C->SlowStatusWidget!=nullptr);
	KnockbackConfirmed=Step;ServerConfirmKnockback(Step);
#endif
}
