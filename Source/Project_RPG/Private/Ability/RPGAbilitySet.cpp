#include "Ability/RPGAbilitySet.h"

#include "Ability/RPGGameplayAbility.h"
#include "Component/RPGAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGAbilitySet)

void FRPGAbilitySet_GrantedHandles::AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle)
{
	if (Handle.IsValid())
	{
		AbilitySpecHandles.Add(Handle);
	}
}

void FRPGAbilitySet_GrantedHandles::AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle)
{
	if (Handle.IsValid())
	{
		GameplayEffectHandles.Add(Handle);
	}
}

void FRPGAbilitySet_GrantedHandles::AddAttributeSet(UAttributeSet* AttributeSet)
{
	if (IsValid(AttributeSet))
	{
		GrantedAttributeSets.Add(AttributeSet);
	}
}

void FRPGAbilitySet_GrantedHandles::TakeFromAbilitySystem(URPGAbilitySystemComponent* AbilitySystemComponent)
{
	if (!IsValid(AbilitySystemComponent) || !AbilitySystemComponent->IsOwnerActorAuthoritative())
	{
		return;
	}

	for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
	{
		if (Handle.IsValid())
		{
			AbilitySystemComponent->ClearAbility(Handle);
		}
	}

	for (const FActiveGameplayEffectHandle& Handle : GameplayEffectHandles)
	{
		if (Handle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(Handle);
		}
	}

	for (UAttributeSet* AttributeSet : GrantedAttributeSets)
	{
		if (IsValid(AttributeSet))
		{
			AbilitySystemComponent->RemoveSpawnedAttribute(AttributeSet);
		}
	}

	AbilitySpecHandles.Reset();
	GameplayEffectHandles.Reset();
	GrantedAttributeSets.Reset();
}

void URPGAbilitySet::GiveToAbilitySystem(
	URPGAbilitySystemComponent* AbilitySystemComponent,
	FRPGAbilitySet_GrantedHandles* OutGrantedHandles,
	UObject* SourceObject) const
{
	if (!IsValid(AbilitySystemComponent) || !AbilitySystemComponent->IsOwnerActorAuthoritative())
	{
		return;
	}

	for (int32 AbilityIndex = 0; AbilityIndex < GrantedGameplayAbilities.Num(); ++AbilityIndex)
	{
		const FRPGAbilitySet_GameplayAbility& AbilityToGrant = GrantedGameplayAbilities[AbilityIndex];
		if (!AbilityToGrant.Ability)
		{
			UE_LOG(LogTemp, Warning, TEXT("AbilitySet %s has an invalid ability at index %d."), *GetNameSafe(this), AbilityIndex);
			continue;
		}

		FGameplayAbilitySpec AbilitySpec(AbilityToGrant.Ability, AbilityToGrant.AbilityLevel);
		AbilitySpec.SourceObject = SourceObject;
		if (AbilityToGrant.InputTag.IsValid())
		{
			AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityToGrant.InputTag);
		}

		const FGameplayAbilitySpecHandle Handle = AbilitySystemComponent->GiveAbility(AbilitySpec);
		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddAbilitySpecHandle(Handle);
		}
	}

	for (int32 EffectIndex = 0; EffectIndex < GrantedGameplayEffects.Num(); ++EffectIndex)
	{
		const FRPGAbilitySet_GameplayEffect& EffectToGrant = GrantedGameplayEffects[EffectIndex];
		if (!EffectToGrant.GameplayEffect)
		{
			UE_LOG(LogTemp, Warning, TEXT("AbilitySet %s has an invalid gameplay effect at index %d."), *GetNameSafe(this), EffectIndex);
			continue;
		}

		const UGameplayEffect* EffectCDO = EffectToGrant.GameplayEffect->GetDefaultObject<UGameplayEffect>();
		const FActiveGameplayEffectHandle Handle = AbilitySystemComponent->ApplyGameplayEffectToSelf(
			EffectCDO,
			EffectToGrant.EffectLevel,
			AbilitySystemComponent->MakeEffectContext());

		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddGameplayEffectHandle(Handle);
		}
	}

	for (int32 AttributeIndex = 0; AttributeIndex < GrantedAttributes.Num(); ++AttributeIndex)
	{
		const FRPGAbilitySet_AttributeSet& AttributeToGrant = GrantedAttributes[AttributeIndex];
		if (!AttributeToGrant.AttributeSet)
		{
			UE_LOG(LogTemp, Warning, TEXT("AbilitySet %s has an invalid attribute set at index %d."), *GetNameSafe(this), AttributeIndex);
			continue;
		}

		UAttributeSet* NewAttributeSet = NewObject<UAttributeSet>(
			AbilitySystemComponent->GetOwner(),
			AttributeToGrant.AttributeSet);
		AbilitySystemComponent->AddAttributeSetSubobject(NewAttributeSet);

		if (OutGrantedHandles)
		{
			OutGrantedHandles->AddAttributeSet(NewAttributeSet);
		}
	}
}
