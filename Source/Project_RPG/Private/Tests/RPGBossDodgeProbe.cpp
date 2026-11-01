#include "Tests/RPGBossMapE2EProbeComponent.h"

#include "Ability/RPGBossTelegraphedStrikeAbility.h"
#include "AbilitySystemComponent.h"
#include "AI/RPGBossAIComponent.h"
#include "AI/RPGBossThreatComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBossCharacter.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameMode/RPGBossBattleGameMode.h"
#include "GameState/RPGBossBattleGameState.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "RPGInputTags.h"
#include "MotionWarpingComponent.h"

namespace
{
	constexpr const TCHAR* RollClassPath = TEXT("/Game/Blueprints/Character/Player/Abilities/GA_Player_Roll.GA_Player_Roll_C");
	FGameplayAbilitySpec* FindRoll(UAbilitySystemComponent* ASC)
	{
		return ASC ? ASC->GetActivatableAbilities().FindByPredicate([](const FGameplayAbilitySpec& Spec)
		{
			return Spec.Ability && Spec.Ability->GetClass()->GetPathName() == RollClassPath;
		}) : nullptr;
	}
	float GetHP(UAbilitySystemComponent* ASC) { return ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()); }
	bool ReadRollWarpDirection(const ARPGPlayer* Player, FVector& Direction)
	{
		// CachedRollingDirection is a BP function local, not an instance property.
		// Inspect its actual persisted output consumed by the authored montage.
		const UMotionWarpingComponent* Warping = Player ? Player->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
		const FMotionWarpingTarget* Target = Warping ? Warping->FindWarpTarget(TEXT("RollingDirection")) : nullptr;
		if (!Target) { return false; }
		Direction = Target->GetTargetTrasform().GetRotation().GetForwardVector();
		return true;
	}
	bool HasBinding(const UEnhancedInputComponent* Input, const UInputAction* Action, ETriggerEvent Event)
	{
		if (!Input || !Action) { return false; }
		for (const auto& Binding : Input->GetActionEventBindings())
		{
			if (Binding && Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event) { return true; }
		}
		return false;
	}
}

void URPGBossMapE2EProbeComponent::CleanupDodgeProbe()
{
	if (DodgeInputSubsystem.IsValid())
	{
		if (DodgeMoveAction.IsValid()) { DodgeInputSubsystem->StopContinuousInputInjectionForAction(DodgeMoveAction.Get()); }
		if (DodgeRollAction.IsValid()) { DodgeInputSubsystem->StopContinuousInputInjectionForAction(DodgeRollAction.Get()); }
	}
	if (DodgeASC.IsValid()) { DodgeASC->OnAbilityEnded.Remove(DodgeEndedHandle); }
	if (DodgeStrikeEndedHandle.IsValid() && OriginalBoss.IsValid()) { OriginalBoss->GetAbilitySystemComponent()->OnAbilityEnded.Remove(DodgeStrikeEndedHandle); }
	DodgeEndedHandle.Reset();
	DodgeStrikeEndedHandle.Reset();
}

bool URPGBossMapE2EProbeComponent::PrepareDodgeAbilities()
{
	// Same explicit server-only fixture as the movement-security authored-roll test.
	// Never replace startup data, grant a synthetic ability, or activate it here.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARPGPlayer* Player = Cast<ARPGPlayer>(It->Get()->GetPawn());
		UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		if (!ASC || ASC->GetAvatarActor() != Player) { return false; }
		FGameplayAbilitySpec* Roll = FindRoll(ASC);
		if (!Roll)
		{
			UClass* Class = LoadClass<UGameplayAbility>(nullptr, RollClassPath);
			if (!Class) { Fail(TEXT("RaidDodgeRollClassMissing")); return false; }
			FGameplayAbilitySpec Grant(Class, 1);
			Grant.SourceObject = Player;
			Grant.GetDynamicSpecSourceTags().AddTag(RPGGameplayTags::InputTag_Roll);
			Roll = ASC->FindAbilitySpecFromHandle(ASC->GiveAbility(Grant));
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_FIXTURE Pawn=%s AuthoredRollGranted=1"), *Player->GetName());
		}
		if (!Roll || !Roll->GetDynamicSpecSourceTags().HasTagExact(RPGGameplayTags::InputTag_Roll))
		{
			Fail(TEXT("RaidDodgeRollInputTagMissing")); return false;
		}
	}
	return true;
}

