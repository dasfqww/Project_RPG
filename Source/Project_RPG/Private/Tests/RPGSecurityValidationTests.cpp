#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Security/RPGSecurityTypes.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityNormalMovementTest,
	"ProjectRPG.Security.Movement.AcceptsNormalServerMovement",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityNormalMovementTest::RunTest(const FString& Parameters)
{
	FRPGMovementSecurityConfig Config;
	Config.FixedPositionTolerance = 50.0f;
	Config.DistanceToleranceMultiplier = 1.25f;
	Config.WalkingSpeedToleranceMultiplier = 1.25f;
	Config.WalkingSpeedTolerance = 50.0f;

	FRPGMovementSecuritySample Previous;
	Previous.ServerTimeSeconds = 1.0;
	Previous.Velocity = FVector(400.0f, 0.0f, 0.0f);
	Previous.DeclaredMaximumSpeed = 400.0f;

	FRPGMovementSecuritySample Current = Previous;
	Current.ServerTimeSeconds = 1.1;
	Current.Location = FVector(40.0f, 0.0f, 0.0f);

	const FRPGMovementSecurityResult Result =
		FRPGSecurityValidationMath::ValidateMovement(
			Previous,
			Current,
			Config);
	TestTrue(TEXT("Normal authoritative movement is accepted"), Result.bValid);
	TestFalse(TEXT("Normal movement is not a speed violation"),
		Result.bSpeedViolation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecuritySpeedHackMovementTest,
	"ProjectRPG.Security.Movement.RejectsWalkingSpeedSpike",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecuritySpeedHackMovementTest::RunTest(const FString& Parameters)
{
	FRPGMovementSecurityConfig Config;
	Config.FixedPositionTolerance = 25.0f;
	Config.DistanceToleranceMultiplier = 1.10f;
	Config.WalkingSpeedToleranceMultiplier = 1.10f;
	Config.WalkingSpeedTolerance = 25.0f;

	FRPGMovementSecuritySample Previous;
	Previous.ServerTimeSeconds = 5.0;
	Previous.DeclaredMaximumSpeed = 400.0f;
	Previous.Velocity = FVector(400.0f, 0.0f, 0.0f);

	FRPGMovementSecuritySample Current = Previous;
	Current.ServerTimeSeconds = 5.1;
	Current.Location = FVector(200.0f, 0.0f, 0.0f);
	Current.Velocity = FVector(2000.0f, 0.0f, 0.0f);

	const FRPGMovementSecurityResult Result =
		FRPGSecurityValidationMath::ValidateMovement(
			Previous,
			Current,
			Config);
	TestFalse(TEXT("Walking speed spike is rejected"), Result.bValid);
	TestTrue(TEXT("Walking speed spike is classified"),
		Result.bSpeedViolation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityAirborneSpeedHackMovementTest,
	"ProjectRPG.Security.Movement.RejectsAirborneHorizontalSpeedHack",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityAirborneSpeedHackMovementTest::RunTest(
	const FString& Parameters)
{
	FRPGMovementSecurityConfig Config;
	Config.FixedPositionTolerance = 25.0f;
	Config.WalkingSpeedToleranceMultiplier = 1.35f;
	Config.WalkingSpeedTolerance = 150.0f;

	FRPGMovementSecuritySample Previous;
	Previous.ServerTimeSeconds = 7.0;
	Previous.DeclaredMaximumSpeed = 400.0f;

	FRPGMovementSecuritySample Current = Previous;
	Current.ServerTimeSeconds = 7.1;
	Current.Location = FVector(180.0f, 0.0f, -50.0f);
	Current.Velocity = FVector(1800.0f, 0.0f, -500.0f);
	Current.bSkipHorizontalSpeedCheck = false;

	const FRPGMovementSecurityResult Result =
		FRPGSecurityValidationMath::ValidateMovement(
			Previous,
			Current,
			Config);
	TestFalse(TEXT("Airborne horizontal speed hack is rejected"), Result.bValid);
	TestTrue(TEXT("Airborne speed hack is classified"), Result.bSpeedViolation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityAuthorizedMovementTest,
	"ProjectRPG.Security.Movement.AcceptsAuthorizedDiscontinuityBudget",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityAuthorizedMovementTest::RunTest(const FString& Parameters)
{
	FRPGMovementSecurityConfig Config;
	Config.FixedPositionTolerance = 50.0f;
	Config.DiscontinuityDistance = 1000.0f;

	FRPGMovementSecuritySample Previous;
	Previous.ServerTimeSeconds = 10.0;
	Previous.DeclaredMaximumSpeed = 400.0f;

	FRPGMovementSecuritySample Current = Previous;
	Current.ServerTimeSeconds = 10.1;
	Current.Location = FVector(3000.0f, 0.0f, 0.0f);
	Current.AuthorizedExtraDistance = 4000.0f;
	Current.bSkipHorizontalSpeedCheck = true;

	const FRPGMovementSecurityResult Result =
		FRPGSecurityValidationMath::ValidateMovement(
			Previous,
			Current,
			Config);
	TestTrue(TEXT("Server-authorized displacement is accepted"), Result.bValid);
	TestFalse(TEXT("Authorized displacement is not classified as teleport"),
		Result.bDiscontinuity);
	TestTrue(TEXT("Authorized displacement consumes a finite budget"),
		Result.AuthorizedDistanceUsed > 0.0f &&
		Result.AuthorizedDistanceUsed <= Current.AuthorizedExtraDistance);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityAbilityRapidFireTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsSameAbilityRapidFire",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityAbilityRapidFireTest::RunTest(const FString& Parameters)
{
	FRPGAbilitySecurityConfig Config;
	Config.ActivationWindowSeconds = 1.0f;
	Config.MaximumActivationsPerWindow = 4;
	Config.MinimumSameAbilityIntervalSeconds = 0.10f;

	const FName AbilityKey(TEXT("/Game/Test/GA_RapidFire"));
	const TArray<double> History{8.0, 9.2};
	TMap<FName, double> LastActivationByAbility;
	LastActivationByAbility.Add(AbilityKey, 9.2);

	const FRPGAbilityActivationSecurityResult NormalResult =
		FRPGSecurityValidationMath::ValidateAbilityActivation(
			AbilityKey,
			9.5,
			History,
			LastActivationByAbility,
			Config);
	TestTrue(TEXT("A normal same-ability interval is accepted"),
		NormalResult.bValid);

	const FRPGAbilityActivationSecurityResult RapidFireResult =
		FRPGSecurityValidationMath::ValidateAbilityActivation(
			AbilityKey,
			9.25,
			History,
			LastActivationByAbility,
			Config);
	TestFalse(TEXT("A same-ability rapid-fire request is rejected"),
		RapidFireResult.bValid);
	TestTrue(TEXT("Rapid fire is classified as an interval violation"),
		RapidFireResult.bSameAbilityIntervalViolation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityAbilityFloodTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsAbilityWindowFlood",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityAbilityFloodTest::RunTest(const FString& Parameters)
{
	FRPGAbilitySecurityConfig Config;
	Config.ActivationWindowSeconds = 1.0f;
	Config.MaximumActivationsPerWindow = 3;
	Config.MinimumSameAbilityIntervalSeconds = 0.01f;

	const TArray<double> History{8.0, 9.1, 9.4, 9.8, 10.5};
	const TMap<FName, double> LastActivationByAbility;
	const FRPGAbilityActivationSecurityResult Result =
		FRPGSecurityValidationMath::ValidateAbilityActivation(
			FName(TEXT("/Game/Test/GA_Flood")),
			10.0,
			History,
			LastActivationByAbility,
			Config);
	TestFalse(TEXT("An activation-window flood is rejected"), Result.bValid);
	TestTrue(TEXT("The flood is classified as a window violation"),
		Result.bActivationWindowViolation);
	TestEqual(TEXT("Only timestamps inside the server window are counted"),
		Result.RecentActivationCount,
		3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityCombatRangeHackTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsCombatRangeHack",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityCombatRangeHackTest::RunTest(const FString& Parameters)
{
	FRPGCombatHitSecuritySample Sample;
	Sample.SourceLocation = FVector::ZeroVector;
	Sample.TargetBoundsOrigin = FVector(900.0f, 0.0f, 0.0f);
	Sample.TargetBoundsExtent = FVector(50.0f);
	Sample.ImpactPoint = Sample.TargetBoundsOrigin;
	Sample.MaximumDistance = 1000.0f;
	Sample.HitLocationTolerance = 50.0f;
	TestTrue(TEXT("An in-range server hit is accepted"),
		FRPGSecurityValidationMath::ValidateCombatHit(Sample).bValid);

	Sample.TargetBoundsOrigin = FVector(1400.0f, 0.0f, 0.0f);
	Sample.ImpactPoint = Sample.TargetBoundsOrigin;
	const FRPGCombatHitSecurityResult Result =
		FRPGSecurityValidationMath::ValidateCombatHit(Sample);
	TestFalse(TEXT("A range-hacked hit is rejected"), Result.bValid);
	TestTrue(TEXT("The hit is classified as a range violation"),
		Result.Failure == ERPGCombatHitValidationFailure::RangeExceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityImpactPointForgeryTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsForgedImpactPoint",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityImpactPointForgeryTest::RunTest(const FString& Parameters)
{
	FRPGCombatHitSecuritySample Sample;
	Sample.SourceLocation = FVector::ZeroVector;
	Sample.TargetBoundsOrigin = FVector(500.0f, 0.0f, 0.0f);
	Sample.TargetBoundsExtent = FVector(50.0f);
	Sample.ImpactPoint = FVector(500.0f, 500.0f, 0.0f);
	Sample.MaximumDistance = 1000.0f;
	Sample.HitLocationTolerance = 25.0f;

	const FRPGCombatHitSecurityResult Result =
		FRPGSecurityValidationMath::ValidateCombatHit(Sample);
	TestFalse(TEXT("A forged impact point is rejected"), Result.bValid);
	TestTrue(TEXT("The hit is classified as an impact-point violation"),
		Result.Failure ==
			ERPGCombatHitValidationFailure::ImpactPointOutsideTarget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityDamageForgeryTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsForgedDamageMagnitude",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityDamageForgeryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Damage at the authored cap is accepted"),
		FRPGSecurityValidationMath::IsDamageMagnitudeValid(5000.0f, 5000.0f));
	TestFalse(TEXT("Damage above the authored cap is rejected"),
		FRPGSecurityValidationMath::IsDamageMagnitudeValid(5000.1f, 5000.0f));
	TestFalse(TEXT("Negative damage is rejected"),
		FRPGSecurityValidationMath::IsDamageMagnitudeValid(-1.0f, 5000.0f));
	TestFalse(TEXT("Non-finite damage is rejected"),
		FRPGSecurityValidationMath::IsDamageMagnitudeValid(
			std::numeric_limits<float>::infinity(),
			5000.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityTargetDataForgeryTest,
	"ProjectRPG.Security.AbuseSimulation.RejectsForgedTargetData",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityTargetDataForgeryTest::RunTest(const FString& Parameters)
{
	FRPGTargetDataSecuritySample Sample;
	Sample.ServerSourceLocation = FVector::ZeroVector;
	Sample.SubmittedSourceLocation = FVector(25.0f, 0.0f, 0.0f);
	Sample.SubmittedTargetLocation = FVector(900.0f, 0.0f, 0.0f);
	Sample.SubmittedAimDirection = FVector::ForwardVector;
	Sample.ServerAimDirection = FVector::ForwardVector;
	Sample.MaximumRange = 1000.0f;
	Sample.SourceLocationTolerance = 50.0f;
	Sample.RangeTolerance = 100.0f;
	Sample.AimToleranceDegrees = 15.0f;
	TestTrue(TEXT("A target inside server drift tolerances is accepted"),
		FRPGSecurityValidationMath::ValidateTargetData(Sample).bValid);

	Sample.SubmittedTargetLocation = FVector(1200.0f, 0.0f, 0.0f);
	FRPGTargetDataSecurityResult Result =
		FRPGSecurityValidationMath::ValidateTargetData(Sample);
	TestFalse(TEXT("Out-of-range TargetData is rejected"), Result.bValid);
	TestTrue(TEXT("TargetData range tampering is classified"),
		Result.bRangeViolation);

	Sample.SubmittedTargetLocation = FVector(0.0f, 900.0f, 0.0f);
	Result = FRPGSecurityValidationMath::ValidateTargetData(Sample);
	TestFalse(TEXT("Aim-divergent TargetData is rejected"), Result.bValid);
	TestTrue(TEXT("TargetData aim tampering is classified"),
		Result.bAimViolation);

	Sample.SubmittedTargetLocation = FVector(900.0f, 0.0f, 0.0f);
	Sample.SubmittedSourceLocation.X =
		std::numeric_limits<float>::quiet_NaN();
	Result = FRPGSecurityValidationMath::ValidateTargetData(Sample);
	TestFalse(TEXT("Non-finite TargetData is rejected"), Result.bValid);
	TestTrue(TEXT("Non-finite TargetData is classified"),
		Result.bInvalidSpatialData);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillSecurityProfileValidationTest,
	"ProjectRPG.Security.SkillProfile.Validation",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSkillSecurityProfileValidationTest::RunTest(
	const FString& Parameters)
{
	FRPGSkillSecurityProfile Profile;
	FString Error;
	TestTrue(TEXT("Default skill security profile is usable"),
		Profile.IsValid(&Error));

	Profile.MaximumHitsPerActivation = 0;
	TestFalse(TEXT("Zero activation hit budget is rejected"),
		Profile.IsValid(&Error));

	Profile.MaximumHitsPerActivation = 1;
	Profile.AuthorizedMovement.bEnabled = true;
	Profile.AuthorizedMovement.DurationSeconds = -1.0f;
	TestFalse(TEXT("Invalid movement window is rejected"),
		Profile.IsValid(&Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityEnforcementThresholdTest,
	"ProjectRPG.Security.Enforcement.ResolvesStagedThresholds",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityEnforcementThresholdTest::RunTest(
	const FString& Parameters)
{
	const FRPGSecurityEnforcementConfig Config;
	TestEqual(TEXT("Low risk remains monitored"),
		FRPGSecurityEnforcementMath::ResolveState(
			24.9f,
			ERPGSecurityEnforcementState::Monitoring,
			Config),
		ERPGSecurityEnforcementState::Monitoring);
	TestEqual(TEXT("Elevated threshold is exact"),
		FRPGSecurityEnforcementMath::ResolveState(
			25.0f,
			ERPGSecurityEnforcementState::Monitoring,
			Config),
		ERPGSecurityEnforcementState::Elevated);
	TestEqual(TEXT("Restricted threshold is exact"),
		FRPGSecurityEnforcementMath::ResolveState(
			50.0f,
			ERPGSecurityEnforcementState::Elevated,
			Config),
		ERPGSecurityEnforcementState::Restricted);
	TestEqual(TEXT("Removal threshold is exact"),
		FRPGSecurityEnforcementMath::ResolveState(
			100.0f,
			ERPGSecurityEnforcementState::Restricted,
			Config),
		ERPGSecurityEnforcementState::RemovalRecommended);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityEnforcementRecoveryTest,
	"ProjectRPG.Security.Enforcement.AppliesRecoveryHysteresis",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityEnforcementRecoveryTest::RunTest(
	const FString& Parameters)
{
	const FRPGSecurityEnforcementConfig Config;
	TestEqual(TEXT("Removal recommendation does not flap near its threshold"),
		FRPGSecurityEnforcementMath::ResolveState(
			80.0f,
			ERPGSecurityEnforcementState::RemovalRecommended,
			Config),
		ERPGSecurityEnforcementState::RemovalRecommended);
	TestEqual(TEXT("Removal recommendation recovers below hysteresis"),
		FRPGSecurityEnforcementMath::ResolveState(
			74.0f,
			ERPGSecurityEnforcementState::RemovalRecommended,
			Config),
		ERPGSecurityEnforcementState::Restricted);
	TestEqual(TEXT("Restricted state does not flap"),
		FRPGSecurityEnforcementMath::ResolveState(
			40.0f,
			ERPGSecurityEnforcementState::Restricted,
			Config),
		ERPGSecurityEnforcementState::Restricted);
	TestEqual(TEXT("Restricted state recovers below hysteresis"),
		FRPGSecurityEnforcementMath::ResolveState(
			37.0f,
			ERPGSecurityEnforcementState::Restricted,
			Config),
		ERPGSecurityEnforcementState::Elevated);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSecurityEnforcementPolicyValidationTest,
	"ProjectRPG.Security.Enforcement.ValidatesPolicy",
	EAutomationTestFlags::EditorContext |
		EAutomationTestFlags::EngineFilter)

bool FRPGSecurityEnforcementPolicyValidationTest::RunTest(
	const FString& Parameters)
{
	FRPGSecurityEnforcementConfig Config;
	FString Error;
	TestTrue(TEXT("Default enforcement policy is valid"),
		Config.IsValid(&Error));

	Config.RestrictedRiskThreshold = Config.ElevatedRiskThreshold;
	TestFalse(TEXT("Non-increasing thresholds are rejected"),
		Config.IsValid(&Error));

	Config.bEnabled = false;
	TestTrue(TEXT("A disabled enforcement policy is inert"),
		FRPGSecurityEnforcementMath::ResolveState(
			1000.0f,
			ERPGSecurityEnforcementState::RemovalRecommended,
			Config) == ERPGSecurityEnforcementState::Monitoring);
	return true;
}

#endif
