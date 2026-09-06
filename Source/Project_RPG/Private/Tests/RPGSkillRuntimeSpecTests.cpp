#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimMontage.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "Engine/AssetManager.h"
#include "FunctionLibrary/RPGSkillConfigBlueprintLibrary.h"
#include "Misc/AutomationTest.h"
#include "RPGAbilityTags.h"
#include "Skill/RPGSkillDefinition.h"
#include "Skill/RPGSkillDefinition_Charge.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "Skill/RPGSkillExecutionPolicy.h"
#include "Skill/RPGSkillExecutionTypes.h"
#include "Skill/RPGSkillTargetingPolicy.h"
#include "Skill/RPGSkillTargetingTypes.h"
#include "UI/MVVM/RPGSkillViewModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillConfigBlueprintLibraryTest,
	"ProjectRPG.Skill.Config.TypeSafeWrapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillConfigBlueprintLibraryTest::RunTest(
	const FString& Parameters)
{
	FRPGSkillInstantExecutionConfig InstantConfig;
	InstantConfig.StartSection = TEXT("Attack");
	const FInstancedStruct InstantStruct =
		URPGSkillConfigBlueprintLibrary::MakeInstantExecutionConfig(
			InstantConfig);
	const FRPGSkillInstantExecutionConfig* WrappedInstant =
		InstantStruct.GetPtr<FRPGSkillInstantExecutionConfig>();
	TestNotNull(TEXT("Instant config retains its concrete type"), WrappedInstant);
	if (WrappedInstant)
	{
		TestEqual(TEXT("Instant config retains its authored section"),
			WrappedInstant->StartSection,
			InstantConfig.StartSection);
	}

	FRPGSkillHoldingExecutionConfig HoldingConfig;
	HoldingConfig.HoldDuration = 4.0f;
	HoldingConfig.SuccessSection = TEXT("End");
	const FInstancedStruct HoldingStruct =
		URPGSkillConfigBlueprintLibrary::MakeHoldingExecutionConfig(
			HoldingConfig);
	const FRPGSkillHoldingExecutionConfig* WrappedHolding =
		HoldingStruct.GetPtr<FRPGSkillHoldingExecutionConfig>();
	TestNotNull(TEXT("Holding config retains its concrete type"), WrappedHolding);
	if (WrappedHolding)
	{
		TestEqual(TEXT("Holding config retains its authored duration"),
			WrappedHolding->HoldDuration,
			HoldingConfig.HoldDuration);
	}

	FRPGSkillSoftTargetingConfig SoftTargetConfig;
	SoftTargetConfig.MaxRange = 875.0f;
	const FInstancedStruct SoftTargetStruct =
		URPGSkillConfigBlueprintLibrary::MakeSoftTargetingConfig(
			SoftTargetConfig);
	const FRPGSkillSoftTargetingConfig* WrappedSoftTarget =
		SoftTargetStruct.GetPtr<FRPGSkillSoftTargetingConfig>();
	TestNotNull(TEXT("Targeting config retains its concrete type"),
		WrappedSoftTarget);
	if (WrappedSoftTarget)
	{
		TestEqual(TEXT("Targeting config retains its authored range"),
			WrappedSoftTarget->MaxRange,
			SoftTargetConfig.MaxRange);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillViewModelReactiveInitializationTest,
	"ProjectRPG.Skill.UI.ReactiveInitialization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillViewModelReactiveInitializationTest::RunTest(
	const FString& Parameters)
{
	URPGPlayerSkillComponent* SkillComponent =
		NewObject<URPGPlayerSkillComponent>();
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	URPGSkillViewModel* ViewModel = NewObject<URPGSkillViewModel>();

	ViewModel->InitializeSkillData(SkillComponent, {Definition});
	TestTrue(TEXT("The skill view model subscribes to model changes"),
		SkillComponent->OnSkillDataChanged.IsBound());
	TestEqual(TEXT("Initial total SP comes from the skill component"),
		ViewModel->TotalSP,
		SkillComponent->GetTotalSP());
	TestEqual(TEXT("One definition creates one slot"),
		ViewModel->SkillSlots.Num(),
		1);

	ViewModel->TotalSP = 0;
	SkillComponent->OnSkillDataChanged.Broadcast(FGameplayTag());
	TestEqual(TEXT("Aggregate model events refresh total SP"),
		ViewModel->TotalSP,
		SkillComponent->GetTotalSP());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillPrimaryAssetIdentityTest,
	"ProjectRPG.Skill.Catalog.PrimaryAssetIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillPrimaryAssetIdentityTest::RunTest(const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;

	const FPrimaryAssetId ExpectedId =
		URPGSkillDefinition::MakePrimaryAssetIdForTag(Definition->SkillTag);
	TestTrue(TEXT("A valid skill tag produces a primary asset id"),
		ExpectedId.IsValid());
	TestEqual(TEXT("Skill definitions use the catalog primary asset type"),
		ExpectedId.PrimaryAssetType,
		URPGSkillDefinition::PrimaryAssetType);
	TestEqual(TEXT("The skill tag is the stable primary asset name"),
		ExpectedId.PrimaryAssetName,
		Definition->SkillTag.GetTagName());
	TestEqual(TEXT("The definition exposes the same stable identity"),
		Definition->GetPrimaryAssetId(),
		ExpectedId);

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGAuthoredSkillCatalogTest,
	"ProjectRPG.Skill.Catalog.AuthoredDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGAuthoredSkillCatalogTest::RunTest(const FString& Parameters)
{
	const TArray<FGameplayTag> ExpectedTags =
	{
		RPGGameplayTags::Player_Ability_Skill_AssultBlade,
		RPGGameplayTags::Player_Ability_Skill_JumpSmash,
		RPGGameplayTags::Player_Ability_Skill_Charge,
		RPGGameplayTags::Player_Ability_Skill_WhirlWind,
	};

	UAssetManager& AssetManager = UAssetManager::Get();
	TArray<FPrimaryAssetId> RegisteredIds;
	AssetManager.GetPrimaryAssetIdList(
		URPGSkillDefinition::PrimaryAssetType,
		RegisteredIds);

	for (const FGameplayTag SkillTag : ExpectedTags)
	{
		const FPrimaryAssetId ExpectedId =
			URPGSkillDefinition::MakePrimaryAssetIdForTag(SkillTag);
		TestTrue(
			*FString::Printf(TEXT("Catalog registers %s"),
				*SkillTag.ToString()),
			RegisteredIds.Contains(ExpectedId));

		const FSoftObjectPath AssetPath =
			AssetManager.GetPrimaryAssetPath(ExpectedId);
		TestTrue(
			*FString::Printf(TEXT("Catalog resolves %s"),
				*SkillTag.ToString()),
			AssetPath.IsValid());
		URPGSkillDefinition* Definition =
			Cast<URPGSkillDefinition>(AssetPath.TryLoad());
		TestNotNull(
			*FString::Printf(TEXT("Catalog loads %s"),
				*SkillTag.ToString()),
			Definition);
		if (Definition)
		{
			TestEqual(TEXT("Loaded definition retains its stable tag"),
				Definition->SkillTag,
				SkillTag);
			FDataValidationContext ValidationContext;
			TestEqual(TEXT("Loaded definition passes data validation"),
				Definition->IsDataValid(ValidationContext),
				EDataValidationResult::Valid);
		}
	}

	return true;
}
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefinitionProgressionRulesTest,
	"ProjectRPG.Skill.Definition.ProgressionRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefinitionProgressionRulesTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->MaxSkillLevel = 5;

	FRPGSkillTripodTier& FirstTier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	FirstTier.RequiredSkillLevel = 2;
	FirstTier.Options.AddDefaulted(2);
	FRPGSkillTripodTier& SecondTier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	SecondTier.RequiredSkillLevel = 4;
	SecondTier.Options.AddDefaulted();

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 99;
	SaveData.SelectedTripodIndices = {1, 7, 0};
	Definition->NormalizeSaveData(SaveData);
	TestEqual(TEXT("Authored maximum level clamps save data"),
		SaveData.SkillLevel,
		5);
	TestEqual(TEXT("Selection count follows authored tripod tiers"),
		SaveData.SelectedTripodIndices.Num(),
		2);
	TestEqual(TEXT("A valid authored option is preserved"),
		SaveData.SelectedTripodIndices[0],
		1);
	TestEqual(TEXT("An out-of-range option is cleared"),
		SaveData.SelectedTripodIndices[1],
		INDEX_NONE);

	SaveData.SkillLevel = 3;
	SaveData.SelectedTripodIndices = {0, 0};
	Definition->NormalizeSaveData(SaveData);
	TestEqual(TEXT("A locked-tier selection is cleared"),
		SaveData.SelectedTripodIndices[1],
		INDEX_NONE);
	TestTrue(TEXT("A locked tier can always be deselected"),
		Definition->IsTripodSelectionAllowed(1, 1, INDEX_NONE));
	TestFalse(TEXT("Unknown tripod options are rejected"),
		Definition->IsTripodSelectionAllowed(5, 1, 2));

	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillDefinitionDataValidationTest,
	"ProjectRPG.Skill.Definition.DataValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillDefinitionDataValidationTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->SkillName = FText::FromString(TEXT("Validation Skill"));
	Definition->SkillTag =
		RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	Definition->SkillMontage = NewObject<UAnimMontage>(Definition);
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();

	FDataValidationContext ValidContext;
	TestEqual(TEXT("A complete definition passes editor validation"),
		Definition->IsDataValid(ValidContext),
		EDataValidationResult::Valid);

	Definition->BaseCooldown = 0.5f;
	FDataValidationContext CooldownContext;
	TestEqual(TEXT("Sub-second authored cooldown is rejected"),
		Definition->IsDataValid(CooldownContext),
		EDataValidationResult::Invalid);

	Definition->BaseCooldown = 1.0f;
	FRPGSkillChargeExecutionConfig ChargeConfig;
	Definition->DefaultExecutionConfig.InitializeAs<
		FRPGSkillChargeExecutionConfig>(ChargeConfig);
	FDataValidationContext PolicyContext;
	TestEqual(TEXT("Mismatched policy and config are rejected"),
		Definition->IsDataValid(PolicyContext),
		EDataValidationResult::Invalid);

	Definition->DefaultExecutionConfig.Reset();
	FRPGSkillTripodTier& Tier =
		Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Charge::StaticClass();
	FDataValidationContext InvalidTripodContext;
	TestEqual(TEXT("A tripod cannot select a policy without its config"),
		Definition->IsDataValid(InvalidTripodContext),
		EDataValidationResult::Invalid);

	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Instant::StaticClass();
	FRPGSkillInstantExecutionConfig InstantConfig;
	Option.OverrideExecutionConfig.InitializeAs<
		FRPGSkillInstantExecutionConfig>(InstantConfig);
	FDataValidationContext ValidTripodContext;
	TestEqual(TEXT("A type-safe tripod execution override is valid"),
		Definition->IsDataValid(ValidTripodContext),
		EDataValidationResult::Valid);

	return true;
}
#endif

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
	FRPGSkillRuntimeSpecCastingChainOverrideTest,
	"ProjectRPG.Skill.RuntimeSpec.CastingChainOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillRuntimeSpecCastingChainOverrideTest::RunTest(
	const FString& Parameters)
{
	URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>();
	Definition->DefaultExecutionPolicyClass =
		URPGSkillExecutionPolicy_Casting::StaticClass();
	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastDuration = 1.5f;
	CastingConfig.CompleteSection = TEXT("CastComplete");
	Definition->DefaultExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);

	FRPGSkillTripodTier& Tier = Definition->TripodTiers.AddDefaulted_GetRef();
	Tier.RequiredSkillLevel = 1;
	FRPGSkillTripodOption& Option = Tier.Options.AddDefaulted_GetRef();
	Option.OverrideExecutionPolicyClass =
		URPGSkillExecutionPolicy_Chain::StaticClass();
	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	ChainConfig.LinkWindowDuration = 0.6f;
	Option.OverrideExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);

	FRPGSkillSaveData SaveData;
	SaveData.SkillLevel = 1;
	SaveData.SelectedTripodIndices = {0, INDEX_NONE, INDEX_NONE};

	FRPGSkillRuntimeSpec RuntimeSpec;
	Definition->BuildRuntimeSpec(nullptr, SaveData, RuntimeSpec);
	TestEqual(
		TEXT("A tripod can replace Casting with Chain"),
		RuntimeSpec.ExecutionPolicyClass.Get(),
		URPGSkillExecutionPolicy_Chain::StaticClass());
	const FRPGSkillChainExecutionConfig* ResolvedConfig =
		RuntimeSpec.ExecutionConfig.GetPtr<FRPGSkillChainExecutionConfig>();
	TestNotNull(TEXT("Chain config is frozen into the activation"), ResolvedConfig);
	if (ResolvedConfig)
	{
		TestEqual(TEXT("Chain section count"),
			ResolvedConfig->ChainSections.Num(), 2);
		TestEqual(TEXT("Chain link window"),
			ResolvedConfig->LinkWindowDuration, 0.6f);
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
	const URPGSkillExecutionPolicy_Casting* CastingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Casting>();
	const URPGSkillExecutionPolicy_Chain* ChainPolicy =
		GetDefault<URPGSkillExecutionPolicy_Chain>();

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

	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastDuration = 1.25f;
	CastingConfig.CompleteSection = TEXT("CastComplete");
	FInstancedStruct CastingStruct;
	CastingStruct.InitializeAs<FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestTrue(
		TEXT("Valid casting config is accepted"),
		CastingPolicy->ValidateExecutionConfig(
			CastingStruct,
			ValidationError));

	CastingConfig.CastDuration = 61.0f;
	CastingStruct.InitializeAs<FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestFalse(
		TEXT("Casting config rejects an excessive server lifetime"),
		CastingPolicy->ValidateExecutionConfig(
			CastingStruct,
			ValidationError));

	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	ChainConfig.LinkWindowDuration = 0.75f;
	FInstancedStruct ChainStruct;
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestTrue(
		TEXT("Valid chain config is accepted"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	ChainConfig.LinkWindowDuration = 5.1f;
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain config rejects an excessive input window"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	ChainConfig.LinkWindowDuration = 0.75f;
	ChainConfig.ChainSections.SetNum(1);
	ChainStruct.InitializeAs<FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain config requires at least two sections"),
		ChainPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	TestFalse(
		TEXT("Casting policy rejects Chain config"),
		CastingPolicy->ValidateExecutionConfig(
			ChainStruct,
			ValidationError));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FRPGSkillAuthoredMontageValidationTest,
	"ProjectRPG.Skill.RuntimeSpec.AuthoredMontageValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillAuthoredMontageValidationTest::RunTest(
	const FString& Parameters)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	// AddAnimCompositeSection is editor-only in UE 5.8.  The runtime-spec
	// validation only needs authored section names, so construct the minimal
	// runtime representation directly for this Development automation test.
	auto AddRuntimeSection = [Montage](const FName SectionName)
	{
		FCompositeSection& Section = Montage->CompositeSections.Emplace_GetRef();
		Section.SectionName = SectionName;
		Section.SetTime(0.0f);
	};
	AddRuntimeSection(TEXT("CastStart"));
	AddRuntimeSection(TEXT("CastComplete"));
	AddRuntimeSection(TEXT("Chain01"));
	AddRuntimeSection(TEXT("Chain02"));

	FText ValidationError;
	FRPGSkillRuntimeSpec RuntimeSpec;
	RuntimeSpec.Montage = Montage;

	FRPGSkillCastingExecutionConfig CastingConfig;
	CastingConfig.CastingSection = TEXT("CastStart");
	CastingConfig.CompleteSection = TEXT("CastComplete");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);
	const URPGSkillExecutionPolicy_Casting* CastingPolicy =
		GetDefault<URPGSkillExecutionPolicy_Casting>();
	TestTrue(
		TEXT("Casting accepts authored montage sections"),
		CastingPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	CastingConfig.CompleteSection = TEXT("MissingSection");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillCastingExecutionConfig>(CastingConfig);
	TestFalse(
		TEXT("Casting rejects a missing completion section before activation"),
		CastingPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	FRPGSkillChainExecutionConfig ChainConfig;
	ChainConfig.ChainSections = {TEXT("Chain01"), TEXT("Chain02")};
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);
	const URPGSkillExecutionPolicy_Chain* ChainPolicy =
		GetDefault<URPGSkillExecutionPolicy_Chain>();
	TestTrue(
		TEXT("Chain accepts authored montage sections"),
		ChainPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

	ChainConfig.ChainSections[1] = TEXT("MissingSection");
	RuntimeSpec.ExecutionConfig.InitializeAs<
		FRPGSkillChainExecutionConfig>(ChainConfig);
	TestFalse(
		TEXT("Chain rejects a missing link section before activation"),
		ChainPolicy->ValidateRuntimeSpecData(RuntimeSpec, ValidationError));

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
