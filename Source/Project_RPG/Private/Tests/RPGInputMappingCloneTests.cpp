#if WITH_DEV_AUTOMATION_TESTS
#include "Controller/RPGInputMappingClone.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGInputMappingCloneTest, "ProjectRPG.Input.RuntimeMappingClone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGInputMappingCloneTest::RunTest(const FString&)
{
	TStrongObjectPtr<UInputMappingContext> Source(NewObject<UInputMappingContext>());
	TStrongObjectPtr<UInputMappingContext> Copy(NewObject<UInputMappingContext>());
	auto* Action = NewObject<UInputAction>(Source.Get());
	auto& Mapping = Source->MapKey(Action, EKeys::W);
	auto* Modifier = NewObject<UInputModifierNegate>(Source.Get());
	Modifier->bX = true;
	Mapping.Modifiers.Add(Modifier);
	Mapping.Triggers.Add(NewObject<UInputTriggerDown>(Source.Get()));
	TestTrue(TEXT("Empty runtime copy restores authored bindings"), RPGInputMappingClone::RestoreMissingRecords(*Source, *Copy));
	TestEqual(TEXT("Mapping count matches source"), Copy->GetMappings().Num(), 1);
	if (Copy->GetMappings().IsEmpty()) return false;
	const auto& Restored = Copy->GetMapping(0);
	TestTrue(TEXT("Action and key are preserved"), Restored.Action == Action && Restored.Key == EKeys::W);
	TestTrue(TEXT("Modifier is independently owned"), Restored.Modifiers.Num() == 1
		&& Restored.Modifiers[0] != Modifier && Restored.Modifiers[0]->GetOuter() == Copy.Get());
	TestTrue(TEXT("Trigger is independently owned"), Restored.Triggers.Num() == 1
		&& Restored.Triggers[0] != Mapping.Triggers[0] && Restored.Triggers[0]->GetOuter() == Copy.Get());
	if (auto* CopiedModifier = Cast<UInputModifierNegate>(Restored.Modifiers[0])) CopiedModifier->bX = false;
	TestTrue(TEXT("Runtime modifier edits do not mutate source"), Modifier->bX);
	Copy->GetMapping(0).Key = EKeys::Y;
	TestFalse(TEXT("Existing player bindings are never reset"), RPGInputMappingClone::RestoreMissingRecords(*Source, *Copy));
	TestTrue(TEXT("Player override is retained"), Copy->GetMapping(0).Key == EKeys::Y);
	TestFalse(TEXT("Source itself cannot be repaired in place"), RPGInputMappingClone::RestoreMissingRecords(*Source, *Source));
	TestTrue(TEXT("Source key remains untouched"), Source->GetMapping(0).Key == EKeys::W);
	TStrongObjectPtr<UInputMappingContext> Empty(NewObject<UInputMappingContext>());
	TStrongObjectPtr<UInputMappingContext> OtherEmpty(NewObject<UInputMappingContext>());
	TestFalse(TEXT("Intentionally empty source stays empty"), RPGInputMappingClone::RestoreMissingRecords(*Empty, *OtherEmpty));
	return true;
}
#endif
