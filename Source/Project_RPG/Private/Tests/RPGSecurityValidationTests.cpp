#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Security/RPGSecurityTypes.h"

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

#endif
