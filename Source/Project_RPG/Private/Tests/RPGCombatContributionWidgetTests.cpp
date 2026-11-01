#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "UI/Content/RPGCombatContributionWidget.h"
#include "AbilitySystemComponent.h"
#include "Attribute/RPGAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Component/Combat/RPGCombatContributionComponent.h"
#include "Component/RPGAbilitySystemComponent.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGCombatControlTestFixture.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWidget.h"
#include <limits>

struct FRPGCombatContributionWidgetTestAccess
{
	static void Refresh(URPGCombatContributionWidget& Widget, AActor* Target) { Widget.RefreshFromTarget(Target); }
	static void Tick(URPGCombatContributionWidget& Widget) { Widget.NativeTick(FGeometry(), 0.f); }
	static void Detach(URPGCombatContributionWidget& Widget) { Widget.NativeDestruct(); }
	static void Attach(URPGCombatContributionWidget& Widget) { Widget.NativeConstruct(); }
	static bool Visible(const URPGCombatContributionWidget& Widget) { return Widget.Card->GetVisibility() != ESlateVisibility::Collapsed; }
	static float Percent(const URPGCombatContributionWidget& Widget, int32 Index) { return Widget.Meters[Index]->GetPercent(); }
	static FString Text(const URPGCombatContributionWidget& Widget, int32 Index) { return Widget.Labels[Index]->GetText().ToString(); }
	static bool RowVisible(const URPGCombatContributionWidget& Widget, int32 Index)
	{ return Widget.MeterRows[Index]->GetVisibility() != ESlateVisibility::Collapsed; }
};

