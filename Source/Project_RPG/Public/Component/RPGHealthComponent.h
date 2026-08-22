#pragma once

#include "Components/ActorComponent.h"
#include "RPGHealthComponent.generated.h"

/** Lightweight health facade used by imported Lyra/D1 assets. */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class PROJECT_RPG_API URPGHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPGHealthComponent();

	UFUNCTION(BlueprintPure, Category = "RPG|Health")
	static URPGHealthComponent* FindHealthComponent(const AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "RPG|Health")
	float GetHealth() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Health")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintCallable, Category = "RPG|Health")
	float GetHealthNormalized() const;

	/** ExpandBoolAsExecs preserves the True/False pins serialized by D1 Blueprints. */
	UFUNCTION(BlueprintCallable, BlueprintPure = false, Category = "RPG|Health",
		meta = (ExpandBoolAsExecs = "ReturnValue"))
	bool IsDeadOrDying() const;
};
