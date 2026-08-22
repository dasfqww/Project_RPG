#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RPGItemFactory.generated.h"

class URPGItemDefinition;
class URPGItemInstance;

/** The only object-allocation entry point for canonical item instances. */
UCLASS()
class PROJECT_RPG_API URPGItemFactory : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "RPG|Item",
		meta = (DefaultToSelf = "Outer"))
	static URPGItemInstance* CreateItemInstance(
		UObject* Outer,
		URPGItemDefinition* Definition,
		int32 Quantity = 1);

	/** Deterministic creation path used by persistence, loot rolls, and tests. */
	static URPGItemInstance* CreateItemInstanceWithSeed(
		UObject* Outer,
		URPGItemDefinition* Definition,
		int32 Quantity,
		int32 GenerationSeed);
};