void URPGBossMapE2EProbeComponent::HandleDodgeEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle == DodgeRollHandle) { bDodgeEndedNaturally = !Data.bWasCancelled; }
}

void URPGBossMapE2EProbeComponent::HandleDodgeStrikeEnded(const FAbilityEndedData& Data)
{
	if (Data.AbilitySpecHandle == DodgeStrikeHandle) { bDodgeStrikeEndedNaturally = !Data.bWasCancelled; }
}

void URPGBossMapE2EProbeComponent::TickDodgeLocalInput()
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController()) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	FGameplayAbilitySpec* Roll = FindRoll(ASC);
	if (!Roll || ASC->GetAvatarActor() != Player || !PC->GetLocalPlayer()) { return; }
	if (!DodgeInputSubsystem.IsValid())
	{
		const UDataAsset_InputConfig* Config = LoadObject<UDataAsset_InputConfig>(nullptr,
			TEXT("/Game/Blueprints/Character/Player/Input/DA_InputConfig.DA_InputConfig"));
		UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PC->InputComponent);
		const UInputAction* Move = Config ? Config->FindNativeInputActionByTag(RPGGameplayTags::InputTag_Move) : nullptr;
		const UInputAction* Action = Config ? Config->FindAbilityInputActionByTag(RPGGameplayTags::InputTag_Roll) : nullptr;
		if (!Input || !Subsystem) { return; }
		if (!Move || Move->ValueType != EInputActionValueType::Axis2D || !Action || Action->ValueType != EInputActionValueType::Boolean ||
			!HasBinding(Input, Move, ETriggerEvent::Triggered) || !HasBinding(Input, Action, ETriggerEvent::Started) || !HasBinding(Input, Action, ETriggerEvent::Completed))
		{
			Fail(TEXT("RaidDodgeAuthoredInputNotBound")); return;
		}
		DodgeInputSubsystem = Subsystem;
		DodgeMoveAction = Move;
		DodgeRollAction = Action;
	}
	if (!bDodgeReadyReported)
	{
		bDodgeReadyReported = true;
		ServerConfirmDodge(0, 0);
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_INPUT_READY BoundMove=1 BoundRoll=1"));
	}
	if (bDodgeInputCompleted) { ServerConfirmDodge(2, DodgeSequence); return; }
	if (!bDodgeInputRequested) { return; }
	if (Player != DodgeTarget.Get()) { Fail(TEXT("RaidDodgeLocalTargetChanged")); return; }
	if (!OriginalBoss.IsValid()) { Fail(TEXT("RaidDodgeBossLost")); return; }
	const double Now = FPlatformTime::Seconds();
	if (DodgeInputStartTime == 0.0)
	{
		DodgeASC = ASC;
		DodgeRollHandle = Roll->Handle;
		DodgeEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleDodgeEnded);
		DodgeInputStartTime = Now;
		FVector Away = (Player->GetActorLocation() - OriginalBoss.Get()->GetActorLocation()).GetSafeNormal2D();
		if (Away.IsNearlyZero()) { Away = -Player->GetActorForwardVector(); }
		const FRotationMatrix Basis(FRotator(0.f, PC->GetControlRotation().Yaw, 0.f));
		const FVector2D Value(FVector::DotProduct(Away, Basis.GetUnitAxis(EAxis::Y)), FVector::DotProduct(Away, Basis.GetUnitAxis(EAxis::X)));
		DodgeInputSubsystem->StartContinuousInputInjectionForAction(DodgeMoveAction.Get(), FInputActionValue(Value), {}, {});
	}
	if (!bDodgeInputSent && Now - DodgeInputStartTime >= .1)
	{
		bDodgeInputSent = true;
		DodgeInputSubsystem->StartContinuousInputInjectionForAction(DodgeRollAction.Get(), FInputActionValue(true), {}, {});
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_INPUT Sequence=%d EnhancedInput=1 ManualActivation=0 LastInput=%s"), DodgeSequence, *Player->GetLastMovementInputVector().ToString());
	}
	if (Now - DodgeInputStartTime >= .4)
	{
		DodgeInputSubsystem->StopContinuousInputInjectionForAction(DodgeMoveAction.Get());
		DodgeInputSubsystem->StopContinuousInputInjectionForAction(DodgeRollAction.Get());
	}
	bDodgeObservedActive |= Roll->IsActive();
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	UAnimMontage* Montage = ASC->GetCurrentMontage();
	bDodgeObservedMontage |= Roll->IsActive() && Anim && Montage && Montage->GetName() == TEXT("AM_Roll") && Anim->Montage_GetPosition(Montage) > .02f;
	if (bDodgeObservedActive && !Roll->IsActive())
	{
		if (!bDodgeEndedNaturally || !bDodgeObservedMontage) { Fail(TEXT("RaidDodgeLocalRollLifetimeFailed")); return; }
		bDodgeInputCompleted = true;
		CleanupDodgeProbe();
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_INPUT_COMPLETE Sequence=%d GAS=1 Montage=1 NaturalEnd=1"), DodgeSequence);
	}
	if (bDodgeInputCompleted) { ServerConfirmDodge(2, DodgeSequence); }
}

