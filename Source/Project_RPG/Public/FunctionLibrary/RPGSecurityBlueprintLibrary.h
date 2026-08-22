#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Security/RPGSecurityTypes.h"
#include "RPGSecurityBlueprintLibrary.generated.h"

class UGameplayEffect;
class URPGSecurityValidationComponent;

/** Safe Blueprint entry points for gameplay-affecting server operations. */
UCLASS()
class PROJECT_RPG_API URPGSecurityBlueprintLibrary
	: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "RPG|Security")
	static URPGSecurityValidationComponent* GetSecurityValidationComponent(
		AActor* Actor);

	/** Validates a server-produced hit without applying an effect. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|Security|Combat",
		meta = (DisplayName = "Validate Authorized Server Hit",
			ExpandBoolAsExecs = "ReturnValue"))
	static bool ValidateAuthorizedServerHit(
		AActor* SourceActor,
		const FHitResult& ServerHit,
		float Damage,
		const FRPGSkillSecurityProfile& SecurityProfile,
		FText& OutError);

	/**
	 * Applies one server-produced hit using an authored skill profile.
	 * Cosmetic clients receive False and can only play presentation separately.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
		Category = "RPG|Security|Combat",
		meta = (DisplayName = "Apply Authorized Server Damage",
			ExpandBoolAsExecs = "ReturnValue"))
	static bool ApplyAuthorizedServerDamage(
		AActor* SourceActor,
		const FHitResult& ServerHit,
		TSubclassOf<UGameplayEffect> DamageEffectClass,
		float Damage,
		FGameplayTag SetByCallerDamageTag,
		const FRPGSkillSecurityProfile& SecurityProfile,
		FActiveGameplayEffectHandle& OutEffectHandle,
		FText& OutError);
};
