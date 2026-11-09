#include "Component/Combat/RPGCombatControlComponent.h"
#include "Riding/RPGRidingDamageScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Component/Combat/RPGCombatMotion.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/RPGApprovedHitContext.h"
#include "Combat/RPGAirbornePresentationProfile.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameplayEffect/RPGAirborneEffect.h"

using namespace RPGCombatMotion;
namespace
{
	bool OwnerMoveAge(const ACharacter* Character, UCharacterMovementComponent* Movement,
		const FRPGAirborneState& State, double& Age)
	{
		if (!Character || Character->GetLocalRole() != ROLE_AutonomousProxy || !Movement || State.MovementStartClientTime < 0.f) return false;
		const auto* Prediction = Movement->GetPredictionData_Client_Character();
		const double MoveAge = Prediction ? Prediction->CurrentTimeStamp - State.MovementStartClientTime : -1.;
		// A reset/stale timestamp cannot produce an unbounded extrapolation.
		if (MoveAge < 0. || !FMath::IsFinite(MoveAge) || MoveAge >= State.MaximumFlightDuration) return false;
		Age = MoveAge; return true;
	}
	FName AirMotionName(const FRPGAirborneState& State)
	{
		return FName(*FString::Printf(TEXT("RPG_Airborne_%u_%u"), State.RecoveryRevision, State.ExecutionId));
	}
	bool IsSequenceEffect(const UClass* Class)
	{
		return Class == URPGKnockdownSequenceEffect::StaticClass() || Class == URPGAirborneEffect::StaticClass()
			|| Class == URPGDownedEffect::StaticClass() || Class == URPGGettingUpEffect::StaticClass();
	}
}

