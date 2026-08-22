// Fill out your copyright notice in the Description page of Project Settings.


#include "Component/RPGAbilitySystemComponent.h"
#include "Ability/RPGGameplayAbility.h"
#include "Ability/RPGPlayerGameplayAbility.h"
#include "RPGGameplayTags.h"

#include "RPGDebugHelper.h"

FActiveGameplayEffect* URPGAbilitySystemComponent::GetActiveGameplayEffectMutable(const FActiveGameplayEffectHandle Handle)
{
	return ActiveGameplayEffects.GetActiveGameplayEffect(Handle);
}

TArray<FActiveGameplayEffectHandle> URPGAbilitySystemComponent::GetAllActiveEffectHandles() const
{
	return ActiveGameplayEffects.GetAllActiveEffectHandles();
}

void URPGAbilitySystemComponent::MarkActiveGameplayEffectDirty(FActiveGameplayEffect* ActiveGameplayEffect)
{
	if (ActiveGameplayEffect)
	{
		ActiveGameplayEffects.MarkItemDirty(*ActiveGameplayEffect);
	}
}

void URPGAbilitySystemComponent::CheckActiveEffectDuration(const FActiveGameplayEffectHandle Handle)
{
	ActiveGameplayEffects.CheckDuration(Handle);
}

void URPGAbilitySystemComponent::OnAbilityInputPressed(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (!AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag)) continue;

		if (InInputTag.MatchesTag(RPGGameplayTags::InputTag_Toggleable)&& AbilitySpec.IsActive())
		{
			CancelAbilityHandle(AbilitySpec.Handle);
		}
		else
		{
			OnAbilitySpecInputPressed(AbilitySpec.Handle);
		}
	}
}

void URPGAbilitySystemComponent::OnAbilityInputReleased(const FGameplayTag& InInputTag)
{
	if (!InInputTag.IsValid())
	{
		return;
	}

	for (FGameplayAbilitySpec& AbilitySpec : GetActivatableAbilities())
	{
		if (AbilitySpec.GetDynamicSpecSourceTags().HasTagExact(InInputTag))
		{
			OnAbilitySpecInputReleased(AbilitySpec.Handle);
		}
	}
}

void URPGAbilitySystemComponent::OnAbilitySpecInputPressed(
	const FGameplayAbilitySpecHandle SpecHandle)
{
	FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	if (!AbilitySpec)
	{
		return;
	}

	AbilitySpec->InputPressed = true;
	if (AbilitySpec->IsActive())
	{
		AbilitySpecInputPressed(*AbilitySpec);
		return;
	}

	TryActivateAbility(AbilitySpec->Handle);
}

void URPGAbilitySystemComponent::OnAbilitySpecInputReleased(
	const FGameplayAbilitySpecHandle SpecHandle)
{
	FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	if (!AbilitySpec)
	{
		return;
	}

	AbilitySpec->InputPressed = false;
	if (AbilitySpec->IsActive())
	{
		AbilitySpecInputReleased(*AbilitySpec);
	}
}

FGameplayAbilitySpecHandle URPGAbilitySystemComponent::FindUniqueAbilitySpecHandleByTag(
	const FGameplayTag& AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return FGameplayAbilitySpecHandle();
	}

	TArray<FGameplayAbilitySpec*> MatchingAbilitySpecs;
	GetActivatableGameplayAbilitySpecsByAllMatchingTags(
		AbilityTag.GetSingleTagContainer(), MatchingAbilitySpecs);
	if (MatchingAbilitySpecs.Num() != 1 || !MatchingAbilitySpecs[0])
	{
		UE_LOG(LogTemp, Warning,
			TEXT("Expected exactly one ability for tag %s, found %d."),
			*AbilityTag.ToString(),
			MatchingAbilitySpecs.Num());
		return FGameplayAbilitySpecHandle();
	}

	return MatchingAbilitySpecs[0]->Handle;
}

bool URPGAbilitySystemComponent::IsAbilitySpecInputPressed(
	const FGameplayAbilitySpecHandle SpecHandle) const
{
	const FGameplayAbilitySpec* AbilitySpec = FindAbilitySpecFromHandle(SpecHandle);
	return AbilitySpec && AbilitySpec->InputPressed;
}

