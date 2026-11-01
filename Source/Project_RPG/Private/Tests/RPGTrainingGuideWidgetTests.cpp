#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "UI/Content/RPGTrainingGuideWidget.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Component/UI/QuickSlotComponent.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Controller/RPGPlayerController.h"
#include "DataAsset/Input/DataAsset_InputConfig.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/AutomationTest.h"
#include "RPGGameplayTags.h"
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

struct FRPGTrainingGuideWidgetTestAccess
{
	static void Refresh(URPGTrainingGuideWidget& Widget) { Widget.RefreshCurrentPawn(); }
	static void Detach(URPGTrainingGuideWidget& Widget) { Widget.NativeDestruct(); }
	static void Attach(URPGTrainingGuideWidget& Widget) { Widget.NativeConstruct(); }
	static bool Visible(const URPGTrainingGuideWidget& Widget) { return Widget.Card->GetVisibility() != ESlateVisibility::Collapsed; }
	static FString Title(const URPGTrainingGuideWidget& Widget) { return Widget.Title->GetText().ToString(); }
	static FString Hint(const URPGTrainingGuideWidget& Widget) { return Widget.Hint->GetText().ToString(); }
	static FString Goal(const URPGTrainingGuideWidget& Widget, int32 Index) { return Widget.Goals[Index]->GetText().ToString(); }
	static bool HasCompletionActions(const URPGTrainingGuideWidget& Widget) { return Widget.CompletionActions != nullptr; }
};

