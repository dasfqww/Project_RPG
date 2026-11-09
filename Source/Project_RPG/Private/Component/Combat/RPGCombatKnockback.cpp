#include "Component/Combat/RPGCombatControlComponent.h"
#include "Riding/RPGRidingDamageScope.h"
#include "Riding/RPGRidingComponent.h"
#include "Component/Combat/RPGCombatMotion.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/RPGBaseCharacter.h"
#include "Combat/RPGHitReactionProfile.h"
#include "Component/Combat/RPGCombatReactionComponent.h"
#include "Component/RPGCharacterMovementComponent.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/RootMotionSource.h"
#include "GameplayEffect/RPGKnockbackEffect.h"

using namespace RPGCombatMotion;

namespace
{
	FName MotionName(uint32 Id, uint32 Life)
	{
		return FName(*FString::Printf(TEXT("RPG_Knockback_%u_%u"), Life, Id));
	}

}

bool URPGCombatControlComponent::ApplyApprovedDamageKnockback(AActor* Source, const FHitResult& Hit,
	const FGuid& Receipt, const FRPGKnockbackSettings& Settings, const FVector& AwayDirection)
{
	if (Settings.IsAirborne()) return ApplyApprovedDamageAirborne(Source, Hit, Receipt, Settings, AwayDirection);
	if (AirborneState.IsActive() || bApplyingAirborne || bEndingAirborne) return false;
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	if (bEnding || bApplyingKnockback || bEndingKnockback || !Character || !Character->HasAuthority() || !Source || !Source->HasAuthority()
		|| Source == Character || Source->GetWorld() != GetWorld() || Hit.GetActor() != Character || !Receipt.IsValid()
		|| !Settings.IsEnabled() || !Supported(Movement) || !Character->GetActorEnableCollision()
		|| AwayDirection.ContainsNaN() || !FMath::IsNearlyEqual(AwayDirection.SizeSquared2D(), 1.0, .01)
		|| !FMath::IsNearlyZero(AwayDirection.Z) || !IsLivingAvatar(Source) || !IsLivingAvatar(Character)) return false;
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Character);
	InitializeWithAbilitySystem(*ASC);
	if (RecentKnockbackHits.Contains(Receipt)) return false;
	if (RecentKnockbackHits.Num() >= 64) RecentKnockbackHits.RemoveAt(0);
	RecentKnockbackHits.Add(Receipt);
	if (IsImmuneTo(ERPGCombatEffectKind::Knockback) || HasUncancellableCombatAbility()) return false;
	TGuardValue<bool> Applying(bApplyingKnockback, true);
	FRPGRidingDamageScope RidingOutcome(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Source), ASC);
	const uint32 Life = RecoveryRevision();
	const uint32 Epoch = LifetimeEpoch;
	EndKnockback(ERPGKnockbackEndReason::Replaced);
	const auto* AnimatingAbility = ASC->GetAnimatingAbility();
	const bool bCancellingMontage = RPGCombatControl::IsStunAffectedAbility(AnimatingAbility);
	TWeakObjectPtr<UAnimInstance> InterruptedAnim = Character->GetMesh()->GetAnimInstance();
	TWeakObjectPtr<UAnimMontage> InterruptedMontage = ASC->GetCurrentMontage();
	const auto* InterruptedInstance = InterruptedAnim.IsValid() && InterruptedMontage.IsValid()
		? InterruptedAnim->GetActiveInstanceForMontage(InterruptedMontage.Get()) : nullptr;
	const int32 InterruptedId = InterruptedInstance ? InterruptedInstance->GetInstanceID() : INDEX_NONE;
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddInstigator(Source, Source); Context.AddSourceObject(this); Context.AddHitResult(Hit);
	FGameplayEffectSpec Effect(GetDefault<URPGKnockbackEffect>(), Context, 1.f);
	Effect.SetDuration(Settings.Duration, true);
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Effect); // Tag entry cancels interruptible combat first.
	const auto* AppliedEffect = ASC->GetActiveGameplayEffect(Handle);
	if (!Handle.IsValid() || !AppliedEffect) return false;
	OwnedKnockback = Handle;
	// Application delegates may inhibit the GE before our inhibition delegate is attached.
	if (AppliedEffect->bIsInhibited) { EndKnockback(ERPGKnockbackEndReason::Cleansed); return false; }
	if (bEnding || !IsLivingAvatar(Character) || RecoveryRevision() != Life || LifetimeEpoch != Epoch || BoundASC.Get() != ASC
		|| !Supported(Movement) || !Character->GetActorEnableCollision() || HasUncancellableCombatAbility())
	{ EndKnockback(ERPGKnockbackEndReason::UnsupportedMovement); return false; }
	// Finish only the exact GAS montage that this control just cancelled. A blend-out must not
	// continue contributing animation root motion, and a newer/foreign montage is never stopped.
	const auto* RemainingInstance = InterruptedAnim.IsValid() && InterruptedMontage.IsValid()
		? InterruptedAnim->GetActiveInstanceForMontage(InterruptedMontage.Get()) : nullptr;
	if (bCancellingMontage && !ASC->GetAnimatingAbility() && RemainingInstance && RemainingInstance->GetInstanceID() == InterruptedId
		&& Character->GetMesh()->IsPlayingRootMotion()) InterruptedAnim->Montage_Stop(0.f, InterruptedMontage.Get());
	if (auto* Riding = Character->FindComponentByClass<URPGRidingComponent>()) Riding->StopServerDashForControl();
	if (bEnding || BoundASC.Get() != ASC || RecoveryRevision() != Life || LifetimeEpoch != Epoch || !IsLivingAvatar(Character))
	{ EndKnockback(ERPGKnockbackEndReason::LifeChanged); return false; }
	if (ForeignMotion(Movement, NAME_None)) { EndKnockback(ERPGKnockbackEndReason::ForeignMovement); return false; }
	auto* Security = Character->FindComponentByClass<URPGSecurityValidationComponent>();
	// A non-combat system's window must not be overwritten. Cancelled combat revoked its own window above.
	if ((Character->IsPlayerControlled() && !Security) || (Security && Security->IsMovementAuthorizationActive()))
	{ EndKnockback(ERPGKnockbackEndReason::ForeignMovement); return false; }
	if (Security)
	{
		KnockbackMovementAuthorization = Security->BeginMovementAuthorization(Settings.Duration + .2f,
			Settings.Distance + 25.f, TEXT("RPG.Knockback"));
		if (!KnockbackMovementAuthorization.IsValid())
		{ EndKnockback(ERPGKnockbackEndReason::UnsupportedMovement); return false; }
	}
	if (auto* Removed = ASC->OnGameplayEffectRemoved_InfoDelegate(Handle)) Removed->AddUObject(this, &ThisClass::HandleKnockbackRemoved);
	if (auto* Inhibited = ASC->OnGameplayEffectInhibitionChangedDelegate(Handle)) Inhibited->AddUObject(this, &ThisClass::HandleKnockbackInhibited);
	KnockbackState.bActive = true;
	KnockbackState.ExecutionId = ++KnockbackSequence;
	KnockbackState.RecoveryRevision = Life;
	KnockbackState.Velocity = AwayDirection * (Settings.Distance / Settings.Duration);
	KnockbackState.PresentationDirection = RPGHitReaction::ResolveDirection(Character->GetActorForwardVector(), -AwayDirection);
	KnockbackState.Duration = Settings.Duration;
	KnockbackState.StartServerTime = ServerNow(GetWorld());
	KnockbackState.EndReason = ERPGKnockbackEndReason::None;
	ApplyKnockbackMotion();
	if (!HasKnockbackMotion()) { EndKnockback(ERPGKnockbackEndReason::UnsupportedMovement); return false; }
	KnockbackLastLocation = Character->GetActorLocation();
	DisplacementLastBaseMovementTotal = Movement->GetServerBasedMovementTotal();
	Character->OnCharacterMovementUpdated.AddUniqueDynamic(this, &ThisClass::OnKnockbackMovementUpdated);
	RidingOutcome.CompleteControl(Handle);
	// Dismount callbacks may cleanse/reset this already accepted control. Never restart its motion.
	if (bEnding || LifetimeEpoch != Epoch || RecoveryRevision() != Life || BoundASC.Get() != ASC
		|| !IsLivingAvatar(Character) || !HasKnockbackMotion())
	{ if (OwnedKnockback == Handle) EndKnockback(ERPGKnockbackEndReason::LifeChanged); return false; }
	GetOwner()->ForceNetUpdate();
	SetComponentTickEnabled(true);
	OnKnockbackStateChanged.Broadcast(KnockbackState);
	UpdateKnockbackPose();
	return KnockbackState.bActive && KnockbackState.RecoveryRevision == Life;
}

