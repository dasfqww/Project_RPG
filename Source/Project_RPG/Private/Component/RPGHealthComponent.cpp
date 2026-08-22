#include "Component/RPGHealthComponent.h"

#include "Character/RPGBaseCharacter.h"
#include "Attribute/RPGAttributeSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGHealthComponent)

URPGHealthComponent::URPGHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

URPGHealthComponent* URPGHealthComponent::FindHealthComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<URPGHealthComponent>() : nullptr;
}

float URPGHealthComponent::GetHealth() const
{
	const ARPGBaseCharacter* Character = Cast<ARPGBaseCharacter>(GetOwner());
	const URPGAttributeSet* AttributeSet = Character ? Character->GetRPGAttributeSet() : nullptr;
	return AttributeSet ? AttributeSet->GetCurrentHealth() : 0.0f;
}

float URPGHealthComponent::GetMaxHealth() const
{
	const ARPGBaseCharacter* Character = Cast<ARPGBaseCharacter>(GetOwner());
	const URPGAttributeSet* AttributeSet = Character ? Character->GetRPGAttributeSet() : nullptr;
	return AttributeSet ? AttributeSet->GetMaxHealth() : 0.0f;
}

float URPGHealthComponent::GetHealthNormalized() const
{
	const float MaxHealth = GetMaxHealth();
	return MaxHealth > 0.0f ? GetHealth() / MaxHealth : 0.0f;
}

bool URPGHealthComponent::IsDeadOrDying() const
{
	return GetHealth() <= 0.0f;
}
