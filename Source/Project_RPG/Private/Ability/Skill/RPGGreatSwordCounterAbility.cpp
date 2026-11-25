#include "Ability/Skill/RPGGreatSwordCounterAbility.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "FunctionLibrary/RPGCombatFunctionLibrary.h"
#include "GameFramework/Pawn.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGClassSkillPolicy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGGreatSwordCounterAbility)

bool URPGGreatSwordCounterAbility::TryInterceptWeaponHit(APawn& Attacker, APawn& Defender)
{
	const auto* Info = GetCurrentActorInfo();
	auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	const auto* Anim = Info ? Info->GetAnimInstance() : nullptr;
	auto* Montage = ActiveSkillSpec.Montage.Get();
	if (!Defender.HasAuthority() || GetSkillSourceActor() != &Defender || !IsActive() || bIsAbilityEnding
		|| !bSkillCommitted || bIntercepted || !ASC || ASC->GetAnimatingAbility() != this
		|| !Anim || !Montage || !Anim->Montage_IsPlaying(Montage)
		|| !CastEquipmentEffectLifetime.IsCurrent(&Defender, &Defender))
	{
		return false;
	}

	const float Position = Anim->Montage_GetPosition(Montage);
	const FVector ToAttacker = Attacker.GetActorLocation() - Defender.GetActorLocation();
	if (Position < 0.07f || Position > 0.33f
		|| ToAttacker.SizeSquared() > FMath::Square(270.f)
		|| FVector::DotProduct(Defender.GetActorForwardVector(), ToAttacker.GetSafeNormal2D()) < 0.5f
		|| !URPGCombatFunctionLibrary::IsTargetPawnHostile(&Defender, &Attacker))
	{
		return false;
	}

	bIntercepted = true;
	CounterTarget = &Attacker;
	return true;
}

void URPGGreatSwordCounterAbility::HandleApplyAOEDamage(const FGameplayEventData& Payload)
{
	auto* Defender = Cast<APawn>(GetSkillSourceActor());
	const auto* Info = GetCurrentActorInfo();
	const auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	const auto* Anim = Info ? Info->GetAnimInstance() : nullptr;
	auto* Montage = ActiveSkillSpec.Montage.Get();
	if (!IsValid(Defender) || !IsActive() || bIsAbilityEnding || !bSkillCommitted || Payload.Instigator != Defender
		|| !ASC || ASC->GetAnimatingAbility() != this || !Anim || !Montage || !Anim->Montage_IsPlaying(Montage))
	{
		return;
	}
	// The first authored marker ends the guard before the strike animation.
	// Only the server determines whether the guard succeeded.
	if (!Defender->HasAuthority()) return;
	if (Anim->Montage_GetPosition(Montage) < 0.7f)
	{
		if (!bIntercepted) FinishSkillExecution(false);
		return;
	}

	auto* Target = CounterTarget.Get();
	CounterTarget.Reset();
	if (!IsValid(Target) || !CastEquipmentEffectLifetime.IsCurrent(Defender, Defender)) return;
	const auto* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	if (TargetASC && TargetASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Invincible)) return;

	TArray<FRPGHitQueryResult> Hits;
	if (!ExecuteActiveSkillHitQuery(Hits)) return;
	const auto* Match = Hits.FindByPredicate([Target](const FRPGHitQueryResult& Hit)
	{
		return Hit.TargetActor == Target;
	});
	if (!Match || !DamageEffectClass) return;

	const uint64 Serial = SkillDamageActivationSerial;
	FText Error;
	const float Damage = ActiveSkillSpec.ScaleHitDamage(ActiveSkillSpec.BaseDamage, CommittedClassDamageMultiplier);
	if (ApplySkillDamageToServerHit(Defender, Match->HitResult, DamageEffectClass, Damage,
		FGameplayTag(), ActiveSkillSpec.SecurityProfile, Error) && IsActive() && Serial == SkillDamageActivationSerial)
	{
		if (const auto* Policy = Cast<IRPGClassSkillPolicy>(ActiveClassSkillPolicy))
			Policy->OnClassSkillHitConfirmed(*this);
	}
}

void URPGGreatSwordCounterAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	CounterTarget.Reset();
	bIntercepted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
