#pragma once
#include "CoreMinimal.h"
#include "Combat/RPGCombatImmunityTypes.h"
class UAnimMontage;

/** Native per-play copy. Notify UObjects never hold actors, tokens or mutable runtime state. */
struct FRPGMontageImmunityWindow
{
	float Start = 0.f, End = 0.f;
	FRPGCombatImmunitySettings Immunities;
	bool Contains(float Position) const { return FMath::IsFinite(Position) && Position >= Start && Position < End; }
};
namespace RPGMontageImmunity
{
	/** At most eight authored montage windows. Invalid data grants no window protection. */
	PROJECT_RPG_API bool ReadWindows(const UAnimMontage& Montage, TArray<FRPGMontageImmunityWindow>& OutWindows);
}
