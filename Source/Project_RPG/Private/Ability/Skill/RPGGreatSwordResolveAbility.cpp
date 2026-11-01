#include "Ability/Skill/RPGGreatSwordResolveAbility.h"

#include "Ability/RPGComboSkillAbility.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Character/RPGBaseCharacter.h"
#include "Combat/RPGCombatControlTypes.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Component/Combat/PlayerCombatComponent.h"
#include "Component/Combat/RPGCombatControlComponent.h"
#include "Engine/World.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "PlayerClass/RPGPlayerClassComponent.h"
#include "PlayerClass/RPGPlayerClassDefinition.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "RPGAbilityTags.h"
#include "RPGGameplayTags.h"
#include "Security/RPGDungeonGameplayAuthority.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"

namespace
{
	bool Living(const AActor* Avatar, const UAbilitySystemComponent* ASC)
	{
		const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
		// ServerOnly GAS abilities still run CanActivateAbility on the owning
		// client before sending their activation RPC. This predicate is read-only.
		return IsValid(Avatar) && !Avatar->IsActorBeingDestroyed()
			&& Avatar->GetWorld() && !Avatar->GetWorld()->bIsTearingDown && ASC && ASC->GetAvatarActor() == Avatar
			&& Stats && FMath::IsFinite(Stats->GetCurrentHealth()) && Stats->GetCurrentHealth() > 0.f
			&& !ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death) && !IsDungeonGameplayRevoked(Avatar);
	}
	UAbilitySystemComponent* FindASC(AActor* Avatar)
	{
		return IsValid(Avatar) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Avatar) : nullptr;
	}
}

URPGGreatSwordResolveGaugeEffect::URPGGreatSwordResolveGaugeEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = URPGAttributeSet::GetCurrentIdentityGaugeAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Amount;
	Amount.DataTag = RPGGameplayTags::Player_Ability_Skill_GainIdentity;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
	Modifiers.Add(Modifier);
}

URPGGreatSwordResolveReadyEffect::URPGGreatSwordResolveReadyEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(10.f));
	FInheritedTagContainer Tags;
	Tags.Added.AddTag(RPGGameplayTags::Player_Status_Identity_GreatSword_Ready);
	auto* Granted = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("ResolveReadyTags"));
	GEComponents.Add(Granted);
	Granted->SetAndApplyTargetTagChanges(Tags);
}

URPGGreatSwordResolveAbility::URPGGreatSwordResolveAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	FGameplayTagContainer Tags;
	Tags.AddTag(RPGGameplayTags::Player_Ability_IdentitySkill);
	SetAssetTags(Tags);
}

bool URPGGreatSwordResolveAbility::IsConfiguredFor(const AActor* Avatar)
{
	const auto* Growth = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Definition = Growth ? Growth->GetPlayerClassDefinition() : nullptr;
	return Definition && Definition->ClassId == FName(TEXT("GreatSwordWarrior"))
		&& Definition->IdentityAbility == StaticClass();
}

bool URPGGreatSwordResolveAbility::CanUseIdentity(const FGameplayAbilityActorInfo* Info, FGameplayAbilitySpecHandle Handle)
{
	AActor* Avatar = Info ? Info->AvatarActor.Get() : nullptr;
	auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	const auto* Growth = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Owner = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	const auto* Combat = IsValid(Avatar) ? Avatar->FindComponentByClass<UPlayerCombatComponent>() : nullptr;
	const auto* Weapon = Combat ? Combat->GetPlayerCurrentEquippedWeapon() : nullptr;
	return Living(Avatar, ASC) && IsConfiguredFor(Avatar) && Growth && Growth->IsClassIdentityUnlocked()
		&& Owner && Owner->CanUseAbilitySpec(Handle, StaticClass()) && IsValid(Weapon)
		&& Combat->GetCharacterCurrentEquippedWeaponType() == ERPGGladiatorWeaponType::GreatSword;
}

bool URPGGreatSwordResolveAbility::CanActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const auto* ASC = Info ? Info->AbilitySystemComponent.Get() : nullptr;
	const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	return !bChangingState && CanUseIdentity(Info, Handle) && !RPGCombatControl::IsActionBlocked(Info->AvatarActor.Get())
		&& Stats && FMath::IsFinite(Stats->GetCurrentIdentityGauge()) && Stats->GetCurrentIdentityGauge() >= 100.f
		&& FMath::IsNearlyEqual(Stats->GetMaxIdentityGauge(), 100.f)
		&& !ASC->HasMatchingGameplayTag(RPGGameplayTags::Player_Status_Identity_GreatSword_Ready)
		&& Super::CanActivateAbility(Handle, Info, SourceTags, TargetTags, OptionalRelevantTags);
}

