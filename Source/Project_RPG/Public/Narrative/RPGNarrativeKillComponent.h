#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Narrative/RPGNarrativeKill.h"
#include "RPGNarrativeKillComponent.generated.h"

UENUM(BlueprintType)
enum class ERPGNarrativeKillCreditPolicy : uint8
{
    Disabled,
    ConfirmedMembersInRadius
};

/** Add to an authored enemy NPC. Defaults make no product-wide participation-policy decision. */
UCLASS(ClassGroup=(Narrative), meta=(BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGNarrativeKillComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    URPGNarrativeKillComponent();
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Narrative") FString EnemyId;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Narrative")
    ERPGNarrativeKillCreditPolicy CreditPolicy = ERPGNarrativeKillCreditPolicy::Disabled;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RPG|Narrative", meta=(ClampMin="100", ClampMax="100000"))
    float CreditRadius = 2500.f;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION() void Arm();
    void DeathTagChanged(FGameplayTag Tag, int32 Count);
    FRPGNarrativeKillLife Life;
    FDelegateHandle DeathHandle;
};