bool URPGCombatControlComponent::ApplyApprovedDamageAirborne(AActor* Source, const FHitResult& Hit,
	const FGuid& Receipt, const FRPGKnockbackSettings& Settings, const FVector& AwayDirection)
{
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	if (bEnding || bApplyingAirborne || bEndingAirborne || bChangingAirbornePhase || bApplyingKnockback || bEndingKnockback || !Character || !Character->HasAuthority()
		|| !Source || !Source->HasAuthority() || Source == Character || Source->GetWorld() != GetWorld()
		|| Hit.GetActor() != Character || !Receipt.IsValid() || !Settings.IsAirborne() || !Supported(Movement)
		|| !Character->GetActorEnableCollision() || AwayDirection.ContainsNaN()
		|| !FMath::IsNearlyEqual(AwayDirection.SizeSquared2D(), 1., .01) || !FMath::IsNearlyZero(AwayDirection.Z)
		|| !IsLivingAvatar(Source) || !IsLivingAvatar(Character)) return false;
	const float Gravity = Movement->GetGravityZ();
	if (!FMath::IsFinite(Gravity) || Gravity >= -1.f || !Movement->GetGravityDirection().Equals(FVector(0, 0, -1))) return false;
	const float Duration = FMath::Sqrt(8.f * Settings.Height / -Gravity);
	if (!FMath::IsFinite(Duration) || Duration < .2f || Duration > 4.f || Settings.Distance / Duration > 2500.f) return false;
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Character);
	InitializeWithAbilitySystem(*ASC);
	if (RecentAirborneHits.Contains(Receipt)) return false;
	if (RecentAirborneHits.Num() >= 64) RecentAirborneHits.RemoveAt(0);
	RecentAirborneHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::Airborne) || HasUncancellableCombatAbility()) return false;
	TGuardValue<bool> Applying(bApplyingAirborne, true);
	FRPGRidingDamageScope RidingOutcome(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source), ASC);
	const FRPGApprovedHitContext Lifetime(Source, Character);
	EndAirborne(ERPGAirborneEndReason::Replaced);
	EndKnockback(ERPGKnockbackEndReason::Replaced);
	if (!Lifetime.IsCurrent(Source, Character)) return false;
	const bool bCancellingMontage = RPGCombatControl::IsStunAffectedAbility(ASC->GetAnimatingAbility());
	TWeakObjectPtr<UAnimInstance> InterruptedAnim = Character->GetMesh()->GetAnimInstance();
	TWeakObjectPtr<UAnimMontage> InterruptedMontage = ASC->GetCurrentMontage();
	const auto* Interrupted = InterruptedAnim.IsValid() && InterruptedMontage.IsValid()
		? InterruptedAnim->GetActiveInstanceForMontage(InterruptedMontage.Get()) : nullptr;
	const int32 InterruptedId = Interrupted ? Interrupted->GetInstanceID() : INDEX_NONE;
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source); Context.AddSourceObject(this); Context.AddHitResult(Hit);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(GetDefault<URPGKnockdownSequenceEffect>(), Context, 1.f));
	const auto* Applied = ASC->GetActiveGameplayEffect(Handle);
	if (!Handle.IsValid() || !Applied) return false;
	if (!Lifetime.IsCurrent(Source, Character) || BoundASC.Get() != ASC || bEnding)
	{ ASC->RemoveActiveGameplayEffect(Handle); return false; }
	OwnedAirborneSequence = Handle;
	if (Applied->bIsInhibited || !Supported(Movement) || HasUncancellableCombatAbility())
	{ EndAirborne(ERPGAirborneEndReason::Cleansed); return false; }
	const auto* Remaining = InterruptedAnim.IsValid() && InterruptedMontage.IsValid()
		? InterruptedAnim->GetActiveInstanceForMontage(InterruptedMontage.Get()) : nullptr;
	if (bCancellingMontage && !ASC->GetAnimatingAbility() && Remaining && Remaining->GetInstanceID() == InterruptedId
		&& Character->GetMesh()->IsPlayingRootMotion()) InterruptedAnim->Montage_Stop(0.f, InterruptedMontage.Get());
	if (auto* Riding = Character->FindComponentByClass<URPGRidingComponent>()) Riding->StopServerDashForControl();
	if (bEnding || BoundASC.Get() != ASC || !Lifetime.IsCurrent(Source, Character))
	{ EndAirborne(ERPGAirborneEndReason::LifeChanged); return false; }
	if (ForeignMotion(Movement, NAME_None)) { EndAirborne(ERPGAirborneEndReason::ForeignMovement); return false; }
	auto* Security = Character->FindComponentByClass<URPGSecurityValidationComponent>();
	if ((Character->IsPlayerControlled() && !Security) || (Security && Security->IsMovementAuthorizationActive()))
	{ EndAirborne(ERPGAirborneEndReason::ForeignMovement); return false; }
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleAirborneRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleAirborneInhibited);
	AirborneState.ExecutionId = ++AirborneSequence;
	AirborneState.RecoveryRevision = RecoveryRevision();
	AirborneState.Distance = Settings.Distance; AirborneState.Height = Settings.Height;
	AirborneState.Yaw = AwayDirection.Rotation().Yaw;
	AirborneState.ArcDuration = Duration;
	AirborneState.MaximumFlightDuration = Duration + 2.f;
	AirborneState.DownDuration = Settings.DownDuration; AirborneState.GetUpDuration = Settings.GetUpDuration;
	AirborneState.StartServerTime = ServerNow(GetWorld());
	AirborneState.MovementStartClientTime = -1.f;
	AirborneState.EndReason = ERPGAirborneEndReason::None;
	if (Security)
	{
		// ValidateMovement consumes a 3D distance budget. Bound both the arc and its horizontal travel.
		AirborneMovementAuthorization = Security->BeginMovementAuthorization(Duration + 2.2f,
			Settings.Distance + 2.f * Settings.Height + 25.f, TEXT("RPG.Airborne"));
		if (!AirborneMovementAuthorization.IsValid()) { EndAirborne(ERPGAirborneEndReason::UnsupportedMovement); return false; }
	}
	bAirborneHasMoved = false;
	AirborneLastLocation = Character->GetActorLocation();
	DisplacementLastBaseMovementTotal = Movement->GetServerBasedMovementTotal();
	Character->LandedDelegate.AddUniqueDynamic(this, &ThisClass::OnAirborneLanded);
	Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::OnAirborneMovementUpdated);
	if (!ChangeAirbornePhase(ERPGAirbornePhase::Airborne)) return false;
	Movement->SetMovementMode(MOVE_Falling);
	AirborneState.bMotionActive = true;
	ApplyAirborneMotion();
	if (!HasAirborneMotion()) { EndAirborne(ERPGAirborneEndReason::UnsupportedMovement); return false; }
	RidingOutcome.CompleteControl(Handle);
	if (bEnding || BoundASC.Get() != ASC || !Lifetime.IsCurrent(Source, Character) || !HasAirborneMotion())
	{ if (OwnedAirborneSequence == Handle) EndAirborne(ERPGAirborneEndReason::LifeChanged); return false; }
	PublishAirborneState();
	return AirborneState.IsActive() && Lifetime.IsCurrent(Source, Character);
}

