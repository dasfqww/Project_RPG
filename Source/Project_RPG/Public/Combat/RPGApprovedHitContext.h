#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class AActor;
class UAbilitySystemComponent;

/** Stack-local lifetime/direction capture made BEFORE damage callbacks. Never a BP/RPC permit. */
struct PROJECT_RPG_API FRPGApprovedHitContext
{
	FRPGApprovedHitContext() = default;
	FRPGApprovedHitContext(AActor* Source, AActor* Target);
	bool IsCurrent(AActor* Source, AActor* Target) const;
	FVector AwayDirection = FVector::ZeroVector;
private:
	TWeakObjectPtr<AActor> SourceActor, TargetActor;
	TWeakObjectPtr<UAbilitySystemComponent> SourceASC, TargetASC;
	uint32 SourceRevision = 0, TargetRevision = 0;
	uint32 SourceControlEpoch = 0, TargetControlEpoch = 0;
};