void URPGAbilitySystemComponent::AbilitySpecInputPressed(FGameplayAbilitySpec& Spec)
{
	UGameplayAbility* AbilityInstance = Spec.GetPrimaryInstance();
	if (AbilityInstance)
	{
		bool bShouldReplicateInput = true;
		if (URPGGameplayAbility* RPGAbility =
			Cast<URPGGameplayAbility>(AbilityInstance))
		{
			bShouldReplicateInput =
				RPGAbility->PreReplicateAbilityInputPressed();
		}

		// Notify GAS input tasks first. A policy may end the ability from its
		// direct InputPressed callback, which would otherwise destroy the task
		// before it can forward the event to a remote server.
		if (bShouldReplicateInput)
		{
			InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputPressed,
				Spec.Handle,
				AbilityInstance->GetCurrentActivationInfo()
					.GetActivationPredictionKey());
		}
	}

	Super::AbilitySpecInputPressed(Spec);
}

void URPGAbilitySystemComponent::AbilitySpecInputReleased(FGameplayAbilitySpec& Spec)
{
	UGameplayAbility* AbilityInstance = Spec.GetPrimaryInstance();
	if (AbilityInstance)
	{
		bool bShouldReplicateInput = true;
		if (URPGGameplayAbility* RPGAbility =
			Cast<URPGGameplayAbility>(AbilityInstance))
		{
			bShouldReplicateInput =
				RPGAbility->PreReplicateAbilityInputReleased();
		}

		// The replicated task must observe release before a local policy can
		// synchronously end the ability and tear that task down.
		if (bShouldReplicateInput)
		{
			InvokeReplicatedEvent(
				EAbilityGenericReplicatedEvent::InputReleased,
				Spec.Handle,
				AbilityInstance->GetCurrentActivationInfo()
					.GetActivationPredictionKey());
		}
	}

	Super::AbilitySpecInputReleased(Spec);
}

void URPGAbilitySystemComponent::GrantPlayerWeaponAbilities(const TArray<FRPGPlayerAbilitySet> InDefaultWeaponAbilities, 
	const TArray<FRPGPlayerSkillSet>& InSkillAbilities, int32 ApplyLevel, 
	TArray<FGameplayAbilitySpecHandle>& OutGrantedAbilitySpecHandles)
{
	if (InDefaultWeaponAbilities.IsEmpty())
	{
		return;
	}

	for (const FRPGPlayerAbilitySet& AbilitySet : InDefaultWeaponAbilities)
	{
		if (!AbilitySet.IsValid()) continue;

		FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
		AbilitySpec.SourceObject = GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);

		OutGrantedAbilitySpecHandles.AddUnique(GiveAbility(AbilitySpec));
	}
	
	for (const FRPGPlayerSkillSet& AbilitySet : InSkillAbilities)
	{
		if (!AbilitySet.IsValid()) continue;

		FGameplayAbilitySpec AbilitySpec(AbilitySet.AbilityToGrant);
		AbilitySpec.SourceObject = GetAvatarActor();
		AbilitySpec.Level = ApplyLevel;
		AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilitySet.InputTag);

		OutGrantedAbilitySpecHandles.AddUnique(GiveAbility(AbilitySpec));
	}
}

void URPGAbilitySystemComponent::RemovedGrantedPlayerWeaponAbilities(UPARAM(ref) TArray<FGameplayAbilitySpecHandle>& InSpecHandlesToRemove)
{
	if (InSpecHandlesToRemove.IsEmpty())
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& SpecHandle : InSpecHandlesToRemove)
	{
		if (SpecHandle.IsValid())
		{
			ClearAbility(SpecHandle);
		}
	}

	InSpecHandlesToRemove.Empty();
}

bool URPGAbilitySystemComponent::TryActivateAbilityByTag(FGameplayTag AbilityTagToActivate)
{
	const FGameplayAbilitySpecHandle SpecHandle =
		FindUniqueAbilitySpecHandleByTag(AbilityTagToActivate);
	return SpecHandle.IsValid() && TryActivateAbility(SpecHandle);
}