bool URPGCombatControlComponent::HasKnockbackMotion() const
{
	if (bEnding || !KnockbackState.bActive || KnockbackState.RecoveryRevision != RecoveryRevision()
		|| ServerNow(GetWorld()) >= KnockbackState.StartServerTime + KnockbackState.Duration) return false;
	const auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const auto Motion = Movement ? Movement->GetRootMotionSource(MotionName(KnockbackState.ExecutionId, KnockbackState.RecoveryRevision)) : nullptr;
	return Motion.IsValid() && !Motion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval)
		&& !Motion->Status.HasFlag(ERootMotionSourceStatusFlags::Finished);
}

void URPGCombatControlComponent::ApplyKnockbackMotion()
{
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	// CMC maps replicated RMS server IDs onto matching locally-created sources on BOTH
	// autonomous and simulated proxies (the same contract used by simulated GAS motion tasks).
	if (!Character || !Supported(Movement)) return;
	const FName Name = MotionName(KnockbackState.ExecutionId, KnockbackState.RecoveryRevision);
	if (KnockbackMotionName != Name) StopKnockbackMotion();
	if (HasKnockbackMotion() || ForeignMotion(Movement, Name)) return;
	const double Elapsed = FMath::Max(0.0, ServerNow(GetWorld()) - KnockbackState.StartServerTime);
	if (!KnockbackState.bActive || KnockbackState.RecoveryRevision != RecoveryRevision() || Elapsed >= KnockbackState.Duration) return;
	auto Motion = MakeShared<FRootMotionSource_ConstantForce>();
	Motion->InstanceName = Name;
	Motion->Priority = 200;
	Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
	Motion->Force = KnockbackState.Velocity;
	Motion->Duration = KnockbackState.Duration;
	Motion->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Motion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::MaintainLastRootMotionVelocity;
	Motion->SetTime(static_cast<float>(Elapsed));
	KnockbackMotionName = Name;
	KnockbackMotionId = Movement->ApplyRootMotionSource(Motion);
}