void URPGBossMapE2EProbeComponent::ClientObserveDodge_Implementation(APawn* Target, int32 Sequence, FVector Center, double EndTime, float TargetHealth, FVector Origin)
{
	if (!bEnabled || !bDodgeScenario || !Target) { return; }
	DodgeTarget = Target;
	DodgeSequence = Sequence;
	DodgeCenter = Center;
	DodgeEndTime = EndTime;
	DodgeHealth = TargetHealth;
	DodgeOrigin = Origin;
	OriginalBoss = GetWorld()->GetGameState<ARPGBossBattleGameState>()->GetBattleSnapshot().Boss;
}

void URPGBossMapE2EProbeComponent::ClientStartDodge_Implementation()
{
	if (bEnabled && bDodgeScenario && DodgeSequence > 0) { bDodgeInputRequested = true; }
}

void URPGBossMapE2EProbeComponent::TickDodgeObserver()
{
	if (DodgeSequence == 0 || !DodgeTarget.IsValid()) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(DodgeTarget.Get());
	if (!Player || !Player->GetAbilitySystemComponent() || !OriginalBoss.IsValid()) { return; }
	const double Now = GetWorld()->GetGameState()->GetServerWorldTimeSeconds();
	if (Now < DodgeEndTime + .2) { return; }
	const float Clearance = FVector::Dist2D(Player->GetActorLocation(), DodgeCenter);
	if (FVector::Dist2D(Player->GetActorLocation(), DodgeOrigin) <= 100.f || Clearance <= 200.f + Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + 25.f ||
		!FMath::IsNearlyEqual(GetHP(Player->GetAbilitySystemComponent()), DodgeHealth)) { return; }
	if (!bDodgeObserverCompleted)
	{
		bDodgeObserverCompleted = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_CLIENT_COMPLETE Sequence=%d MovementReplicated=1 HealthUnchanged=1 Clearance=%g"), DodgeSequence, Clearance);
	}
	ServerConfirmDodge(1, DodgeSequence);
}