bool URPGGreatSwordResolveAbility::ApplyGaugeDelta(UGameplayAbility& Ability, float Delta)
{
	AActor* Avatar = Ability.GetAvatarActorFromActorInfo();
	auto* ASC = Ability.GetAbilitySystemComponentFromActorInfo();
	const auto* Stats = ASC ? ASC->GetSet<URPGAttributeSet>() : nullptr;
	if (!IsValid(Avatar) || !Avatar->HasAuthority() || !Living(Avatar, ASC) || !Stats || !FMath::IsFinite(Delta) || !FMath::IsFinite(Stats->GetCurrentIdentityGauge())
		|| !FMath::IsNearlyEqual(Stats->GetMaxIdentityGauge(), 100.f)) return false;
	if (Delta < 0.f && Stats->GetCurrentIdentityGauge() < -Delta) return false;
	if (Delta > 0.f) Delta = FMath::Clamp(Delta, 0.f, FMath::Max(0.f, 100.f - Stats->GetCurrentIdentityGauge()));
	if (Delta == 0.f) return true;
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.SetAbility(&Ability);
	Context.AddSourceObject(&Ability);
	const auto Spec = ASC->MakeOutgoingSpec(URPGGreatSwordResolveGaugeEffect::StaticClass(), 1.f, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(RPGGameplayTags::Player_Ability_Skill_GainIdentity, Delta);
	return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).WasSuccessfullyApplied();
}

void URPGGreatSwordResolveAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* Event)
{
	if (!Info || !Info->IsNetAuthority() || !CanActivateAbility(Handle, Info)) { EndAbility(Handle, Info, ActivationInfo, true, true); return; }
	TGuardValue<bool> Guard(bChangingState, true);
	Super::ActivateAbility(Handle, Info, ActivationInfo, Event);
	if (!IsActive() || !CanUseIdentity(Info, Handle) || RPGCombatControl::IsActionBlocked(Info->AvatarActor.Get()))
	{
		if (IsActive()) EndAbility(Handle, Info, ActivationInfo, true, true);
		return;
	}
	// Reserve the opportunity's exact lifetime before attribute delegates can reenter.
	RemoveOpportunity();
	auto* Avatar = Cast<ARPGBaseCharacter>(Info->AvatarActor.Get());
	auto* Growth = Avatar ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	ArmedAvatar = Avatar; ArmedASC = Info->AbilitySystemComponent;
	ArmedRestoreGeneration = Growth ? Growth->GetSkillRestoreGeneration() : 0;
	ArmedCombatRevision = Avatar ? Avatar->GetCombatPresentationRevision() : 0;
	const auto* Control = Avatar ? Avatar->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	ArmedLifetimeEpoch = Control ? Control->GetLifetimeEpoch() : 0;
	const bool bConsumed = Avatar && Growth && ApplyGaugeDelta(*this, -100.f);
	if (bConsumed && IsActive() && CanUseIdentity(Info, Handle)
		&& Avatar->GetCombatPresentationRevision() == ArmedCombatRevision)
	{
		FGameplayEffectContextHandle Context = ArmedASC->MakeEffectContext();
		Context.SetAbility(this); Context.AddSourceObject(this);
		const auto Spec = ArmedASC->MakeOutgoingSpec(URPGGreatSwordResolveReadyEffect::StaticClass(), 1.f, Context);
		if (Spec.IsValid()) ReadyEffect = ArmedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		// Applying a GE may remove this grant or end its Pawn. Never leave a detached opportunity.
		if (!CanUseIdentity(Info, Handle)) RemoveOpportunity();
	}
	if (IsActive()) EndAbility(Handle, Info, ActivationInfo, true, !bConsumed);
}

bool URPGGreatSwordResolveAbility::HasCurrentOpportunity() const
{
	const auto* Avatar = Cast<ARPGBaseCharacter>(ArmedAvatar.Get());
	const auto* Growth = Avatar ? Avatar->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr;
	const auto* Control = Avatar ? Avatar->FindComponentByClass<URPGCombatControlComponent>() : nullptr;
	if (bChangingState || !Avatar || !CurrentActorInfo || ArmedASC.Get() != CurrentActorInfo->AbilitySystemComponent.Get()
		|| Avatar != CurrentActorInfo->AvatarActor.Get() || !CanUseIdentity(CurrentActorInfo, CurrentSpecHandle)
		|| Avatar->GetCombatPresentationRevision() != ArmedCombatRevision || !Growth
		|| Growth->GetSkillRestoreGeneration() != ArmedRestoreGeneration || !Control
		|| Control->GetLifetimeEpoch() != ArmedLifetimeEpoch) return false;
	const auto* Effect = ArmedASC->GetActiveGameplayEffect(ReadyEffect);
	return Effect && Effect->GetTimeRemaining(Avatar->GetWorld()->GetTimeSeconds()) > 0.f;
}

