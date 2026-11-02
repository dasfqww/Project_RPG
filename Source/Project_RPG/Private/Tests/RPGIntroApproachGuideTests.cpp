#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Content/RPGIntroApproachGuideWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Progression/RPGIntroApproachObjectiveComponent.h"
#include "Component/Progression/RPGTrainingObjectiveComponent.h"
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
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"

struct FRPGIntroApproachGuideTestAccess
{
	static void Refresh(URPGIntroApproachGuideWidget& W) { W.RefreshCurrentPawn(); }
	static void Detach(URPGIntroApproachGuideWidget& W) { W.NativeDestruct(); }
	static void Attach(URPGIntroApproachGuideWidget& W) { W.NativeConstruct(); }
	static bool Visible(const URPGIntroApproachGuideWidget& W) { return W.Card->GetVisibility() != ESlateVisibility::Collapsed; }
	static FString Title(const URPGIntroApproachGuideWidget& W) { return W.Title->GetText().ToString(); }
	static FString Hint(const URPGIntroApproachGuideWidget& W) { return W.Hint->GetText().ToString(); }
	static FString Goal(const URPGIntroApproachGuideWidget& W, int32 I) { return W.Goals[I]->GetText().ToString(); }
	static bool Actions(const URPGIntroApproachGuideWidget& W) { return W.CompletionActions != nullptr; }
};