namespace
{
	using Access = FRPGTrainingGuideWidgetTestAccess;
	// Simulated owner-only replica: these tests exercise UI, never server goal approval.
	void SetReplica(URPGTrainingObjectiveComponent& Component, const FRPGTrainingObjectiveProgress& Progress)
	{
		auto* Property = FindFProperty<FStructProperty>(Component.GetClass(), TEXT("Progress"));
		check(Property);
		*Property->ContainerPtrToValuePtr<FRPGTrainingObjectiveProgress>(&Component) = Progress;
	}
	struct FFixture
	{
		UWorld* World = nullptr;
		ARPGPlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		URPGTrainingObjectiveComponent* Objectives = nullptr;
		URPGTrainingGuideWidget* Widget = nullptr;
		UDataAsset_InputConfig* Input = nullptr;
		TSharedPtr<SWidget> Slate;
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Controller = World->SpawnActor<ARPGPlayerController>();
			if (!Controller) return false;
			auto* Player = NewObject<ULocalPlayer>(GEngine);
			Controller->Player = Player; Player->PlayerController = Controller;
			Controller->SetAsLocalPlayerController(); World->AddController(Controller);
			Pawn = World->SpawnActor<APawn>();
			if (!Pawn) return false;
			Controller->Possess(Pawn);
			Objectives = AddObjectives(Pawn);
			Input = NewObject<UDataAsset_InputConfig>(Controller);
			Input->DefaultMappingContext = NewObject<UInputMappingContext>(Input);
			auto* Property = FindFProperty<FObjectProperty>(Controller->GetClass(), TEXT("InputConfigDataAsset"));
			if (!Property) return false;
			Property->SetObjectPropertyValue_InContainer(Controller, Input);
			Widget = NewObject<URPGTrainingGuideWidget>(World);
			Widget->SetOwningPlayer(Controller);
			if (!Widget->Initialize()) return false;
			Slate = Widget->TakeWidget();
			return Widget->WidgetTree && Widget->GetOwningPlayer() == Controller && Widget->GetWorld() == World;
		}
		URPGTrainingObjectiveComponent* AddObjectives(APawn* Target)
		{
			auto* Result = NewObject<URPGTrainingObjectiveComponent>(Target);
			Target->AddInstanceComponent(Result); Result->RegisterComponent();
			return Result;
		}
		UInputAction* Map(FGameplayTag Tag, FKey Key)
		{
			auto* Action = NewObject<UInputAction>(Input);
			FWarriorInputActionConfig Row; Row.InputTag = Tag; Row.InputAction = Action;
			Input->NativeInputActions.Add(Row);
			Input->DefaultMappingContext->MapKey(Action, Key);
			return Action;
		}
		~FFixture()
		{
			if (Widget) { Access::Detach(*Widget); Slate.Reset(); Widget->ReleaseSlateResources(true); }
			if (World) RPGTestWorld::Destroy(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingGuideProgressTest, "ProjectRPG.Progression.TrainingGuide.ProgressAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingGuideProgressTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Local widget fixture ready"), F.Initialize())) return false;
	TestFalse(TEXT("No active server progress starts hidden"), Access::Visible(*F.Widget));
	TestEqual(TEXT("Hidden card keeps the root available for late replication"), F.Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("Guide does not take input focus"), F.Widget->IsFocusable());
	FRPGTrainingObjectiveProgress Progress; Progress.bActive = true;
	SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Active practice appears"), Access::Visible(*F.Widget));
	TestTrue(TEXT("Zero completed goals"), Access::Title(*F.Widget).Contains(TEXT("0/4")));
	TestTrue(TEXT("Basic attack is the first suggested action"), Access::Hint(*F.Widget).Contains(TEXT("기본 공격")));
	Progress.bChargeCompleted = true; SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Goals can complete out of display order"), Access::Title(*F.Widget).Contains(TEXT("1/4")));
	TestTrue(TEXT("Completion is readable without relying on color"), Access::Goal(*F.Widget, 3).Contains(TEXT("[완료]")));
	Progress.bBasicAttackCompleted = true; Progress.bEvadeCompleted = true;
	SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Suggestion skips already completed goals"), Access::Hint(*F.Widget).Contains(TEXT("돌진 베기")));
	Progress.bAssaultCompleted = true; SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Four goal counter"), Access::Title(*F.Widget).Contains(TEXT("4/4")));
	TestTrue(TEXT("Reports personal practice completion"), Access::Hint(*F.Widget).Contains(TEXT("조작 실습 완료")));
	TestTrue(TEXT("Completion describes the result/return step instead of an indefinite next prompt"),
		Access::Hint(*F.Widget).Contains(TEXT("결과 화면")) && !Access::Hint(*F.Widget).Contains(TEXT("다음 안내를 기다려")));
	TestFalse(TEXT("A standalone presentation replica cannot manufacture authenticated result controls"), Access::HasCompletionActions(*F.Widget));
	TestFalse(TEXT("Does not claim experience was settled"), Access::Hint(*F.Widget).Contains(TEXT("경험치")));
	TestTrue(TEXT("Presentation does not mutate the observed progress"), F.Objectives->GetTrainingProgress().IsComplete());
	Progress.bActive = false; SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Inactive progress hides even if old completion flags remain"), Access::Visible(*F.Widget));
	TestTrue(TEXT("No stale complete text after deactivation"), Access::Hint(*F.Widget).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingGuideLifetimeTest, "ProjectRPG.Progression.TrainingGuide.PawnAndWidgetLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingGuideLifetimeTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	FRPGTrainingObjectiveProgress Progress; Progress.bActive = true; Progress.bBasicAttackCompleted = true;
	SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestEqual(TEXT("Reads only the owning Pawn"), F.Widget->GetDisplayedPawn(), F.Pawn);
	F.Controller->UnPossess(); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Unpossess clears immediately"), Access::Visible(*F.Widget));
	TestNull(TEXT("No stale Pawn reference"), F.Widget->GetDisplayedPawn());
	auto* Second = F.World->SpawnActor<APawn>(); F.Controller->Possess(Second); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Ordinary Pawn has no training card"), Access::Visible(*F.Widget));
	auto* SecondObjectives = F.AddObjectives(Second); Progress.bBasicAttackCompleted = false;
	SetReplica(*SecondObjectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Late component/progress becomes visible"), Access::Visible(*F.Widget));
	TestTrue(TEXT("New Pawn does not inherit first Pawn progress"), Access::Title(*F.Widget).Contains(TEXT("0/4")));
	Access::Detach(*F.Widget); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Detached widgets cannot repopulate"), Access::Visible(*F.Widget));
	Access::Attach(*F.Widget);
	TestTrue(TEXT("Reattached widget reads current owner"), Access::Visible(*F.Widget));
	SecondObjectives->DestroyComponent(); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Removed objective source clears the card"), Access::Visible(*F.Widget));
	auto* Character = F.World->SpawnActor<ARPGStunTestCharacter>();
	F.Controller->Possess(Character);
	auto* ASC = Character->GetRPGAbilitySystemComponent(); ASC->InitAbilityActorInfo(Character, Character);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
	SetReplica(*F.AddObjectives(Character), Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Living Pawn displays progress"), Access::Visible(*F.Widget));
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Death clears previous guidance"), Access::Visible(*F.Widget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGTrainingGuideInputTest, "ProjectRPG.Progression.TrainingGuide.CurrentInputAndSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGTrainingGuideInputTest::RunTest(const FString&)
{
	FFixture F;
	if (!TestTrue(TEXT("Fixture ready"), F.Initialize())) return false;
	auto* Attack = F.Map(RPGGameplayTags::InputTag_LightAttack_GreatSword, EKeys::J);
	FRPGTrainingObjectiveProgress Progress; Progress.bActive = true;
	SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Uses live basic attack mapping"), Access::Hint(*F.Widget).Contains(TEXT("[J]")));
	F.Input->DefaultMappingContext->UnmapAllKeysFromAction(Attack);
	F.Input->DefaultMappingContext->MapKey(Attack, EKeys::K); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Rebinding is reflected without reconstructing the widget"), Access::Hint(*F.Widget).Contains(TEXT("[K]")));
	Progress.bBasicAttackCompleted = Progress.bEvadeCompleted = true;
	SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Unbound skill gives a configuration hint"), Access::Hint(*F.Widget).Contains(TEXT("배치와 키")));
	auto* Slots = NewObject<UQuickSlotComponent>(F.Pawn); F.Pawn->AddInstanceComponent(Slots); Slots->RegisterComponent();
	auto* SlotProperty = FindFProperty<FArrayProperty>(Slots->GetClass(), TEXT("SkillSlots")); check(SlotProperty);
	auto& SlotData = *SlotProperty->ContainerPtrToValuePtr<TArray<FRPGQuickSlotContent>>(Slots);
	SlotData.SetNum(8); SlotData[2].AbilityTag = RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	const FGameplayTag SlotTag = RPGGameplayTags::InputTag_QuickSkill_3;
	if (!TestTrue(TEXT("Existing slot input tag is registered"), SlotTag.IsValid())) return false;
	F.Map(SlotTag, EKeys::P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Skill hint follows its assigned slot rather than a fixed Q key"), Access::Hint(*F.Widget).Contains(TEXT("[P]")));
	SlotData[2].AbilityTag = RPGGameplayTags::Player_Ability_Skill_Charge;
	Progress.bAssaultCompleted = true; SetReplica(*F.Objectives, Progress); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Charge uses current slot key and hold/release guidance"), Access::Hint(*F.Widget).Contains(TEXT("[P]"))
		&& Access::Hint(*F.Widget).Contains(TEXT("끝까지 모은 뒤")));
	TestEqual(TEXT("Reading hints never edits the slot"), Slots->GetSkillSlotContent(2)->AbilityTag, RPGGameplayTags::Player_Ability_Skill_Charge.GetTag());
	return true;
}

#endif
