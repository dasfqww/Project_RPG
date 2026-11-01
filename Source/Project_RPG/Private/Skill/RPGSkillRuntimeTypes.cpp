#include "Skill/RPGSkillRuntimeTypes.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RPGSkillRuntimeTypes)

void FRPGSkillRuntimeSpec::Reset()
{
	SkillTag = FGameplayTag::EmptyTag;
	bGreatSwordRageSkill = false;
	GreatSwordRageCost = 50.f;
	GreatSwordRageDamageMultiplier = 1.15f;
	DisplayName = FText::GetEmpty();
	SkillLevel = 1;
	SelectedTripodIndices.Reset();
	TripodTags.Reset();
	StatScalars.Reset();
	Icon = nullptr;
	Montage = nullptr;
	VFX = nullptr;
	ActionClass = nullptr;
	ExecutionPolicyClass = nullptr;
	ExecutionConfig.Reset();
	TargetingPolicyClass = nullptr;
	TargetingConfig.Reset();
	BaseCooldown = 1.0f;
	CooldownReductionSeconds = 0.0f;
	BaseDamage = 0.0f;
	Contributions = FRPGCombatContributionAmounts();
	ContributionHitSection = NAME_None;
	HitReactionPolicy = ERPGAttackHitReactionPolicy::Legacy;
	StunDuration = 0.f;
	Knockback = FRPGKnockbackSettings();
	Slow = FRPGSlowSettings();
	Burn = FRPGBurnSettings();
	DefenseReduction = FRPGDefenseReductionSettings();
	DamageTakenIncrease = FRPGDamageTakenIncreaseSettings();
	Stagger = FRPGStaggerSettings();
	ActivationImmunities = FRPGCombatImmunitySettings();
	TargetingProfile = FRPGHitQueryProfile();
	SecurityProfile = FRPGSkillSecurityProfile();
}

float FRPGSkillRuntimeSpec::GetStatScalar(
	const FGameplayTag& StatTag,
	const float DefaultValue) const
{
	if (const float* Value = StatScalars.Find(StatTag))
	{
		return *Value;
	}
	return DefaultValue;
}

bool FRPGSkillRuntimeSpec::HasTripodTag(const FGameplayTag& TripodTag) const
{
	return TripodTag.IsValid() && TripodTags.HasTagExact(TripodTag);
}

bool FRPGSkillRuntimeSpec::TryGetHitQueryProfile(FRPGHitQueryProfile& OutProfile) const
{
	OutProfile = {};
	const FGameplayTag RadiusTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Radius"), false);
	const float Scalar = RadiusTag.IsValid() ? GetStatScalar(RadiusTag) : 1.f;
	if (!FMath::IsFinite(Scalar) || Scalar <= 0.f || !FRPGHitQueryMath::IsShapeValid(TargetingProfile.Shape)) return false;
	FRPGHitQueryProfile Candidate = TargetingProfile;
	if (Scalar == 1.f) { OutProfile = MoveTemp(Candidate); return true; }
	FRPGHitQueryShape& Shape = Candidate.Shape;
	double HorizontalExtent = 0.0;
	switch (Shape.Type)
	{
	case ERPGHitQueryShape::Sphere:
		Shape.Radius *= Scalar;
		HorizontalExtent = Shape.Radius;
		break;
	case ERPGHitQueryShape::Capsule:
		Shape.Radius *= Scalar;
		Shape.HalfHeight *= Scalar;
		HorizontalExtent = FMath::Max(Shape.Radius, Shape.HalfHeight);
		break;
	case ERPGHitQueryShape::Sector:
	case ERPGHitQueryShape::RingSector:
		Shape.Radius *= Scalar;
		Shape.InnerRadius *= Scalar;
		HorizontalExtent = Shape.Radius;
		break;
	case ERPGHitQueryShape::Box:
		Shape.BoxHalfExtent.X *= Scalar;
		Shape.BoxHalfExtent.Y *= Scalar;
		HorizontalExtent = FVector2D(Shape.BoxHalfExtent.X, Shape.BoxHalfExtent.Y).Size();
		break;
	case ERPGHitQueryShape::LineSweep:
		Shape.Radius *= Scalar;
		HorizontalExtent = static_cast<double>(Shape.Length) + Shape.Radius;
		break;
	default:
		return false;
	}
	// A modifier cannot authorize an arbitrarily large broadphase or extend the
	// server's damage permission. The existing per-hit distance/final caps also apply.
	if (!FRPGHitQueryMath::IsShapeValid(Shape) || !FMath::IsFinite(HorizontalExtent)
		|| !FMath::IsFinite(SecurityProfile.MaximumServerHitDistance) || SecurityProfile.MaximumServerHitDistance <= 0.f
		|| HorizontalExtent > SecurityProfile.MaximumServerHitDistance) return false;
	OutProfile = MoveTemp(Candidate);
	return true;
}