void URPGCombatControlComponent::PublishAirborneState()
{
	GetOwner()->ForceNetUpdate(); SetComponentTickEnabled(true);
	OnAirborneStateChanged.Broadcast(AirborneState); UpdateAirbornePose();
}

bool URPGCombatControlComponent::ChangeAirbornePhase(ERPGAirbornePhase Phase)
{
	if (bEnding || bEndingAirborne || bChangingAirbornePhase || !BoundASC.IsValid()) return false;
	TGuardValue<bool> Changing(bChangingAirbornePhase, true);
	auto* ASC = BoundASC.Get();
	const auto* Root = ASC->GetActiveGameplayEffect(OwnedAirborneSequence);
	if (!Root || Root->bIsInhibited) { EndAirborne(ERPGAirborneEndReason::Cleansed); return false; }
	const auto Context = Root->Spec.GetContext();
	const uint32 Epoch = LifetimeEpoch, Id = AirborneState.ExecutionId;
	const auto Previous = OwnedAirbornePhase; OwnedAirbornePhase = {};
	if (Previous.IsValid()) ASC->RemoveActiveGameplayEffect(Previous);
	if (bEnding || BoundASC.Get() != ASC || Epoch != LifetimeEpoch || Id != AirborneState.ExecutionId
		|| !IsLivingAvatar(GetOwner()) || !ASC->GetActiveGameplayEffect(OwnedAirborneSequence)) return false;
	const UGameplayEffect* Definition = Phase == ERPGAirbornePhase::Airborne ? static_cast<const UGameplayEffect*>(GetDefault<URPGAirborneEffect>())
		: Phase == ERPGAirbornePhase::Downed ? static_cast<const UGameplayEffect*>(GetDefault<URPGDownedEffect>()) : GetDefault<URPGGettingUpEffect>();
	FGameplayEffectSpec Spec(Definition, Context, 1.f);
	if (Phase != ERPGAirbornePhase::Airborne) Spec.SetDuration(Phase == ERPGAirbornePhase::Downed ? AirborneState.DownDuration : AirborneState.GetUpDuration, true);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	const auto* Effect = ASC->GetActiveGameplayEffect(Handle);
	if (bEnding || BoundASC.Get() != ASC || Epoch != LifetimeEpoch || Id != AirborneState.ExecutionId
		|| !IsLivingAvatar(GetOwner()) || !Effect || Effect->bIsInhibited || !ASC->GetActiveGameplayEffect(OwnedAirborneSequence))
	{
		ASC->RemoveActiveGameplayEffect(Handle);
		EndAirborne(ERPGAirborneEndReason::Cleansed); return false;
	}
	OwnedAirbornePhase = Handle;
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleAirborneRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleAirborneInhibited);
	StopAirbornePose();
	AirborneState.Phase = Phase; ++AirborneState.PhaseRevision;
	AirborneState.PhaseStartServerTime = ServerNow(GetWorld());
	AirborneState.PhaseEndServerTime = Phase == ERPGAirbornePhase::Airborne
		? AirborneState.StartServerTime + AirborneState.MaximumFlightDuration
		: AirborneState.PhaseStartServerTime + Effect->GetTimeRemaining(GetWorld()->GetTimeSeconds());
	PublishAirborneState(); return AirborneState.Phase == Phase;
}

