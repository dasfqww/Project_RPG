#include "Skill/QuickSlot/RPGSkillQuickSlotServices.h"

#include "Ability/RPGGameplayAbility.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "GameFramework/Actor.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGClassSkillEligibility.h"

FRPGGASSkillQuickSlotAccess::FRPGGASSkillQuickSlotAccess(URPGAbilitySystemComponent* InASC) : ASC(InASC) {}

FGameplayAbilitySpecHandle FRPGGASSkillQuickSlotAccess::Resolve(const FGameplayTag SkillTag) const
{
	URPGAbilitySystemComponent* Component = ASC.Get();
	if (!Component || !SkillTag.IsValid()) return {};
	FScopedAbilityListLock Lock(*Component);
	FGameplayAbilitySpecHandle Result;
	for (const FGameplayAbilitySpec& Spec : Component->GetActivatableAbilities())
	{
		const UGameplayAbility* Ability = Spec.GetPrimaryInstance() ? Spec.GetPrimaryInstance() : Spec.Ability.Get();
		const auto* Container = Cast<URPGGameplayAbility_SkillContainer>(Ability);
		const URPGSkillDefinition* Definition = Container ? Container->GetSkillDefinition() : nullptr;
		const bool bMatches = Definition ? Definition->SkillTag == SkillTag
			: (Ability && Ability->GetAssetTags().HasTagExact(SkillTag));
		if (!bMatches) continue;
		// Eight combat slots are for acquired class actives, never common/identity abilities.
		if (!Container || !RPGClassSkillEligibility::CanAssign(Component->GetAvatarActor(),
			SkillTag, Definition, Ability->GetClass(), Spec.Handle)) return {};
		// Registration/lookup must not depend on current cooldown or blocked tags.
		if (Result.IsValid()) return {}; // ambiguous grant, never choose arbitrarily
		Result = Spec.Handle;
	}
	return Result;
}

FRPGSkillQuickSlotPresentation FRPGGASSkillQuickSlotAccess::Read(const FGameplayTag SkillTag) const
{
	FRPGSkillQuickSlotPresentation Result;
	URPGAbilitySystemComponent* Component = ASC.Get();
	if (!Component) return Result;
	FScopedAbilityListLock Lock(*Component);
	const FGameplayAbilitySpec* Spec = Component->FindAbilitySpecFromHandle(Resolve(SkillTag));
	if (!Spec) return Result;
	const UGameplayAbility* Ability = Spec->GetPrimaryInstance() ? Spec->GetPrimaryInstance() : Spec->Ability.Get();
	const auto* Container = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	const URPGSkillDefinition* Definition = Container ? Container->GetSkillDefinition() : nullptr;
	if (Definition)
	{
		// Show the current configuration for the next cast, without modifying an
		// in-flight ability's frozen spec or duplicating Definition override rules.
		AActor* AvatarActor = Component->GetAvatarActor();
		FRPGSkillSaveData SaveData;
		if (const URPGPlayerSkillComponent* SkillComponent = AvatarActor
			? AvatarActor->FindComponentByClass<URPGPlayerSkillComponent>() : nullptr)
		{
			SaveData = SkillComponent->GetSkillSaveData(Definition->SkillTag);
		}
		FRPGSkillRuntimeSpec PresentationSpec;
		Definition->BuildRuntimeSpec(AvatarActor, SaveData, PresentationSpec);
		Result.Icon = PresentationSpec.Icon;
		Result.Name = Definition->SkillName;
	}
	else if (const auto* RPGAbility = Cast<URPGGameplayAbility>(Ability))
	{
		Result.Icon = RPGAbility->GetAbilityIcon();
	}
	if (Ability && Component->AbilityActorInfo.IsValid())
	{
		Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, Component->AbilityActorInfo.Get(),
			Result.CooldownRemaining, Result.CooldownDuration);
		if (!FMath::IsFinite(Result.CooldownRemaining) || !FMath::IsFinite(Result.CooldownDuration))
		{
			Result.CooldownRemaining = Result.CooldownDuration = 0.0f;
		}
		Result.CooldownRemaining = FMath::Max(0.0f, Result.CooldownRemaining);
		Result.CooldownDuration = FMath::Max(0.0f, Result.CooldownDuration);
	}
	Result.bIsGranted = Ability != nullptr;
	return Result;
}

void FRPGGASSkillQuickSlotAccess::Press(const FGameplayAbilitySpecHandle Handle)
{
	if (URPGAbilitySystemComponent* Component = ASC.Get()) Component->OnAbilitySpecInputPressed(Handle);
}

void FRPGGASSkillQuickSlotAccess::Release(const FGameplayAbilitySpecHandle Handle)
{
	if (URPGAbilitySystemComponent* Component = ASC.Get()) Component->OnAbilitySpecInputReleased(Handle);
}

bool FRPGSkillQuickSlotInputRouter::Press(FPressedSlots& State, const int32 Slot,
	const FGameplayAbilitySpecHandle Handle, IRPGSkillQuickSlotInput& Input)
{
	if (Slot < 0 || !Handle.IsValid() || State.Contains(Slot)) return false;
	const bool bAlreadyHeld = State.FindKey(Handle) != nullptr;
	State.Add(Slot, Handle); // publish before reentrant input callbacks
	if (!bAlreadyHeld) Input.Press(Handle);
	return true;
}

void FRPGSkillQuickSlotInputRouter::Release(FPressedSlots& State, const int32 Slot,
	IRPGSkillQuickSlotInput& Input)
{
	FGameplayAbilitySpecHandle Handle;
	if (!State.RemoveAndCopyValue(Slot, Handle)) return;
	// If multiple keys reference one spec, the last key-up releases it.
	if (State.FindKey(Handle) == nullptr) Input.Release(Handle);
}

void FRPGSkillQuickSlotInputRouter::ReleaseAll(FPressedSlots& State, IRPGSkillQuickSlotInput& Input)
{
	TSet<FGameplayAbilitySpecHandle> Handles;
	for (const auto& Pair : State) Handles.Add(Pair.Value);
	State.Reset();
	for (const FGameplayAbilitySpecHandle Handle : Handles) Input.Release(Handle);
}
