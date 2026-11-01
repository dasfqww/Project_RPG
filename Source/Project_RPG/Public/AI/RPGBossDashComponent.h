#pragma once

#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RPGBossDashComponent.generated.h"

/** Presentation snapshot only. CharacterMovement replicates actual collision-resolved movement. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGBossDashState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Boss") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 EncounterId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 PhaseIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") int32 Sequence = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") FGameplayTag ActionTag;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") FVector StartLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") FVector EndLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") float HitRadius = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly, Category = "Boss") double EndServerTime = 0.0;
};

/** No ASC, movement driver, RPC, or client damage decision. */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGBossDashComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URPGBossDashComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintPure, Category = "Boss|Dash") FRPGBossDashState GetDashState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "Boss|Dash") bool IsDashActive() const;
	// Native authoritative ability handoff. A stale execution cannot clear a newer one.
	bool BeginDash(const FRPGBossDashState& NewState);
	bool EndDash(int32 EncounterId, int32 Sequence);
private:
	UPROPERTY(Replicated) FRPGBossDashState State;
};