void URPGCombatControlComponent::StopKnockbackMotion()
{
	auto* Character = Cast<ACharacter>(GetOwner());
	if (Character) Character->OnCharacterMovementUpdated.RemoveDynamic(this, &ThisClass::OnKnockbackMovementUpdated);
	Retire(Character, KnockbackMotionName);
	KnockbackMotionName = NAME_None; KnockbackMotionId = 0;
}

void URPGCombatControlComponent::EndKnockback(ERPGKnockbackEndReason Reason)
{
	if (bEndingKnockback) return;
	TGuardValue<bool> Ending(bEndingKnockback, true);
	StopKnockbackMotion(); StopKnockbackPose();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (auto* Security = GetOwner()->FindComponentByClass<URPGSecurityValidationComponent>())
			Security->EndMovementAuthorization(KnockbackMovementAuthorization, true);
		KnockbackMovementAuthorization.Invalidate();
		const bool bWasActive = KnockbackState.bActive;
		KnockbackState.bActive = false;
		if (bWasActive)
		{
			KnockbackState.EndReason = Reason;
			GetOwner()->ForceNetUpdate();
		}
		TSet<FActiveGameplayEffectHandle> Handles;
		if (OwnedKnockback.IsValid()) Handles.Add(OwnedKnockback);
		OwnedKnockback = {};
		if (BoundASC.IsValid())
			for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
			{
				const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
				if (Effect && Effect->Spec.Def->GetClass() == URPGKnockbackEffect::StaticClass()
					&& Effect->Spec.GetContext().GetSourceObject() == this) Handles.Add(Handle);
			}
		for (auto Handle : Handles) if (BoundASC.IsValid()) BoundASC->RemoveActiveGameplayEffect(Handle);
		if (bWasActive) OnKnockbackStateChanged.Broadcast(KnockbackState);
	}
}