namespace
{
	using Access = FRPGCombatContributionWidgetTestAccess;
	FRPGCombatContributionState Snapshot(float Stagger = 0.f, float PartBreak = 0.f)
	{
		FRPGCombatContributionState State;
		State.bEnabled = true;
		State.StaggerThreshold = State.PartBreakThreshold = 100.f;
		State.Stagger = Stagger; State.PartBreak = PartBreak;
		State.bStaggerComplete = Stagger == 100.f; State.bPartBroken = PartBreak == 100.f;
		State.Generation = State.Revision = 1;
		return State;
	}
	// UI-only replica fixture: no production setter or approved-hit bypass is added.
	void SetReplica(URPGCombatContributionComponent& Receiver, const FRPGCombatContributionState& State)
	{
		auto* Property = FindFProperty<FStructProperty>(Receiver.GetClass(), TEXT("State"));
		check(Property);
		*Property->ContainerPtrToValuePtr<FRPGCombatContributionState>(&Receiver) = State;
	}
	struct FWidgetScope
	{
		UWorld* World = nullptr;
		URPGCombatContributionWidget* Widget = nullptr;
		TSharedPtr<SWidget> Slate;
		bool Initialize()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
			if (!World) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay(); World->GetWorldSettings()->NotifyBeginPlay();
			Widget = NewObject<URPGCombatContributionWidget>(World);
			if (!Widget->Initialize()) return false;
			Slate = Widget->TakeWidget();
			return Widget->WidgetTree && Widget->GetWorld() == World;
		}
		~FWidgetScope()
		{
			Slate.Reset();
			if (Widget) Widget->ReleaseSlateResources(true);
			if (World) RPGTestWorld::Destroy(World);
		}
		URPGCombatContributionComponent* Receiver(AActor* Target)
		{
			auto* Result = NewObject<URPGCombatContributionComponent>(Target);
			Target->AddInstanceComponent(Result); Result->RegisterComponent();
			return Result;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionWidgetProgressTest, "ProjectRPG.Combat.ContributionUI.ProgressAndReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionWidgetProgressTest::RunTest(const FString&)
{
	FWidgetScope Scope;
	if (!TestTrue(TEXT("Native widget and isolated world are ready"), Scope.Initialize())) return false;
	auto& Widget = *Scope.Widget;
	TestFalse(TEXT("No target starts hidden"), Access::Visible(Widget));
	TestEqual(TEXT("Hidden inner card does not stop future root ticks"), Widget.GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestFalse(TEXT("Presentation cannot steal keyboard focus"), Widget.IsFocusable());
	auto* Target = Scope.World->SpawnActor<AActor>();
	auto* Receiver = Scope.Receiver(Target);
	const auto State = Snapshot(20.f, 40.f);
	SetReplica(*Receiver, State);
	Access::Refresh(Widget, Target);
	TestTrue(TEXT("Enabled receiver appears"), Access::Visible(Widget));
	TestEqual(TEXT("Target identity is not cached across different targets"), Widget.GetDisplayedTarget(), Target);
	TestEqual(TEXT("Stagger uses its replicated threshold"), Access::Percent(Widget, 0), .2f);
	TestEqual(TEXT("Part break is a separate meter"), Access::Percent(Widget, 1), .4f);
	TestTrue(TEXT("Numbers accompany color"), Access::Text(Widget, 0).Contains(TEXT("20/100")));
	TestEqual(TEXT("Rendering does not mutate receiver values"), Receiver->GetContributionState().Stagger, State.Stagger);
	TestEqual(TEXT("Rendering does not publish a server revision"), Receiver->GetContributionState().Revision, State.Revision);
	SetReplica(*Receiver, Snapshot(100.f, 40.f));
	Access::Refresh(Widget, Target);
	TestEqual(TEXT("Completed contribution fills the meter"), Access::Percent(Widget, 0), 1.f);
	TestTrue(TEXT("Threshold completion is explicit"), Access::Text(Widget, 0).Contains(TEXT("완료")));
	auto Single = Snapshot(25.f);
	Single.PartBreakThreshold = 0.f;
	SetReplica(*Receiver, Single); Access::Refresh(Widget, Target);
	TestFalse(TEXT("A disabled mechanic has no empty fake meter"), Access::RowVisible(Widget, 1));
	TestTrue(TEXT("A hidden mechanic clears old completion text"), Access::Text(Widget, 1).IsEmpty());
	auto Reset = Snapshot(); ++Reset.Generation;
	SetReplica(*Receiver, Reset); Access::Refresh(Widget, Target);
	TestEqual(TEXT("A new receiver generation displays zero immediately"), Access::Percent(Widget, 0), 0.f);
	TestFalse(TEXT("Previous completion does not persist after reset"), Access::Text(Widget, 0).Contains(TEXT("완료")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionWidgetLifetimeTest, "ProjectRPG.Combat.ContributionUI.TargetAndWidgetLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionWidgetLifetimeTest::RunTest(const FString&)
{
	FWidgetScope Scope;
	if (!TestTrue(TEXT("Widget ready"), Scope.Initialize())) return false;
	auto& Widget = *Scope.Widget;
	auto* First = Scope.World->SpawnActor<AActor>();
	auto* Second = Scope.World->SpawnActor<AActor>();
	SetReplica(*Scope.Receiver(First), Snapshot(80.f));
	SetReplica(*Scope.Receiver(Second), Snapshot(25.f));
	Access::Refresh(Widget, First); Access::Refresh(Widget, Second);
	TestEqual(TEXT("Switching targets reads the second meter"), Access::Percent(Widget, 0), .25f);
	Access::Refresh(Widget, nullptr);
	TestFalse(TEXT("Unlocking clears the card"), Access::Visible(Widget));
	TestNull(TEXT("Unlocking clears the weak target"), Widget.GetDisplayedTarget());
	TestTrue(TEXT("No stale numeric text remains"), Access::Text(Widget, 0).IsEmpty());
	Access::Refresh(Widget, First);
	Access::Refresh(Widget, Scope.World->SpawnActor<AActor>());
	TestFalse(TEXT("Ordinary targets without a receiver have no mechanic UI"), Access::Visible(Widget));
	Access::Refresh(Widget, First); First->Destroy(); Access::Refresh(Widget, First);
	TestFalse(TEXT("Destroyed targets hide before any replication callback"), Access::Visible(Widget));
	Access::Refresh(Widget, Second); Access::Detach(Widget); Access::Refresh(Widget, Second);
	TestFalse(TEXT("Detached widgets cannot repopulate stale content"), Access::Visible(Widget));
	Access::Attach(Widget);
	TestFalse(TEXT("Reconstruction without an owning combat context clears old content"), Access::Visible(Widget));
	Access::Refresh(Widget, Second); Access::Tick(Widget);
	TestFalse(TEXT("A tick without a local owning Pawn cannot keep a target card"), Access::Visible(Widget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGContributionWidgetInvalidStateTest, "ProjectRPG.Combat.ContributionUI.InvalidStateAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGContributionWidgetInvalidStateTest::RunTest(const FString&)
{
	FWidgetScope Scope;
	if (!TestTrue(TEXT("Widget ready"), Scope.Initialize())) return false;
	auto& Widget = *Scope.Widget;
	auto* Target = Scope.World->SpawnActor<ARPGStunTestCharacter>();
	auto* ASC = Target->GetRPGAbilitySystemComponent();
	ASC->InitAbilityActorInfo(Target, Target);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetMaxHealthAttribute(), 100.f);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 100.f);
	auto* Receiver = Scope.Receiver(Target);
	SetReplica(*Receiver, Snapshot(20.f)); Access::Refresh(Widget, Target);
	TestTrue(TEXT("Living combat target shows its current contribution"), Access::Visible(Widget));
	TArray<FRPGCombatContributionState> Invalid;
	auto State = Snapshot(); State.bEnabled = false; Invalid.Add(State);
	State = Snapshot(); State.Stagger = -1.f; Invalid.Add(State);
	State = Snapshot(); State.StaggerThreshold = std::numeric_limits<float>::quiet_NaN(); Invalid.Add(State);
	State = Snapshot(); State.PartBreak = 101.f; Invalid.Add(State);
	State = Snapshot(); State.bStaggerComplete = true; Invalid.Add(State);
	State = Snapshot(); State.StaggerThreshold = State.PartBreakThreshold = 0.f; Invalid.Add(State);
	for (const auto& Bad : Invalid)
	{
		SetReplica(*Receiver, Bad); Access::Refresh(Widget, Target);
		TestFalse(TEXT("Invalid or disabled state is not clamped into a believable gauge"), Access::Visible(Widget));
		TestEqual(TEXT("Invalid state clears the displayed percentage"), Access::Percent(Widget, 0), 0.f);
	}
	SetReplica(*Receiver, Snapshot(20.f)); Access::Refresh(Widget, Target);
	ASC->SetNumericAttributeBase(URPGAttributeSet::GetCurrentHealthAttribute(), 0.f);
	Access::Refresh(Widget, Target);
	TestFalse(TEXT("Dead targets hide even if an old enabled snapshot remains"), Access::Visible(Widget));
	TestNull(TEXT("Death clears the target identity"), Widget.GetDisplayedTarget());
	return true;
}

#endif
