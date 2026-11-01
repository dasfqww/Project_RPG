#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Progress/RPGActionProgressWidget.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "Tests/RPGTestWorldCleanup.h"
#include "UObject/Package.h"
#include "Widgets/SWidget.h"

struct FRPGActionProgressWidgetTestAccess
{
	static void Attach(URPGActionProgressWidget& W) { W.NativeConstruct(); }
	static void Detach(URPGActionProgressWidget& W) { W.NativeDestruct(); }
	static UProgressBar* Bar(URPGActionProgressWidget& W) { return W.ProgressBar; }
	static UTextBlock* Label(URPGActionProgressWidget& W) { return W.ActionLabel; }
	static UTextBlock* Time(URPGActionProgressWidget& W) { return W.TimeLabel; }
	static UTextBlock* Stage(URPGActionProgressWidget& W) { return W.StageLabel; }
	static UTextBlock* Phase(URPGActionProgressWidget& W) { return W.PhaseLabel; }
	static UImage* Icon(URPGActionProgressWidget& W) { return W.ActionIcon; }
};

namespace
{
using Access = FRPGActionProgressWidgetTestAccess;
struct FWidgetFixture
{
	UWorld* World = nullptr;
	APawn* Pawn = nullptr;
	UPlayerUIComponent* UI = nullptr;
	USceneComponent* Source = nullptr;
	URPGActionProgressWidget* Widget = nullptr;
	TSharedPtr<SWidget> Slate;
	bool Initialize()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass()));
		if (!World) return false;
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		Pawn = World->SpawnActor<APawn>();
		if (!Pawn) return false;
		UI = AddUI();
		Source = NewObject<USceneComponent>(Pawn);
		Widget = NewObject<URPGActionProgressWidget>(World);
		return Widget->Initialize();
	}
	UPlayerUIComponent* AddUI()
	{
		auto* Component = NewObject<UPlayerUIComponent>(Pawn);
		Pawn->AddInstanceComponent(Component);
		Component->RegisterComponent();
		return Component;
	}
	void Show() { Widget->SetProgressSource(UI); Slate = Widget->TakeWidget(); }
	~FWidgetFixture()
	{
		if (Widget) { Access::Detach(*Widget); Widget->SetProgressSource(nullptr); Widget->ReleaseSlateResources(true); }
		Slate.Reset();
		RPGTestWorld::Destroy(World);
	}
};
FRPGActionProgressData Timed()
{
	FRPGActionProgressData Data;
	Data.Kind = ERPGActionProgressKind::Casting;
	Data.Label = FText::FromString(TEXT("Cast"));
	Data.bHasDuration = true; Data.DurationSeconds = 4.f; Data.ElapsedSeconds = 1.f;
	Data.TimeDisplay = ERPGActionProgressTimeDisplay::Remaining;
	return Data;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressWidgetPresentationTest, "ProjectRPG.UI.ActionProgressWidget.Presentation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressWidgetPresentationTest::RunTest(const FString&)
{
	FWidgetFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	auto Data = Timed();
	const auto Handle = F.UI->BeginActionProgress(F.Source, Data);
	F.Show();
	if (!TestNotNull(TEXT("Fallback progress control"), Access::Bar(*F.Widget))) return false;
	TestTrue(TEXT("Late widget sees current execution"), F.Widget->GetDisplayedProgress().Handle == Handle);
	TestEqual(TEXT("Initial fraction"), Access::Bar(*F.Widget)->GetPercent(), .25f);
	TestEqual(TEXT("Initial label"), Access::Label(*F.Widget)->GetText().ToString(), FString(TEXT("Cast")));
	TestFalse(TEXT("Remaining time rendered"), Access::Time(*F.Widget)->GetText().IsEmpty());
	for (const auto Kind : {ERPGActionProgressKind::Holding, ERPGActionProgressKind::Casting,
		ERPGActionProgressKind::Charge, ERPGActionProgressKind::Interaction})
	{
		Data = Timed(); Data.Kind = Kind; Data.ElapsedSeconds = 4.f;
		Data.Phase = ERPGActionProgressPhase::Ready; Data.Stage = 2; Data.StageCount = 2;
		const auto Active = F.UI->BeginActionProgress(F.Source, Data);
		TestEqual(TEXT("All kinds display without concrete gameplay dependency"), F.Widget->GetDisplayedProgress().Data.Kind, Kind);
		TestEqual(TEXT("Full bar"), Access::Bar(*F.Widget)->GetPercent(), 1.f);
		TestEqual(TEXT("Ready keeps display visible"), F.Widget->GetVisibility(), ESlateVisibility::HitTestInvisible);
		TestFalse(TEXT("Ready label"), Access::Phase(*F.Widget)->GetText().IsEmpty());
		TestFalse(TEXT("Stage label"), Access::Stage(*F.Widget)->GetText().IsEmpty());
		Data.Phase = ERPGActionProgressPhase::AwaitingConfirmation;
		F.UI->UpdateActionProgress(Active, Data);
		TestTrue(TEXT("Waiting is still active"), F.Widget->GetDisplayedProgress().bActive);
		F.UI->EndActionProgress(Active, ERPGActionProgressEndReason::Completed);
		TestEqual(TEXT("End collapses whole module"), F.Widget->GetVisibility(), ESlateVisibility::Collapsed);
		TestTrue(TEXT("End clears text"), Access::Label(*F.Widget)->GetText().IsEmpty());
		TestEqual(TEXT("End clears bar"), Access::Bar(*F.Widget)->GetPercent(), 0.f);
	}
	Data = Timed(); Data.bHasDuration = false; Data.TimeDisplay = ERPGActionProgressTimeDisplay::None;
	Data.Icon = NewObject<UTexture2D>(GetTransientPackage());
	F.UI->BeginActionProgress(F.Source, Data);
	TestTrue(TEXT("Unknown duration uses marquee"), Access::Bar(*F.Widget)->UseMarquee());
	TestEqual(TEXT("Optional time hidden"), Access::Time(*F.Widget)->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("Loaded soft icon displayed"), Access::Icon(*F.Widget)->GetBrush().GetResourceObject(), static_cast<UObject*>(Data.Icon.Get()));
	Data = Timed(); Data.bReverseFill = true;
	F.UI->BeginActionProgress(F.Source, Data);
	TestEqual(TEXT("Reverse fill is supplied by state"), Access::Bar(*F.Widget)->GetPercent(), .75f);
	TestFalse(TEXT("Known duration disables marquee"), Access::Bar(*F.Widget)->UseMarquee());
	TestNull(TEXT("New action does not retain old icon"), Access::Icon(*F.Widget)->GetBrush().GetResourceObject());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressWidgetLifetimeTest, "ProjectRPG.UI.ActionProgressWidget.BindingLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressWidgetLifetimeTest::RunTest(const FString&)
{
	FWidgetFixture F; if (!TestTrue(TEXT("Fixture"), F.Initialize())) return false;
	F.Show();
	TestEqual(TEXT("Empty source hides display"), F.Widget->GetVisibility(), ESlateVisibility::Collapsed);
	auto Data = Timed();
	const auto A = F.UI->BeginActionProgress(F.Source, Data);
	auto* OtherUI = F.AddUI(); Data.Label = FText::FromString(TEXT("Other Pawn"));
	const auto B = OtherUI->BeginActionProgress(F.Source, Data);
	F.Widget->SetProgressSource(OtherUI);
	TestFalse(TEXT("Old source unsubscribed"), F.UI->OnActionProgressChangedNative.IsBoundToObject(F.Widget));
	F.UI->EndActionProgress(A, ERPGActionProgressEndReason::Cancelled);
	TestTrue(TEXT("Old source end cannot hide new display"), F.Widget->GetDisplayedProgress().Handle == B && F.Widget->GetDisplayedProgress().bActive);
	Access::Detach(*F.Widget);
	TestFalse(TEXT("Detached unsubscribed"), OtherUI->OnActionProgressChangedNative.IsBoundToObject(F.Widget));
	TestTrue(TEXT("Widget detach never cancels gameplay"), OtherUI->GetActionProgressSnapshot().bActive);
	Data.ElapsedSeconds = 3.f; OtherUI->UpdateActionProgress(B, Data);
	TestFalse(TEXT("Detached display stays clear"), F.Widget->GetDisplayedProgress().bActive);
	Access::Attach(*F.Widget);
	TestEqual(TEXT("Reattach reads current state"), Access::Bar(*F.Widget)->GetPercent(), .75f);
	OtherUI->UnregisterComponent();
	TestEqual(TEXT("Source lifecycle hides display without widget Tick"), F.Widget->GetVisibility(), ESlateVisibility::Collapsed);
	F.Widget->SetProgressSource(nullptr);
	TestFalse(TEXT("Explicit unbind releases subscription"), OtherUI->OnActionProgressChangedNative.IsBoundToObject(F.Widget));
	OtherUI->RegisterComponent();
	OtherUI->BeginActionProgress(F.Source, Data);
	TestFalse(TEXT("Unbound source cannot revive display"), F.Widget->GetDisplayedProgress().bActive);
	return true;
}
#endif
