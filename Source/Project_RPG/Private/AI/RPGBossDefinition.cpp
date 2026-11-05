#include "AI/RPGBossDefinition.h"
#include "Ability/RPGBossGameplayAbility.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool URPGBossDefinition::ValidateDefinition(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (!FMath::IsFinite(EnrageSeconds) || EnrageSeconds < 0.f || EnrageSeconds > 86400.f ||
		!FMath::IsFinite(EnragedAttackMultiplier) || EnragedAttackMultiplier < 1.f || EnragedAttackMultiplier > 10.f)
	{
		OutError = FText::FromString(TEXT("Enrage time must be finite and within 0..86400 seconds; attack multiplier must be within 1..10."));
		return false;
	}
	TSet<FGameplayTag> Tags;
	if (Phases.IsEmpty() || Phases[0].HealthThreshold != 1.0f || Phases[0].bRequiresExplicitTransition)
	{
		OutError = FText::FromString(TEXT("A boss needs an automatic initial phase at health threshold 1.0."));
		return false;
	}
	float PreviousThreshold = 2.0f;
	for (int32 PhaseIndex = 0; PhaseIndex < Phases.Num(); ++PhaseIndex)
	{
		const FRPGBossPhaseDefinition& Phase = Phases[PhaseIndex];
		if ((!Phase.Patterns.IsEmpty() && (!Phase.AbilityPatterns.IsEmpty() || Phase.bRandomizePatterns)) ||
			static_cast<uint8>(Phase.PatternSelection) > static_cast<uint8>(ERPGBossPatternSelection::Sequence) ||
			(Phase.Patterns.IsEmpty() && Phase.PatternSelection != ERPGBossPatternSelection::Priority) ||
			(Phase.PhaseEntryPattern.IsConfigured() && Phase.PhaseEntryAbility) ||
			Phase.RequiredSummonDefeats < 0 || Phase.RequiredSummonDefeats > 8 ||
			(Phase.RequiredSummonDefeats > 0 && !Phase.SummonWaveCompletionPhase.IsValid()))
		{
			OutError = FText::FromString(TEXT("Use either legacy or entry-based patterns, a valid selection mode, and a summon objective for a defeat quota."));
			return false;
		}
		TSet<FGameplayTag> PatternIds;
		for (const FRPGBossPatternDefinition& Pattern : Phase.Patterns)
		{
			if (!Pattern.Validate(OutError)) return false;
			if (PatternIds.Contains(Pattern.PatternId))
			{
				OutError = FText::FromString(TEXT("Pattern IDs must be unique within a phase, including its entry mechanic."));
				return false;
			}
			PatternIds.Add(Pattern.PatternId);
		}
		if (Phase.PhaseEntryPattern.IsConfigured())
		{
			if (!Phase.PhaseEntryPattern.Validate(OutError)) return false;
			if (PatternIds.Contains(Phase.PhaseEntryPattern.PatternId))
			{
				OutError = FText::FromString(TEXT("A phase entry mechanic must have its own pattern ID."));
				return false;
			}
		}
		if (!FMath::IsFinite(Phase.TimeLimitSeconds) || Phase.TimeLimitSeconds < 0.f || Phase.TimeLimitSeconds > 86400.f ||
			(Phase.TimeLimitSeconds > 0.f && Phase.TimeLimitSeconds < .01f))
		{
			OutError = FText::FromString(TEXT("Phase time limit must be zero (disabled) or within 0.01..86400 seconds."));
			return false;
		}
		if (Phase.SummonWaveCompletionPhase.IsValid() &&
			(Phase.TimeLimitSeconds <= 0.f || !Phases.IsValidIndex(PhaseIndex + 1) ||
				!Phases[PhaseIndex + 1].bRequiresExplicitTransition ||
				Phases[PhaseIndex + 1].PhaseTag != Phase.SummonWaveCompletionPhase))
		{
			OutError = FText::FromString(TEXT("A summon wave objective needs a phase time limit and the immediately following explicitly gated completion phase."));
			return false;
		}
		if (!Phase.PhaseTag.IsValid() || Tags.Contains(Phase.PhaseTag) ||
			!FMath::IsFinite(Phase.HealthThreshold) || Phase.HealthThreshold <= 0.0f ||
			Phase.HealthThreshold >= PreviousThreshold)
		{
			OutError = FText::FromString(TEXT("Phase tags must be unique and valid; thresholds must strictly decrease and remain above zero."));
			return false;
		}
		Tags.Add(Phase.PhaseTag);
		PreviousThreshold = Phase.HealthThreshold;
		if (Phase.GetPhaseEntryAbility())
		{
			const UClass* EntryClass = Phase.GetPhaseEntryAbility().Get();
			if (!Phase.bSuspendCombatAI || Phase.TimeLimitSeconds <= 0.f ||
				!EntryClass->IsChildOf(URPGBossGameplayAbility::StaticClass()) ||
				EntryClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				OutError = FText::FromString(TEXT("A phase entry ability must be a concrete boss ability in a combat-AI-suspended phase with a time limit."));
				return false;
			}
		}
		TSet<UClass*> Patterns;
		for (const TSubclassOf<URPGBossGameplayAbility>& Pattern : Phase.AbilityPatterns)
		{
			UClass* Class = Pattern.Get();
			if (!Class || !Class->IsChildOf(URPGBossGameplayAbility::StaticClass()) ||
				Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) || Patterns.Contains(Class))
			{
				OutError = FText::FromString(TEXT("Phase ability patterns must be unique, concrete boss ability classes."));
				return false;
			}
			Patterns.Add(Class);
		}
	}
	return true;
}

int32 URPGBossDefinition::ResolvePhase(float HealthFraction, int32 CurrentPhase) const
{
	if (Phases.IsEmpty() || !FMath::IsFinite(HealthFraction))
	{
		return INDEX_NONE;
	}
	int32 Result = FMath::Clamp(CurrentPhase, 0, Phases.Num() - 1);
	while (Phases.IsValidIndex(Result + 1) && !Phases[Result + 1].bRequiresExplicitTransition &&
		HealthFraction <= Phases[Result + 1].HealthThreshold)
	{
		++Result;
	}
	return Result;
}

#if WITH_EDITOR
EDataValidationResult URPGBossDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	FText Error;
	if (!ValidateDefinition(Error))
	{
		Context.AddError(Error);
		return EDataValidationResult::Invalid;
	}
	return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
