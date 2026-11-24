#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Combat/RPGHitReactionTypes.h"
#include "RPGKnockbackTypes.generated.h"

struct FGameplayAbilitySpec;

/** Shared attack reaction for Player/NPC. GroundFallDuration selects a stationary knockdown. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGKnockbackSettings
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1000", Units="cm"))
	float Distance = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1", ClampMax="2", Units="s", EditCondition="Height <= 0"))
	float Duration = .4f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="600", Units="cm"))
	float Height = 0.f;
	/** Time from impact until the grounded fall reaches the down pose. Zero disables grounded knockdown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="2", Units="s"))
	float GroundFallDuration = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1", ClampMax="4", Units="s", EditCondition="Height > 0 || GroundFallDuration > 0"))
	float DownDuration = .8f;
	/** Optional upper bound for the down time. Zero uses DownDuration as a fixed time. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="4", Units="s", EditCondition="Height > 0 || GroundFallDuration > 0"))
	float MaxDownDuration = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(ClampMin="0.1", ClampMax="3", Units="s", EditCondition="Height > 0 || GroundFallDuration > 0"))
	float GetUpDuration = 1.2f;
	bool IsValid() const;
	bool IsEnabled() const { return (Distance > 0.f || Height > 0.f || GroundFallDuration > 0.f) && IsValid(); }
	bool IsAirborne() const { return Height > 0.f && IsValid(); }
	bool IsGroundedKnockdown() const { return GroundFallDuration > 0.f && IsValid(); }
};

UENUM(BlueprintType)
enum class ERPGKnockbackEndReason : uint8
{
	None, Completed, Replaced, Wall, Cleansed, LifeChanged, ForeignMovement, UnsupportedMovement
};

/** Server-owned execution parameters; CMC remains the sole location/collision transport. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGKnockbackState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bActive = false;
	UPROPERTY() uint32 ExecutionId = 0;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY(BlueprintReadOnly) FVector Velocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) ERPGHitReactionDirection PresentationDirection = ERPGHitReactionDirection::Front;
	UPROPERTY(BlueprintReadOnly) float Duration = 0.f;
	UPROPERTY(BlueprintReadOnly) double StartServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) ERPGKnockbackEndReason EndReason = ERPGKnockbackEndReason::None;
};

namespace RPGKnockback
{
	PROJECT_RPG_API FGameplayTag StatusTag();
	PROJECT_RPG_API FGameplayTag ImmunityTag();
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGKnockbackSettings& Settings);
	PROJECT_RPG_API FRPGKnockbackSettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
