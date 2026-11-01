#include "Tests/RPGBossAttackScenario.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Attribute/RPGAttributeSet.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Combat/HitQuery/RPGHitQueryTypes.h"
#include "Combat/HitQuery/RPGHitQuerySubsystem.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Engine/World.h"
#include "Item/Weapon/RPGPlayerWeapon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RPGGameplayTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillExecutionTypes.h"

namespace
{
	bool QuerySoftCandidates(AActor* Source, const FRPGSkillSoftTargetingConfig& Config,
		FVector Direction, bool bServerTolerance, TArray<FRPGHitQueryResult>& OutCandidates)
	{
		const auto* Query = IsValid(Source) && Source->GetWorld() ? Source->GetWorld()->GetSubsystem<URPGHitQuerySubsystem>() : nullptr;
		if (!Query) { return false; }
		Direction.Z = 0.f;
		Direction = Direction.GetSafeNormal();
		if (Direction.IsNearlyZero()) Direction = Source->GetActorForwardVector().GetSafeNormal2D();
		FRPGHitQueryContext Context;
		Context.SourceActor = Source;
		Context.QueryTransform = FTransform(Direction.Rotation(), Source->GetActorLocation());
		Context.Profile.Shape.Type = ERPGHitQueryShape::Sector;
		const float Tolerance = bServerTolerance ? Config.ServerRangeTolerance : 0.f;
		Context.Profile.Shape.Radius = Config.MaxRange + Tolerance;
		Context.Profile.Shape.HalfHeight = Config.VerticalHalfHeight + Tolerance;
		Context.Profile.Shape.AngleDegrees = FMath::Min((Config.AssistAngleDegrees + (bServerTolerance ? Config.ServerAssistConeToleranceDegrees : 0.f)) * 2.f, 360.f);
		Context.Profile.Filter = Config.CandidateFilter;
		Context.Profile.Filter.SortMode = ERPGHitQuerySortMode::None;
		Context.Profile.Filter.MaxResults = 0;
		return Query->ExecuteHitQuery(Context, OutCandidates);
	}

