#include "Ability/AbilityTask/RPGAbilityTask_PlayScaledSkillMontageAndWait.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "GameFramework/Character.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAbilityTask_PlayScaledSkillMontageAndWait)

URPGAbilityTask_PlayScaledSkillMontageAndWait* URPGAbilityTask_PlayScaledSkillMontageAndWait::PlayScaledSkillMontage(
	URPGGameplayAbility_SkillContainer* OwningAbility, UAnimMontage* Montage, float PlaybackRate, FName Section)
{
	if (!IsValid(OwningAbility) || !OwningAbility->IsActive() || !IsValid(Montage)) return nullptr;
	float Scale = 1.f;
	const auto& Spec = OwningAbility->GetActiveSkillSpec();
	if (!Spec.SkillTag.IsValid() || Montage != Spec.Montage.Get()
		|| !Spec.TryGetMontageMovementScale(Scale) || Scale == 1.f) return nullptr;
	UAbilitySystemGlobals::NonShipping_ApplyGlobalAbilityScaler_Rate(PlaybackRate);
	auto* Task = NewAbilityTask<URPGAbilityTask_PlayScaledSkillMontageAndWait>(OwningAbility, TEXT("SkillExecutionMontage"));
	Task->MontageToPlay = Montage;
	Task->Rate = PlaybackRate;
	Task->StartSection = Section;
	Task->StartTimeSeconds = 0.f;
	Task->bStopWhenAbilityEnds = true;
	Task->bAllowInterruptAfterBlendOut = !Spec.ContributionHitSection.IsNone();
	Task->AnimRootMotionTranslationScale = Scale;
	Task->MovementProfile = Spec.SecurityProfile.AuthorizedMovement;
	Task->ExpectedAvatar = Cast<ACharacter>(OwningAbility->GetAvatarActorFromActorInfo());
	Task->ExpectedActivationSerial = OwningAbility->GetSkillActivationSerial();
	return Task;
}

void URPGAbilityTask_PlayScaledSkillMontageAndWait::Activate()
{
	const auto* Owner = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	if (!Owner || !Owner->IsActive() || Owner->GetSkillActivationSerial() != ExpectedActivationSerial)
	{
		// A late task must not cancel a later activation through old delegates.
		EndTask();
		return;
	}
	const auto* Info = Ability ? Ability->GetCurrentActorInfo() : nullptr;
	ACharacter* Avatar = ExpectedAvatar.Get();
	if (!IsValid(Avatar) || !Info || Info->AvatarActor.Get() != Avatar
		|| (!Avatar->HasAuthority() && !Info->IsLocallyControlled()))
	{
		RejectMovement();
		return;
	}
	if (Avatar->HasAuthority())
	{
		auto* Security = Avatar->FindComponentByClass<URPGSecurityValidationComponent>();
		// Never replace another dash/teleport's active permission.
		if (!Security || Security->IsMovementAuthorizationActive())
		{
			RejectMovement();
			return;
		}
		AuthorizationHandle = Security->BeginMovementAuthorization(MovementProfile.DurationSeconds,
			MovementProfile.ExtraDistance, MovementProfile.Reason.IsNone() ? GetFName() : MovementProfile.Reason);
		if (!AuthorizationHandle.IsValid())
		{
			RejectMovement();
			return;
		}
		AuthorizedSecurity = Security;
	}
	// GAS applies/replicates the scale. Its stop path unbinds blend-out
	// delegates, so OnDestroy must also restore a scale still owned by us.
	bMayOwnTranslationScale = true;
	Super::Activate();
}

void URPGAbilityTask_PlayScaledSkillMontageAndWait::RejectMovement()
{
	if (ShouldBroadcastAbilityTaskDelegates()) OnCancelled.Broadcast();
	EndTask();
}

void URPGAbilityTask_PlayScaledSkillMontageAndWait::OnDestroy(bool bInOwnerFinished)
{
	auto* Owner = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	auto* Avatar = ExpectedAvatar.Get();
	const auto* Info = Owner ? Owner->GetCurrentActorInfo() : nullptr;
	const auto* ASC = AbilitySystemComponent.Get();
	if (bMayOwnTranslationScale && IsValid(Avatar) && Info && Info->AvatarActor.Get() == Avatar
		&& Owner->GetSkillActivationSerial() == ExpectedActivationSerial
		&& (!ASC || !ASC->GetAnimatingAbility() || ASC->GetAnimatingAbility() == Owner)
		&& FMath::IsNearlyEqual(Avatar->GetAnimRootMotionTranslationScale(), AnimRootMotionTranslationScale))
	{
		Avatar->SetAnimRootMotionTranslationScale(1.f);
	}
	bMayOwnTranslationScale = false;
	// Captured component + exact handle: an avatar switch or later permission
	// must never make this old task revoke another cast's authorization.
	if (auto* Security = AuthorizedSecurity.Get())
		Security->EndMovementAuthorization(AuthorizationHandle, MovementProfile.bResetBaselineWhenFinished);
	AuthorizationHandle.Invalidate();
	AuthorizedSecurity.Reset();
	Super::OnDestroy(bInOwnerFinished);
}
