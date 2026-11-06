#pragma once
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "RPGRidingActorTests.generated.h"

UCLASS(Transient, NotBlueprintable)
class URPGRidingTestAttributes : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY() FGameplayAttributeData CurrentHealth;
};

/** Empty test actor; it has no admission or ownership grant. Test injection is separately dev-only. */
UCLASS(Transient, NotBlueprintable)
class ARPGRidingTestPawn : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    ARPGRidingTestPawn();
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return ASC; }
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> ASC;
    UPROPERTY() TObjectPtr<URPGRidingTestAttributes> Attributes;
};
