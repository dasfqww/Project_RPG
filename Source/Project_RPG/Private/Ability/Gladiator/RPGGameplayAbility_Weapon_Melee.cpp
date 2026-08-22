#include "Ability/Gladiator/RPGGameplayAbility_Weapon_Melee.h"

#include "Ability/Gladiator/RPGGladiatorEffectActors.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Component/RPGSecurityValidationComponent.h"
#include "DrawDebugHelpers.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "GameFramework/Pawn.h"
#include "RPGGameplayTags.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGameplayAbility_Weapon_Melee)

URPGGameplayAbility_Weapon_Melee::URPGGameplayAbility_Weapon_Melee(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URPGGameplayAbility_Weapon_Melee::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	ResetHitActors();
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
}

void URPGGameplayAbility_Weapon_Melee::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility,
	const bool bWasCancelled)
{
	ResetHitActors();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void URPGGameplayAbility_Weapon_Melee::ParseTargetData(
	const FGameplayAbilityTargetDataHandle& InTargetDataHandle,
	TArray<int32>& OutCharacterHitIndexes,
	TArray<int32>& OutBlockHitIndexes)
{
	OutCharacterHitIndexes.Reset();
	OutBlockHitIndexes.Reset();

	// Prevent duplicate entries inside this payload without marking the actor as
	// damaged. ProcessHitResult owns the persistent per-trace cache.
	TSet<TWeakObjectPtr<AActor>> PayloadActors;
	for (int32 Index = 0; Index < InTargetDataHandle.Num(); ++Index)
	{
		const FGameplayAbilityTargetData* TargetData = InTargetDataHandle.Get(Index);
		const FHitResult* HitResult = TargetData ? TargetData->GetHitResult() : nullptr;
		AActor* TargetActor = HitResult ? ResolveTargetActor(*HitResult) : nullptr;
		if (!TargetActor || !IsHostileTarget(TargetActor))
		{
			continue;
		}

		const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
		if (CachedHitActors.Contains(WeakTarget) || PayloadActors.Contains(WeakTarget))
		{
			continue;
		}

		PayloadActors.Add(WeakTarget);
		if (IsCharacterBlockingHit(TargetActor))
		{
			OutBlockHitIndexes.Add(Index);
		}
		else
		{
			OutCharacterHitIndexes.Add(Index);
		}
	}
}

void URPGGameplayAbility_Weapon_Melee::ProcessHitResult(
	FHitResult HitResult,
	const float Damage,
	const bool bBlockingHit,
	UAnimMontage* BackwardMontage,
	AActor* WeaponActor)
{
	TryProcessHitResult(HitResult, Damage, bBlockingHit, BackwardMontage, WeaponActor);
}

bool URPGGameplayAbility_Weapon_Melee::TryProcessHitResult(
	const FHitResult& HitResult,
	const float Damage,
	const bool bBlockingHit,
	UAnimMontage* BackwardMontage,
	AActor* WeaponActor)
{
	AActor* TargetActor = ResolveTargetActor(HitResult);
	if (!TargetActor || !IsHostileTarget(TargetActor))
	{
		return false;
	}

	const TWeakObjectPtr<AActor> WeakTarget(TargetActor);
	if (CachedHitActors.Contains(WeakTarget))
	{
		return false;
	}
	CachedHitActors.Add(WeakTarget);

	const bool bIsAuthority = HasAuthority(&CurrentActivationInfo);
	AActor* SourceActor = GetAvatarActorFromActorInfo();
	FHitResult ContextHitResult = HitResult;
	ContextHitResult.bBlockingHit = bBlockingHit;
	if (bIsAuthority)
	{
		if (URPGSecurityValidationComponent* Security = SourceActor
			? SourceActor->FindComponentByClass<
				URPGSecurityValidationComponent>()
			: nullptr)
		{
			FString RejectionReason;
			if (!Security->ValidateCombatHit(
				TargetActor,
				ContextHitResult,
				MaximumServerHitDistance,
				ServerHitLocationTolerance,
				RejectionReason))
			{
				return false;
			}
		}
	}

	ExecuteImpactCue(ContextHitResult, bBlockingHit);
	DrawDebugHitPoint(ContextHitResult);

	// D1 used a custom montage-blocking extension here. The current project has
	// no equivalent yet, but the argument remains serialized for imported assets.
	(void)BackwardMontage;

	if (!bIsAuthority)
	{
		return true;
	}

	const float AppliedDamage = bBlockingHit
		? Damage * FMath::Max(0.0f, BlockHitDamageMultiplier)
		: Damage;
	AActor* EffectCauser = WeaponActor ? WeaponActor : SourceActor;
	FRPGSkillSecurityProfile SecurityProfile;
	SecurityProfile.MaximumServerHitDistance = MaximumServerHitDistance;
	SecurityProfile.HitLocationTolerance = ServerHitLocationTolerance;
	SecurityProfile.MaximumDamagePerHit = MaximumServerDamagePerHit;
	if (!RPGGladiatorEffectActors::ApplyDamage(
		SourceActor,
		TargetActor,
		ContextHitResult,
		AppliedDamage,
		nullptr,
		EffectCauser,
		SecurityProfile))
	{
		CachedHitActors.Remove(WeakTarget);
		return false;
	}

	FGameplayEventData HitReactPayload;
	HitReactPayload.Instigator = GetAvatarActorFromActorInfo();
	HitReactPayload.Target = TargetActor;
	HitReactPayload.EventMagnitude = AppliedDamage;
	HitReactPayload.ContextHandle.AddHitResult(ContextHitResult);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		TargetActor, RPGGameplayTags::Shared_Event_HitReact, HitReactPayload);
	return true;
}

