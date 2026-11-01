#pragma once

#include "GameplayTagContainer.h"
#include "RPGCombatImmunityTypes.generated.h"

class AActor;
class UAbilitySystemComponent;
struct FGameplayAbilitySpec;

/** Groups are independent: displacement protection does not protect against stun. */
UENUM(BlueprintType, meta=(Bitflags, UseEnumValuesAsMaskValuesInEditor="true"))
enum class ERPGCombatImmunity : uint8
{
	None = 0 UMETA(Hidden),
	HitReaction = 1,
	Displacement = 2,
	StatusEffects = 4,
	Stun = 8,
	Knockback = 16,
	Airborne = 32,
	Slow = 64,
	Burn = 128
};
ENUM_CLASS_FLAGS(ERPGCombatImmunity);

UENUM(BlueprintType)
enum class ERPGCombatEffectKind : uint8 { HitReaction, Stun, Knockback, Airborne, Slow, Burn, Stagger, DefenseReduction, DamageTakenIncrease };

/** Authored on shared grants/SkillDefinition; zero preserves the existing behavior. */
USTRUCT(BlueprintType)
struct PROJECT_RPG_API FRPGCombatImmunitySettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(Bitmask, BitmaskEnum="/Script/Project_RPG.ERPGCombatImmunity"))
	int32 Flags = 0;
	bool IsValid() const { return Flags >= 0 && (Flags & ~255) == 0; }
	bool IsEnabled() const { return IsValid() && Flags != 0; }
	bool Protects(ERPGCombatEffectKind Kind) const;
	FGameplayTagContainer GetGrantedTags() const;
};

namespace RPGCombatImmunity
{
	PROJECT_RPG_API FGameplayTag HitReactionTag();
	PROJECT_RPG_API bool HasTags(const UAbilitySystemComponent* ASC, ERPGCombatEffectKind Kind);
	PROJECT_RPG_API bool IsImmune(const AActor* Actor, ERPGCombatEffectKind Kind);
	PROJECT_RPG_API bool SetGrantedSettings(FGameplayAbilitySpec& Spec, const FRPGCombatImmunitySettings& Settings);
	/** Absent metadata falls back to the class; malformed metadata rejects the grant/activation. */
	PROJECT_RPG_API FRPGCombatImmunitySettings GetGrantedSettings(const FGameplayAbilitySpec* Spec);
}