bool URPGCombatControlComponent::CancelKnockback(uint32 ExecutionId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !KnockbackState.bActive || KnockbackState.ExecutionId != ExecutionId) return false;
	EndKnockback(ERPGKnockbackEndReason::Cleansed); return true;
}

void URPGCombatControlComponent::HandleKnockbackRemoved(const FGameplayEffectRemovalInfo& Info)
{
	if (Info.ActiveEffect && Info.ActiveEffect->Handle == OwnedKnockback)
		EndKnockback(Info.bPrematureRemoval ? ERPGKnockbackEndReason::Cleansed : ERPGKnockbackEndReason::Completed);
}

void URPGCombatControlComponent::HandleKnockbackInhibited(FActiveGameplayEffectHandle Handle, bool bInhibited)
{
	if (bInhibited && Handle == OwnedKnockback) EndKnockback(ERPGKnockbackEndReason::Cleansed);
}

void URPGCombatControlComponent::HandleKnockbackImpact(const FHitResult& Hit)
{
	if (GetOwner()->HasAuthority() && KnockbackState.bActive && Hit.bBlockingHit
		&& FVector::DotProduct(Hit.ImpactNormal, KnockbackState.Velocity.GetSafeNormal2D()) < -.2f)
		EndKnockback(ERPGKnockbackEndReason::Wall);
}

void URPGCombatControlComponent::OnKnockbackMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	if (!KnockbackState.bActive || !GetOwner()->HasAuthority()) return;
	const FVector Location = GetOwner()->GetActorLocation();
	const auto* Character = Cast<ACharacter>(GetOwner());
	const auto* Movement = Character ? Cast<URPGCharacterMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	const FVector BaseTotal = Movement ? Movement->GetServerBasedMovementTotal() : FVector::ZeroVector;
	// Include carry applied between remote client moves as well as in this move.
	const FVector PreviousLocation = OldLocation - (BaseTotal - DisplacementLastBaseMovementTotal);
	if (OldLocation.ContainsNaN() || Location.ContainsNaN() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f
		|| PreviousLocation.ContainsNaN() || FVector::Dist2D(KnockbackLastLocation, PreviousLocation) > 25.f
		|| FVector::Dist2D(OldLocation, Location) > KnockbackState.Velocity.Size2D() * DeltaSeconds + 25.f)
	{ EndKnockback(ERPGKnockbackEndReason::ForeignMovement); return; }
	KnockbackLastLocation = Location;
	DisplacementLastBaseMovementTotal = BaseTotal;
	(void)OldVelocity;
}

void URPGCombatControlComponent::TickKnockback()
{
	if (!KnockbackState.bActive) return;
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (GetOwner()->HasAuthority())
	{
		if (!IsLivingAvatar(GetOwner()) || KnockbackState.RecoveryRevision != RecoveryRevision())
			EndKnockback(ERPGKnockbackEndReason::LifeChanged);
		else if (!Supported(Movement)) EndKnockback(ERPGKnockbackEndReason::UnsupportedMovement);
		else if (ForeignMotion(Movement, KnockbackMotionName)) EndKnockback(ERPGKnockbackEndReason::ForeignMovement);
		else if (!BoundASC.IsValid() || !BoundASC->GetActiveGameplayEffect(OwnedKnockback)) EndKnockback(ERPGKnockbackEndReason::Cleansed);
		else if (!HasKnockbackMotion()) EndKnockback(ERPGKnockbackEndReason::Completed);
	}
	else if (!IsLivingAvatar(GetOwner()) || KnockbackState.RecoveryRevision != RecoveryRevision()
		|| ServerNow(GetWorld()) >= KnockbackState.StartServerTime + KnockbackState.Duration)
	{
		StopKnockbackMotion(); StopKnockbackPose(); return;
	}
	else ApplyKnockbackMotion();
	UpdateKnockbackPose();
}

