#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;

enum class ERPGHUDDebuffKind : uint8
{
	Stagger, Stun, Knockback, Knockdown, Slow, Burn, DefenseReduction, DamageTakenIncrease
};

/** Read-only projection. GameplayEffects remain the source of duration and removal. */
struct PROJECT_RPG_API FRPGHUDDebuff
{
	ERPGHUDDebuffKind Kind = ERPGHUDDebuffKind::Slow;
	double RemainingSeconds = 0.;
	bool bTimed = true;
};

namespace RPGDebuffPresentation
{
	PROJECT_RPG_API TArray<FRPGHUDDebuff> Read(const UAbilitySystemComponent* Source, double LocalWorldTime);
	/** Surviving types keep their slot order; newly observed types append. */
	PROJECT_RPG_API TArray<FRPGHUDDebuff> StableOrder(const TArray<FRPGHUDDebuff>& Current, const TArray<FRPGHUDDebuff>& Previous);
	PROJECT_RPG_API FText Name(ERPGHUDDebuffKind Kind);
	PROJECT_RPG_API FText TimeText(const FRPGHUDDebuff& Item);
	PROJECT_RPG_API FLinearColor Color(ERPGHUDDebuffKind Kind);
}
