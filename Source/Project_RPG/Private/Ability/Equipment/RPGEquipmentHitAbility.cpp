#include "Ability/Equipment/RPGEquipmentHitAbility.h"
#include "Ability/Skill/RPGGreatSwordResolveAbility.h"
#include "AbilitySystemComponent.h"
#include "Component/Equipment/RPGAuthoritativeEquipmentComponent.h"
#include "Component/Combat/RPGShieldComponent.h"
#include "Item/Definition/RPGItemDefinitionFragments.h"
#include "Skill/RPGApprovedSkillHitEvent.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "GameFramework/Actor.h"

URPGEquipmentHitAbility::URPGEquipmentHitAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnly;
	AbilityActivationPolicy = ERPGAbilityActivationPolicy::OnGiven;
	ActivationPolicy = ERPGGladiatorAbilityActivationPolicy::Manual;
	bCountTowardServerActivationRateLimit = false;
}

bool URPGEquipmentHitAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* Relevant) const
{
	// A passive must also bind when equipment is restored during stun/death. Its
	// event consumer, not input activation rules, checks the living combat lifetime.
	return Info && Info->IsNetAuthority() && Info->AvatarActor.IsValid()
		&& UGameplayAbility::CanActivateAbility(Handle, Info, SourceTags, TargetTags, Relevant);
}

void URPGEquipmentHitAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Activation, const FGameplayEventData* Event)
{
	UGameplayAbility::ActivateAbility(Handle, Info, Activation, Event);
	Unbind();
	if (!Info || !Info->IsNetAuthority() || !Info->AbilitySystemComponent.IsValid())
	{
		EndAbility(Handle, Info, Activation, false, true);
		return;
	}
	ObservedASC = Info->AbilitySystemComponent;
	ObservedAvatar = Info->AvatarActor;
	ObservedEquipment = ObservedAvatar.IsValid() ? ObservedAvatar->FindComponentByClass<URPGAuthoritativeEquipmentComponent>() : nullptr;
	if (!ObservedEquipment.IsValid()) { EndAbility(Handle, Info, Activation, false, true); return; }
	EffectSource = FGuid::NewGuid();
	HitHandle = ObservedASC->GenericGameplayEventCallbacks.FindOrAdd(URPGApprovedSkillHitEvent::EventTag()).AddUObject(this, &ThisClass::HandleApprovedHit);
	EquipmentHandle = ObservedEquipment->OnEquipmentSnapshotsChanged.AddUObject(this, &ThisClass::HandleEquipmentChanged);
	ActiveStateHandle = ObservedEquipment->OnActiveEquipStateChanged.AddUObject(this, &ThisClass::HandleActiveStateChanged);
}

void URPGEquipmentHitAbility::HandleApprovedHit(const FGameplayEventData* Payload)
{
	const auto* Event = Payload ? Cast<URPGApprovedSkillHitEvent>(Payload->OptionalObject) : nullptr;
	const auto* Spec = GetCurrentAbilitySpec();
	const auto* Definition = Spec ? Cast<URPGItemDefinition>(Spec->SourceObject.Get()) : nullptr;
	if (!IsActive() || !Event || !Event->IsCurrent() || !Definition || !ObservedASC.IsValid()
		|| !ObservedAvatar.IsValid() || GetAvatarActorFromActorInfo() != ObservedAvatar.Get()
		|| ObservedASC->GetAvatarActor() != ObservedAvatar.Get() || !ObservedEquipment.IsValid()
		|| !ObservedEquipment->IsAbilityFromEffectContext(CurrentSpecHandle, Event->GetEquipment())
		|| Definition->GetPrimaryAssetId() != Event->GetEquipment().DefinitionId
		|| Definition->GetDefinitionVersion() != Event->GetEquipment().DefinitionVersion) return;
	const auto* Trigger = GetTrigger(*Definition);
	if (!Trigger || !Trigger->RequiredSkillTag.IsValid() || Trigger->RequiredSkillTag != Event->GetSkillTag()
		|| (Trigger->bRequireMaximumCharge && !Event->WasMaximumCharge())
		|| (Trigger->bRequireFinisher && !Event->WasFinisher())) return;
	const TWeakObjectPtr<URPGGameplayAbility_SkillContainer> Key(Event->GetSkill());
	if (const auto* Previous = LastAwardedActivation.Find(Key); Previous && *Previous >= Event->GetActivationSerial()) return;
	LastAwardedActivation.Add(Key, Event->GetActivationSerial()); // Before any reentrant effect callback.
	LastSource = Event->GetEquipment();
	ApplyHitEffect(*Event, *Definition);
}