bool URPGCombatControlComponent::HasAirborneMotion() const
{
	if (bEnding || AirborneState.Phase != ERPGAirbornePhase::Airborne || !AirborneState.bMotionActive
		|| AirborneState.RecoveryRevision != RecoveryRevision()) return false;
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	double Elapsed = ServerNow(GetWorld()) - AirborneState.StartServerTime;
	OwnerMoveAge(Character, Movement, AirborneState, Elapsed);
	if (Elapsed >= AirborneState.ArcDuration) return false;
	const auto Motion = Movement ? Movement->GetRootMotionSource(AirMotionName(AirborneState)) : nullptr;
	return Motion.IsValid() && !Motion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
		&& !Motion->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
}

void URPGCombatControlComponent::ApplyAirborneMotion()
{
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const auto Name = AirMotionName(AirborneState);
	if (AirborneMotionName != Name) StopAirborneMotion();
	const double Now = ServerNow(GetWorld());
	double Elapsed = Now - AirborneState.StartServerTime;
	// CMC may correct the owner's position before this snapshot arrives. Use
	// its move-clock origin instead of replaying ascent from a lagging GameState.
	const bool bUseMoveClock = OwnerMoveAge(Character, Movement, AirborneState, Elapsed);
	if (!bUseMoveClock && Now < AirborneState.StartServerTime) return; // Wait for an initial GameState clock.
	if (!Supported(Movement) || AirborneState.Phase != ERPGAirbornePhase::Airborne || !AirborneState.bMotionActive
		|| AirborneState.RecoveryRevision != RecoveryRevision() || Elapsed >= AirborneState.ArcDuration) return;
	if (HasAirborneMotion())
	{
		// CharacterMovement may replicate its source before this component snapshot.
		AirborneMotionName = Name;
		return;
	}
	if (ForeignMotion(Movement, Name)) return;
	auto Motion = MakeShared<FRootMotionSource_JumpForce>();
	Motion->InstanceName = Name; Motion->Priority = 200; Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
	Motion->Rotation = FRotator(0.f, AirborneState.Yaw, 0.f);
	Motion->Distance = AirborneState.Distance; Motion->Height = AirborneState.Height; Motion->Duration = AirborneState.ArcDuration;
	Motion->bDisableTimeout = false;
	Motion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
	Motion->SetTime(static_cast<float>(Elapsed));
	if (bUseMoveClock) Motion->StartTime = AirborneState.MovementStartClientTime;
	AirborneMotionName = Name; Movement->ApplyRootMotionSource(Motion);
	if (Character->HasAuthority() && Character->IsPlayerControlled() && !Character->IsLocallyControlled()
		&& Motion->IsStartTimeValid()) AirborneState.MovementStartClientTime = Motion->GetStartTime();
}

void URPGCombatControlComponent::StopAirborneMotion()
{
	Retire(Cast<ACharacter>(GetOwner()), AirborneMotionName); AirborneMotionName = NAME_None;
}
void URPGCombatControlComponent::ReleaseAirborneAuthorization()
{
	if (auto* Security = GetOwner()->FindComponentByClass<URPGSecurityValidationComponent>())
		Security->EndMovementAuthorization(AirborneMovementAuthorization, true);
	AirborneMovementAuthorization.Invalidate();
}