void URPGBossMapE2EProbeComponent::TickDodgeServer()
{
	if (!bSawCombatDamage || CombatClients.Num() != 2 || !OriginalBoss.IsValid()) { return; }
	ARPGBossCharacter* Boss = OriginalBoss.Get();
	const FRPGBossTelegraphState Telegraph = Boss->BossAIComponent->GetTelegraphState();
	if (DodgeSequence == 0)
	{
		if (!Telegraph.bActive || Telegraph.Sequence <= FirstCombatSequence || Telegraph.ActionTag != FGameplayTag::RequestGameplayTag(TEXT("Boss.Action.SwordMan.Strike")) ||
			Telegraph.EndServerTime - GetWorld()->GetTimeSeconds() < .95) { return; }
		ARPGPlayer* Player = Cast<ARPGPlayer>(Boss->BossThreatComponent->GetCurrentTarget());
		APlayerController* TargetPC = Player ? Cast<APlayerController>(Player->GetController()) : nullptr;
		URPGBossMapE2EProbeComponent* TargetProbe = TargetPC ? TargetPC->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
		UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
		FGameplayAbilitySpec* Roll = FindRoll(ASC);
		if (!TargetProbe || !Roll || Roll->IsActive()) { return; }
		DodgeTarget = Player;
		DodgeASC = ASC;
		DodgeRollHandle = Roll->Handle;
		DodgeEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::HandleDodgeEnded);
		DodgeSequence = Telegraph.Sequence;
		DodgeCenter = Telegraph.AimLocation;
		DodgeEndTime = Telegraph.EndServerTime;
		DodgeHealth = GetHP(ASC);
		DodgeOrigin = Player->GetActorLocation();
		URPGSecurityValidationComponent* Security = Player->FindComponentByClass<URPGSecurityValidationComponent>();
		if (!Security) { Fail(TEXT("RaidDodgeSecurityMissing")); return; }
		DodgeViolationBaseline = Security->GetTotalViolationCount();
		for (const FGameplayAbilitySpec& Spec : Boss->GetAbilitySystemComponent()->GetActivatableAbilities())
		{
			if (const auto* Attack = Cast<URPGBossTelegraphedStrikeAbility>(Spec.Ability); Attack && Attack->ActionTag == Telegraph.ActionTag && Spec.IsActive()) { DodgeStrikeHandle = Spec.Handle; }
		}
		if (!DodgeStrikeHandle.IsValid()) { Fail(TEXT("RaidDodgeStrikeNotActive")); return; }
		if (FVector::Dist2D(DodgeOrigin, DodgeCenter) > 200.f) { Fail(TEXT("RaidDodgeTargetAlreadyOutsideStrike")); return; }
		DodgeStrikeEndedHandle = Boss->GetAbilitySystemComponent()->OnAbilityEnded.AddUObject(this, &ThisClass::HandleDodgeStrikeEnded);
		for (const auto& PC : ReadyClients)
		{
			PC->FindComponentByClass<URPGBossMapE2EProbeComponent>()->ClientObserveDodge(Player, DodgeSequence, DodgeCenter, DodgeEndTime, DodgeHealth, DodgeOrigin);
		}
		if (TargetPC->IsLocalController()) { TargetProbe->ClientObserveDodge(Player, DodgeSequence, DodgeCenter, DodgeEndTime, DodgeHealth, DodgeOrigin); }
		TargetProbe->ClientStartDodge();
		Deadline = FPlatformTime::Seconds() + 15.0;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_SERVER_REQUEST Sequence=%d Target=%s Host=%d NaturalTarget=1 Center=%s Origin=%s"), DodgeSequence, *Player->GetName(), int32(TargetPC->IsLocalController()), *DodgeCenter.ToString(), *DodgeOrigin.ToString());
	}
	ARPGPlayer* Player = Cast<ARPGPlayer>(DodgeTarget.Get());
	if (!Player || !DodgeASC.IsValid() || Boss->BossThreatComponent->GetCurrentTarget() != Player || Boss->BossAIComponent->GetBossState().PhaseIndex != 0)
	{
		Fail(TEXT("RaidDodgeAttackContextChanged")); return;
	}
	URPGSecurityValidationComponent* Security = Player->FindComponentByClass<URPGSecurityValidationComponent>();
	FGameplayAbilitySpec* Roll = DodgeASC->FindAbilitySpecFromHandle(DodgeRollHandle);
	FVector CachedDirection = FVector::ZeroVector;
	const bool bHasDirection = ReadRollWarpDirection(Player, CachedDirection);
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_SERVER_SAMPLE Left=%g Location=%s Distance=%g HP=%g Before=%g Violations=%d Roll=%d Authorized=%d LastInput=%s Acceleration=%s CachedDirection=%s"),
		DodgeEndTime - GetWorld()->GetTimeSeconds(), *Player->GetActorLocation().ToString(), FVector::Dist2D(Player->GetActorLocation(), DodgeCenter), GetHP(DodgeASC.Get()), DodgeHealth,
		Security ? Security->GetTotalViolationCount() - DodgeViolationBaseline : -1, Roll ? int32(Roll->IsActive()) : -1, Security ? int32(Security->IsMovementAuthorizationActive()) : -1,
		*Player->GetLastMovementInputVector().ToString(), *Player->GetCharacterMovement()->GetCurrentAcceleration().ToString(), *CachedDirection.ToString());
	if (!Roll || !Security || Security->GetTotalViolationCount() != DodgeViolationBaseline || !FMath::IsNearlyEqual(GetHP(DodgeASC.Get()), DodgeHealth))
	{
		Fail(TEXT("RaidDodgeDamageOrSecurityViolation")); return;
	}
	UAnimInstance* Anim = Player->GetMesh()->GetAnimInstance();
	UAnimMontage* Montage = DodgeASC->GetCurrentMontage();
	const bool bRollMontagePlaying = Roll->IsActive() && Anim && Montage && Montage->GetName() == TEXT("AM_Roll") &&
		Anim->Montage_GetPosition(Montage) > .02f && Player->GetMesh()->IsPlayingRootMotion();
	// GAS may report active before BP activation has reached direction calculation.
	// Check the warp output once the actual montage is consuming it, not earlier.
	if (bRollMontagePlaying && !bDodgeObservedMontage)
	{
		if (!bHasDirection) { Fail(TEXT("RaidDodgeRollWarpTargetMissing")); return; }
		const FVector Expected = (DodgeOrigin - Boss->GetActorLocation()).GetSafeNormal2D();
		if (CachedDirection.ContainsNaN() || FVector::DotProduct(CachedDirection.GetSafeNormal2D(), Expected) < .9f)
		{
			UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_DIRECTION Expected=%s Actual=%s"), *Expected.ToString(), *CachedDirection.ToString());
			Fail(TEXT("RaidDodgeServerRollDirectionMismatch")); return;
		}
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_DIRECTION_MATCH Server=1 Direction=%s"), *CachedDirection.ToString());
	}
	bDodgeObservedActive |= Roll->IsActive();
	bDodgeObservedAuthorization |= Roll->IsActive() && Security->IsMovementAuthorizationActive() && Security->GetAuthorizedMovementReason() == TEXT("PlayerRoll");
	bDodgeObservedMontage |= bRollMontagePlaying;
	if (!bDodgeServerValidated && GetWorld()->GetTimeSeconds() >= DodgeEndTime + .2)
	{
		if (Roll->IsActive()) { return; }
		const FGameplayAbilitySpec* Strike = Boss->GetAbilitySystemComponent()->FindAbilitySpecFromHandle(DodgeStrikeHandle);
		const float Clearance = FVector::Dist2D(Player->GetActorLocation(), DodgeCenter);
		if (!bDodgeObservedActive || !bDodgeObservedAuthorization || !bDodgeObservedMontage || !bDodgeEndedNaturally || Roll->IsActive() ||
			Security->IsMovementAuthorizationActive() || !Strike || Strike->IsActive() || !bDodgeStrikeEndedNaturally ||
			Telegraph.Sequence != DodgeSequence || Telegraph.bActive ||
			Clearance <= 200.f + Player->GetCapsuleComponent()->GetScaledCapsuleRadius() + 25.f || FVector::Dist2D(Player->GetActorLocation(), DodgeOrigin) <= 100.f)
		{
			Fail(TEXT("RaidDodgeDidNotEscapeAuthoredStrike")); return;
		}
		bDodgeServerValidated = true;
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_SERVER_RESULT Sequence=%d GAS=1 RootMotion=1 Authorized=1 NaturalEnd=1 AuthorizationRevoked=1 StrikeCompleted=1 HealthUnchanged=1 Clearance=%g"), DodgeSequence, Clearance);
	}
	if (bDodgeServerValidated && bDodgeTargetInputConfirmed && DodgeObserverClients.Num() == 2)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_DODGE_SERVER_COMPLETE Input=1 GAS=1 Escaped=1 NoDamage=1 Clients=2"));
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_NATURAL_SERVER_COMPLETE Navigation=1 BTMove=1 GASDamage=1 Clients=2"));
		bFinished = true;
		CleanupDodgeProbe();
		SetComponentTickEnabled(false);
	}
}

