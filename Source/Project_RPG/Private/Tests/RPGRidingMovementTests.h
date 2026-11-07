#pragma once
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "RPGRidingMovementTests.generated.h"

class UAbilitySystemComponent;
class URPGAttributeSet;

UCLASS(Transient, NotBlueprintable)
class ARPGRidingMovementTestPawn : public ACharacter, public IAbilitySystemInterface
{
    GENERATED_BODY()
public:
    ARPGRidingMovementTestPawn(const FObjectInitializer& Initializer = FObjectInitializer::Get());
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    UPROPERTY() TObjectPtr<UAbilitySystemComponent> ASC;
    UPROPERTY() TObjectPtr<URPGAttributeSet> Attributes;
};