void URPGCombatControlComponent::EndAirborne(ERPGAirborneEndReason Reason)
{
	if (bEndingAirborne) return;
	TGuardValue<bool> Ending(bEndingAirborne, true);
	auto* Character = Cast<ACharacter>(GetOwner());
	if (Character)
	{
		Character->LandedDelegate.RemoveDynamic(this, &ThisClass::OnAirborneLanded);
		Character->OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::OnAirborneMovementUpdated);
	}
	StopAirborneMotion(); StopAirbornePose();
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	ReleaseAirborneAuthorization();
	const bool bWasActive = AirborneState.IsActive();
	AirborneState.Phase = ERPGAirbornePhase::None; AirborneState.bMotionActive = false;
	AirborneState.EndReason = Reason;
	TSet<FActiveGameplayEffectHandle> Handles;
	if (OwnedAirborneSequence.IsValid()) Handles.Add(OwnedAirborneSequence);
	if (OwnedAirbornePhase.IsValid()) Handles.Add(OwnedAirbornePhase);
	OwnedAirborneSequence = {}; OwnedAirbornePhase = {};
	if (BoundASC.IsValid())
		for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
		{
			const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
			if (Effect && IsSequenceEffect(Effect->Spec.Def->GetClass()) && Effect->Spec.GetContext().GetSourceObject() == this) Handles.Add(Handle);
		}
	for (auto Handle : Handles) if (BoundASC.IsValid()) BoundASC->RemoveActiveGameplayEffect(Handle);
	if (bWasActive) PublishAirborneState();
}

bool URPGCombatControlComponent::CancelAirborne(uint32 ExecutionId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !AirborneState.IsActive() || AirborneState.ExecutionId != ExecutionId) return false;
	EndAirborne(ERPGAirborneEndReason::Cleansed); return true;
}

void URPGCombatControlComponent::HandleAirborneRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (bEndingAirborne || !Info.ActiveEffect) return;
	const auto Handle = Info.ActiveEffect->Handle;
	if (Handle == OwnedAirborneSequence) { EndAirborne(ERPGAirborneEndReason::Cleansed); return; }
	if (Handle != OwnedAirbornePhase || bChangingAirbornePhase) return;
	OwnedAirbornePhase = {};
	if (Info.bPrematureRemoval || AirborneState.Phase == ERPGAirbornePhase::Airborne)
	{ EndAirborne(ERPGAirborneEndReason::Cleansed); return; }
	auto* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->GetCharacterMovement()->IsMovingOnGround())
	{ EndAirborne(ERPGAirborneEndReason::UnsupportedMovement); return; }
	if (AirborneState.Phase == ERPGAirbornePhase::Downed) ChangeAirbornePhase(ERPGAirbornePhase::GettingUp);
	else EndAirborne(ERPGAirborneEndReason::Completed);
}
void URPGCombatControlComponent::HandleAirborneInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited)
{
	if (bInhibited && (Handle == OwnedAirborneSequence || Handle == OwnedAirbornePhase)) EndAirborne(ERPGAirborneEndReason::Cleansed);
}

void URPGCombatControlComponent::OnAirborneLanded(const FHitResult& Hit)
{
	auto* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !Character->HasAuthority() || AirborneState.Phase != ERPGAirbornePhase::Airborne
		|| !bAirborneHasMoved || !Hit.bBlockingHit || !Character->GetCharacterMovement()->IsWalkable(Hit)) return;
	AirborneState.bMotionActive = false; StopAirborneMotion(); ReleaseAirborneAuthorization();
	ChangeAirbornePhase(ERPGAirbornePhase::Downed);
}

void URPGCombatControlComponent::HandleAirborneImpact(const FHitResult& Hit)
{
	if (!GetOwner()->HasAuthority() || !AirborneState.bMotionActive || AirborneState.Phase != ERPGAirbornePhase::Airborne || !Hit.bBlockingHit) return;
	const FVector Direction = FRotator(0.f, AirborneState.Yaw, 0.f).Vector();
	if (Hit.ImpactNormal.Z < -.2f || FVector::DotProduct(Hit.ImpactNormal, Direction) < -.2f)
	{
		AirborneState.bMotionActive = false; StopAirborneMotion(); PublishAirborneState();
		// Native falling physics handles the ceiling/wall and subsequent real landing.
	}
}

