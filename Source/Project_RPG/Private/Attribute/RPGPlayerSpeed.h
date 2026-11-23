#pragma once

#include "Attribute/RPGAttributeSet.h"

namespace RPGPlayerSpeed
{
	inline constexpr float MaxScale = 1.4f;

	inline float CombineScale(float BaseScale, float BonusPercent)
	{
		if (!FMath::IsFinite(BaseScale) || BaseScale < 0.f || !FMath::IsFinite(BonusPercent))
			return 0.f;

		const double AppliedScale = static_cast<double>(BaseScale) * (1.0 + BonusPercent / 100.0);
		return static_cast<float>(FMath::Clamp(AppliedScale, 0.0, static_cast<double>(MaxScale)));
	}

	inline float AttackScale(const URPGAttributeSet& Stats)
	{
		return CombineScale(Stats.GetAttackSpeed(), Stats.GetAttackSpeedPercent());
	}

	inline float MovementScale(const URPGAttributeSet& Stats)
	{
		return CombineScale(Stats.GetMoveSpeed(), Stats.GetMoveSpeedPercent());
	}
}