	class FSkillContainerHitEvidence final : public IRPGBossAttackHitEvidence
	{
	public:
		bool ContainsCommittedServerHit(const FGameplayEffectSpec& Effect, const AActor* Target, const UGameplayAbility* ActiveAbility) const override
		{
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(ActiveAbility);
			const FHitResult* Hit = Effect.GetContext().GetHitResult();
			// Ordinary NPC damage stamps a server hit but does not set a source
			// ability in its GE context. The caller separately pins the active spec
			// and source ASC; do not claim context-level ability provenance.
			return Target && Skill && Skill->IsActive() && Skill->GetActiveSkillSpec().ExecutionPolicyClass &&
				Skill->GetAvatarActorFromActorInfo() && Skill->GetAvatarActorFromActorInfo()->HasAuthority() &&
				Hit && Hit->GetActor() == Target;
		}
		bool ContainsServerHit(const FGameplayEffectSpec& Effect, const AActor* Target) const override
		{
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Effect.GetContext().GetAbilityInstance_NotReplicated());
			const FHitResult* Hit = Effect.GetContext().GetHitResult();
			TArray<FRPGHitQueryResult> Hits;
			return Target && Skill && Skill->IsActive() && Skill->GetActiveSkillSpec().ExecutionPolicyClass &&
				Hit && Hit->GetActor() == Target && Skill->ExecuteActiveSkillHitQuery(Hits) &&
				Hits.ContainsByPredicate([Target](const FRPGHitQueryResult& Result) { return Result.TargetActor == Target; });
		}
	};

	class FSkillContainerChannelObservation final : public IRPGBossAttackChannelObservation
	{
	public:
		explicit FSkillContainerChannelObservation(FName InRepeatingSection) : RepeatingSection(InRepeatingSection) {}
		bool ReadChannel(const UGameplayAbility* Ability, const FAnimMontageInstance* MontageInstance,
			FRPGBossAttackChannelSnapshot& Out) const override
		{
			Out = {};
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability);
			if (!Skill || !MontageInstance) return false;
			const auto& Spec = Skill->GetActiveSkillSpec();
			const auto* Config = Spec.ExecutionConfig.GetPtr<FRPGSkillHoldingExecutionConfig>();
			if (!Config || !Spec.Montage || MontageInstance->Montage != Spec.Montage || !Spec.ExecutionPolicyClass ||
				!Spec.ExecutionPolicyClass->IsChildOf(URPGSkillExecutionPolicy_Holding::StaticClass())) return false;
			const int32 Loop = Spec.Montage->GetSectionIndex(RepeatingSection);
			if (Loop == INDEX_NONE || Spec.Montage->CompositeSections[Loop].NextSectionName != RepeatingSection ||
				Spec.Montage->GetSectionIndex(Config->HoldingSection) == INDEX_NONE ||
				Spec.Montage->GetSectionIndex(Config->SuccessSection) == INDEX_NONE) return false;
			const float Rate = MontageInstance->GetPlayRate();
			const float Scalar = Spec.GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.HoldTime"), false));
			const float SafeScalar = FMath::IsFinite(Scalar) ? FMath::Max(.01f, Scalar) : 1.f;
			auto Scaled = [SafeScalar](float Time) { return Time <= 0.f ? 0.f : FMath::Max(.01f, Time * SafeScalar); };
			const float End = Scaled(Config->PerfectZoneEndTime > 0.f ? Config->PerfectZoneEndTime : Config->HoldDuration);
			const float AutoStop = Config->bAutoReleaseAtPerfectZoneEnd ? End : Scaled(Config->HoldDuration);
			const float Hold = AutoStop * .45f;
			if (!FMath::IsFinite(Rate) || Rate <= 0.f || !FMath::IsFinite(Hold) || Hold <= Scaled(Config->PerfectZoneStartTime) || Hold >= End) return false;
			const float Period = Spec.Montage->GetSectionLength(Loop) / Rate;
			if (!FMath::IsFinite(Period) || Period <= 0.f || Hold <= Period * 3.f) return false;
			Out.ManualHoldSeconds = Hold;
			Out.LoopSeconds = Period;
			Out.FrameAdvanceSeconds = FMath::Max(0.f, MontageInstance->GetDeltaMoved() / Rate);
			Out.StopSection = Config->SuccessSection;
			Out.bInputPressed = Skill->IsSkillInputPressed();
			return true;
		}
	private:
		const FName RepeatingSection;
	};

	class FSkillContainerChargeObservation final : public IRPGBossAttackChargeObservation
	{
	public:
		bool ReadCharge(const UGameplayAbility* Ability, FRPGBossAttackChargeSnapshot& Out) const override
		{
			Out = {};
			const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability);
			const auto* Config = Skill ? Skill->GetActiveSkillSpec().ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>() : nullptr;
			if (!Config || !Skill->GetActiveSkillSpec().ExecutionPolicyClass ||
				!Skill->GetActiveSkillSpec().ExecutionPolicyClass->IsChildOf(URPGSkillExecutionPolicy_Charge::StaticClass())) return false;
			const float Scalar = Skill->GetActiveSkillSpec().GetStatScalar(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.ChargeTime"), false));
			const float SafeScalar = FMath::IsFinite(Scalar) ? FMath::Max(Scalar, .01f) : 1.f;
			Out.TimePerLevel = FMath::Max(.01f, Config->ChargeTimePerLevel * SafeScalar);
			Out.MinimumReleaseLevel = Config->MinimumReleaseLevel;
			Out.MaxChargeLevel = Config->MaxChargeLevel;
			Out.MaxHoldTime = Config->MaxChargeHoldTime;
			Out.ReleaseSection = Config->ReleaseSection;
			Out.bInputPressed = Skill->IsSkillInputPressed();
			return true;
		}
	};
}

bool RPGBossAttackScenarios::AcceptsChannelCadence(double DeltaSeconds, float PreviousAdvanceSeconds,
	float FrameAdvanceSeconds, float LoopSeconds, int32 HitsInFrame)
{
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0 || !FMath::IsFinite(PreviousAdvanceSeconds) || PreviousAdvanceSeconds < 0.f ||
		!FMath::IsFinite(FrameAdvanceSeconds) || FrameAdvanceSeconds < 0.f || !FMath::IsFinite(LoopSeconds) || LoopSeconds <= 0.f || HitsInFrame <= 0) return false;
	const int32 TraversalBudget = FMath::Max(1, FMath::CeilToInt(FMath::Min(FrameAdvanceSeconds / LoopSeconds, 64.f)));
	return HitsInFrame <= TraversalBudget && DeltaSeconds + PreviousAdvanceSeconds >= LoopSeconds * .5;
}