namespace RPGIntroApproachGuideTests
{
	using Access = FRPGIntroApproachGuideTestAccess;
	void SetReplica(URPGIntroApproachObjectiveComponent& Component, const FRPGIntroApproachProgress& Progress)
	{
		auto* Property = FindFProperty<FStructProperty>(Component.GetClass(), TEXT("Progress"));
		check(Property); *Property->ContainerPtrToValuePtr<FRPGIntroApproachProgress>(&Component) = Progress;
	}
	struct FFixture
	{
		UWorld* World = nullptr;
		ARPGPlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		URPGIntroApproachObjectiveComponent* Objectives = nullptr;
		URPGIntroApproachGuideWidget* Widget = nullptr;
		UDataAsset_InputConfig* Input = nullptr;
		TSharedPtr<SWidget> Slate;
		URPGIntroApproachObjectiveComponent* AddObjectives(APawn* Target)
		{
			auto* Result = NewObject<URPGIntroApproachObjectiveComponent>(Target);
			Target->AddInstanceComponent(Result); Result->RegisterComponent(); return Result;
		}
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); World->InitializeActorsForPlay(FURL());
			Controller = World->SpawnActor<ARPGPlayerController>();
			if (!Controller) return false;
			auto* Player = NewObject<ULocalPlayer>(GEngine); Controller->Player = Player; Player->PlayerController = Controller;
			Controller->SetAsLocalPlayerController(); World->AddController(Controller);
			Pawn = World->SpawnActor<APawn>(); if (!Pawn) return false;
			Controller->Possess(Pawn); Objectives = AddObjectives(Pawn);
			Input = NewObject<UDataAsset_InputConfig>(Controller); Input->DefaultMappingContext = NewObject<UInputMappingContext>(Input);
			auto* Property = FindFProperty<FObjectProperty>(Controller->GetClass(), TEXT("InputConfigDataAsset"));
			if (!Property) return false; Property->SetObjectPropertyValue_InContainer(Controller, Input);
			Widget = NewObject<URPGIntroApproachGuideWidget>(World); Widget->SetOwningPlayer(Controller);
			if (!Widget->Initialize()) return false; Slate = Widget->TakeWidget();
			return Widget->WidgetTree && Widget->GetOwningPlayer() == Controller;
		}
		UInputAction* Map(FGameplayTag Tag, FKey Key)
		{
			auto* Action = NewObject<UInputAction>(Input);
			FWarriorInputActionConfig Row; Row.InputTag = Tag; Row.InputAction = Action;
			Input->NativeInputActions.Add(Row); Input->DefaultMappingContext->MapKey(Action, Key); return Action;
		}
		~FFixture()
		{
			if (Widget) { Access::Detach(*Widget); Slate.Reset(); Widget->ReleaseSlateResources(true); }
			if (World) RPGTestWorld::Destroy(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroGuideProgressTest, "ProjectRPG.Progression.Intro1Guide.ProgressAndCompletion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroGuideProgressTest::RunTest(const FString&)
{
	using namespace RPGIntroApproachGuideTests;
	FFixture F; if (!TestTrue(TEXT("Local Intro1 view"), F.Initialize())) return false;
	TestFalse(TEXT("No active server replica is hidden"), Access::Visible(*F.Widget));
	TestFalse(TEXT("The guide does not capture gameplay focus"), F.Widget->IsFocusable());
	TestEqual(TEXT("Intro1 result scope is distinct from training"), FString(F.Widget->GetPracticeContentId()), FString(TEXT("Progression.Intro1")));
	FRPGIntroApproachProgress P; P.bActive = true; SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Three-step progress counter"), Access::Title(*F.Widget).Contains(TEXT("0/3")));
	TestTrue(TEXT("First prompt describes the start location"), Access::Hint(*F.Widget).Contains(TEXT("01 출발")));
	P.bStartReached = true; SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Completed step is readable without relying on colour"), Access::Goal(*F.Widget, 0).Contains(TEXT("[완료]")));
	P.bApproachCompleted = true; P.bAttackCompleted = true; SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("All three approved replica flags complete the view"), Access::Title(*F.Widget).Contains(TEXT("3/3")));
	TestTrue(TEXT("The view waits for party/settlement and explains return"), Access::Hint(*F.Widget).Contains(TEXT("정산 확인")) && Access::Hint(*F.Widget).Contains(TEXT("로비")));
	TestFalse(TEXT("Standalone replica cannot create authenticated result controls"), Access::Actions(*F.Widget));
	TestTrue(TEXT("Reading UI never changes canonical progress"), F.Objectives->GetApproachProgress().IsComplete());
	P.bActive = false; SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Inactive stale completion clears the card"), Access::Visible(*F.Widget));
	TestTrue(TEXT("Old completion text is removed"), Access::Hint(*F.Widget).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroGuideInputTest, "ProjectRPG.Progression.Intro1Guide.CurrentInputAndSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroGuideInputTest::RunTest(const FString&)
{
	using namespace RPGIntroApproachGuideTests;
	FFixture F; if (!TestTrue(TEXT("Mapped input fixture"), F.Initialize())) return false;
	FRPGIntroApproachProgress P; P.bActive = P.bStartReached = true;
	SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Unslotted skill explains explicit configuration/save"), Access::Hint(*F.Widget).Contains(TEXT("편성·저장")));
	auto* Slots = NewObject<UQuickSlotComponent>(F.Pawn); F.Pawn->AddInstanceComponent(Slots); Slots->RegisterComponent();
	auto* Property = FindFProperty<FArrayProperty>(Slots->GetClass(), TEXT("SkillSlots")); check(Property);
	auto& Data = *Property->ContainerPtrToValuePtr<TArray<FRPGQuickSlotContent>>(Slots);
	Data.SetNum(8); // Owner-replica presentation fixture, never a server configuration request.
	Data[2].AbilityTag = RPGGameplayTags::Player_Ability_Skill_AssultBlade;
	auto* Action = F.Map(RPGGameplayTags::InputTag_QuickSkill_3, EKeys::P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Approach follows current slot key"), Access::Hint(*F.Widget).Contains(TEXT("[P]")));
	TestTrue(TEXT("Approach starts outside the goal"), Access::Hint(*F.Widget).Contains(TEXT("표시 밖")));
	F.Input->DefaultMappingContext->UnmapAllKeysFromAction(Action); F.Input->DefaultMappingContext->MapKey(Action, EKeys::O);
	Access::Refresh(*F.Widget); TestTrue(TEXT("Remapping updates the existing view"), Access::Hint(*F.Widget).Contains(TEXT("[O]")));
	P.bApproachCompleted = true; SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("Missing newly acquired WhirlWind never invents a binding"), Access::Hint(*F.Widget).Contains(TEXT("회전 베기를 편성")));
	Data[2].AbilityTag = RPGGameplayTags::Player_Ability_Skill_WhirlWind; Access::Refresh(*F.Widget);
	TestTrue(TEXT("WhirlWind follows its saved slot key and both endpoint constraints"), Access::Hint(*F.Widget).Contains(TEXT("[O]")) && Access::Hint(*F.Widget).Contains(TEXT("정상 종료")));
	TestEqual(TEXT("Guidance never edits the slot"), Slots->GetSkillSlotContent(2)->AbilityTag, RPGGameplayTags::Player_Ability_Skill_WhirlWind.GetTag());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGIntroGuideLifetimeTest, "ProjectRPG.Progression.Intro1Guide.PawnAndWidgetLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGIntroGuideLifetimeTest::RunTest(const FString&)
{
	using namespace RPGIntroApproachGuideTests;
	FFixture F; if (!TestTrue(TEXT("Possession fixture"), F.Initialize())) return false;
	FRPGIntroApproachProgress P; P.bActive = P.bStartReached = true;
	SetReplica(*F.Objectives, P); Access::Refresh(*F.Widget);
	TestEqual(TEXT("Only owning Pawn is observed"), F.Widget->GetDisplayedPawn(), F.Pawn);
	F.Controller->UnPossess(); Access::Refresh(*F.Widget);
	TestFalse(TEXT("Unpossess clears"), Access::Visible(*F.Widget));
	TestNull(TEXT("No stale Pawn reference"), F.Widget->GetDisplayedPawn());
	auto* Next = F.World->SpawnActor<APawn>(); F.Controller->Possess(Next);
	auto* Training = NewObject<URPGTrainingObjectiveComponent>(Next); Next->AddInstanceComponent(Training); Training->RegisterComponent();
	Access::Refresh(*F.Widget); TestFalse(TEXT("Training Pawn cannot display an Intro1 card"), Access::Visible(*F.Widget));
	auto* NextGoals = F.AddObjectives(Next); P.bStartReached = false; SetReplica(*NextGoals, P); Access::Refresh(*F.Widget);
	TestTrue(TEXT("New Pawn uses its own progress"), Access::Title(*F.Widget).Contains(TEXT("0/3")));
	Access::Detach(*F.Widget); Access::Refresh(*F.Widget); TestFalse(TEXT("Detached view cannot repopulate"), Access::Visible(*F.Widget));
	Access::Attach(*F.Widget); TestTrue(TEXT("Reattached view reads current owner"), Access::Visible(*F.Widget));
	NextGoals->DestroyComponent(); Access::Refresh(*F.Widget); TestFalse(TEXT("Removed source hides"), Access::Visible(*F.Widget));
	return true;
}
#endif