void URPGEquipmentHitAbility::HandleEquipmentChanged()
{
	if (LastSource.IsValid() && (!ObservedEquipment.IsValid()
		|| !ObservedEquipment->IsAbilityFromEffectContext(CurrentSpecHandle, LastSource))) ReleaseEffect();
}

void URPGEquipmentHitAbility::HandleActiveStateChanged(EEquipState, EEquipState) { ReleaseEffect(); }

void URPGEquipmentHitAbility::Unbind()
{
	ReleaseEffect();
	if (ObservedASC.IsValid())
		if (auto* Delegate = ObservedASC->GenericGameplayEventCallbacks.Find(URPGApprovedSkillHitEvent::EventTag())) Delegate->Remove(HitHandle);
	if (ObservedEquipment.IsValid())
	{
		ObservedEquipment->OnEquipmentSnapshotsChanged.Remove(EquipmentHandle);
		ObservedEquipment->OnActiveEquipStateChanged.Remove(ActiveStateHandle);
	}
	ObservedASC.Reset(); ObservedEquipment.Reset(); ObservedAvatar.Reset();
	LastAwardedActivation.Reset(); LastSource = {};
	EffectSource.Invalidate();
}

void URPGEquipmentHitAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* Info, const FGameplayAbilityActivationInfo Activation, const bool bReplicate, const bool bCancelled)
{
	Unbind();
	UGameplayAbility::EndAbility(Handle, Info, Activation, bReplicate, bCancelled);
}

void URPGEquipmentHitAbility::OnRemoveAbility(const FGameplayAbilityActorInfo* Info, const FGameplayAbilitySpec& Spec)
{
	Unbind();
	Super::OnRemoveAbility(Info, Spec);
}

const URPGItemHitTriggerDefinitionFragment* URPGEquipmentShieldOnHitAbility::GetTrigger(const URPGItemDefinition& Definition) const
{ return Definition.FindFragmentByClass<URPGItemShieldOnHitDefinitionFragment>(); }

void URPGEquipmentShieldOnHitAbility::ApplyHitEffect(const URPGApprovedSkillHitEvent&, const URPGItemDefinition& Definition)
{
	const auto* Data = Definition.FindFragmentByClass<URPGItemShieldOnHitDefinitionFragment>();
	if (Data) if (auto* Shield = URPGShieldComponent::FindOrCreate(GetAvatarActorFromActorInfo()))
	{
		OwnedShield = Shield;
		Shield->GrantOrRefresh(EffectSource, Data->MaximumHealthFraction, Data->DurationSeconds);
	}
}

void URPGEquipmentShieldOnHitAbility::ReleaseEffect()
{
	if (OwnedShield.IsValid()) OwnedShield->RemoveSource(EffectSource);
	OwnedShield.Reset();
}

const URPGItemHitTriggerDefinitionFragment* URPGEquipmentResolveOnHitAbility::GetTrigger(const URPGItemDefinition& Definition) const
{ return Definition.FindFragmentByClass<URPGItemResolveOnHitDefinitionFragment>(); }

void URPGEquipmentResolveOnHitAbility::ApplyHitEffect(const URPGApprovedSkillHitEvent& Event, const URPGItemDefinition& Definition)
{
	const auto* Data = Definition.FindFragmentByClass<URPGItemResolveOnHitDefinitionFragment>();
	if (Data && Event.GetSkill()) URPGGreatSwordResolveAbility::AwardConfirmedSkillHit(*Event.GetSkill(), Data->Amount);
}
