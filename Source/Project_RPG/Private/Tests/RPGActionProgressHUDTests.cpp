#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "UI/Progress/RPGActionProgressWidget.h"
#include "UI/HUD/RPGHUD.h"
#include "Component/UI/PlayerUIComponent.h"
#include "Components/SceneComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Tests/RPGActionProgressPresentationTestWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "Layout/Children.h"
#include "Slate/SGameLayerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SVirtualWindow.h"

namespace
{
	bool ContainsWidget(const TSharedPtr<SWidget>& Root, const TSharedPtr<SWidget>& Target)
	{
		if (!Root || !Target) return false;
		if (Root == Target) return true;
		if (auto* Children = Root->GetChildren())
			for (int32 Index = 0; Index < Children->Num(); ++Index)
				if (ContainsWidget(Children->GetChildAt(Index), Target)) return true;
		return false;
	}
	struct FViewportFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>(GEngine)};
		UWorld* World = nullptr;
		UGameViewportClient* Viewport = nullptr;
		APlayerController* Controller = nullptr;
		ARPGHUD* HUD = nullptr;
		TSharedPtr<SVirtualWindow> Window;
		bool bInitialized = false;
		bool Initialize()
		{
			if (!FSlateApplication::IsInitialized()) return false;
			Instance->InitializeForMinimalNetRPC(FName(*FString::Printf(TEXT("/Temp/ActionProgressHUD_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
			bInitialized = true;
			World = Instance->GetWorld();
			if (!World) return false;
			// Minimal RPC worlds disable actor callbacks; this case exercises real BeginPlay/EndPlay.
#if UE_SUPPORT_FOR_ACTOR_TICK_DISABLE
			World->EnableActorTickAndUserCallbacks(true);
#endif
			// Actor ProcessEvent suppresses dynamic delegates before actors are initialized.
			World->InitializeActorsForPlay(FURL());
			if (!World->AreActorsInitialized()) return false;
			Viewport = NewObject<UGameViewportClient>(GEngine);
			Instance->GetWorldContext()->GameViewport = Viewport;
			Viewport->Init(*Instance->GetWorldContext(), Instance.Get(), false);
			Window = SNew(SVirtualWindow).Size(FVector2D(1280.f, 720.f));
			auto Overlay = SNew(SOverlay);
			auto Layers = SNew(SGameLayerManager);
			Overlay->AddSlot()[Layers];
			Window->SetContent(Overlay);
			FSlateApplication::Get().RegisterVirtualWindow(Window.ToSharedRef());
			// This headless adapter owns a virtual window rather than binding an OS scene viewport.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			Viewport->SetViewportOverlayWidget(Window, Overlay);
			Viewport->SetGameLayerManager(Layers);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FString Error;
			auto* Player = Instance->CreateLocalPlayer(0, Error, false);
			if (!Player) return false;
			Controller = World->SpawnActor<APlayerController>();
			if (!Controller) return false;
			Controller->SetPlayer(Player); World->AddController(Controller);
			if (Player->GetPlayerController(World) != Controller) return false;
			return true;
		}
		ARPGHUD* CreateHUD(TFunction<void(ARPGHUD&)> Configure = {})
		{
			FActorSpawnParameters Parameters; Parameters.Owner = Controller;
			HUD = World->SpawnActor<ARPGHUD>(Parameters);
			if (HUD) { HUD->PostInitializeComponents(); if (Configure) Configure(*HUD); HUD->DispatchBeginPlay(); }
			return HUD;
		}
		TArray<URPGActionProgressWidget*> Views() const
		{
			TArray<URPGActionProgressWidget*> Result;
			for (TObjectIterator<URPGActionProgressWidget> It; It; ++It)
				if (It->GetWorld() == World && It->GetOwningPlayer() == Controller && It->IsInViewport()) Result.Add(*It);
			return Result;
		}
		~FViewportFixture()
		{
			if (HUD && !HUD->IsActorBeingDestroyed()) HUD->Destroy();
			if (Viewport) Viewport->RemoveAllViewportWidgets();
			if (Window && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterVirtualWindow(Window.ToSharedRef());
			if (bInitialized) Instance->Shutdown();
			if (Viewport) Viewport->DetachViewportClient();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
		}
	};
	UPlayerUIComponent* AddProgress(APawn* Pawn)
	{
		auto* UI = NewObject<UPlayerUIComponent>(Pawn);
		Pawn->AddInstanceComponent(UI); UI->RegisterComponent();
		return UI;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressHUDTest,
	"ProjectRPG.UI.ActionProgressHUD.ViewportPossessionLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressHUDTest::RunTest(const FString&)
{
	FViewportFixture F;
	if (!TestTrue(TEXT("Owned local viewport"), F.Initialize())) return false;
	auto* PawnA = F.World->SpawnActor<APawn>();
	auto* PawnB = F.World->SpawnActor<APawn>();
	if (!TestTrue(TEXT("Two Pawns"), PawnA && PawnB)) return false;
	auto* UIA = AddProgress(PawnA);
	auto* UIB = AddProgress(PawnB);
	auto* Source = NewObject<USceneComponent>(PawnA);
	F.Controller->Possess(PawnA);
	FRPGActionProgressData Data;
	Data.Kind = ERPGActionProgressKind::Casting;
	Data.Label = FText::FromString(TEXT("Pawn A cast"));
	Data.bHasDuration = true; Data.DurationSeconds = 4.f; Data.ElapsedSeconds = 1.f;
	const auto A = UIA->BeginActionProgress(Source, Data);
	if (!TestNotNull(TEXT("Product HUD"), F.CreateHUD())) return false;
	auto Views = F.Views();
	if (!TestEqual(TEXT("HUD creates exactly one progress view"), Views.Num(), 1)) return false;
	TStrongObjectPtr<URPGActionProgressWidget> First(Views[0]);
	TestTrue(TEXT("Added to actual player Slate layer"), ContainsWidget(F.Window, First->GetCachedWidget()));
	TestTrue(TEXT("HUD binds initial possessed Pawn and restores existing action"), First->GetDisplayedProgress().Handle == A);
	TestFalse(TEXT("Progress never takes focus"), First->IsFocusable());
	TestEqual(TEXT("Progress never intercepts gameplay input"), First->GetVisibility(), ESlateVisibility::HitTestInvisible);

	F.Controller->Possess(PawnB);
	TestFalse(TEXT("Pawn replacement clears old presentation"), First->GetDisplayedProgress().bActive);
	TestFalse(TEXT("Old Pawn subscription removed"), UIA->OnActionProgressChangedNative.IsBoundToObject(First.Get()));
	Data.Label = FText::FromString(TEXT("Pawn B cast"));
	const auto B = UIB->BeginActionProgress(Source, Data);
	TestTrue(TEXT("New Pawn bound via ownership event"), First->GetDisplayedProgress().Handle == B);
	const auto OldPawnAction = UIA->BeginActionProgress(Source, Data);
	UIA->EndActionProgress(OldPawnAction, ERPGActionProgressEndReason::Cancelled);
	TestTrue(TEXT("Old Pawn events cannot overwrite current view"), First->GetDisplayedProgress().Handle == B && First->GetDisplayedProgress().bActive);
	F.Controller->UnPossess();
	TestEqual(TEXT("No Pawn hides progress immediately"), First->GetVisibility(), ESlateVisibility::Collapsed);
	TestFalse(TEXT("Unpossess releases source subscription"), UIB->OnActionProgressChangedNative.IsBoundToObject(First.Get()));
	F.Controller->Possess(PawnB);
	const auto C = UIB->BeginActionProgress(Source, Data);
	TestTrue(TEXT("Repossession accepts fresh action"), First->GetDisplayedProgress().Handle == C);

	F.HUD->Destroy(); F.HUD = nullptr;
	TestFalse(TEXT("HUD EndPlay removes view from viewport"), First->IsInViewport());
	TestFalse(TEXT("HUD EndPlay removes Slate entry"), ContainsWidget(F.Window, First->GetCachedWidget()));
	TestFalse(TEXT("HUD EndPlay releases progress subscription"), UIB->OnActionProgressChangedNative.IsBoundToObject(First.Get()));
	TestTrue(TEXT("HUD removal leaves gameplay state active"), UIB->GetActionProgressSnapshot().bActive);
	if (!TestNotNull(TEXT("Replacement HUD"), F.CreateHUD())) return false;
	Views = F.Views();
	if (!TestEqual(TEXT("Replacement does not accumulate views"), Views.Num(), 1)) return false;
	TestTrue(TEXT("Replacement restores current action"), Views[0]->GetDisplayedProgress().Handle == C);
	TestTrue(TEXT("Replacement is a fresh view"), Views[0] != First.Get());
	F.Controller->UnPossess();
	auto* PawnC = F.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("Pawn for replication ordering"), PawnC)) return false;
	auto* UIC = AddProgress(PawnC);
	// Exercise the engine RepNotify path with Controller::Pawn arriving first.
	// This is an ordering fixture, not a real network packet test.
	F.Controller->SetPawnFromRep(PawnC);
	TestNull(TEXT("Pawn Controller has not arrived yet"), PawnC->GetController());
	const auto D = UIC->BeginActionProgress(Source, Data);
	TestTrue(TEXT("RepNotify binds source even before reverse ownership arrives"), Views[0]->GetDisplayedProgress().Handle == D);
	F.Controller->SetPawnFromRep(nullptr);
	TestFalse(TEXT("RepNotify clears source on replicated unpossess"), Views[0]->GetDisplayedProgress().bActive);
	TestFalse(TEXT("RepNotify removes old source subscription"), UIC->OnActionProgressChangedNative.IsBoundToObject(Views[0]));
	F.HUD->Destroy(); F.HUD = nullptr;
	TestEqual(TEXT("Final cleanup removes all progress views"), F.Views().Num(), 0);
	F.Controller->UnPossess();
	TestFalse(TEXT("Retired view cannot rebind after later possession"), First->GetDisplayedProgress().bActive);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRPGActionProgressHUDConfigurationTest,
	"ProjectRPG.UI.ActionProgressHUD.PresentationConfiguration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRPGActionProgressHUDConfigurationTest::RunTest(const FString&)
{
	FViewportFixture F;
	if (!TestTrue(TEXT("Local viewport"), F.Initialize())) return false;
	F.CreateHUD([](ARPGHUD& H)
	{
		H.ActionProgressWidgetClass = URPGActionProgressPresentationTestWidget::StaticClass();
		H.ActionProgressSize = FVector2D(560.f, 140.f);
		H.ActionProgressAnchor = FVector2D(.25f, .8f);
		H.ActionProgressAlignment = FVector2D(.1f, .2f);
		H.ActionProgressOffset = FVector2D(24.f, -18.f);
		H.ActionProgressZOrder = 17;
	});
	auto Views = F.Views();
	if (!TestEqual(TEXT("Configured view created"), Views.Num(), 1)) return false;
	TestEqual(TEXT("Configured class selected"), Views[0]->GetClass(), URPGActionProgressPresentationTestWidget::StaticClass());
	const auto Slot = UGameViewportSubsystem::Get()->GetWidgetSlot(Views[0]);
	TestEqual(TEXT("Anchor applied"), Slot.Anchors.Minimum, FVector2D(.25f, .8f));
	TestEqual(TEXT("Alignment applied"), Slot.Alignment, FVector2D(.1f, .2f));
	TestEqual(TEXT("Offset applied"), FVector2D(Slot.Offsets.Left, Slot.Offsets.Top), FVector2D(24.f, -18.f));
	TestEqual(TEXT("Size applied"), FVector2D(Slot.Offsets.Right, Slot.Offsets.Bottom), FVector2D(560.f, 140.f));
	TestEqual(TEXT("Z order applied"), Slot.ZOrder, 17);
	F.HUD->Destroy(); F.HUD = nullptr;
	F.CreateHUD([](ARPGHUD& H){H.ActionProgressWidgetClass = nullptr;});
	TestEqual(TEXT("Null class opts out of the module"), F.Views().Num(), 0);
	F.HUD->Destroy(); F.HUD = nullptr;
	F.CreateHUD([](ARPGHUD& H){H.ActionProgressSize.X = -1.f;});
	TestEqual(TEXT("Invalid dimensions do not enter Slate"), F.Views().Num(), 0);
	F.HUD->Destroy(); F.HUD = nullptr;
	{
		TGuardValue<bool> DestroyInConstruct(URPGActionProgressPresentationTestWidget::bDestroyHUDOnConstruct, true);
		auto* HUD = F.CreateHUD([](ARPGHUD& H){H.ActionProgressWidgetClass = URPGActionProgressPresentationTestWidget::StaticClass();});
		TestTrue(TEXT("Authored Construct can end the HUD"), HUD && HUD->IsActorBeingDestroyed());
		F.HUD = nullptr;
	}
	TestEqual(TEXT("Ended HUD leaves no registered progress view"), F.Views().Num(), 0);
	for (TObjectIterator<URPGActionProgressPresentationTestWidget> It; It; ++It)
		if (It->GetWorld() == F.World) TestFalse(TEXT("No half-added Slate entry after Construct ends HUD"), ContainsWidget(F.Window, It->GetCachedWidget()));
	return true;
}
#endif