TConstArrayView<FRPGBossAttackScenario> RPGBossAttackScenarios::All()
{
	static const FSkillContainerHitEvidence Hits;
	static const FSkillContainerChargeObservation Charge;
	static const FSkillContainerChannelObservation Channel(FName(TEXT("Toggle")));
	// Function-local initialization: native gameplay tags must already be registered.
	static const FRPGBossAttackScenario Scenarios[] = {
		{ nullptr, TEXT("DefaultAttack"),
			TEXT("/Game/Blueprints/Character/Player/Abilities/DefaultAttack/GA_Player_DefaultAttack.GA_Player_DefaultAttack_C"),
			TEXT("AM_GreatSword_DefaultAttack"), FGameplayTag(RPGGameplayTags::Shared_Event_Hit), false, 3.0, 0, {}, nullptr, nullptr, {}, NAME_None },
		{ TEXT("RPGBossMapE2EJumpSmash"), TEXT("JumpSmash"),
			TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_JumpSmash.GA_Player_JumpSmash_C"),
			TEXT("AM_PlayerSkill"), FGameplayTag(RPGGameplayTags::Player_Event_AOE), true, .15, 1, {}, &Hits, nullptr, {}, NAME_None },
		{ TEXT("RPGBossMapE2EAssultBlade"), TEXT("AssultBlade"),
			TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_PlayerSkill_AssultBlade.GA_PlayerSkill_AssultBlade_C"),
			TEXT("AM_PlayerSkill"), FGameplayTag(RPGGameplayTags::Shared_Event_Hit), false, .15, 1, {}, &Hits, nullptr, {}, NAME_None },
		{ TEXT("RPGBossMapE2EChargeSlash"), TEXT("ChargeSlash"),
			TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_ChargeSlash.GA_Player_ChargeSlash_C"),
			TEXT("AM_PlayerChargeSkill"), FGameplayTag(RPGGameplayTags::Player_Event_AOE), false, -1.0, 1,
			FGameplayTag(RPGGameplayTags::Player_Status_Rage_Active), &Hits, &Charge,
			{ FName(TEXT("Prepare")), FName(TEXT("Loop")) }, FName(TEXT("Attack")) },
		{ TEXT("RPGBossMapE2EWhirlWind"), TEXT("WhirlWind"),
			TEXT("/Game/Blueprints/Character/Player/Abilities/Skill/GA_Player_WhirlWind.GA_Player_WhirlWind_C"),
			TEXT("AM_PlayerToggleSkill"), FGameplayTag(RPGGameplayTags::Player_Event_AOE), false, -1.0, 0,
			{}, &Hits, nullptr, { FName(TEXT("Toggle")) }, FName(TEXT("End")), &Channel }
	};
	return MakeArrayView(Scenarios);
}

int32 RPGBossAttackScenarios::CountSelectedSkills(const TCHAR* CommandLine)
{
	int32 Count = 0;
	for (const auto& Scenario : All())
		if (Scenario.CommandFlag && FParse::Param(CommandLine, Scenario.CommandFlag)) ++Count;
	return Count;
}

const FRPGBossAttackScenario& RPGBossAttackScenarios::Select(const TCHAR* CommandLine)
{
	for (const auto& Scenario : All())
		if (Scenario.CommandFlag && FParse::Param(CommandLine, Scenario.CommandFlag)) return Scenario;
	return All()[0]; // Conflicting command flags are rejected by the owning probe/runner.
}

const FRPGBossAttackScenario& RPGBossAttackScenarios::Current()
{
	return Select(FCommandLine::Get());
}

FGameplayAbilitySpec* RPGBossAttackScenarios::FindAbility(UAbilitySystemComponent* ASC, const FRPGBossAttackScenario& Scenario)
{
	return ASC ? ASC->GetActivatableAbilities().FindByPredicate([&Scenario](const FGameplayAbilitySpec& Spec)
	{
		return Spec.Ability && Spec.Ability->GetClass()->GetPathName() == Scenario.AbilityPath;
	}) : nullptr;
}

FGameplayTag RPGBossAttackScenarios::ResolveInputTag(const ARPGPlayerWeapon* Weapon, const FRPGBossAttackScenario& Scenario)
{
	if (!Scenario.IsSkill()) return RPGGameplayTags::InputTag_LightAttack_GreatSword;
	if (Weapon)
		for (const auto& Entry : Weapon->PlayerWeaponData.SpecialWeaponAbilities)
			if (Entry.AbilityToGrant && Entry.AbilityToGrant->GetPathName() == Scenario.AbilityPath) return Entry.InputTag;
	return {};
}

FGameplayTag RPGBossAttackScenarios::ResolveSkillTag(UAbilitySystemComponent* ASC, const FRPGBossAttackScenario& Scenario)
{
	const FGameplayAbilitySpec* Spec = FindAbility(ASC, Scenario);
	const UGameplayAbility* Ability = Spec ? (Spec->GetPrimaryInstance() ? Spec->GetPrimaryInstance() : Spec->Ability.Get()) : nullptr;
	const auto* Container = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	const URPGSkillDefinition* Definition = Container ? Container->GetSkillDefinition() : nullptr;
	return Definition ? Definition->SkillTag : FGameplayTag();
}

