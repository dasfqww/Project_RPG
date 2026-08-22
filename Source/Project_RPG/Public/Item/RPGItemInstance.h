#pragma once

#include "CoreMinimal.h"
#include "Item/RPGItemRuntimeTypes.h"
#include "RPGItemInstance.generated.h"

class URPGItemDefinition;

DECLARE_MULTICAST_DELEGATE_OneParam(
	FOnRPGItemInstanceChanged,
	const class URPGItemInstance&);

/** Replicated runtime identity and mutable state for one item stack. */
UCLASS(BlueprintType)
class PROJECT_RPG_API URPGItemInstance : public UObject
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool IsSupportedForNetworking() const override { return true; }

	bool Initialize(
		URPGItemDefinition* InDefinition,
		int32 RequestedQuantity,
		int32 GenerationSeed);

	bool SetQuantity(int32 NewQuantity);

	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	URPGItemDefinition* GetDefinition() const { return Definition; }

	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	int32 GetQuantity() const { return State.GetQuantity(); }

	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	int32 GetMaxStackSize() const;

	UFUNCTION(BlueprintPure, Category = "RPG|Item")
	bool IsStackable() const;

	const FRPGItemInstanceState& GetState() const { return State; }

	FOnRPGItemInstanceChanged OnChanged;

private:
	UFUNCTION()
	void OnRep_Definition();

	UFUNCTION()
	void OnRep_State();

	void BroadcastChanged();

	UPROPERTY(ReplicatedUsing = OnRep_Definition)
	TObjectPtr<URPGItemDefinition> Definition;

	UPROPERTY(ReplicatedUsing = OnRep_State)
	FRPGItemInstanceState State;
};
