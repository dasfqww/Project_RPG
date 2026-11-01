#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Character/RPGNonPlayerCharacter.h"
#include "Skill/RPGSkillDefinition.h"
#include <limits>
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionContentTest,
	"ProjectRPG.Combat.Contribution.ContentFreezeAndEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionContentTest::RunTest(const FString&)
{
	auto* Charge = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_ChargeSlash.DA_Skill_ChargeSlash"));
	auto* Jump = LoadObject<URPGSkillDefinition>(nullptr,
		TEXT("/Game/Blueprints/GameData/Skill/DA_Skill_JumpSmash.DA_Skill_JumpSmash"));
	if (!Charge || !Jump) return false;
	for (auto* Definition : {Charge, Jump})
	{
		for (const FName Choice : {FName(TEXT("Stagger")), FName(TEXT("PartBreak"))})
		{
			const int32 Level = Definition == Jump && Choice == TEXT("PartBreak") ? 4 : 7;
			FRPGSkillSaveData Save;
			Save.SkillLevel = Level;
			Save.SelectedTripodIndices.Init(INDEX_NONE, Definition->TripodTiers.Num());
			int32 Tier, Option;
			if (!TestTrue(TEXT("Stable saved contribution choice resolves at its authored tier"),
				Definition->ResolveSavedTripod(Level, Choice, Tier, Option))) return false;
			Save.SelectedTripodIndices[Tier] = Option;
			FRPGSkillRuntimeSpec Runtime;
			Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
			FRPGCombatContributionAmounts Amounts;
			TestTrue(TEXT("Authored contribution amounts resolve"), Runtime.TryGetCombatContributions(Amounts));
			TestEqual(TEXT("Only the selected mechanic receives the 25 percent bonus"),
				Choice == TEXT("Stagger") ? Amounts.Stagger : Amounts.PartBreak, 25.f);
			TestEqual(TEXT("The other mechanic retains its baseline"),
				Choice == TEXT("Stagger") ? Amounts.PartBreak : Amounts.Stagger, 20.f);
			Save.SkillLevel = 1;
			Definition->BuildRuntimeSpec(nullptr, Save, Runtime);
			Runtime.TryGetCombatContributions(Amounts);
			TestEqual(TEXT("Locked selections cannot enhance level-one participation"), Amounts.Stagger, 20.f);
			TestEqual(TEXT("Level-one part participation is independent from investment"), Amounts.PartBreak, 20.f);
		}
	}
	auto* Copy = DuplicateObject<URPGSkillDefinition>(Charge, GetTransientPackage());
	FRPGSkillRuntimeSpec Frozen;
	Copy->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Frozen);
	Copy->Contributions.Stagger = 99.f;
	FRPGCombatContributionAmounts Amounts;
	Frozen.TryGetCombatContributions(Amounts);
	TestEqual(TEXT("An active snapshot retains its pre-edit contribution"), Amounts.Stagger, 20.f);
	Frozen.StatScalars.Add(FGameplayTag::RequestGameplayTag(TEXT("Shared.Stat.StaggerContribution")),
		std::numeric_limits<float>::quiet_NaN());
	TestFalse(TEXT("Invalid scalar cannot reach a receiver"), Frozen.TryGetCombatContributions(Amounts));
	TestEqual(TEXT("Failed resolution does not leak partially resolved values"), Amounts.Stagger, 0.f);
	Frozen.Reset();
	TestEqual(TEXT("Snapshot reuse resets contribution state"), Frozen.Contributions.Stagger, 0.f);
#if WITH_EDITOR
	FDataValidationContext Valid;
	TestEqual(TEXT("Actual contribution definition passes editor validation"), Charge->IsDataValid(Valid), EDataValidationResult::Valid);
	Copy->Contributions.PartBreak = -1.f;
	FDataValidationContext Invalid;
	TestEqual(TEXT("Editor rejects malformed base units"), Copy->IsDataValid(Invalid), EDataValidationResult::Invalid);
#endif
	auto* DummyClass = LoadClass<ARPGNonPlayerCharacter>(nullptr,
		TEXT("/Game/Blueprints/Character/NPC/DummyNPC/BP_Dummy_Gdn.BP_Dummy_Gdn_C"));
	if (!TestNotNull(TEXT("Authored practice target"), DummyClass)) return false;
	const auto* Receiver = DummyClass->GetDefaultObject<ARPGNonPlayerCharacter>()->FindComponentByClass<URPGCombatContributionComponent>();
	if (!TestNotNull(TEXT("Practice target has a native receiver"), Receiver)) return false;
	TestTrue(TEXT("Only explicitly authored targets opt in"), Receiver->GetReceiverProfile().bEnabled);
	TestFalse(TEXT("Ordinary NPCs retain disabled mechanics"),
		GetDefault<ARPGNonPlayerCharacter>()->FindComponentByClass<URPGCombatContributionComponent>()->GetReceiverProfile().bEnabled);
	TestEqual(TEXT("Practice target uses a visible threshold of one hundred units"), Receiver->GetReceiverProfile().StaggerThreshold, 100.f);
	return true;
}

#endif
