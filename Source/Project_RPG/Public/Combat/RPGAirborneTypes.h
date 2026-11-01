#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "RPGAirborneTypes.generated.h"

UENUM(BlueprintType)
enum class ERPGAirbornePhase : uint8 { None, Airborne, Downed, GettingUp };

UENUM(BlueprintType)
enum class ERPGAirborneEndReason : uint8
{
	None, Completed, Replaced, Cleansed, LifeChanged, ForeignMovement, UnsupportedMovement, LandingTimeout
};

/** Authoritative phase snapshot. CharacterMovement owns location and actual landing. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGAirborneState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) ERPGAirbornePhase Phase = ERPGAirbornePhase::None;
	UPROPERTY() uint32 ExecutionId = 0;
	UPROPERTY() uint32 RecoveryRevision = 0;
	UPROPERTY() uint32 PhaseRevision = 0;
	UPROPERTY(BlueprintReadOnly) bool bMotionActive = false;
	UPROPERTY(BlueprintReadOnly) float Distance = 0.f;
	UPROPERTY(BlueprintReadOnly) float Height = 0.f;
	UPROPERTY() float Yaw = 0.f;
	UPROPERTY(BlueprintReadOnly) float ArcDuration = 0.f;
	UPROPERTY(BlueprintReadOnly) float MaximumFlightDuration = 0.f;
	UPROPERTY(BlueprintReadOnly) float DownDuration = 0.f;
	UPROPERTY(BlueprintReadOnly) float GetUpDuration = 0.f;
	UPROPERTY(BlueprintReadOnly) double StartServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) double PhaseStartServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) double PhaseEndServerTime = 0.;
	UPROPERTY(BlueprintReadOnly) ERPGAirborneEndReason EndReason = ERPGAirborneEndReason::None;
	/** CMC move-clock origin for the autonomous owner; -1 for non-player motion. */
	UPROPERTY() float MovementStartClientTime = -1.f;
	bool IsActive() const { return Phase != ERPGAirbornePhase::None; }
};

namespace RPGAirborne
{
	PROJECT_RPG_API FGameplayTag SequenceTag();
	PROJECT_RPG_API FGameplayTag AirborneTag();
	PROJECT_RPG_API FGameplayTag DownedTag();
	PROJECT_RPG_API FGameplayTag GettingUpTag();
	PROJECT_RPG_API FGameplayTag ImmunityTag();
}
