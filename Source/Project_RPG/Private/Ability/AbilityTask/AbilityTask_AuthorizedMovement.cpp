#include "Ability/AbilityTask/AbilityTask_AuthorizedMovement.h"

#include "Component/RPGSecurityValidationComponent.h"
#include "Engine/World.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AbilityTask_AuthorizedMovement)

UAbilityTask_AuthorizedMovement*
UAbilityTask_AuthorizedMovement::AuthorizedSkillMovementWindow(
	UGameplayAbility* OwningAbility)
{
	FRPGSkillMovementSecurityProfile Profile;
	if (const URPGGameplayAbility_SkillContainer* SkillAbility =
		Cast<URPGGameplayAbility_SkillContainer>(OwningAbility))
	{
		Profile = SkillAbility->GetActiveSkillSpec()
			.SecurityProfile.AuthorizedMovement;
	}
	return AuthorizedMovementWindow(OwningAbility, Profile);
}

UAbilityTask_AuthorizedMovement*
UAbilityTask_AuthorizedMovement::AuthorizedMovementWindow(
	UGameplayAbility* OwningAbility,
	const FRPGSkillMovementSecurityProfile& MovementProfile)
{
	UAbilityTask_AuthorizedMovement* Task =
		NewAbilityTask<UAbilityTask_AuthorizedMovement>(OwningAbility);
	Task->CachedProfile = MovementProfile;
	return Task;
}

void UAbilityTask_AuthorizedMovement::Activate()
{
	Super::Activate();
	const FGameplayAbilityActorInfo* ActorInfo =
		Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!CachedProfile.bEnabled ||
		!FMath::IsFinite(CachedProfile.DurationSeconds) ||
		CachedProfile.DurationSeconds <= 0.0f ||
		!FMath::IsFinite(CachedProfile.ExtraDistance) ||
		CachedProfile.ExtraDistance < 0.0f)
	{
		Reject(FText::FromString(TEXT("The authored movement security profile is invalid.")));
		return;
	}
	if (!IsValid(AvatarActor))
	{
		Reject(FText::FromString(TEXT("The movement ability has no valid avatar.")));
		return;
	}
	if (!AvatarActor->HasAuthority() && !(ActorInfo && ActorInfo->IsLocallyControlled()))
	{
		bResolved = true;
		EndTask();
		return;
	}

	EffectiveReason = CachedProfile.Reason.IsNone()
		? FName(*GetNameSafe(Ability ? Ability->GetClass() : nullptr))
		: CachedProfile.Reason;
	if (AvatarActor->HasAuthority())
	{
		URPGSecurityValidationComponent* Security =
			AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>();
		if (!Security)
		{
			Reject(FText::FromString(
				TEXT("The authoritative avatar has no Security Validation Component.")));
			return;
		}
		Security->AuthorizeMovementDiscontinuity(
			CachedProfile.DurationSeconds,
			CachedProfile.ExtraDistance,
			EffectiveReason);
		bAuthorizationApplied = true;
	}

	bStarted = true;
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnAuthorized.Broadcast();
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			FinishTimerHandle,
			this,
			&ThisClass::FinishWindow,
			CachedProfile.DurationSeconds,
			false);
	}
	else
	{
		FinishWindow();
	}
}

void UAbilityTask_AuthorizedMovement::FinishWindow()
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;
	RevokeAuthorization();
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnFinished.Broadcast();
	}
	EndTask();
}

void UAbilityTask_AuthorizedMovement::Reject(const FText& Reason)
{
	if (bResolved)
	{
		return;
	}
	bResolved = true;
	if (ShouldBroadcastAbilityTaskDelegates())
	{
		OnRejected.Broadcast(Reason);
	}
	EndTask();
}

void UAbilityTask_AuthorizedMovement::RevokeAuthorization()
{
	if (!bAuthorizationApplied)
	{
		return;
	}
	const FGameplayAbilityActorInfo* ActorInfo =
		Ability ? Ability->GetCurrentActorInfo() : nullptr;
	AActor* AvatarActor = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (URPGSecurityValidationComponent* Security = IsValid(AvatarActor)
		? AvatarActor->FindComponentByClass<URPGSecurityValidationComponent>()
		: nullptr)
	{
		Security->CancelMovementAuthorization(
			EffectiveReason,
			CachedProfile.bResetBaselineWhenFinished);
	}
	bAuthorizationApplied = false;
}

void UAbilityTask_AuthorizedMovement::OnDestroy(const bool bInOwnerFinished)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FinishTimerHandle);
	}
	const bool bWasCancelled = bStarted && !bResolved;
	RevokeAuthorization();
	if (bWasCancelled && ShouldBroadcastAbilityTaskDelegates())
	{
		OnCancelled.Broadcast();
	}
	bResolved = true;
	Super::OnDestroy(bInOwnerFinished);
}
