#include "UI/Lobby/RPGCharacterConfigurationPreview.h"

#include "Progression/RPGCharacterProgressionSnapshot.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"

#define LOCTEXT_NAMESPACE "RPGCharacterConfigurationPreview"

bool RPGCharacterConfigurationPreview::Resolve(const URPGSkillDefinition& Definition,
	const FRPGSavedProgressionSkill& Draft, FRPGSkillRuntimeSpec& OutSpec)
{
	OutSpec.Reset();
	if (Draft.SkillTag != Definition.SkillTag || !Draft.SkillTag.IsValid() || Draft.Level < 1
		|| Definition.ClampSkillLevel(Draft.Level) != Draft.Level || Draft.TripodIds.Num() != 2) return false;
	FRPGSkillSaveData Save;
	Save.SkillLevel = Draft.Level;
	Save.SelectedTripodIndices.Init(INDEX_NONE, Definition.TripodTiers.Num());
	for (int32 Tier = 0; Tier < Draft.TripodIds.Num(); ++Tier)
	{
		if (Draft.TripodIds[Tier].IsEmpty()) continue;
		const int32 RequiredLevel = Tier == 0 ? 4 : 7; // Existing frontend save contract, not asset array indices.
		int32 TierIndex, OptionIndex;
		if (!Definition.ResolveSavedTripod(RequiredLevel, FName(*Draft.TripodIds[Tier]), TierIndex, OptionIndex)
			|| !Definition.IsTripodSelectionAllowed(Draft.Level, TierIndex, OptionIndex)) return false;
		Save.SelectedTripodIndices[TierIndex] = OptionIndex;
	}
	FRPGSkillRuntimeSpec Candidate;
	Definition.BuildRuntimeSpec(nullptr, Save, Candidate);
	FRPGCombatContributionAmounts Amounts;
	FRPGHitQueryProfile Profile;
	if (!Candidate.TryGetCombatContributions(Amounts) || !Candidate.TryGetHitQueryProfile(Profile)) return false;
	OutSpec = MoveTemp(Candidate);
	return true;
}

FText RPGCharacterConfigurationPreview::Describe(const URPGSkillDefinition* Definition,
	const FRPGSavedProgressionSkill& Draft)
{
	FRPGSkillRuntimeSpec Spec;
	if (!IsValid(Definition) || !Resolve(*Definition, Draft, Spec))
		return LOCTEXT("Unavailable", "선택한 구성의 수치 정보를 확인할 수 없습니다.");
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(2);
	auto Number = [&Format](float Value) { return FText::AsNumber(Value, &Format); };
	auto ScaledTime = [&Spec](float Time, const TCHAR* Tag)
	{
		const float Scalar = Spec.GetStatScalar(FGameplayTag::RequestGameplayTag(Tag, false));
		return FMath::Max(.01f, Time * (FMath::IsFinite(Scalar) ? FMath::Max(.01f, Scalar) : 1.f));
	};
	TArray<FText> Parts;
	Parts.Add(FText::Format(LOCTEXT("Cooldown", "쿨타임 {0}초"), Number(Spec.GetCooldownDuration())));
	const float DamageScalar = Spec.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.Damage"), false));
	if (FMath::IsFinite(DamageScalar) && DamageScalar > 0.f && !FMath::IsNearlyEqual(DamageScalar, 1.f))
		Parts.Add(FText::Format(LOCTEXT("Damage", "기본 피해 대비 {0}배"), Number(DamageScalar)));
	if (const auto* Charge = Spec.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>())
		Parts.Add(FText::Format(LOCTEXT("Charge", "완충 {0}초"), Number(
			ScaledTime(Charge->ChargeTimePerLevel, TEXT("Shared.Stat.ChargeTime")) * Charge->MaxChargeLevel)));
	if (const auto* Holding = Spec.ExecutionConfig.GetPtr<FRPGSkillHoldingExecutionConfig>())
	{
		Parts.Add(FText::Format(LOCTEXT("Holding", "유지 최대 {0}초"), Number(ScaledTime(Holding->HoldDuration, TEXT("Shared.Stat.HoldTime")))));
		if (Holding->bConstrainMovement)
			Parts.Add(Holding->MovementSpeedScalar == 0.f ? LOCTEXT("Stationary", "제자리 유지")
				: FText::Format(LOCTEXT("Mobile", "이동 속도 {0}%"), Number(Holding->MovementSpeedScalar * 100.f)));
	}
	FRPGHitQueryProfile Profile;
	Spec.TryGetHitQueryProfile(Profile);
	if (Profile.Shape.Type == ERPGHitQueryShape::Sphere || Profile.Shape.Type == ERPGHitQueryShape::Sector
		|| Profile.Shape.Type == ERPGHitQueryShape::RingSector)
		Parts.Add(FText::Format(LOCTEXT("Radius", "공격 반경 {0}m"), Number(Profile.Shape.Radius / 100.f)));
	float Movement = 1.f;
	if (Spec.TryGetMontageMovementScale(Movement) && !FMath::IsNearlyEqual(Movement, 1.f))
		Parts.Add(FText::Format(LOCTEXT("Movement", "이동 거리 {0}%"), Number(Movement * 100.f)));
	FRPGCombatContributionAmounts Contributions;
	Spec.TryGetCombatContributions(Contributions);
	if (Contributions.Stagger > 0.f)
		Parts.Add(FText::Format(LOCTEXT("Stagger", "무력화 기여 {0}"), Number(Contributions.Stagger)));
	if (Contributions.PartBreak > 0.f)
		Parts.Add(FText::Format(LOCTEXT("PartBreak", "부위 파괴 기여 {0}"), Number(Contributions.PartBreak)));
	if (!Spec.ContributionHitSection.IsNone()) Parts.Add(LOCTEXT("Finisher", "기여는 종료 타격에만 적용"));
	return FText::Format(LOCTEXT("Preview", "기본 상태 미리보기: {0}"), FText::Join(LOCTEXT("Separator", " · "), Parts));
}

#undef LOCTEXT_NAMESPACE
