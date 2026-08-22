#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefaultCooldownTest,
	"ProjectRPG.Skill.Cooldown.DefaultAndMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefaultCooldownTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(
		nullptr,
		FRPGSkillSaveData(),
		RuntimeSpec);

	TestEqual(
		TEXT("Every skill definition defaults to a one second cooldown"),
		RuntimeSpec.BaseCooldown,
		1.0f);
	TestEqual(
		TEXT("Default resolved cooldown is one second"),
		RuntimeSpec.GetCooldownDuration(),
		1.0f);

	RuntimeSpec.BaseCooldown = 0.0f;
	TestEqual(
		TEXT("Zero authored cooldown cannot bypass the repeat-input guard"),
		RuntimeSpec.GetCooldownDuration(),
		1.0f);

	RuntimeSpec.BaseCooldown = 4.0f;
	TestEqual(
		TEXT("Longer authored cooldowns remain intact"),
		RuntimeSpec.GetCooldownDuration(),
		4.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillSecurityProfileFreezeTest,
	"ProjectRPG.Skill.RuntimeSpec.SecurityProfileFreeze",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillSecurityProfileFreezeTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SecurityProfile.MaximumServerHitDistance = 1350.0f;
	Definition->SecurityProfile.MaximumTargetsPerQuery = 4;
	Definition->SecurityProfile.MaximumHitsPerActivation = 8;
	Definition->SecurityProfile.AuthorizedMovement.bEnabled = true;
	Definition->SecurityProfile.AuthorizedMovement.ExtraDistance = 750.0f;

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), RuntimeSpec);
	TestEqual(TEXT("Hit range is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumServerHitDistance, 1350.0f);
	TestEqual(TEXT("Target cap is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumTargetsPerQuery, 4);
	TestEqual(TEXT("Hit budget is frozen into the activation"),
		RuntimeSpec.SecurityProfile.MaximumHitsPerActivation, 8);
	TestEqual(TEXT("Movement budget is frozen into the activation"),
		RuntimeSpec.SecurityProfile.AuthorizedMovement.ExtraDistance, 750.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRuntimeSpecExecutionOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.ExecutionOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecExecutionOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Charge::StaticClass();

	FRPGSkillChargeExecutionConfig DefaultConfig;
	DefaultConfig.MaxChargeLevel = 3;
	Definition->DefaultExecutionConfig
		.InitializeAs<FRPGSkillChargeExecutionConfig>(DefaultConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Instant");
	Option.OverrideExecutionConfig
		.InitializeAs<FRPGSkillInstantExecutionConfig>(InstantConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);

	TestEqual(
		TEXT("Tripod replaces the execution policy"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Instant::StaticClass());
	const FRPGSkillInstantExecutionConfig* ResolvedConfig =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillInstantExecutionConfig>();
	TestNotNull(TEXT("Tripod replaces policy-specific config"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(
			TEXT("Resolved instant montage section"),
			ResolvedConfig->StartSection,
			FName(TEXT("Instant")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGChargeDefinitionRuntimeTranslationTest,
	"ProjectRPG.Skill.RuntimeSpec.LegacyChargeDefinitionTranslation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGChargeDefinitionRuntimeTranslationTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition_Charge* Definition =
		NewObject<URPGSkillDefinition_Charge>();
	Definition->ChargeTimePerLevel = 0.75f;
	Definition->MaxChargeLevel = 4;
	Definition->MaxChargeHoldTime = 1.25f;
	Definition->MontageSections.SectionNamesToPlay.Add(0, TEXT("Charge"));
	Definition->MontageSections.SectionNamesToPlay.Add(1, TEXT("Release"));

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(
		nullptr,
		FRPGSkillSaveData(),
		RuntimeSpec);

	TestEqual(
		TEXT("Legacy charge definitions resolve the new charge policy"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Charge::StaticClass());
	const FRPGSkillChargeExecutionConfig* Config =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChargeExecutionConfig>();
	TestNotNull(TEXT("Charge config is frozen into the runtime spec"), Config);
	if (Config)
	{
		TestEqual(TEXT("Charge levels"), Config->MaxChargeLevel, 4);
		TestEqual(TEXT("Time per level"), Config->ChargeTimePerLevel, 0.75f);
		TestEqual(TEXT("Maximum hold"), Config->MaxChargeHoldTime, 1.25f);
		TestEqual(TEXT("Charge section"), Config->ChargeSection, FName(TEXT("Charge")));
		TestEqual(TEXT("Release section"), Config->ReleaseSection, FName(TEXT("Release")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillExecutionConfigValidationTest,
	"ProjectRPG.Skill.ExecutionConfig.PolicyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillExecutionConfigValidationTest::RunTest(
	const FString& Parameters)
{
	const URPGSkillExecutionPolicy_Holding* HoldingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Holding>();
	const URPGSkillExecutionPolicy_Combo* ComboPolicy =
		GetDefault<URPGSkillExecutionPolicy_Combo>();

	FRPGSkillHoldingExecutionConfig HoldingConfig;
	HoldingConfig.HoldDuration = 1.0f;
	HoldingConfig.PerfectZoneStartTime = 0.75f;
	HoldingConfig.PerfectZoneEndTime = 1.0f;
	HoldingConfig.SuccessSection = TEXT("HoldingSuccess");
	FInstancedStruct HoldingStruct;
	HoldingStruct.InitializeAs<FRPGSkillHoldingExecutionConfig>(
		HoldingConfig);

	FText ValidationError;
	TestTrue(
		TEXT("Valid holding config is accepted"),
		HoldingPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	HoldingConfig.PerfectZoneStartTime = 1.1f;
	HoldingStruct.InitializeAs<FRPGSkillHoldingExecutionConfig>(
		HoldingConfig);
	TestFalse(
		TEXT("Holding perfect zone outside duration is rejected"),
		HoldingPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	FRPGSkillComboExecutionConfig ComboConfig;
	ComboConfig.ComboSections = {
		TEXT("Combo01"),
		TEXT("Combo02"),
		TEXT("Combo03")};
	FInstancedStruct ComboStruct;
	ComboStruct.InitializeAs<FRPGSkillComboExecutionConfig>(ComboConfig);
	TestTrue(
		TEXT("Valid combo config is accepted"),
		ComboPolicy->ValidateExecutionConfig(
			ComboStruct,
			ValidationError));

	ComboConfig.ComboSections.Add(NAME_None);
	ComboStruct.InitializeAs<FRPGSkillComboExecutionConfig>(ComboConfig);
	TestFalse(
		TEXT("Combo config with an empty section is rejected"),
		ComboPolicy->ValidateExecutionConfig(
			ComboStruct,
			ValidationError));

	TestFalse(
		TEXT("Policy/config type mismatch is rejected"),
		ComboPolicy->ValidateExecutionConfig(
			HoldingStruct,
			ValidationError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillRuntimeSpecTargetingOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.TargetingOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecTargetingOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultTargetingPolicyClass =
		URPGSkillTargetingPolicy_CameraDirection::StaticClass();
	FRPGSkillCameraDirectionTargetingConfig CameraConfig;
	CameraConfig.MaxRange = 5000.0f;
	Definition->DefaultTargetingConfig.InitializeAs<
		FRPGSkillCameraDirectionTargetingConfig>(CameraConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideTargetingPolicyClass =
		URPGSkillTargetingPolicy_SoftTarget::StaticClass();
	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	SoftTargetConfig.MaxRange = 900.0f;
	SoftTargetConfig.AssistAngleDegrees = 30.0f;
	Option.OverrideTargetingConfig.InitializeAs<
		FRPGSkillSoftTargetingConfig>(SoftTargetConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);

	TestEqual(
		TEXT("Tripod replaces only the targeting policy"),
		RuntimeSpec.TargetingPolicyClass.Get(),
		URPGSkillTargetingPolicy_SoftTarget::StaticClass());
	const FRPGSkillSoftTargetingConfig* ResolvedConfig =
		RuntimeSpec.TargetingConfig.GetPtr<FRPGSkillSoftTargetingConfig>();
	TestNotNull(TEXT("Tripod freezes the soft-target config"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(
			TEXT("Resolved assist range"),
			ResolvedConfig->MaxRange,
			900.0f);
		TestEqual(
			TEXT("Resolved assist angle"),
			ResolvedConfig->AssistAngleDegrees,
			30.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillTargetingPolicyValidationTest,
	"ProjectRPG.Skill.Targeting.PolicyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillTargetingPolicyValidationTest::RunTest(
	const FString& Parameters)
{
	const URPGSkillTargetingPolicy_CameraDirection* CameraPolicy =
		GetDefault<URPGSkillTargetingPolicy_CameraDirection>();
	const URPGSkillTargetingPolicy_SoftTarget* SoftTargetPolicy =
		GetDefault<URPGSkillTargetingPolicy_SoftTarget>();
	const URPGSkillTargetingPolicy_GroundPoint* GroundPolicy =
		GetDefault<URPGSkillTargetingPolicy_GroundPoint>();

	FText ValidationError;
	FRPGSkillCameraDirectionTargetingConfig CameraConfig;
	FInstancedStruct CameraStruct;
	CameraStruct.InitializeAs<FRPGSkillCameraDirectionTargetingConfig>(
		CameraConfig);
	TestTrue(
		TEXT("Valid camera direction config is accepted"),
		CameraPolicy->ValidateTargetingConfig(
			CameraStruct,
			ValidationError));

	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	FInstancedStruct SoftTargetStruct;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestTrue(
		TEXT("Valid soft-target config is accepted"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	SoftTargetConfig.AngleScoreWeight = 0.0f;
	SoftTargetConfig.DistanceScoreWeight = 0.0f;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestFalse(
		TEXT("Soft-target config rejects zero score weights"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	SoftTargetConfig.AngleScoreWeight = 0.75f;
	SoftTargetConfig.DistanceScoreWeight = 0.25f;
	SoftTargetConfig.ServerAssistConeToleranceDegrees = 46.0f;
	SoftTargetStruct.InitializeAs<FRPGSkillSoftTargetingConfig>(
		SoftTargetConfig);
	TestFalse(
		TEXT("Soft-target server cone rejects excessive validation grace"),
		SoftTargetPolicy->ValidateTargetingConfig(
			SoftTargetStruct,
			ValidationError));

	FRPGSkillGroundPointTargetingConfig GroundConfig;
	FInstancedStruct GroundStruct;
	GroundStruct.InitializeAs<FRPGSkillGroundPointTargetingConfig>(
		GroundConfig);
	TestTrue(
		TEXT("Valid ground-point config is accepted"),
		GroundPolicy->ValidateTargetingConfig(
			GroundStruct,
			ValidationError));

	GroundConfig.ServerAimToleranceDegrees = 181.0f;
	GroundStruct.InitializeAs<FRPGSkillGroundPointTargetingConfig>(
		GroundConfig);
	TestFalse(
		TEXT("Network validation rejects aim tolerance above 180 degrees"),
		GroundPolicy->ValidateTargetingConfig(
			GroundStruct,
			ValidationError));

	TestTrue(
		TEXT("Centered nearby candidates score above cone-edge candidates"),
		FRPGSkillTargetingMath::CalculateSoftTargetScore(
			0.0f, 100.0f, 45.0f, 1000.0f, 0.75f, 0.25f) >
		FRPGSkillTargetingMath::CalculateSoftTargetScore(
			45.0f, 100.0f, 45.0f, 1000.0f, 0.75f, 0.25f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillTargetDataCodecTest,
	"ProjectRPG.Skill.Targeting.TargetDataCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillTargetDataCodecTest::RunTest(const FString& Parameters)
{
	FRPGSkillTargetResult Original;
	Original.bIsValid = true;
	Original.SourceLocation = FVector(100.0, -50.0, 25.0);
	Original.TargetLocation = FVector(900.0, 150.0, 75.0);
	Original.AimDirection =
		(Original.TargetLocation - Original.SourceLocation).GetSafeNormal();
	Original.HitQueryTransform = FTransform(
		Original.AimDirection.Rotation(),
		Original.SourceLocation);
	Original.bOrientSourceToAim = true;

	const FGameplayAbilityTargetDataHandle Encoded =
		FRPGSkillTargetDataCodec::Encode(Original);
	TestTrue(TEXT("Valid target result encodes to GAS TargetData"),
		Encoded.IsValid(0));

	FRPGSkillTargetResult Decoded;
	TestTrue(TEXT("Standard GAS TargetData decodes to a skill target"),
		FRPGSkillTargetDataCodec::Decode(Encoded, Decoded));
	TestEqual(TEXT("Source location survives the codec"),
		Decoded.SourceLocation, Original.SourceLocation);
	TestEqual(TEXT("Target location survives the codec"),
		Decoded.TargetLocation, Original.TargetLocation);
	TestTrue(TEXT("Aim direction is rebuilt from spatial data"),
		Decoded.AimDirection.Equals(Original.AimDirection, KINDA_SMALL_NUMBER));
	TestFalse(TEXT("Client orientation flags are not trusted"),
		Decoded.bOrientSourceToAim);

	FRPGSkillTargetResult MalformedResult;
	TestFalse(TEXT("Empty TargetData is rejected"),
		FRPGSkillTargetDataCodec::Decode(
			FGameplayAbilityTargetDataHandle(),
			MalformedResult));

	return true;
}

#endif