void URPGGreatSwordResolveAbility::RemoveOpportunity()
{
	const auto Handle = ReadyEffect;
	ReadyEffect.Invalidate(); // Invalidate BEFORE effect removal callbacks can claim it again.
	if (ArmedASC.IsValid() && Handle.IsValid()) ArmedASC->RemoveActiveGameplayEffect(Handle);
}

void URPGGreatSwordResolveAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* Info, const FGameplayAbilitySpec& Spec)
{
	TGuardValue<bool> Guard(bChangingState, true);
	RemoveOpportunity();
	Super::OnRemoveAbility(Info, Spec);
}

void URPGGreatSwordResolveAbility::AwardConfirmedSkillHit(URPGGameplayAbility_SkillContainer& Skill)
{
	AActor* Avatar = Skill.GetAvatarActorFromActorInfo();
	auto* ASC = FindASC(Avatar);
	const auto* Owner = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	const auto* Identity = ASC ? ASC->FindAbilitySpecFromClass(StaticClass()) : nullptr;
	if (!Skill.IsActive() || !Owner || !Identity || !CanUseIdentity(Skill.GetCurrentActorInfo(), Identity->Handle)
		|| !Owner->CanUseAbilitySpec(Skill.GetCurrentAbilitySpecHandle(), Skill.GetClass())) return;
	ApplyGaugeDelta(Skill, 20.f);
}

void URPGGreatSwordResolveAbility::AwardCompletedBasicCombo(URPGComboSkillAbility& Combo)
{
	auto* ASC = FindASC(Combo.GetAvatarActorFromActorInfo());
	const auto* Identity = ASC ? ASC->FindAbilitySpecFromClass(StaticClass()) : nullptr;
	if (!Combo.IsActive() || !Identity || !CanUseIdentity(Combo.GetCurrentActorInfo(), Identity->Handle)
		|| !Combo.GetAssetTags().HasTagExact(RPGGameplayTags::Player_Ability_Attack_Light_GreatSword)) return;
	ApplyGaugeDelta(Combo, 10.f);
}

void URPGGreatSwordResolveAbility::ConsumeForCommittedSkill(URPGGameplayAbility_SkillContainer& Skill, float& OutMultiplier)
{
	OutMultiplier = 1.f;
	auto* Avatar = Skill.GetAvatarActorFromActorInfo();
	if (!IsValid(Avatar) || !Avatar->HasAuthority()) return;
	auto* ASC = FindASC(Avatar);
	const auto* Owner = IsValid(Avatar) ? Avatar->FindComponentByClass<URPGPlayerClassComponent>() : nullptr;
	const FGameplayTag Tag = Skill.GetActiveSkillSpec().SkillTag;
	if (!Skill.IsActive() || !Skill.GetActiveSkillSpec().ExecutionPolicyClass || !Owner
		|| !Owner->CanUseAbilitySpec(Skill.GetCurrentAbilitySpecHandle(), Skill.GetClass())
		|| (Tag != RPGGameplayTags::Player_Ability_Skill_Charge && Tag != RPGGameplayTags::Player_Ability_Skill_WhirlWind)) return;
	if (Tag == RPGGameplayTags::Player_Ability_Skill_Charge
		&& !Skill.GetActiveSkillSpec().ExecutionPolicyClass->IsChildOf(URPGSkillExecutionPolicy_Charge::StaticClass())) return;
	const auto* Spec = ASC ? ASC->FindAbilitySpecFromClass(StaticClass()) : nullptr;
	auto* Identity = Spec ? Cast<URPGGreatSwordResolveAbility>(Spec->GetPrimaryInstance()) : nullptr;
	if (!Identity || !Identity->HasCurrentOpportunity() || RPGCombatControl::IsActionBlocked(Avatar)) return;
	TGuardValue<bool> Guard(Identity->bChangingState, true);
	OutMultiplier = 1.25f; // Freeze BEFORE removal delegates can dispatch this cast's first hit.
	Identity->RemoveOpportunity();
}