bool RPGBossAttackScenarios::HasLiveAimCandidate(UAbilitySystemComponent* ASC,
	const FRPGBossAttackScenario& Scenario, const AActor* Victim, FVector ViewDirection)
{
	const FGameplayAbilitySpec* Spec = FindAbility(ASC, Scenario);
	const UGameplayAbility* Ability = Spec ? (Spec->GetPrimaryInstance() ? Spec->GetPrimaryInstance() : Spec->Ability.Get()) : nullptr;
	const auto* Container = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	const URPGSkillDefinition* Definition = Container ? Container->GetSkillDefinition() : nullptr;
	AActor* Source = ASC ? ASC->GetAvatarActor() : nullptr;
	const auto* VictimASC = IsValid(Victim) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Victim)) : nullptr;
	const float Health = VictimASC ? VictimASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : 0.f;
	if (!Definition || !IsValid(Source) || !FMath::IsFinite(Health) || Health <= 0.f ||
		VictimASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)) { return false; }
	FRPGSkillSaveData SaveData;
	if (const auto* Skills = Source->FindComponentByClass<URPGPlayerSkillComponent>()) SaveData = Skills->GetSkillSaveData(Definition->SkillTag);
	FRPGSkillRuntimeSpec Preview;
	Definition->BuildRuntimeSpec(Source, SaveData, Preview);
	const auto* Config = Preview.TargetingConfig.GetPtr<FRPGSkillSoftTargetingConfig>();
	TArray<FRPGHitQueryResult> Candidates;
	return Config && QuerySoftCandidates(Source, *Config, ViewDirection, false, Candidates) &&
		Candidates.ContainsByPredicate([Victim](const FRPGHitQueryResult& Hit) { return Hit.TargetActor == Victim; });
}

void RPGBossAttackScenarios::ObserveSubmittedTarget(const UGameplayAbility* Ability,
	const FGameplayAbilityTargetDataHandle& Data, const AActor* ExpectedVictim)
{
	const auto* Skill = Cast<URPGGameplayAbility_SkillContainer>(Ability);
	FRPGSkillTargetResult Submitted;
	AActor* Source = Skill ? Skill->GetAvatarActorFromActorInfo() : nullptr;
	const bool bDecoded = FRPGSkillTargetDataCodec::Decode(Data, Submitted);
	if (!IsValid(Source) || !Source->HasAuthority() || !bDecoded)
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_TARGET_DATA Decode=%d Source=%s Entries=%d"), int32(bDecoded), *GetNameSafe(Source), Data.Num());
		return;
	}
	const auto* Config = Skill->GetActiveSkillSpec().TargetingConfig.GetPtr<FRPGSkillSoftTargetingConfig>();
	const auto* Query = Source->GetWorld()->GetSubsystem<URPGHitQuerySubsystem>();
	FVector ViewOrigin, ViewDirection;
	if (!Config || !Query || !Skill->GetSkillCameraAimRay(ViewOrigin, ViewDirection))
	{
		UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_TARGET_DATA Submitted=%s Expected=%s Match=%d FrozenSoftConfig=%d Query=%d DetailedObservationUnavailable=1"),
			*GetNameSafe(Submitted.TargetActor.Get()), *GetNameSafe(ExpectedVictim), int32(Submitted.TargetActor == ExpectedVictim), int32(Config != nullptr), int32(Query != nullptr));
		return;
	}
	ViewDirection.Z = 0.f;
	ViewDirection = ViewDirection.GetSafeNormal();
	TArray<FRPGHitQueryResult> Candidates;
	QuerySoftCandidates(Source, *Config, ViewDirection, true, Candidates);
	AActor* Target = Submitted.TargetActor;
	const auto* ASC = IsValid(Target) ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target) : nullptr;
	const bool bCandidate = Candidates.ContainsByPredicate([Target](const FRPGHitQueryResult& Hit) { return Hit.TargetActor == Target; });
	const bool bEligible = IsValid(Target) && Query->IsTargetEligible(Source, Target, Source->GetActorLocation(), Config->CandidateFilter);
	UE_LOG(LogTemp, Display, TEXT("RAIDMAP_E2E_SUMMON_PLAYER_TARGET_DATA Submitted=%s Expected=%s Match=%d HP=%g Death=%d Collision=%d Eligible=%d ServerCandidate=%d Candidates=%d Distance=%g SourceDelta=%g ViewDot=%g"),
		*GetNameSafe(Target), *GetNameSafe(ExpectedVictim), int32(Target == ExpectedVictim),
		ASC ? ASC->GetNumericAttribute(URPGAttributeSet::GetCurrentHealthAttribute()) : -1.f,
		int32(ASC && ASC->HasMatchingGameplayTag(RPGGameplayTags::Shared_Status_Death)), int32(IsValid(Target) && Target->GetActorEnableCollision()),
		int32(bEligible), int32(bCandidate), Candidates.Num(), IsValid(Target) ? FVector::Dist2D(Source->GetActorLocation(), Target->GetActorLocation()) : -1.f,
		FVector::Dist(Source->GetActorLocation(), Submitted.SourceLocation), IsValid(Target) ? FVector::DotProduct(ViewDirection, (Target->GetActorLocation() - Source->GetActorLocation()).GetSafeNormal2D()) : -1.f);
}