void URPGBossMapE2EProbeComponent::ServerConfirmDodge_Implementation(int32 Checkpoint, int32 Sequence)
{
	if (!bEnabled || !bDodgeScenario) { return; }
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	ARPGBossBattleGameMode* Mode = GetWorld()->GetAuthGameMode<ARPGBossBattleGameMode>();
	URPGBossMapE2EProbeComponent* Driver = Mode ? Mode->FindComponentByClass<URPGBossMapE2EProbeComponent>() : nullptr;
	if (!PC || !Driver || Driver->bFinished || !Driver->bDodgeScenario) { return; }
	ARPGPlayer* Player = Cast<ARPGPlayer>(PC->GetPawn());
	if (Checkpoint == 0 && !PC->IsLocalController() && Player && FindRoll(Player->GetAbilitySystemComponent())) { Driver->DodgeReadyClients.Add(PC); }
	else if (Sequence > 0 && Sequence == Driver->DodgeSequence && Driver->ServerStage == 2)
	{
		if (Checkpoint == 1 && Driver->bDodgeServerValidated && Driver->ReadyClients.Contains(PC)) { Driver->DodgeObserverClients.Add(PC); }
		if (Checkpoint == 2 && PC->GetPawn() == Driver->DodgeTarget.Get()) { Driver->bDodgeTargetInputConfirmed = true; }
	}
}
