#if WITH_DEV_AUTOMATION_TESTS

#include "Skill/QuickSlot/RPGSkillQuickSlotServices.h"
#include "Skill/QuickSlot/RPGQuickSlotInputBindings.h"
#include "Character/RPGPlayer.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Component/Skill/RPGPlayerSkillComponent.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "DataTable/SkillData.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameplayAbilitySpec.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "RPGInputTags.h"
#include "Skill/RPGGameplayAbility_SkillContainer.h"
#include "Skill/RPGSkillDefinition.h"
#include "UI/RPGDragDropOperation.h"
#include "UI/RPGQuickSlotWidget.h"
#include "UI/Skill/RPGSkillQuickSlotDragDrop.h"
#include "UI/ViewModel/RPGSkillSlotViewModel.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"

namespace RPGSkillQuickSlotTests
{
struct FInput : IRPGSkillQuickSlotInput
{
	TArray<FGameplayAbilitySpecHandle> Presses, Releases;
	virtual void Press(FGameplayAbilitySpecHandle Handle) override { Presses.Add(Handle); }
	virtual void Release(FGameplayAbilitySpecHandle Handle) override { Releases.Add(Handle); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillQuickSlotHeldInputTest,
	"ProjectRPG.Skill.QuickSlot.HeldInputIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillQuickSlotHeldInputTest::RunTest(const FString& Parameters)
{
	using FRouter = FRPGSkillQuickSlotInputRouter;
	FRouter::FPressedSlots State;
	RPGSkillQuickSlotTests::FInput Input;
	// GenerateNewHandle is not exported; the exported spec constructor creates the identity inside GAS.
	const FGameplayAbilitySpecHandle First = FGameplayAbilitySpec(URPGGameplayAbility_SkillContainer::StaticClass(), 1).Handle;
	const FGameplayAbilitySpecHandle Replacement = FGameplayAbilitySpec(URPGGameplayAbility_SkillContainer::StaticClass(), 1).Handle;
	if (!TestTrue(TEXT("Fixture uses two distinct valid ability identities"),
		First.IsValid() && Replacement.IsValid() && First != Replacement)) return false;
	TestTrue(TEXT("One press is recorded"), FRouter::Press(State, 0, First, Input));
	TestFalse(TEXT("Held/repeated input cannot reroute after assignment changes"), FRouter::Press(State, 0, Replacement, Input));
	TestEqual(TEXT("Only one initial press"), Input.Presses.Num(), 1);
	FRouter::Release(State, 0, Input);
	TestEqual(TEXT("Release targets the original ability"), Input.Releases[0], First);
	FRouter::Release(State, 0, Input);
	TestEqual(TEXT("Duplicate key-up does not release a different ability"), Input.Releases.Num(), 1);
	TestTrue(TEXT("A fresh press uses the replacement"), FRouter::Press(State, 0, Replacement, Input));
	TestTrue(TEXT("A second key may share the same spec"), FRouter::Press(State, 1, Replacement, Input));
	TestEqual(TEXT("Sharing a spec does not press it twice"), Input.Presses.Num(), 2);
	FRouter::Release(State, 0, Input);
	TestEqual(TEXT("One held key still owns the spec"), Input.Releases.Num(), 1);
	FRouter::ReleaseAll(State, Input);
	TestEqual(TEXT("UI opening/pawn teardown releases the last owner"), Input.Releases.Num(), 2);
	TestTrue(TEXT("Release-all empties input state"), State.IsEmpty());
	TestFalse(TEXT("Invalid handles do not create held state"), FRouter::Press(State, 0, {}, Input));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillQuickSlotInputDataTest,
	"ProjectRPG.Skill.QuickSlot.DataDrivenBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillQuickSlotInputDataTest::RunTest(const FString& Parameters)
{
	UDataAsset_InputConfig* Config = NewObject<UDataAsset_InputConfig>();
	UInputAction* First = NewObject<UInputAction>(Config);
	UInputAction* Last = NewObject<UInputAction>(Config);
	UInputAction* Item = NewObject<UInputAction>(Config);
	UInputAction* Alias = NewObject<UInputAction>(Config);
	auto Add = [Config](FGameplayTag Tag, UInputAction* Action)
	{
		FWarriorInputActionConfig Entry;
		Entry.InputTag = Tag; Entry.InputAction = Action;
		Config->NativeInputActions.Add(Entry);
	};
	Add(RPGGameplayTags::InputTag_QuickSkill_1, First);
	Add(RPGGameplayTags::InputTag_QuickSkill_8, Last);
	Add(RPGGameplayTags::InputTag_QuickItem_F1, Alias);
	Add(RPGGameplayTags::InputTag_QuickItem_1, Item);
	const auto Bindings = FRPGQuickSlotInputBindings::Read(Config);
	TestEqual(TEXT("Configured rows, not a fixed eight-entry loop"), Bindings.Num(), 4);
	TestEqual(TEXT("Slot indices come from stable input tags"), Bindings[1].SlotIndex, 7);
	TestEqual(TEXT("Both existing item hotkeys remain bound"), Bindings[2].SlotIndex, Bindings[3].SlotIndex);
	TestEqual(TEXT("The numeric item mapping supplies its label"),
		FRPGQuickSlotInputBindings::FindTag(Config, false, 0), RPGGameplayTags::InputTag_QuickItem_1.GetTag());
	FWarriorInputActionConfig Legacy;
	Legacy.InputTag = RPGGameplayTags::InputTag_Skill1;
	Legacy.InputAction = First;
	Config->AbilityInputActions.Add(Legacy);
	TestTrue(TEXT("A quick-slot action suppresses direct hardwired skill input"),
		FRPGQuickSlotInputBindings::OwnsAbilityInput(Config, Legacy.InputTag));
	Legacy.InputAction = NewObject<UInputAction>(Config);
	Config->AbilityInputActions[0] = Legacy;
	Config->DefaultMappingContext = NewObject<UInputMappingContext>(Config);
	Config->DefaultMappingContext->MapKey(First, EKeys::Q);
	Config->DefaultMappingContext->MapKey(Last, EKeys::F);
	Config->DefaultMappingContext->MapKey(Item, EKeys::One);
	UInputMappingContext* SlotContext = NewObject<UInputMappingContext>(Config);
	FRPGQuickSlotInputBindings::RebuildSkillContext(Config, *SlotContext);
	TestEqual(TEXT("Only skill mappings are projected above weapon input"), SlotContext->GetMappings().Num(), 2);
	Config->DefaultMappingContext->UnmapAllKeysFromAction(First);
	Config->DefaultMappingContext->MapKey(First, EKeys::E);
	FRPGQuickSlotInputBindings::RebuildSkillContext(Config, *SlotContext);
	TestFalse(TEXT("Rebuilding removes the old quick-slot key"), SlotContext->GetMappings().ContainsByPredicate(
		[](const auto& Mapping) { return Mapping.Key == EKeys::Q; }));
	Config->DefaultMappingContext->UnmapAllKeysFromAction(First);
	Config->DefaultMappingContext->MapKey(First, EKeys::Q);
	Config->DefaultMappingContext->MapKey(Legacy.InputAction, EKeys::Q);
	TestTrue(TEXT("Different actions on the same key cannot execute two skills"),
		FRPGQuickSlotInputBindings::OwnsAbilityInput(Config, Legacy.InputTag));
	Config->DefaultMappingContext->UnmapAllKeysFromAction(Legacy.InputAction);
	Config->DefaultMappingContext->MapKey(Legacy.InputAction, EKeys::T);
	TestFalse(TEXT("Independent ability keys are retained"),
		FRPGQuickSlotInputBindings::OwnsAbilityInput(Config, Legacy.InputTag));
	bool bSkill = true; int32 Index = 0;
	TestFalse(TEXT("Non-slot tags cannot be parsed as slot indices"),
		FRPGQuickSlotInputBindings::ParseTag(RPGGameplayTags::InputTag_Move, bSkill, Index));
	Add(RPGGameplayTags::InputTag_QuickSkill_1, First);
	TestEqual(TEXT("Repeated identical rows bind only once"), FRPGQuickSlotInputBindings::Read(Config).Num(), 4);
	Add(RPGGameplayTags::InputTag_QuickSkill_2, First);
	TestEqual(TEXT("One action for two skills rejects both; items and independent skills survive"),
		FRPGQuickSlotInputBindings::Read(Config).Num(), 3);
	Config->NativeInputActions.Pop();
	Add(RPGGameplayTags::InputTag_QuickSkill_1, Last);
	TestEqual(TEXT("Different actions for one slot reject all affected skill rows"),
		FRPGQuickSlotInputBindings::Read(Config).Num(), 2);
	Config->NativeInputActions.Pop();
	Config->DefaultMappingContext->MapKey(Last, EKeys::Q);
	FRPGQuickSlotInputBindings::RebuildSkillContext(Config, *SlotContext);
	TestTrue(TEXT("Conflicting skills cannot enter the projected context"), SlotContext->GetMappings().IsEmpty());
	TestEqual(TEXT("Two skill actions on one physical key fail closed"),
		FRPGQuickSlotInputBindings::Read(Config).Num(), 2);
	Config->AbilityInputActions[0].InputAction = First;
	TestTrue(TEXT("A conflicted slot still suppresses the old fixed skill"),
		FRPGQuickSlotInputBindings::OwnsAbilityInput(Config, Legacy.InputTag));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillQuickSlotRegistrationTest,
	"ProjectRPG.Skill.QuickSlot.RegistrationAndDrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillQuickSlotRegistrationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Isolated registration world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	ARPGPlayer* Player = World->SpawnActor<ARPGPlayer>();
	if (!TestNotNull(TEXT("Native player"), Player)) return false;
	URPGAbilitySystemComponent* ASC = Player->GetRPGAbilitySystemComponent();
	UQuickSlotComponent* Slots = Player->FindComponentByClass<UQuickSlotComponent>();
	URPGPlayerSkillComponent* Palette = Player->FindComponentByClass<URPGPlayerSkillComponent>();
	if (!TestNotNull(TEXT("Ability system"), ASC) || !TestNotNull(TEXT("Slot storage"), Slots) ||
		!TestNotNull(TEXT("Palette owner"), Palette)) return false;
	ASC->InitAbilityActorInfo(Player, Player);
	URPGSkillDefinition* LastDefinition = nullptr;
	auto Grant = [this, ASC, &LastDefinition](const FGameplayTag Tag)
	{
		const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(URPGGameplayAbility_SkillContainer::StaticClass(), 1));
		const auto* Property = FindFProperty<FObjectPropertyBase>(URPGGameplayAbility_SkillContainer::StaticClass(), TEXT("SkillDefinition"));
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Handle);
		UGameplayAbility* Instance = Spec ? Spec->GetPrimaryInstance() : nullptr;
		if (!TestNotNull(TEXT("Definition property"), Property) || !TestNotNull(TEXT("Granted instance"), Instance))
			return FGameplayAbilitySpecHandle();
		URPGSkillDefinition* Definition = NewObject<URPGSkillDefinition>(ASC);
		Definition->SkillTag = Tag;
		LastDefinition = Definition;
		Property->SetObjectPropertyValue_InContainer(Instance, Definition);
		return Handle;
	};
	// Registered tags serve as transient identities; no authored data or CDO is edited.
	const FGameplayTag First = RPGGameplayTags::InputTag_QuickSkill_1;
	const FGameplayTag Second = RPGGameplayTags::InputTag_QuickSkill_2;
	if (!Grant(First).IsValid() || !Grant(Second).IsValid()) return false;

	// Base icons must agree even when only the table has an icon.
	URPGSkillSlotViewModel* PaletteSlot = NewObject<URPGSkillSlotViewModel>(ASC);
	UTexture2D* DirectIcon = NewObject<UTexture2D>(ASC);
	UTexture2D* TableIcon = NewObject<UTexture2D>(ASC);
	const auto CheckIcon = [this, LastDefinition, PaletteSlot, Slots, Second](
		const TCHAR* Context, UTexture2D* Expected)
	{
		PaletteSlot->SetSkillDefinition(LastDefinition);
		TestEqual(FString::Printf(TEXT("%s: palette icon"), Context), PaletteSlot->SkillIcon, Expected);
		TestEqual(FString::Printf(TEXT("%s: quick-slot icon"), Context), Slots->ReadSkillPresentation(Second).Icon, Expected);
		FRPGSkillRuntimeSpec Runtime;
		LastDefinition->BuildRuntimeSpec(nullptr, FRPGSkillSaveData(), Runtime);
		TestEqual(FString::Printf(TEXT("%s: runtime icon"), Context), Runtime.Icon.Get(), Expected);
	};
	LastDefinition->SkillIcon = DirectIcon;
	CheckIcon(TEXT("No data table uses Definition"), DirectIcon);
	UDataTable* PresentationTable = NewObject<UDataTable>(ASC);
	PresentationTable->RowStruct = FRPGSkillDataTable::StaticStruct();
	const FName PresentationRow(TEXT("Presentation"));
	FRPGSkillDataTable Row;
	Row.SkillIcon = TableIcon;
	PresentationTable->AddRow(PresentationRow, Row);
	LastDefinition->SkillDataHandle.DataTable = PresentationTable;
	LastDefinition->SkillDataHandle.RowName = PresentationRow;
	CheckIcon(TEXT("Table icon retains runtime precedence"), TableIcon);
	LastDefinition->SkillIcon = nullptr;
	CheckIcon(TEXT("Missing direct icon still uses table"), TableIcon);
	Row.SkillIcon = nullptr;
	PresentationTable->AddRow(PresentationRow, Row);
	LastDefinition->SkillIcon = DirectIcon;
	CheckIcon(TEXT("Empty table icon falls back to Definition"), DirectIcon);
	LastDefinition->SkillIcon = nullptr;
	CheckIcon(TEXT("Missing icons remain empty"), nullptr);

	Slots->SetSkillSlot(0, First); Slots->SetSkillSlot(1, Second);
	TestEqual(TEXT("Registration uses the Definition's identity"), Slots->GetSkillSlotContent(0)->AbilityTag, First);
	Slots->SetSkillSlot(1, First);
	TestEqual(TEXT("Moving onto an occupied slot swaps both registrations"), Slots->GetSkillSlotContent(0)->AbilityTag, Second);
	TestEqual(TEXT("Destination owns the moved skill"), Slots->GetSkillSlotContent(1)->AbilityTag, First);
	Slots->SetSkillSlot(0, RPGGameplayTags::InputTag_QuickSkill_8);
	TestEqual(TEXT("An ungranted identity cannot replace a skill"), Slots->GetSkillSlotContent(0)->AbilityTag, Second);
	{
		ON_SCOPE_EXIT { Player->SetRole(ROLE_Authority); };
		// Python/editor scripts force local RPC dispatch. The server bodies must
		// reject client roles rather than re-entering the public request methods.
		FEditorScriptExecutionGuard ScriptGuard;
		for (const ENetRole Role : { ROLE_AutonomousProxy, ROLE_SimulatedProxy })
		{
			Player->SetRole(Role);
			Slots->SetSkillSlot(2, First);
			Slots->SetItemSlotById(0, FGuid::NewGuid());
			Slots->ClearSlot(true, 0);
			Slots->ClearSlot(false, 0);
			TestEqual(TEXT("Local client RPC dispatch preserves the registered skill"),
				Slots->GetSkillSlotContent(0)->AbilityTag, Second);
			TestEqual(TEXT("Local client RPC dispatch preserves the other skill"),
				Slots->GetSkillSlotContent(1)->AbilityTag, First);
			TestTrue(TEXT("Local client RPC dispatch cannot move a registration"),
				Slots->GetSkillSlotContent(2)->IsEmpty());
			TestTrue(TEXT("Local client RPC dispatch cannot assign an item"),
				Slots->GetItemSlotContent(0)->IsEmpty());
		}
	}
	URPGDragDropOperation* Drag = FRPGSkillQuickSlotDragDrop::Create(Slots, Palette, First, nullptr);
	if (!TestNotNull(TEXT("Skill palette drag snapshot"), Drag)) return false;
	TestTrue(TEXT("Palette drop requests registration"), FRPGSkillQuickSlotDragDrop::Drop(*Drag, *Slots, 2));
	TestTrue(TEXT("Palette drop moves an existing registration"), Slots->GetSkillSlotContent(1)->IsEmpty());
	URPGDragDropOperation* Stale = FRPGSkillQuickSlotDragDrop::Create(Slots, Slots, First, nullptr, 2);
	if (!TestNotNull(TEXT("Quick-slot drag snapshot"), Stale)) return false;
	Slots->ClearSlot(true, 2);
	TestFalse(TEXT("A changed source cannot complete an old drag"), FRPGSkillQuickSlotDragDrop::Drop(*Stale, *Slots, 3));
	APawn* Foreign = World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Foreign pawn"), Foreign)) return false;
	Drag->SourceComponent = NewObject<URPGPlayerSkillComponent>(Foreign);
	TestFalse(TEXT("Cross-pawn drops are rejected"), FRPGSkillQuickSlotDragDrop::Drop(*Drag, *Slots, 3));
	Grant(Second);
	TestFalse(TEXT("Duplicate grants fail closed"), Slots->CanAssignSkillSlot(3, Second));
	ASC->ClearAllAbilities();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGSkillQuickSlotRightClickTest,
	"ProjectRPG.Skill.QuickSlot.RightClickInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRPGSkillQuickSlotRightClickTest::RunTest(const FString& Parameters)
{
	URPGQuickSlotWidget* Widget = NewObject<URPGQuickSlotWidget>();
	const FBoolProperty* SkillSlotProperty = FindFProperty<FBoolProperty>(
		URPGQuickSlotWidget::StaticClass(), TEXT("bIsSkillSlot"));
	if (!TestNotNull(TEXT("Skill-slot mode property"), SkillSlotProperty)) return false;
	const FPointerEvent RightPress(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
		TSet<FKey>{EKeys::RightMouseButton}, EKeys::RightMouseButton, 0.0f,
		FModifierKeysState());
	TestFalse(TEXT("Item slots retain their existing right-press routing"),
		Widget->NativeOnMouseButtonDown(FGeometry(), RightPress).IsEventHandled());
	SkillSlotProperty->SetPropertyValue_InContainer(Widget, true);
	TestTrue(TEXT("Skill slots consume clear presses before the viewport can capture them"),
		Widget->NativeOnMouseButtonDown(FGeometry(), RightPress).IsEventHandled());
	TestTrue(TEXT("Repeated clear presses also stay in UI when Slate emits a double-click"),
		Widget->NativeOnMouseButtonDoubleClick(FGeometry(), RightPress).IsEventHandled());
	return true;
}

#endif