void URPGCombatControlComponent::OnAirborneMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!AirborneState.IsActive() || !GetOwner()->HasAuthority()) return;
	const FVector Location = GetOwner()->GetActorLocation();
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Movement = Character ? Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	const FVector BaseTotal = Movement ? Movement->GetServerBasedMovementTotal() : FVector::ZeroVector;
	// Remote authority also applies based movement between client moves, outside
	// PerformMovement. Remove only that measured carry; CMC's teleport flag and
	// LastUpdateLocation cannot distinguish it from a genuine external move.
	const FVector PreviousLocation = OldLocation - (BaseTotal - DisplacementLastBaseMovementTotal);
	if (OldLocation.ContainsNaN() || Location.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f
		|| PreviousLocation.ContainsNaN() || FVector::Dist(AirborneLastLocation, PreviousLocation) > 25.f
		|| FVector::Dist2D(OldLocation, Location) > AirborneState.Distance / AirborneState.ArcDuration * DeltaSeconds + 25.f)
	{ EndAirborne(ERPGAirborneEndReason::ForeignMovement); return; }
	if (AirborneState.Phase == ERPGAirbornePhase::Airborne && (Location.Z > OldLocation.Z + .01f || OldVelocity.Z < -.01f)) bAirborneHasMoved = true;
	AirborneLastLocation = Location;
	DisplacementLastBaseMovementTotal = BaseTotal;
}

void URPGCombatControlComponent::TickAirborne()
{
	if (!AirborneState.IsActive()) return;
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (GetOwner()->HasAuthority())
	{
		if (!IsLivingAvatar(GetOwner()) || AirborneState.RecoveryRevision != RecoveryRevision()) EndAirborne(ERPGAirborneEndReason::LifeChanged);
		else if (!Supported(Movement) || (AirborneState.Phase != ERPGAirbornePhase::Airborne && !Movement->IsMovingOnGround())) EndAirborne(ERPGAirborneEndReason::UnsupportedMovement);
		else if (ForeignMotion(Movement, AirborneMotionName)) EndAirborne(ERPGAirborneEndReason::ForeignMovement);
		else if (!BoundASC.IsValid() || !BoundASC->GetActiveGameplayEffect(OwnedAirborneSequence) || !BoundASC->GetActiveGameplayEffect(OwnedAirbornePhase)) EndAirborne(ERPGAirborneEndReason::Cleansed);
		else if (AirborneState.Phase == ERPGAirbornePhase::Airborne)
		{
			if (ServerNow(GetWorld()) >= AirborneState.PhaseEndServerTime) EndAirborne(ERPGAirborneEndReason::LandingTimeout);
			else if (AirborneState.bMotionActive && !HasAirborneMotion())
			{ AirborneState.bMotionActive = false; StopAirborneMotion(); PublishAirborneState(); }
		}
	}
	else if (!IsLivingAvatar(GetOwner()) || AirborneState.RecoveryRevision != RecoveryRevision() || ServerNow(GetWorld()) >= AirborneState.PhaseEndServerTime)
	{ StopAirborneMotion(); StopAirbornePose(); return; }
	else ApplyAirborneMotion();
	UpdateAirbornePose();
}

void URPGCombatControlComponent::OnRep_AirborneState()
{
	if (!AirborneState.IsActive() || !AirborneState.bMotionActive || AirborneMotionName != AirMotionName(AirborneState)) StopAirborneMotion();
	if (AirbornePoseRevision != AirborneState.PhaseRevision) StopAirbornePose();
	if (AirborneState.IsActive()) ApplyAirborneMotion();
	SetComponentTickEnabled(AirborneState.IsActive() || StunState.bActive || KnockbackState.bActive || SlowState.bActive || BurnState.Stacks > 0 || StaggerState.bActive || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());
	OnAirborneStateChanged.Broadcast(AirborneState); UpdateAirbornePose();
}

void URPGCombatControlComponent::StopAirbornePose()
{
	auto* Anim = AirborneAnimInstance.Get(); auto* Montage = AirborneMontage.Get();
	const auto* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	const bool bOwned = Instance && Instance->GetInstanceID() == AirborneMontageInstanceId;
	AirborneAnimInstance.Reset(); AirborneMontage.Reset(); AirborneMontageInstanceId = INDEX_NONE;
	if (bOwned) Anim->Montage_Stop(.05f, Montage);
}

