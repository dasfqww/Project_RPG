#include "Component/Combat/RPGCombatControlComponent.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Combat/RPGApprovedHitContext.h"
#include "GameplayEffect/RPGCombatImmunityEffect.h"

bool URPGCombatControlComponent::IsImmuneTo(ERPGCombatEffectKind Kind) const
{
	if (GetOwner() && GetOwner()->HasAuthority()) const_cast<ThisClass*>(this)->RefreshMontageImmunity();
	const auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!ASC || ASC->GetAvatarActor() != GetOwner()) return false;
	bool bDefault = DefaultImmunities.Protects(Kind);
	switch (Kind)
	{
	case ERPGCombatEffectKind::Stun: bDefault |= bStunImmune; break;
	case ERPGCombatEffectKind::Knockback: bDefault |= bKnockbackImmune; break;
	case ERPGCombatEffectKind::Airborne: bDefault |= bAirborneImmune; break;
	case ERPGCombatEffectKind::Slow: bDefault |= bSlowImmune; break;
	case ERPGCombatEffectKind::DefenseReduction: bDefault |= bDefenseReductionImmune; break;
	case ERPGCombatEffectKind::DamageTakenIncrease: bDefault |= bDamageTakenIncreaseImmune; break;
	default: break;
	}
	return bDefault || RPGCombatImmunity::HasTags(ASC, Kind);
}

bool URPGCombatControlComponent::BeginAbilityImmunity(UGameplayAbility& Ability, FGuid Token,
	const FRPGCombatImmunitySettings& Settings)
{
	if (bEnding || bClearingAbilityImmunities || !GetOwner() || !GetOwner()->HasAuthority() || !Token.IsValid() || !Settings.IsEnabled()
		|| !Ability.IsInstantiated() || !Ability.IsActive() || Ability.GetAvatarActorFromActorInfo() != GetOwner()
		|| Ability.GetNetExecutionPolicy() == EGameplayAbilityNetExecutionPolicy::LocalOnly || !IsLivingAvatar(GetOwner())) return false;
	auto* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!ASC || Ability.GetAbilitySystemComponentFromActorInfo() != ASC) return false;
	InitializeWithAbilitySystem(*ASC);
	if (AbilityImmunities.Contains(Token) || AbilityImmunities.Num() >= 32) return false;
	const uint32 Epoch = LifetimeEpoch, Revision = RecoveryRevision();
	AbilityImmunities.Add(Token, {&Ability, {}});
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(this);
	AbilityImmunities.FindChecked(Token).Context = Context;
	FGameplayEffectSpec Spec(GetDefault<URPGCombatImmunityEffect>(), Context, 1.f);
	Spec.DynamicGrantedTags = Settings.GetGrantedTags();
	const auto Handle = ASC->ApplyGameplayEffectSpecToSelf(Spec);
	auto* Lease = AbilityImmunities.Find(Token);
	if (!Lease || !Handle.IsValid() || !ASC->GetActiveGameplayEffect(Handle) || ASC->GetActiveGameplayEffect(Handle)->bIsInhibited
		|| bEnding || BoundASC.Get() != ASC || LifetimeEpoch != Epoch || RecoveryRevision() != Revision
		|| !IsLivingAvatar(GetOwner()) || !Ability.IsActive() || Ability.GetAvatarActorFromActorInfo() != GetOwner())
	{
		AbilityImmunities.Remove(Token);
		if (Handle.IsValid()) ASC->RemoveActiveGameplayEffect(Handle);
		return false;
	}
	Lease->Effect = Handle;
	BoundRecoveryRevision = Revision;
	SetComponentTickEnabled(true);
	GetOwner()->ForceNetUpdate();
	return true;
}

void URPGCombatControlComponent::EndAbilityImmunity(const FGuid& Token)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	FAbilityImmunityLease Lease;
	if (!AbilityImmunities.RemoveAndCopyValue(Token, Lease)) return;
	// Remove the record first: an effect callback may start a new execution on the same GA.
	if (BoundASC.IsValid())
	{
		if (Lease.Effect.IsValid()) BoundASC->RemoveActiveGameplayEffect(Lease.Effect);
		else if (Lease.Context.IsValid())
			for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
			{
				const auto* Active = BoundASC->GetActiveGameplayEffect(Handle);
				if (Active && Active->Spec.Def == GetDefault<URPGCombatImmunityEffect>()
					&& Active->Spec.GetContext().Get() == Lease.Context.Get()) BoundASC->RemoveActiveGameplayEffect(Handle);
			}
	}
	GetOwner()->ForceNetUpdate();
}

void URPGCombatControlComponent::ClearAbilityImmunities()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bClearingAbilityImmunities) return;
	TGuardValue<bool> Guard(bClearingAbilityImmunities, true);
	StopMontageImmunity();
	AbilityImmunities.Reset();
	if (!BoundASC.IsValid()) return;
	// Includes a pending GE whose application callback reset the avatar before Apply returned.
	for (auto Handle : BoundASC->GetActiveEffects(FGameplayEffectQuery()))
	{
		const auto* Effect = BoundASC->GetActiveGameplayEffect(Handle);
		if (Effect && Effect->Spec.Def->GetClass() == URPGCombatImmunityEffect::StaticClass()
			&& Effect->Spec.GetContext().GetSourceObject() == this) BoundASC->RemoveActiveGameplayEffect(Handle);
	}
}

void URPGCombatControlComponent::RefreshAbilityImmunities()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	TArray<FGuid> Ended;
	for (const auto& Pair : AbilityImmunities)
	{
		const auto* Ability = Pair.Value.Ability.Get();
		if (!Ability || !Ability->IsActive() || Ability->GetAvatarActorFromActorInfo() != GetOwner()
			|| !BoundASC.IsValid() || Ability->GetAbilitySystemComponentFromActorInfo() != BoundASC.Get()
			|| (Pair.Value.Effect.IsValid() && !BoundASC->GetActiveGameplayEffect(Pair.Value.Effect))) Ended.Add(Pair.Key);
	}
	for (const auto& Token : Ended) EndAbilityImmunity(Token);
}