bool FRPGSkillRuntimeSpec::TryGetMontageMovementScale(float& OutScale) const
{
	OutScale = 1.f;
	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.MovementDistance"), false);
	const float Scale = Tag.IsValid() ? GetStatScalar(Tag) : 1.f;
	if (!FMath::IsFinite(Scale) || Scale <= 0.f) return false;
	if (Scale != 1.f)
	{
		const auto& Movement = SecurityProfile.AuthorizedMovement;
		if (!SecurityProfile.IsValid() || !Movement.bEnabled || Movement.DurationSeconds > 10.f
			|| Movement.ExtraDistance <= 0.f || Movement.ExtraDistance > 100000.f) return false;
	}
	OutScale = Scale;
	return true;
}

bool FRPGSkillRuntimeSpec::TryGetCombatContributions(FRPGCombatContributionAmounts& OutAmounts) const
{
	OutAmounts = FRPGCombatContributionAmounts();
	if (!Contributions.IsValid()) return false;
	const auto StaggerTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.StaggerContribution"), false);
	const auto PartTag = FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.PartBreakContribution"), false);
	const float StaggerScalar = GetStatScalar(StaggerTag);
	const float PartScalar = GetStatScalar(PartTag);
	if (!FMath::IsFinite(StaggerScalar) || StaggerScalar <= 0.f || !FMath::IsFinite(PartScalar) || PartScalar <= 0.f) return false;
	FRPGCombatContributionAmounts Resolved;
	Resolved.Stagger = Contributions.Stagger * StaggerScalar;
	Resolved.PartBreak = Contributions.PartBreak * PartScalar;
	if (!Resolved.IsValid()) return false;
	OutAmounts = Resolved;
	return true;
}

float FRPGSkillRuntimeSpec::GetCooldownDuration(
	const float MinimumDuration) const
{
	const float SafeMinimum = FMath::IsFinite(MinimumDuration)
		? FMath::Max(MinimumDuration, 1.0f)
		: 1.0f;
	const float SafeBaseCooldown = FMath::IsFinite(BaseCooldown)
		? FMath::Max(BaseCooldown, 0.0f)
		: SafeMinimum;

	const FGameplayTag CooldownStatTag =
		FGameplayTag::RequestGameplayTag(
			TEXT("Shared.Stat.Cooldown"),
			false);
	const float CooldownScalar = CooldownStatTag.IsValid()
		? GetStatScalar(CooldownStatTag)
		: 1.0f;
	const float Reduction = FMath::IsFinite(CooldownReductionSeconds)
		? FMath::Max(CooldownReductionSeconds, 0.0f) : 0.0f;
	const float ScaledDuration = FMath::IsFinite(CooldownScalar)
		? FMath::Max(SafeBaseCooldown - Reduction, 0.0f) * FMath::Max(CooldownScalar, 0.0f)
		: SafeMinimum;
	return FMath::IsFinite(ScaledDuration) ? FMath::Max(ScaledDuration, SafeMinimum) : SafeMinimum;
}