void URPGGameplayAbility_Weapon_Melee::ResetHitActors()
{
	CachedHitActors.Reset();
}

bool URPGGameplayAbility_Weapon_Melee::IsCharacterBlockingHit(AActor* TargetActor) const
{
	if (!TargetActor)
	{
		return false;
	}

	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor);
	if (!TargetASC)
	{
		TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(TargetActor->GetOwner());
	}
	if (!TargetASC)
	{
		return false;
	}

	const FGameplayTag D1BlockingTag = FGameplayTag::RequestGameplayTag(TEXT("Status.Block"), false);
	const bool bHasBlockingTag =
		TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Player_Status_Blocking) ||
		(D1BlockingTag.IsValid() && TargetASC->HasMatchingGameplayTag(D1BlockingTag));
	if (!bHasBlockingTag)
	{
		return false;
	}

	const AActor* Defender = TargetASC->GetAvatarActor();
	const AActor* Attacker = GetAvatarActorFromActorInfo();
	if (!Defender || !Attacker)
	{
		return false;
	}

	const FVector DefenderToAttacker =
		(Attacker->GetActorLocation() - Defender->GetActorLocation()).GetSafeNormal2D();
	if (DefenderToAttacker.IsNearlyZero())
	{
		return true;
	}

	const FVector DefenderForward = Defender->GetActorForwardVector().GetSafeNormal2D();
	const float FacingDot = FVector::DotProduct(DefenderForward, DefenderToAttacker);
	const float FacingAngle = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(FacingDot, -1.0f, 1.0f)));
	return FacingAngle <= BlockingAngle;
}

AActor* URPGGameplayAbility_Weapon_Melee::ResolveTargetActor(const FHitResult& HitResult) const
{
	AActor* HitActor = HitResult.GetActor();
	if (!HitActor)
	{
		return nullptr;
	}

	UAbilitySystemComponent* TargetASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor);
	if (!TargetASC)
	{
		TargetASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(HitActor->GetOwner());
	}
	return TargetASC ? TargetASC->GetAvatarActor() : nullptr;
}

bool URPGGameplayAbility_Weapon_Melee::IsHostileTarget(const AActor* TargetActor) const
{
	const APawn* SourcePawn = Cast<APawn>(GetAvatarActorFromActorInfo());
	const APawn* TargetPawn = Cast<APawn>(TargetActor);
	return SourcePawn && TargetPawn && URPGCombatFunctionLibrary::IsTargetPawnHostile(
		const_cast<APawn*>(SourcePawn), const_cast<APawn*>(TargetPawn));
}

void URPGGameplayAbility_Weapon_Melee::ExecuteImpactCue(
	const FHitResult& HitResult,
	const bool bBlockingHit) const
{
	UAbilitySystemComponent* SourceASC = CurrentActorInfo
		? CurrentActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	const FGameplayTag ImpactCueTag =
		FGameplayTag::RequestGameplayTag(TEXT("GameplayCue.Weapon.Impact"), false);
	if (!SourceASC || !ImpactCueTag.IsValid())
	{
		return;
	}

	FGameplayCueParameters CueParameters;
	CueParameters.Instigator = GetAvatarActorFromActorInfo();
	CueParameters.EffectCauser = GetFirstEquipmentActor();
	CueParameters.Location = HitResult.ImpactPoint;
	CueParameters.Normal = HitResult.ImpactNormal;
	CueParameters.PhysicalMaterial = bBlockingHit ? nullptr : HitResult.PhysMaterial;
	SourceASC->ExecuteGameplayCue(ImpactCueTag, CueParameters);
}

void URPGGameplayAbility_Weapon_Melee::DrawDebugHitPoint(const FHitResult& HitResult) const
{
#if WITH_EDITOR
	if (bShowDebug && GetWorld())
	{
		const FColor Color = HasAuthority(&CurrentActivationInfo) ? FColor::Red : FColor::Green;
		DrawDebugSphere(GetWorld(), HitResult.ImpactPoint, 4.0f, 16, Color, false, 5.0f);
	}
#endif
}
