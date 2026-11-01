#pragma once

#include "CoreMinimal.h"
#include "RPGHitReactionTypes.generated.h"

class UAnimMontage;
struct FGameplayAbilitySpec;

/** Authored on attack data, never on a presentation-only GA subclass. */
UENUM(BlueprintType)
enum class ERPGAttackHitReactionPolicy : uint8
{
	Legacy,
	None,
	Cosmetic
};

UENUM(BlueprintType)
enum class ERPGHitReactionDirection : uint8
{
	Front,
	Right,
	Back,
	Left
};

USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGHitReactionMontage
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	TObjectPtr<UAnimMontage> Montage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FName Section;
};

/** Native routing result, independent of the already committed damage result. */
enum class ERPGHitReactionRoute : uint8
{
	Unsupported,
	Suppressed,
	Duplicate,
	Dispatched
};

namespace RPGHitReaction
{
	PROJECT_RPG_API bool IsValidPolicy(ERPGAttackHitReactionPolicy Policy);
	/** Grant-time metadata; preserves input tags and the existing SourceObject. */
	PROJECT_RPG_API bool SetGrantedPolicy(FGameplayAbilitySpec& Spec, ERPGAttackHitReactionPolicy Policy);
	PROJECT_RPG_API ERPGAttackHitReactionPolicy GetGrantedPolicy(const FGameplayAbilitySpec* Spec);
	/** Victim-relative planar direction. Invalid/coincident input uses Front. */
	PROJECT_RPG_API ERPGHitReactionDirection ResolveDirection(
		const FVector& VictimForward, const FVector& VictimToSource);

	/** Wrap-safe ordering for one component's transient presentation stream. */
	PROJECT_RPG_API bool IsNewerSequence(uint32 Candidate, uint32 Previous);
}