void URPGCombatControlComponent::OnRep_KnockbackState()
{
	if (!KnockbackState.bActive || KnockbackMotionName != MotionName(KnockbackState.ExecutionId, KnockbackState.RecoveryRevision))
	{ StopKnockbackMotion(); StopKnockbackPose(); }
	if (KnockbackState.bActive) ApplyKnockbackMotion();
	SetComponentTickEnabled(KnockbackState.bActive || StunState.bActive || AirborneState.IsActive() || SlowState.bActive || BurnState.Stacks > 0 || StaggerState.bActive || !OwnedDefenseReductions.IsEmpty() || !OwnedDamageTakenIncreases.IsEmpty());
	OnKnockbackStateChanged.Broadcast(KnockbackState);
	UpdateKnockbackPose();
}

void URPGCombatControlComponent::StopKnockbackPose()
{
	auto* Anim = KnockbackAnimInstance.Get(); auto* Montage = KnockbackMontage.Get();
	const auto* Instance = Anim && Montage ? Anim->GetActiveInstanceForMontage(Montage) : nullptr;
	const bool bOwned = Instance && Instance->GetInstanceID() == KnockbackMontageInstanceId;
	KnockbackAnimInstance.Reset(); KnockbackMontage.Reset(); KnockbackMontageInstanceId = INDEX_NONE;
	if (bOwned) Anim->Montage_Stop(.05f, Montage);
}

void URPGCombatControlComponent::UpdateKnockbackPose()
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer) return;
	if (!KnockbackState.bActive || bEnding || !IsLivingAvatar(GetOwner()) || KnockbackState.RecoveryRevision != RecoveryRevision()
		|| ServerNow(GetWorld()) >= KnockbackState.StartServerTime + KnockbackState.Duration)
	{ StopKnockbackPose(); return; }
	if (KnockbackPoseExecution == KnockbackState.ExecutionId) return;
	auto* Character = Cast<ACharacter>(GetOwner());
	auto* Receiver = GetOwner()->FindComponentByClass<URPGCombatReactionComponent>();
	const auto* Profile = KnockbackPresentationProfile ? KnockbackPresentationProfile.Get() : (Receiver ? Receiver->PresentationProfile.Get() : nullptr);
	auto* Mesh = Character ? Character->GetMesh() : nullptr;
	auto* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!Profile || !Anim || !Mesh->GetSkeletalMeshAsset() || Anim->IsAnyMontagePlaying()) return;
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (ASC && ASC->GetAnimatingAbility()) return;
	const auto* Configured = Profile->DirectionMontages.Find(KnockbackState.PresentationDirection);
	const auto& Selected = Configured ? *Configured : Profile->FallbackMontage;
	if (!URPGCombatReactionComponent::IsSafeCosmeticMontage(Selected.Montage, Selected.Section)
		|| Mesh->GetSkeletalMeshAsset()->GetSkeleton() != Selected.Montage->GetSkeleton()) return;
	const int32 Section = Selected.Section.IsNone() ? 0 : Selected.Montage->GetSectionIndex(Selected.Section);
	float Start = 0.f, End = 0.f; Selected.Montage->GetSectionStartAndEndTime(Section, Start, End);
	const float Rate = FMath::Clamp((End - Start) / KnockbackState.Duration, .1f, 4.f);
	const float Offset = static_cast<float>(ServerNow(GetWorld()) - KnockbackState.StartServerTime) * Rate;
	if (Start + Offset >= End || Anim->Montage_Play(Selected.Montage, Rate, EMontagePlayReturnType::MontageLength, Start + Offset, false) <= 0.f) return;
	Anim->Montage_SetNextSection(Selected.Montage->GetSectionName(Section), NAME_None, Selected.Montage);
	if (const auto* Instance = Anim->GetActiveInstanceForMontage(Selected.Montage))
	{
		KnockbackPoseExecution = KnockbackState.ExecutionId;
		KnockbackAnimInstance = Anim; KnockbackMontage = Selected.Montage; KnockbackMontageInstanceId = Instance->GetInstanceID();
	}
}