void URPGCombatControlComponent::UpdateAirbornePose()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	const double Now = ServerNow(GetWorld());
	if (bEnding || !AirborneState.IsActive() || !IsLivingAvatar(GetOwner()) || AirborneState.RecoveryRevision != RecoveryRevision()
		|| Now >= AirborneState.PhaseEndServerTime) { StopAirbornePose(); return; }
	if (Now < AirborneState.PhaseStartServerTime) return;
	auto* Character = Cast<ACharacter>(GetOwner()); auto* Mesh = Character ? Character->GetMesh() : nullptr;
	auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (AirbornePoseRevision != 0 && AirborneAnimInstance.Get() != Anim)
	{
		// Initial actor/mesh replication can replace the animation instance after
		// this phase was received. Retire only the old instance that we owned.
		StopAirbornePose(); AirbornePoseRevision = 0;
	}
	const bool bSamePhase = AirbornePoseRevision == AirborneState.PhaseRevision;
	const auto* Profile = AirbornePresentationProfile.Get();
	if (!Profile || !Anim || !Mesh->GetSkeletalMeshAsset() || (!bSamePhase && Anim->IsAnyMontagePlaying())) return;
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (ASC && ASC->GetAnimatingAbility()) return;
	const auto& Selected = AirborneState.Phase == ERPGAirbornePhase::Airborne ? Profile->Airborne
		: AirborneState.Phase == ERPGAirbornePhase::Downed ? Profile->Downed : Profile->GettingUp;
	if (!URPGCombatReactionComponent::IsSafeCosmeticMontage(Selected.Montage, Selected.Section)
		|| Mesh->GetSkeletalMeshAsset()->GetSkeleton() != Selected.Montage->GetSkeleton()) return;
	const int32 Section = Selected.Section.IsNone() ? 0 : Selected.Montage->GetSectionIndex(Selected.Section);
	float Start = 0.f, End = 0.f; Selected.Montage->GetSectionStartAndEndTime(Section, Start, End);
	if (!FMath::IsFinite(Start) || !FMath::IsFinite(End) || End <= Start) return;
	const bool bLoop = AirborneState.Phase != ERPGAirbornePhase::GettingUp;
	const float Rate = bLoop ? 1.f : FMath::Clamp((End - Start) / AirborneState.GetUpDuration, .1f, 4.f);
	const float Elapsed = (Now - AirborneState.PhaseStartServerTime) * Rate;
	const float Offset = bLoop ? FMath::Fmod(Elapsed, End - Start) : Elapsed;
	if (bSamePhase)
	{
		const auto* Owned = Anim->GetActiveInstanceForMontage(Selected.Montage);
		if (AirborneAnimInstance.Get() != Anim || AirborneMontage.Get() != Selected.Montage
			|| !Owned || Owned->GetInstanceID() != AirborneMontageInstanceId || !Anim->Montage_IsPlaying(Selected.Montage)) return;
		float Error = FMath::Abs(Anim->Montage_GetPosition(Selected.Montage) - Start - Offset);
		if (bLoop) Error = FMath::Min(Error, FMath::Abs(End - Start - Error));
		// The initial server-time estimate can change after phase replication.
		// Seek the exact cosmetic instance we own; preserve interrupted/foreign plays.
		if (Error > .1f && Start + Offset < End) Anim->Montage_SetPosition(Selected.Montage, Start + Offset);
		return;
	}
	if (Start + Offset >= End || Anim->Montage_Play(Selected.Montage, Rate, EMontagePlayReturnType::MontageLength, Start + Offset, false) <= 0.f) return;
	const FName SectionName = Selected.Montage->GetSectionName(Section);
	Anim->Montage_SetNextSection(SectionName, bLoop ? SectionName : NAME_None, Selected.Montage);
	if (const auto* Instance = Anim->GetActiveInstanceForMontage(Selected.Montage))
	{
		AirbornePoseRevision = AirborneState.PhaseRevision;
		AirborneAnimInstance = Anim; AirborneMontage = Selected.Montage; AirborneMontageInstanceId = Instance->GetInstanceID();
	}
}
