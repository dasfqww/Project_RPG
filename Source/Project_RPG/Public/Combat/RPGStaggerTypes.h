#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Combat/RPGHitReactionTypes.h"
#include "RPGStaggerTypes.generated.h"
struct FGameplayAbilitySpec;
class AActor;

/** Optional short hit stun. Distinct from stun ailments and from cosmetic-only HitReact. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGStaggerSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="2", Units="s"))
	float Duration = 0.f;
	bool IsValid() const { return FMath::IsFinite(Duration) && (Duration == 0.f || (Duration >= .05f && Duration <= 2.f)); }
	bool IsEnabled() const { return Duration > 0.f && IsValid(); }
};

/** Read-only projection of server GE state, including a frozen presentation direction. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGStaggerState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY(BlueprintReadOnly) double StartServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) double EndServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) ERPGHitReactionDirection Direction = ERPGHitReactionDirection::Front;
	UPROPERTY() uint32 ExecutionId = 0;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY() uint32 Revision = 0;
};

namespace RPGStagger
{
	PROJECT_RPG_API FGameplayTag StatusTag();
	PROJECT_RPG_API bool IsStaggered(const AActor* Actor);
	PROJECT_RPG_API bool HasHigherControl(const AActor* Actor);
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGStaggerSettings& Settings);
	PROJECT_RPG_API FRPGStaggerSettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
